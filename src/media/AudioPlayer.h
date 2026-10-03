#pragma once
#include <QObject>
#include <QString>
#include <QVector>
#include <QStringList>
#include "core/Identifier.h"
#include "composition/Effect.h"
#include <memory>

namespace openvegas::media {
struct NativeAudioModule {
    core::Identifier pluginId;
    QStringList parameters;
    QString instanceKey;
    composition::Effect sourceEffect;
    double shotOrigin = 0.0;
    double shotRate = 1.0;
    double shotFps = 30.0;
    // Timeline seconds of the owning clip; native audio positions are local
    // to this layer, and AudioReverse derives its ranges from the duration.
    double layerStart = 0.0;
    double layerDuration = 0.0;
};
// Timeline coordinates; every active source contributes to one PCM master.
struct AudioClip {
    QString path;
    double start = 0, end = 0, sourceStart = 0, speed = 1, gain = 1;
    QVector<NativeAudioModule> nativeEffects;
    // An animated level (the layer's Audio > Level keys): linear gain samples
    // every envelopeStep timeline seconds from envelopeStart, multiplied into
    // `gain` and interpolated between samples. Empty means a constant gain.
    QVector<float> envelope;
    double envelopeStart = 0, envelopeStep = 0;
    int audioStreamIndex = -1;
    double gainAt(double timelineSeconds) const {
        if (envelope.isEmpty() || envelopeStep <= 0) return gain;
        const double position = (timelineSeconds - envelopeStart) / envelopeStep;
        if (position <= 0) return gain * envelope.first();
        const qsizetype index = qsizetype(position);
        if (index + 1 >= envelope.size()) return gain * envelope.last();
        const double fraction = position - double(index);
        return gain * (envelope[index] + (envelope[index + 1] - envelope[index]) * fraction);
    }
};
// An audio transition between two entries of the clip list (-1 = silence).
// Both sources play through the window (extended into their handles); the
// module combines them instead of their plain sum. Timeline seconds.
struct AudioTransition {
    int fromClip = -1;
    int toClip = -1;
    double start = 0, end = 0, cut = 0;
    core::Identifier pluginId;
    QStringList parameters;
};
class AudioPlayer : public QObject {
    Q_OBJECT
public:
    explicit AudioPlayer(QObject* parent = nullptr);
    ~AudioPlayer() override;
    static bool isAvailable();
    void setSource(const QString& path);
    const QString& source() const { return m_source; }
    void setClips(const QVector<AudioClip>& clips, double duration,
                  const QVector<AudioTransition>& transitions = {});
    void play(double timelineSeconds);
    void pause();
    void stop();
    void seek(double timelineSeconds);
    void setMuted(bool muted);
    bool isPlaying() const;
    double position() const;
signals:
    void levelsChanged(double left, double right);
    // Diagnostic/analysis tap of the PCM16 LE master, before the native device.
    void pcmMixed(const QByteArray& samples, double timelineSeconds);
    void playbackError(const QString& message);
private:
    struct Engine;
    std::unique_ptr<Engine> m_engine;
    QVector<AudioClip> m_clips;
    QVector<AudioTransition> m_transitions;
    QString m_source;
    double m_duration = 0, m_position = 0;
    bool m_muted = false;
    quint64 m_generation = 0;
};
}
