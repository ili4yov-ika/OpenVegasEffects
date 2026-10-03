#pragma once

#include <QString>
#include <QVector>

namespace openvegas {
namespace composition { class Composition; }
namespace media { class MediaManager; }
namespace render {

// One FFmpeg audio input of an export, placed `delay` seconds after the
// export start. rawPcm inputs are stereo s16le 48 kHz files already carrying
// gain, native Audio effects and audio transitions; the others are media
// files read from `source` at `speed` and scaled by `gain`.
struct ExportAudioInput
{
    QString path;
    double source = 0.0;
    double sourceDuration = 0.0;
    double delay = 0.0;
    double speed = 1.0;
    double gain = 1.0;
    bool rawPcm = false;
    int audioStreamIndex = -1;
};

struct ExportAudioRequest
{
    const composition::Composition* composition = nullptr;
    const media::MediaManager* media = nullptr;
    QString ffmpeg;
    QString ffprobe;
    QString temporaryDirectory;
    double frameRate = 30.0;
    double exportStart = 0.0;
    double exportEnd = 0.0;
};

bool fileHasAudioStream(const QString& ffprobe, const QString& path);

// Collects the audio of the composition's top-level layers for an export.
// Clips with native Audio effects or audio transitions are decoded (with the
// handles their transitions play into), processed with layer-local positions
// and combined over each transition window, then written as raw PCM. Must run
// on one thread; releases that thread's native audio modules when done.
bool buildExportAudioInputs(const ExportAudioRequest& request,
                            QVector<ExportAudioInput>& inputs, QString* error);

} // namespace render
} // namespace openvegas
