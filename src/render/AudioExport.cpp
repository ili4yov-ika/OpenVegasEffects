#include "render/AudioExport.h"

#include "composition/Composition.h"
#include "composition/Transition.h"
#include "media/MediaManager.h"
#include "plugin/NativeEffectRender.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QProcess>
#include <QtMath>

#include <cmath>
#include <cstring>

namespace openvegas {
namespace render {

namespace {

constexpr int kRate = 48000;

// The messages predate this file and are translated in MainWindow's context.
QString translated(const char* text)
{
    return QCoreApplication::translate("openvegas::ui::MainWindow", text);
}

QStringList valuesAt(const composition::Effect& effect, int frame)
{
    QStringList values;
    values.reserve(effect.parameterValues.size());
    for (int i = 0; i < effect.parameterValues.size(); ++i) {
        values.append(effect.parameterAt(i, frame).toString());
    }
    return values;
}

// A clip rendered to PCM because it carries native Audio effects or takes
// part in an audio transition; `start` is the composition time of samples[0]
// and the clip's gain is already applied.
struct PcmClip
{
    QVector<qint16> samples;
    double start = 0.0;
};

bool renderPcmClip(const ExportAudioRequest& request, const composition::Layer& owner,
                   const composition::Clip& clip,
                   const QString& mediaPath, int audioStreamIndex, double spanStart, double spanEnd,
                   const QVector<const composition::Effect*>& effects,
                   const QString& instancePrefix, PcmClip& result, QString* error)
{
    const double speed = clip.speed > 0.0 ? clip.speed : 1.0;
    const double spanDuration = spanEnd - spanStart;
    const QString decodedPath = QDir(request.temporaryDirectory)
                                    .filePath(instancePrefix + QStringLiteral("-decoded.pcm"));
    QProcess decode;
    QStringList decodeArguments {
        QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"),
        QStringLiteral("-ss"),
        QString::number(clip.sourceStartSeconds + (spanStart - clip.startSeconds) * speed, 'f', 6),
        QStringLiteral("-t"), QString::number(spanDuration * speed, 'f', 6),
        QStringLiteral("-i"), mediaPath, QStringLiteral("-vn"),
        QStringLiteral("-ac"), QStringLiteral("2"),
        QStringLiteral("-ar"), QString::number(kRate),
        QStringLiteral("-af"), QStringLiteral("atempo=%1").arg(speed, 0, 'f', 6),
        QStringLiteral("-f"), QStringLiteral("s16le"), decodedPath};
    if (audioStreamIndex >= 0) {
        decodeArguments.insert(decodeArguments.size() - 1, QStringLiteral("-map"));
        decodeArguments.insert(decodeArguments.size() - 1, QStringLiteral("0:a:%1").arg(audioStreamIndex));
    }
    decode.start(request.ffmpeg, decodeArguments);
    if (!decode.waitForFinished(-1) || decode.exitStatus() != QProcess::NormalExit
        || decode.exitCode() != 0) {
        if (error) {
            *error = translated(QT_TRANSLATE_NOOP("openvegas::ui::MainWindow",
                                                  "Native audio decode failed: %1"))
                         .arg(QString::fromUtf8(decode.readAllStandardError()).trimmed());
        }
        return false;
    }
    QFile decodedFile(decodedPath);
    if (!decodedFile.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = translated(QT_TRANSLATE_NOOP("openvegas::ui::MainWindow",
                                                  "Could not read decoded audio: %1"))
                         .arg(decodedPath);
        }
        return false;
    }
    const QByteArray bytes = decodedFile.readAll();
    decodedFile.close();
    QFile::remove(decodedPath);
    // Media shorter than the span leaves silence at the end.
    result.samples = QVector<qint16>(qsizetype(qRound64(spanDuration * kRate)) * 2, qint16(0));
    std::memcpy(result.samples.data(), bytes.constData(),
                size_t(qMin<qint64>(bytes.size(),
                                    qint64(result.samples.size()) * qint64(sizeof(qint16)))));
    result.start = spanStart;

    const double fps = request.frameRate;
    const qint64 firstSample = qRound64((spanStart - clip.startSeconds) * kRate);
    const plugin::NativeAudioLayer layer {qRound64(clip.durationSeconds * kRate), fps};
    for (int effectIndex = 0; effectIndex < effects.size(); ++effectIndex) {
        const composition::Effect& effect = *effects.at(effectIndex);
        // Animated values advance with the realtime 10 ms granularity;
        // static ones can use larger blocks.
        const bool animated = !effect.animation.isEmpty();
        const QStringList staticValues = valuesAt(effect, qRound(clip.startSeconds * fps));
        const bool ok = plugin::applyNativeAudioEffectBlocks(
            result.samples, 2, kRate, firstSample, effect.pluginId,
            [&](qint64 frameOffset) {
                return animated
                    ? valuesAt(effect, qRound((spanStart + double(frameOffset) / kRate) * fps))
                    : staticValues;
            },
            instancePrefix + QStringLiteral("-%1").arg(effectIndex), animated ? 480 : 4096,
            layer);
        if (!ok) {
            if (error) {
                *error = translated(QT_TRANSLATE_NOOP("openvegas::ui::MainWindow",
                                                      "Native audio effect failed: %1"))
                             .arg(effect.name);
            }
            return false;
        }
    }
    // The layer's Audio > Level plus the clip's own. An animated level is
    // evaluated every 10 ms and interpolated in between, as during playback.
    const auto gainAt = [&](double seconds) {
        return qPow(10.0, (owner.audioLevelAtSeconds(seconds, fps) + clip.audioLevel) / 20.0);
    };
    const qsizetype frameCount = result.samples.size() / 2;
    if (owner.transform.audioLevelCurve.isEmpty()) {
        const double gain = gainAt(spanStart);
        for (qint16& sample : result.samples) {
            sample = qint16(qBound(-32768.0, std::round(sample * gain), 32767.0));
        }
        return true;
    }
    constexpr qsizetype block = kRate / 100;
    double from = gainAt(spanStart);
    for (qsizetype blockStart = 0; blockStart < frameCount; blockStart += block) {
        const double to = gainAt(spanStart + double(blockStart + block) / kRate);
        const qsizetype blockEnd = qMin(frameCount, blockStart + block);
        for (qsizetype frame = blockStart; frame < blockEnd; ++frame) {
            const double gain = from + (to - from) * double(frame - blockStart) / block;
            for (int channel = 0; channel < 2; ++channel) {
                qint16& sample = result.samples[frame * 2 + channel];
                sample = qint16(qBound(-32768.0, std::round(sample * gain), 32767.0));
            }
        }
        from = to;
    }
    return true;
}

// Replaces the plain sum of a transition's two clips over its window: the
// module's result goes into one clip's buffer and the other side is silenced.
void applyTransition(const composition::Layer& layer, const composition::TransitionWindow& window,
                     double fps, QHash<int, PcmClip>& clips)
{
    const qint32 total = qint32(qMax<qint64>(1, qRound64((window.end - window.start) * kRate)));
    const qint32 cut = qint32(qRound64((window.cut - window.start) * kRate));
    // copy: read the window; write: replace it; otherwise silence it.
    const auto portion = [&](int clipIndex, QVector<qint16>* copy, const QVector<qint16>* write) {
        auto it = clips.find(clipIndex);
        if (it == clips.end()) return false;
        const qint64 offset = qRound64((window.start - it->start) * kRate);
        for (qint32 frame = 0; frame < total; ++frame) {
            const qint64 at = offset + frame;
            if (at < 0 || at * 2 + 1 >= it->samples.size()) continue;
            for (int channel = 0; channel < 2; ++channel) {
                qint16& sample = it->samples[qsizetype(at * 2 + channel)];
                const qsizetype local = qsizetype(frame) * 2 + channel;
                if (copy) (*copy)[local] = sample;
                else sample = write ? write->at(local) : qint16(0);
            }
        }
        return true;
    };
    QVector<qint16> from(qsizetype(total) * 2, qint16(0));
    QVector<qint16> to(from);
    const bool hasFrom = portion(window.fromClip, &from, nullptr);
    const bool hasTo = portion(window.toClip, &to, nullptr);
    if (!hasFrom && !hasTo) return;
    const composition::Effect& effect =
        layer.clips.at(window.ownerClip).effects.at(window.effectIndex);
    QVector<qint16> mixed;
    if (!plugin::nativeAudioTransitionRenderingVerified(effect.pluginId)
        || !plugin::applyNativeAudioTransition(mixed, from, to, 2, 0, total, effect.pluginId,
                                               valuesAt(effect, qRound(window.start * fps)), cut)
        || mixed.size() != from.size()) {
        // Same cross-fade the realtime mixer falls back to.
        mixed.resize(from.size());
        for (qint32 frame = 0; frame < total; ++frame) {
            const double w = double(frame) / total;
            for (int channel = 0; channel < 2; ++channel) {
                const qsizetype s = qsizetype(frame) * 2 + channel;
                mixed[s] = qint16(std::lround(from[s] * (1.0 - w) + to[s] * w));
            }
        }
    }
    if (hasFrom) {
        portion(window.fromClip, nullptr, &mixed);
        portion(window.toClip, nullptr, nullptr);
    } else {
        portion(window.toClip, nullptr, &mixed);
    }
}

} // namespace

bool fileHasAudioStream(const QString& ffprobe, const QString& path)
{
    if (ffprobe.isEmpty() || path.isEmpty()) return false;
    QProcess process;
    process.start(ffprobe, {QStringLiteral("-v"), QStringLiteral("error"),
        QStringLiteral("-select_streams"), QStringLiteral("a:0"),
        QStringLiteral("-show_entries"), QStringLiteral("stream=index"),
        QStringLiteral("-of"), QStringLiteral("csv=p=0"), path});
    if (!process.waitForFinished(3000)) { process.kill(); return false; }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0
        && !process.readAllStandardOutput().trimmed().isEmpty();
}

bool buildExportAudioInputs(const ExportAudioRequest& request,
                            QVector<ExportAudioInput>& inputs, QString* error)
{
    if (!request.composition || !request.media) return true;
    const double fps = request.frameRate > 0.0 ? request.frameRate : 30.0;
    const auto fail = [] {
        plugin::releaseNativeEffectThreadRenderer();
        return false;
    };
    int layerIndex = 0;
    for (const composition::Layer& layer : request.composition->layers()) {
        ++layerIndex;
        if (!layer.visible || layer.muted) continue;
        const auto windows = composition::transitionWindows(
            layer, 0.5 / fps, [](const composition::Effect& effect) {
                return plugin::isAudioTransition(effect.pluginId);
            });
        QHash<int, PcmClip> pcmClips;
        for (int clipIndex = 0; clipIndex < layer.clips.size(); ++clipIndex) {
            const composition::Clip& clip = layer.clips.at(clipIndex);
            const media::MediaAsset asset = request.media->assetById(clip.mediaId);
            if (!asset.isValid() || !fileHasAudioStream(request.ffprobe, asset.filePath())) {
                continue;
            }
            const double speed = clip.speed > 0.0 ? clip.speed : 1.0;
            QVector<const composition::Effect*> nativeEffects;
            for (const composition::Effect& effect : clip.effects) {
                if (effect.enabled && plugin::nativeAudioEffectRenderingVerified(effect.pluginId)) {
                    nativeEffects.append(&effect);
                }
            }
            bool inTransition = false;
            double spanStart = clip.startSeconds;
            double spanEnd = clip.endSeconds();
            for (const auto& window : windows) {
                if (window.toClip == clipIndex) {
                    spanStart = qMin(spanStart, window.start);
                    inTransition = true;
                }
                if (window.fromClip == clipIndex) {
                    spanEnd = qMax(spanEnd, window.end);
                    inTransition = true;
                }
            }
            if (nativeEffects.isEmpty() && !inTransition && layer.transform.audioLevelCurve.isEmpty()) {
                // Plain clips keep the direct FFmpeg input/filter path; an
                // animated level goes through PCM like the native effects.
                const double first = qMax(request.exportStart, clip.startSeconds);
                const double last = qMin(request.exportEnd, clip.endSeconds());
                if (last <= first) continue;
                inputs.append({asset.filePath(),
                               clip.sourceStartSeconds + (first - clip.startSeconds) * speed,
                               (last - first) * speed, first - request.exportStart, speed,
                               qPow(10.0, (layer.transform.audioLevel + clip.audioLevel) / 20.0), false, asset.audioStreamIndex()});
                continue;
            }
            // Native positions are layer-local and modules such as
            // AudioReverse or the reverbs read outside the exported part, so
            // the whole clip - plus the handles its transitions play into -
            // is processed and trimmed afterwards. The media has no material
            // before its own start.
            spanStart = qMax(spanStart, clip.startSeconds - clip.sourceStartSeconds / speed);
            if (spanEnd <= spanStart) continue;
            PcmClip pcm;
            if (!renderPcmClip(request, layer, clip, asset.filePath(), asset.audioStreamIndex(), spanStart, spanEnd, nativeEffects,
                               QStringLiteral("export-%1-%2").arg(layerIndex).arg(clipIndex),
                               pcm, error)) {
                return fail();
            }
            pcmClips.insert(clipIndex, std::move(pcm));
        }
        for (const auto& window : windows) {
            applyTransition(layer, window, fps, pcmClips);
        }
        for (auto it = pcmClips.cbegin(); it != pcmClips.cend(); ++it) {
            const double clipEnd = it->start + double(it->samples.size() / 2) / kRate;
            const double first = qMax(request.exportStart, it->start);
            const double last = qMin(request.exportEnd, clipEnd);
            if (last <= first) continue;
            const qsizetype keepFirst = qsizetype(qRound64((first - it->start) * kRate));
            const qsizetype keepFrames = qMin(qsizetype(qRound64((last - first) * kRate)),
                                              it->samples.size() / 2 - keepFirst);
            if (keepFrames <= 0) continue;
            const QString rawPath = QDir(request.temporaryDirectory)
                                        .filePath(QStringLiteral("audio-native-%1.pcm")
                                                      .arg(inputs.size()));
            QFile rawFile(rawPath);
            const qint64 byteCount = qint64(keepFrames) * 2 * qint64(sizeof(qint16));
            if (!rawFile.open(QIODevice::WriteOnly | QIODevice::Truncate)
                || rawFile.write(reinterpret_cast<const char*>(
                                     it->samples.constData() + keepFirst * 2), byteCount)
                       != byteCount) {
                if (error) {
                    *error = translated(QT_TRANSLATE_NOOP("openvegas::ui::MainWindow",
                                                          "Could not write processed audio: %1"))
                                 .arg(rawPath);
                }
                return fail();
            }
            rawFile.close();
            inputs.append({rawPath, 0.0, double(keepFrames) / kRate,
                           first - request.exportStart, 1.0, 1.0, true});
        }
    }
    plugin::releaseNativeEffectThreadRenderer();
    return true;
}

} // namespace render
} // namespace openvegas
