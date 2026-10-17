#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QThread>

#include <memory>

namespace openvegas {
namespace composition {
class Composition;
}
namespace media {
class MediaManager;
}
namespace render {

// The mix of a nested shot for playback, when its composite clip processes
// what it plays (native audio effects, audio transitions on it): the
// reference runs the composite clip's DSP after the child mix, so playback
// plays the shot's mix as one source and puts the clip's processing on it,
// as export does (renderNestedPcmClip). Mixed in the background as export
// mixes a shot (writeCompositionAudio), kept by the shot's state - an
// edited shot is mixed again; until a mix is there, playback expands the
// shot as before.
class NestedAudioMixCache : public QObject
{
    Q_OBJECT

public:
    explicit NestedAudioMixCache(QObject* parent = nullptr);
    ~NestedAudioMixCache() override;

    void setDirectory(const QString& directory) { m_directory = directory; }
    void setPreRenderDirectory(const QString& directory) { m_preRenders = directory; }
    // The file the shot's mix goes to as the shot is now.
    QString pathFor(const composition::Composition& shot, const media::MediaManager* media) const;
    // The shot's mix as it is now; empty while it is made (the first call
    // starts that) or when the shot has no audio.
    QString mixFor(const std::shared_ptr<composition::Composition>& shot,
                   const std::shared_ptr<media::MediaManager>& media);
    bool isBusy() const { return !m_building.isEmpty(); }

signals:
    void mixReady(const QString& shotId);

private:
    QString m_directory;
    QString m_preRenders;
    QSet<QString> m_building;   // paths being mixed
    QSet<QString> m_silent;     // paths of shots without audio
    QList<QPointer<QThread>> m_threads;
};

} // namespace render
} // namespace openvegas
