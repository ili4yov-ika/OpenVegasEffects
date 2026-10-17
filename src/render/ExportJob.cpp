#include "render/ExportJob.h"

#include "composition/Composition.h"
#include "core/Log.h"
#include "media/ExrImage.h"
#include "media/MediaManager.h"
#include "render/AudioExport.h"
#include "render/RenderManager.h"
#include "render/VideoEncoder.h"

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <cmath>

namespace openvegas {
namespace render {

ExportJob::ExportJob(Request request, QObject* parent)
    : QObject(parent), m_request(std::move(request))
{
}

ExportJob::~ExportJob()
{
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(2000);
    }
    for (const auto& renderer : m_renderers) renderer->manager->cancelAll();
    m_writers.waitForDone();
}

int ExportJob::rendererCount(int renderThreads, int frames)
{
    // Automatic: half the cores, as each renderer's own effects and decoding
    // use more than one, and no more than four frames in memory at once.
    const int automatic = qBound(1, QThread::idealThreadCount() / 2, 4);
    return qBound(1, renderThreads > 0 ? renderThreads : automatic, qMax(1, frames));
}

int ExportJob::framesFromCache() const
{
    int frames = 0;
    for (const auto& renderer : m_renderers) frames += renderer->manager->diskCacheHits();
    return frames;
}

void ExportJob::start()
{
    if (m_running || !m_request.composition) return;
    m_temporary = std::make_unique<QTemporaryDir>();
    if (!m_temporary->isValid() || !QDir().mkpath(QFileInfo(m_request.outputPath).absolutePath())) {
        finish(false, tr("Cannot create export directory"));
        return;
    }
    m_request.lastFrame = qMax(m_request.firstFrame, m_request.lastFrame);
    m_running = true;
    m_clock.start();
    m_next = m_request.firstFrame;
    m_done = 0;
    m_writing = 0;
    m_patient = true;
    emit progress(0, frameCount());
    if (m_request.preset.isValid() && !m_request.preset.hasVideo()) {
        // An audio-only preset needs no frames.
        m_done = frameCount();
        emit progress(m_done, frameCount());
        emit encoding();
        encode();
        return;
    }
    const int count = rendererCount(m_request.renderThreads, frameCount());
    m_writers.setMaxThreadCount(qBound(1, count + 1, qMax(1, QThread::idealThreadCount())));
    for (int i = 0; i < count; ++i) {
        auto renderer = std::make_unique<Renderer>();
        renderer->manager = std::make_unique<RenderManager>();
        RenderManager* manager = renderer->manager.get();
        manager->setComposition(m_request.composition);
        manager->setMediaManager(m_request.media);
        manager->setPreRenderDirectory(m_request.preRenderDirectory);
        // Turbo: frames the playback cache already made of this state.
        if (!m_request.cacheDirectory.isEmpty()) manager->setDiskCacheDirectory(m_request.cacheDirectory);
        Renderer* slot = renderer.get();
        connect(manager, &RenderManager::frameReady, this,
                [this, slot](int frameIndex, const QByteArray& rgba, const QSize& size) {
            consume(*slot, frameIndex, rgba, size);
        });
        m_renderers.push_back(std::move(renderer));
    }
    if (count > 1) OV_LOG_INFO(QStringLiteral("Export renders %1 frames at once").arg(count));
    for (const auto& renderer : m_renderers) requestNext(*renderer);
}

void ExportJob::cancel()
{
    if (!m_running) return;
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
    }
    for (const auto& renderer : m_renderers) renderer->manager->cancelAll();
    finish(false, tr("Export cancelled"));
}

void ExportJob::requestNext(Renderer& renderer)
{
    renderer.frame = -1;
    // Frames waiting for the writers hold their pixels, so past a few of them
    // rendering waits for the disk (written() picks it up again).
    if (!m_running || m_next > m_request.lastFrame || m_writing >= 2 * m_writers.maxThreadCount()) return;
    renderer.frame = m_next++;
    renderer.waiting.invalidate();
    render(renderer);
}

void ExportJob::render(Renderer& renderer)
{
    const composition::Composition& shot = *m_request.composition;
    const double fps = shot.fpsDenominator() > 0 ? double(shot.fpsNumerator()) / shot.fpsDenominator() : 30.0;
    renderer.manager->requestFrame(renderer.frame, renderer.frame / fps, QSize(shot.width(), shot.height()), true);
}

void ExportJob::consume(Renderer& renderer, int frameIndex, const QByteArray& rgba, const QSize& size)
{
    const composition::Composition& shot = *m_request.composition;
    if (!m_running || frameIndex != renderer.frame || size != QSize(shot.width(), shot.height())) return;
    if (!renderer.manager->lastFrameComplete()) {
        // A video frame was not decoded yet: the frame is rendered again once
        // the decode service has had a turn - for up to five seconds, and
        // after decoding has once failed to deliver, for a moment only.
        if (!renderer.waiting.isValid()) renderer.waiting.start();
        if (renderer.waiting.elapsed() < (m_patient ? 5000 : 100)) {
            QTimer::singleShot(50, this, [this, &renderer, frameIndex] {
                if (m_running && renderer.frame == frameIndex) render(renderer);
            });
            return;
        }
        if (m_patient) OV_LOG_WARN(QStringLiteral("Export frame %1 is missing video frames").arg(frameIndex));
        m_patient = false;
    }
    const QString framePath = QDir(m_temporary->path()).filePath(
        QStringLiteral("frame-%1.png").arg(frameIndex, 8, 10, QLatin1Char('0')));
    ++m_writing;
    m_writers.start([this, rgba, size, frameIndex, framePath] {
        const QImage image(reinterpret_cast<const uchar*>(rgba.constData()), size.width(), size.height(),
                           size.width() * 4, QImage::Format_RGBA8888);
        // An intermediate file read once: light compression writes it
        // several times faster than the default.
        const bool ok = image.save(framePath, "PNG", 80);
        QMetaObject::invokeMethod(this, [this, frameIndex, ok] { written(frameIndex, ok); },
                                  Qt::QueuedConnection);
    });
    requestNext(renderer);
}

void ExportJob::written(int frame, bool ok)
{
    --m_writing;
    if (!m_running) return;
    if (!ok) {
        finish(false, tr("Could not write export frame %1").arg(frame));
        return;
    }
    ++m_done;
    emit progress(m_done, frameCount());
    if (m_done >= frameCount()) {
        emit encoding();
        encode();
        return;
    }
    // Renderers held back while the writers caught up.
    for (const auto& renderer : m_renderers)
        if (renderer->frame < 0) requestNext(*renderer);
}

void ExportJob::encode()
{
    const QString suffix = QFileInfo(m_request.outputPath).suffix().toLower();
    const ExportPreset& preset = m_request.preset;
    const bool sequence = preset.isValid() ? preset.format == ExportFormat::ImageSequence
                                           : suffix != QLatin1String("mp4") && suffix != QLatin1String("mov");
    if (sequence) {
        writeSequence();
        return;
    }
    const bool video = !preset.isValid() || preset.hasVideo();
    const bool withAudio = !preset.isValid() || preset.hasAudio();
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty()) {
        finish(false, tr("FFmpeg was not found; choose an image-sequence preset"));
        return;
    }
    const composition::Composition& shot = *m_request.composition;
    const double fps = shot.fpsDenominator() > 0 ? double(shot.fpsNumerator()) / shot.fpsDenominator() : 30.0;
    QStringList arguments{QStringLiteral("-y")};
    if (video) {
        arguments << QStringLiteral("-framerate") << QString::number(fps, 'f', 6)
                  << QStringLiteral("-start_number") << QString::number(m_request.firstFrame)
                  << QStringLiteral("-i")
                  << QDir(m_temporary->path()).filePath(QStringLiteral("frame-%08d.png"));
    }
    QVector<ExportAudioInput> audioInputs;
    const int audioRate = media::normalizedAudioSampleRate(shot.audioSampleRate());
    const double exportStart = m_request.firstFrame / fps;
    const double exportDuration = frameCount() / fps;
    ExportAudioRequest audioRequest;
    audioRequest.composition = m_request.composition.get();
    audioRequest.media = m_request.media.get();
    audioRequest.ffmpeg = ffmpeg;
    audioRequest.ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
    audioRequest.temporaryDirectory = m_temporary->path();
    audioRequest.frameRate = fps;
    audioRequest.exportStart = exportStart;
    audioRequest.exportEnd = exportStart + exportDuration;
    audioRequest.preRenderDirectory = m_request.preRenderDirectory;
    QString audioError;
    if (withAudio && !buildExportAudioInputs(audioRequest, audioInputs, &audioError)) {
        finish(false, audioError);
        return;
    }
    if (!video && audioInputs.isEmpty()) {
        finish(false, tr("The shot has no audio to export"));
        return;
    }
    arguments += exportAudioInputArguments(audioInputs);
    if (!audioInputs.isEmpty()) {
        arguments << QStringLiteral("-filter_complex")
                  << exportAudioMixFilter(audioInputs, audioRate, video ? 1 : 0);
        if (video) arguments << QStringLiteral("-map") << QStringLiteral("0:v:0");
        arguments << QStringLiteral("-map") << QStringLiteral("[aout]");
    }
    if (preset.isValid()) {
        // The preset's codecs, size, frame rate and rates.
        const bool h264 = preset.format == ExportFormat::MP4
                          || (preset.format == ExportFormat::MOV && preset.movCodec == ExportPreset::MovCodec::H264);
        QString encoder;
        if (video && h264) {
            encoder = chooseH264Encoder(ffmpeg, m_request.hardwareEncoding);
            OV_LOG_INFO(QStringLiteral("Export H.264 encoder: %1").arg(encoder));
        }
        arguments << exportVideoArguments(preset, encoder);
        if (video) {
            const QString filter = exportVideoFilter(preset, QSize(shot.width(), shot.height()),
                                                     shot.pixelAspectValue());
            if (!filter.isEmpty()) arguments << QStringLiteral("-vf") << filter;
            if (!preset.frameRateUseSource && preset.frameRate > 0.0)
                arguments << QStringLiteral("-r") << QString::number(preset.frameRate, 'f', 6);
        }
        arguments << (audioInputs.isEmpty() ? QStringList{QStringLiteral("-an")} : exportAudioArguments(preset));
    } else if (suffix == QLatin1String("mov")) {
        arguments << QStringLiteral("-c:v") << QStringLiteral("prores_ks")
                  << QStringLiteral("-profile:v") << QStringLiteral("3")
                  << QStringLiteral("-pix_fmt") << QStringLiteral("yuv422p10le");
    } else {
        // Options > Render "Use hardware encoding": a GPU H.264 encoder when
        // one works on this machine, libx264 otherwise.
        const QString encoder = chooseH264Encoder(ffmpeg, m_request.hardwareEncoding);
        OV_LOG_INFO(QStringLiteral("Export H.264 encoder: %1").arg(encoder));
        arguments << h264EncoderArguments(encoder)
                  << QStringLiteral("-movflags") << QStringLiteral("+faststart");
    }
    // A shot with non-square pixels: the frames are its pixels, and the
    // stream says how wide they are (AudioVideoSettings PixelAspectRatio).
    if (!preset.isValid() && std::abs(shot.pixelAspectValue() - 1.0) > 1e-9)
        arguments << QStringLiteral("-vf")
                  << QStringLiteral("setsar=%1").arg(sampleAspectRatio(shot.pixelAspectValue()));
    if (!preset.isValid() && !audioInputs.isEmpty())
        arguments << QStringLiteral("-c:a")
                  << (suffix == QLatin1String("mov") ? QStringLiteral("pcm_s16le") : QStringLiteral("aac"))
                  << QStringLiteral("-ar") << QString::number(audioRate)
                  << QStringLiteral("-ac") << QStringLiteral("2");
    arguments << QStringLiteral("-t") << QString::number(exportDuration, 'f', 6);
    arguments << m_request.outputPath;
    auto* process = new QProcess(this);
    m_process = process;
    connect(process, &QProcess::finished, this, [this, process](int exitCode, QProcess::ExitStatus status) {
        const bool ok = status == QProcess::NormalExit && exitCode == 0;
        process->deleteLater();
        finish(ok, ok ? tr("Exported video to %1").arg(QDir::toNativeSeparators(m_request.outputPath))
                      : tr("FFmpeg export failed"));
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        process->deleteLater();
        finish(false, tr("Could not start FFmpeg: %1").arg(process->errorString()));
    });
    process->start(ffmpeg, arguments);
}

void ExportJob::writeSequence()
{
    const QFileInfo target(m_request.outputPath);
    const ExportPreset& preset = m_request.preset;
    const QString suffix = preset.isValid() ? preset.extension() : target.suffix().toLower();
    const composition::Composition& shot = *m_request.composition;
    const QSize shotSize(shot.width(), shot.height());
    for (int frame = m_request.firstFrame; frame <= m_request.lastFrame; ++frame) {
        const QString source = QDir(m_temporary->path()).filePath(
            QStringLiteral("frame-%1.png").arg(frame, 8, 10, QLatin1Char('0')));
        const QString number = QStringLiteral("%1").arg(frame - m_request.firstFrame + 1, 8, 10,
                                                         QLatin1Char('0'));
        // A preset's prefix stands in for the task's name.
        const QString output = target.dir().filePath(
            preset.isValid() && !preset.prefix.isEmpty()
                ? QStringLiteral("%1%2.%3").arg(preset.prefix, number, suffix)
                : QStringLiteral("%1-%2.%3").arg(target.completeBaseName(), number, suffix));
        QImage image(source);
        if (preset.isValid()) image = sequenceFrame(image, preset, shotSize, shot.pixelAspectValue());
        QString error;
        const int quality = preset.isValid() && suffix == QLatin1String("jpg") ? preset.quality : -1;
        const bool ok = suffix == QLatin1String("exr") ? media::writeExr(image, output, &error)
                                                       : image.save(output, nullptr, quality);
        if (!ok) {
            finish(false, tr("Image sequence export failed: %1").arg(output));
            return;
        }
    }
    finish(true, tr("Exported image sequence to %1").arg(QDir::toNativeSeparators(target.absolutePath())));
}

QImage ExportJob::sequenceFrame(const QImage& frame, const ExportPreset& preset, const QSize& source,
                                double pixelAspect)
{
    QImage image = frame.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    const QSize target = preset.resolution(source);
    if (target != source) {
        // As exportVideoFilter scales a movie: to the square shape first,
        // then to the preset's size by its scale mode.
        if (std::abs(pixelAspect - 1.0) > 1e-9) {
            image = image.scaled(qMax(1, int(std::lround(image.width() * pixelAspect))), image.height(),
                                 Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        }
        if (preset.scaleMode == ExportPreset::ScaleMode::IgnoreAspectRatio) {
            image = image.scaled(target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        } else {
            const bool expand = preset.scaleMode == ExportPreset::ScaleMode::KeepAspectRatioByExpanding;
            const QImage fitted = image.scaled(target, expand ? Qt::KeepAspectRatioByExpanding
                                                              : Qt::KeepAspectRatio,
                                               Qt::SmoothTransformation);
            QImage canvas(target, QImage::Format_RGBA8888_Premultiplied);
            canvas.fill(Qt::transparent);
            QPainter painter(&canvas);
            painter.drawImage(QPointF((target.width() - fitted.width()) / 2.0,
                                      (target.height() - fitted.height()) / 2.0), fitted);
            painter.end();
            image = canvas;
        }
    }
    if (!preset.alpha || preset.imageFormat == ExportPreset::ImageFormat::Jpg
        || preset.imageFormat == ExportPreset::ImageFormat::Bmp) {
        // Without alpha the picture stands on black.
        QImage opaque(image.size(), QImage::Format_RGB32);
        opaque.fill(Qt::black);
        QPainter painter(&opaque);
        painter.drawImage(0, 0, image);
        painter.end();
        return opaque;
    }
    return image.convertToFormat(QImage::Format_RGBA8888);
}

void ExportJob::finish(bool ok, const QString& message)
{
    if (!m_running && !m_temporary) {
        emit finished(ok, message);
        return;
    }
    m_running = false;
    for (const auto& renderer : m_renderers) {
        renderer->manager->disconnect(this);
        renderer->manager->cancelAll();
    }
    m_writers.waitForDone();
    m_temporary.reset();
    emit finished(ok, message);
}

} // namespace render
} // namespace openvegas
