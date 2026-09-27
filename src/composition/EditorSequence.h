#pragma once

#include <QString>
#include <QVector>

namespace openvegas {
namespace composition {

// A track header inside an EditorSequence (mirrors reference "VideoTrack" /
// "AudioTrack" elements in the .vegfx <EditorSequence> section). Pure
// header/state, no clip contents — clips live in layers/compositions.
struct SequenceTrack
{
    QString name;
    QString id;          // identifying GUID
    bool visible = true; // video
    bool muted = false;  // audio
    bool solo = false;   // audio
    bool locked = false;
    double audioLevel = 0.0;    // audioLevel property
    double stereoBalance = 0.0; // stereoBalance property
};

// Mirrors reference project::EditorSequence (RTTI EditorSequence::VideoTracks /
// AudioTracks). A sequence owns a set of video/audio track headers and its own
// audio/video settings + timeline/render state; it references the primary
// composition(s) that actually hold layers/clips.
struct EditorSequence
{
    QString name = QStringLiteral("Editor");

    long long cti = 0;          // <CTI> current time indicator (frames)
    long long inPoint = 0;      // <InPoint>
    long long outPoint = 0;     // <OutPoint>
    double timelineZoom = 0.0;
    int timelineTimeFormat = 1000;
    int timelineSnapMode = 1000;
    int timelineScrollSyncMode = 0;
    bool timelineValueGraph = false;
    bool timelineGraphAutoZoom = true;

    int videoPreviewSize = 1002;  // <VideoPreviewSize>
    int audioPreviewSize = 1002;  // <AudioPreviewSize>
    int previewMode = 1002;       // <PreviewMode>

    // <AudioVideoSettings>
    long long frameCount = 0;
    int audioSampleRate = 48000;
    int width = 0;
    int height = 0;
    double fps = 30.0;

    // <RenderSettings>
    bool motionBlurEnabled = true;
    double shutterAngle = 180.0;
    double shutterPhase = -90.0;
    int maxNumOfSamples = 20;
    bool useAdaptiveSamples = true;

    QVector<SequenceTrack> videoTracks;
    QVector<SequenceTrack> audioTracks;
    SequenceTrack masterTrack;
};

} // namespace composition
} // namespace openvegas
