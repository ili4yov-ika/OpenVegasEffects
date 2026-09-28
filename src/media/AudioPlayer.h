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
};
// Timeline coordinates; every active source contributes to one PCM master.
struct AudioClip {
    QString path;
    double start = 0, end = 0, sourceStart = 0, speed = 1, gain = 1;
    QVector<NativeAudioModule> nativeEffects;
};
class AudioPlayer : public QObject {
    Q_OBJECT
public:
    explicit AudioPlayer(QObject* parent = nullptr);
    ~AudioPlayer() override;
    static bool isAvailable();
    void setSource(const QString& path);
    const QString& source() const { return m_source; }
    void setClips(const QVector<AudioClip>& clips, double duration);
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
    QString m_source;
    double m_duration = 0, m_position = 0;
    bool m_muted = false;
    quint64 m_generation = 0;
};
}
