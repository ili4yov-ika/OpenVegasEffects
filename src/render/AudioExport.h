#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include "media/AudioFormat.h"

namespace openvegas {
namespace composition { class Composition; struct Layer; }
namespace media { class MediaManager; }
namespace render {

// One FFmpeg audio input of an export, placed `delay` seconds after the
// export start. rawPcm inputs are stereo s16le files at sampleRate, already carrying
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
    int sampleRate = media::kDefaultAudioSampleRate;
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
    // The project's pre-render root: a nested shot pre-rendered as it is now
    // plays its pre-render's audio instead of being mixed again.
    QString preRenderDirectory;
};

bool fileHasAudioStream(const QString& ffprobe, const QString& path);

// FFmpeg input/filter construction shared by final export and nested mixing.
QStringList exportAudioInputArguments(const QVector<ExportAudioInput>& inputs);
QString exportAudioMixFilter(const QVector<ExportAudioInput>& inputs, int sampleRate,
                             int firstInputIndex = 1);

// Collects audio recursively, mixing a nested composition before applying its
// owning clip's speed, native effects, level and transitions. Cycles fail the
// request without appending partial input lists.
// Clips with native Audio effects or audio transitions are decoded (with the
// handles their transitions play into), processed with layer-local positions
// and combined over each transition window, then written as raw PCM. Must run
// on one thread; releases that thread's native audio modules when done.
bool buildExportAudioInputs(const ExportAudioRequest& request,
                            QVector<ExportAudioInput>& inputs, QString* error);

// A whole shot's mix - its layers, nested shots and audio effects, as export
// mixes them - written to `path` as 16-bit stereo PCM WAV at the shot's rate
// (through a part file, so a reader never sees half of it). True with no
// file when the shot has no audio. Nested shots pre-rendered under
// `preRenderDirectory` play their pre-render's audio.
bool writeCompositionAudio(const composition::Composition& shot, const media::MediaManager* media,
                           const QString& preRenderDirectory, const QString& path, QString* error);

// Whether a nested shot's clip has audio processing of its own - native
// audio effects, an audio transition on it - which the reference applies
// after the shot's own mix (its composite clip's DSP after the child mix).
bool nestedClipProcessesItsMix(const composition::Layer& layer, int clipIndex, double frameRate);

} // namespace render
} // namespace openvegas
