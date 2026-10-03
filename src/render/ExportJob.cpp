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
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
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
    if (m_renderer) m_renderer->cancelAll();
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
    m_renderer = std::make_unique<RenderManager>();
    m_renderer->setComposition(m_request.composition);
    m_renderer->setMediaManager(m_request.media);
    m_renderer->setPreRenderDirectory(m_request.preRenderDirectory);
    connect(m_renderer.get(), &RenderManager::frameReady, this, &ExportJob::consume);
    m_running = true;
    m_clock.start();
    m_frame = m_request.firstFrame;
    m_done = 0;
    m_primed = false;
    emit progress(0, frameCount());
    requestNext();
}

void ExportJob::cancel()
{
    if (!m_running) return;
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
    }
    if (m_renderer) m_renderer->cancelAll();
    finish(false, tr("Export cancelled"));
}

void ExportJob::requestNext()
{
    if (!m_running || !m_renderer) return;
    const composition::Composition& shot = *m_request.composition;
    const double fps = shot.fpsDenominator() > 0 ? double(shot.fpsNumerator()) / shot.fpsDenominator() : 30.0;
    m_renderer->requestFrame(m_frame, m_frame / fps, QSize(shot.width(), shot.height()), true);
}

void ExportJob::consume(int frameIndex, const QByteArray& rgba, const QSize& size)
{
    const composition::Composition& shot = *m_request.composition;
    if (!m_running || frameIndex != m_frame || size != QSize(shot.width(), shot.height())) return;
    // The first pass primes on-demand video decoders: the decode service gets
    // a turn before the same frame is requested again and kept.
    if (!m_primed) {
        m_primed = true;
        QTimer::singleShot(100, this, &ExportJob::requestNext);
        return;
    }
    QImage image(reinterpret_cast<const uchar*>(rgba.constData()), size.width(), size.height(),
                 size.width() * 4, QImage::Format_RGBA8888);
    const QString framePath = QDir(m_temporary->path()).filePath(
        QStringLiteral("frame-%1.png").arg(m_frame, 8, 10, QLatin1Char('0')));
    if (!image.copy().save(framePath)) {
        finish(false, tr("Could not write export frame %1").arg(m_frame));
        return;
    }
    ++m_done;
    emit progress(m_done, frameCount());
    if (m_frame >= m_request.lastFrame) {
        emit encoding();
        encode();
        return;
    }
    ++m_frame;
    m_primed = false;
    requestNext();
}

void ExportJob::encode()
{
    const QString suffix = QFileInfo(m_request.outputPath).suffix().toLower();
    if (suffix != QLatin1String("mp4") && suffix != QLatin1String("mov")) {
        writeSequence();
        return;
    }
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty()) {
        finish(false, tr("FFmpeg was not found; choose an image-sequence preset"));
        return;
    }
    const composition::Composition& shot = *m_request.composition;
    const double fps = shot.fpsDenominator() > 0 ? double(shot.fpsNumerator()) / shot.fpsDenominator() : 30.0;
    QStringList arguments{QStringLiteral("-y"), QStringLiteral("-framerate"),
                          QString::number(fps, 'f', 6), QStringLiteral("-start_number"),
                          QString::number(m_request.firstFrame), QStringLiteral("-i"),
                          QDir(m_temporary->path()).filePath(QStringLiteral("frame-%08d.png"))};
    QVector<ExportAudioInput> audioInputs;
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
    QString audioError;
    if (!buildExportAudioInputs(audioRequest, audioInputs, &audioError)) {
        finish(false, audioError);
        return;
    }
    for (const auto& input : audioInputs) {
        if (input.rawPcm) {
            arguments << QStringLiteral("-f") << QStringLiteral("s16le")
                      << QStringLiteral("-ar") << QStringLiteral("48000")
                      << QStringLiteral("-ac") << QStringLiteral("2")
                      << QStringLiteral("-t") << QString::number(input.sourceDuration, 'f', 6)
                      << QStringLiteral("-i") << input.path;
        } else {
            arguments << QStringLiteral("-ss") << QString::number(input.source, 'f', 6)
                      << QStringLiteral("-t") << QString::number(input.sourceDuration, 'f', 6)
                      << QStringLiteral("-i") << input.path;
        }
    }
    QStringList audioFilters;
    QStringList audioLabels;
    for (int i = 0; i < audioInputs.size(); ++i) {
        const auto& input = audioInputs.at(i);
        const QString label = QStringLiteral("a%1").arg(i);
        audioLabels << QStringLiteral("[%1]").arg(label);
        const QString stream = input.audioStreamIndex >= 0
            ? QStringLiteral("%1:a:%2").arg(i + 1).arg(input.audioStreamIndex)
            : QStringLiteral("%1:a").arg(i + 1);
        audioFilters << QStringLiteral("[%1]asetpts=PTS-STARTPTS,atempo=%2,volume=%3,adelay=%4:all=1[%5]")
            .arg(stream).arg(input.speed, 0, 'f', 6).arg(input.gain, 0, 'f', 6)
            .arg(qRound64(input.delay * 1000.0)).arg(label);
    }
    if (!audioInputs.isEmpty()) {
        audioFilters << QStringLiteral("%1amix=inputs=%2:normalize=0:dropout_transition=0[aout]")
            .arg(audioLabels.join(QString())).arg(audioInputs.size());
        arguments << QStringLiteral("-filter_complex") << audioFilters.join(QLatin1Char(';'))
                  << QStringLiteral("-map") << QStringLiteral("0:v:0")
                  << QStringLiteral("-map") << QStringLiteral("[aout]");
    }
    if (suffix == QLatin1String("mov")) {
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
    if (std::abs(shot.pixelAspectValue() - 1.0) > 1e-9)
        arguments << QStringLiteral("-vf")
                  << QStringLiteral("setsar=%1").arg(sampleAspectRatio(shot.pixelAspectValue()));
    if (!audioInputs.isEmpty())
        arguments << QStringLiteral("-c:a")
                  << (suffix == QLatin1String("mov") ? QStringLiteral("pcm_s16le") : QStringLiteral("aac"));
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
    const QString suffix = target.suffix().toLower();
    for (int frame = m_request.firstFrame; frame <= m_request.lastFrame; ++frame) {
        const QString source = QDir(m_temporary->path()).filePath(
            QStringLiteral("frame-%1.png").arg(frame, 8, 10, QLatin1Char('0')));
        const QString output = target.dir().filePath(QStringLiteral("%1-%2.%3")
            .arg(target.completeBaseName()).arg(frame - m_request.firstFrame + 1, 8, 10,
                                                 QLatin1Char('0')).arg(suffix));
        const QImage image(source);
        QString error;
        const bool ok = suffix == QLatin1String("exr") ? media::writeExr(image, output, &error)
                                                       : image.save(output);
        if (!ok) {
            finish(false, tr("Image sequence export failed: %1").arg(output));
            return;
        }
    }
    finish(true, tr("Exported image sequence to %1").arg(QDir::toNativeSeparators(target.absolutePath())));
}

void ExportJob::finish(bool ok, const QString& message)
{
    if (!m_running && !m_temporary) {
        emit finished(ok, message);
        return;
    }
    m_running = false;
    if (m_renderer) {
        m_renderer->disconnect(this);
        m_renderer->cancelAll();
    }
    m_temporary.reset();
    emit finished(ok, message);
}

} // namespace render
} // namespace openvegas
