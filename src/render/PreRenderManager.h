#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <memory>

namespace openvegas {
namespace composition {
class Composition;
}
namespace media {
class MediaManager;
}
namespace render {

class RenderManager;

// The reference's PreRenderManager: the composite shots asked to be
// pre-rendered (AssetPreRenderMenu "Make Pre-Render(s)") wait in a queue and
// are rendered one at a time, each into its folder under the project's
// pre-render directory, where RenderWorker reads them instead of rendering the
// shot live while its state matches. PreRenderProgressLabel shows where each
// one is: in the queue, being prepared, in progress, pre-rendered.
//
// A shot edited after its pre-render (its render state - layers, nested
// shots, media files - no longer what was rendered) is queued again once the
// edits have settled; until it is done again the shot renders live.
class PreRenderManager : public QObject
{
    Q_OBJECT

public:
    enum class State { None, Queued, Preparing, InProgress, Done, Failed };

    explicit PreRenderManager(QObject* parent = nullptr);
    ~PreRenderManager() override;

    // Options/PreRenderDirectoryPath/<project>.
    void setPreRenderRoot(const QString& root);
    QString preRenderRoot() const { return m_root; }
    void setMediaManager(std::shared_ptr<media::MediaManager> media) { m_media = std::move(media); }
    // How long a changed shot waits for more edits before it is queued again.
    void setSettleDelay(int milliseconds) { m_settle.setInterval(milliseconds); }

    // Make Pre-Render(s): queued behind the others (again, when it was done).
    void request(const std::shared_ptr<composition::Composition>& shot);
    // Remove Pre-Render(s): taken out of the queue, stopped, its files gone.
    void remove(const std::shared_ptr<composition::Composition>& shot);
    // Every task stopped and the queue emptied ("Waiting for pre-render tasks
    // to cancel."), as before the pre-render directory is deleted.
    void cancelAll();
    // Looks for pre-rendered shots edited since (call after changes).
    void checkForChanges();

    State state(const QString& shotId) const;
    // PreRenderProgressLabel's text for the shot; empty when it has none.
    QString statusText(const QString& shotId) const;
    int framesDone(const QString& shotId) const;
    int framesTotal(const QString& shotId) const;
    bool isBusy() const { return !m_queue.isEmpty() || m_running; }

signals:
    // A shot's state or progress changed.
    void stateChanged(const QString& shotId);
    void failed(const QString& shotId, const QString& reason);

private:
    struct Entry
    {
        std::weak_ptr<composition::Composition> shot;
        State state = State::None;
        int done = 0;
        int total = 0;
        QString signature;   // the render state it was made for
    };

    QString folderFor(const composition::Composition& shot) const;
    QString signatureOf(const composition::Composition& shot) const;
    void setState(const QString& id, State state);
    void startNext();
    void finishRunning(bool completed, const QString& reason = QString());

    QString m_root;
    std::shared_ptr<media::MediaManager> m_media;
    QHash<QString, Entry> m_entries;   // by shot ID
    QStringList m_queue;               // shot IDs waiting
    QString m_runningId;
    QPointer<RenderManager> m_running;
    QTimer m_settle;
    QStringList m_changed;             // shots to queue again once settled
};

} // namespace render
} // namespace openvegas
