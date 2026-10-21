#include "render/PreRenderManager.h"

#include <QCoreApplication>
#include <QDir>

#include "composition/Composition.h"
#include "core/Log.h"
#include "media/AudioFormat.h"
#include "media/MediaManager.h"
#include "render/AudioExport.h"
#include "render/RenderManager.h"

#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

namespace openvegas {
namespace render {

PreRenderManager::PreRenderManager(QObject* parent)
    : QObject(parent)
{
    m_settle.setSingleShot(true);
    m_settle.setInterval(2000);
    connect(&m_settle, &QTimer::timeout, this, [this] {
        const QStringList changed = m_changed;
        m_changed.clear();
        for (const QString& id : changed) {
            const auto it = m_entries.constFind(id);
            if (it == m_entries.constEnd()) continue;
            if (const auto shot = it->shot.lock()) request(shot);
        }
    });
}

PreRenderManager::~PreRenderManager()
{
    cancelAll();
    if (m_audioThread) m_audioThread->wait();
}


void PreRenderManager::mixAudio(const QString& id)
{
    const auto shot = m_entries.value(id).shot.lock();
    if (!shot) {
        finishRunning(true);
        return;
    }
    // A copy of the shot as it was rendered, mixed away from the interface.
    auto snapshot = std::make_shared<composition::Composition>(*shot);
    const QString path = preRenderAudioPath(m_root, *shot, m_media.get());
    const QString root = m_root;
    const std::shared_ptr<media::MediaManager> media = m_media;
    const quint64 generation = m_generation;
    QPointer<PreRenderManager> self(this);
    QThread* thread = QThread::create([snapshot, path, root, media, generation, id, self] {
        QString error;
        const bool ok = writeCompositionAudio(*snapshot, media.get(), root, path, &error);
        if (!ok) OV_LOG_WARN(QStringLiteral("Pre-render audio of %1: %2").arg(id, error));
        if (PreRenderManager* manager = self.data()) {
            QMetaObject::invokeMethod(manager, [self, generation, id] {
                if (!self || self->m_generation != generation || self->m_runningId != id) return;
                // Without audio (none, or no FFmpeg) the frames still stand.
                self->finishRunning(true);
            }, Qt::QueuedConnection);
        }
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    m_audioThread = thread;
    thread->start();
}

void PreRenderManager::setPreRenderRoot(const QString& root)
{
    if (root == m_root) return;
    cancelAll();
    m_root = root;
    m_entries.clear();
}

QString PreRenderManager::folderFor(const composition::Composition& shot) const
{
    return preRenderFolder(m_root, shot);
}

QString PreRenderManager::signatureOf(const composition::Composition& shot) const
{
    // The state the frames are keyed by (layers, nested shots, media files),
    // at the start of the shot and at its size over transparency.
    return renderStateKey(shot, m_media.get(), 0.0, QSize(qMax(1, shot.width()), qMax(1, shot.height())),
                          true, true);
}

void PreRenderManager::setState(const QString& id, State state)
{
    Entry& entry = m_entries[id];
    if (entry.state == state && state != State::InProgress) return;
    entry.state = state;
    emit stateChanged(id);
}

void PreRenderManager::request(const std::shared_ptr<composition::Composition>& shot)
{
    if (!shot || m_root.isEmpty()) return;
    const QString id = shot->id().value();
    if (id.isEmpty()) return;
    Entry& entry = m_entries[id];
    entry.shot = shot;
    if (m_runningId == id) {
        // Asked again while running: start over behind the others.
        finishRunning(false);
    }
    m_queue.removeAll(id);
    m_queue.append(id);
    entry.done = 0;
    entry.total = 0;
    setState(id, State::Queued);
    startNext();
}

void PreRenderManager::remove(const std::shared_ptr<composition::Composition>& shot)
{
    if (!shot) return;
    const QString id = shot->id().value();
    m_queue.removeAll(id);
    m_changed.removeAll(id);
    if (m_runningId == id) finishRunning(false);
    const QString folder = folderFor(*shot);
    if (!folder.isEmpty()) QDir(folder).removeRecursively();
    m_entries.remove(id);
    emit stateChanged(id);
    startNext();
}

void PreRenderManager::cancelAll()
{
    m_settle.stop();
    m_changed.clear();
    const QStringList waiting = m_queue;
    m_queue.clear();
    for (const QString& id : waiting) setState(id, State::None);
    if (!m_runningId.isEmpty()) {
        const QString id = m_runningId;
        finishRunning(false);
        setState(id, State::None);
    }
}

void PreRenderManager::checkForChanges()
{
    bool any = false;
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        if (it->state != State::Done) continue;
        const auto shot = it->shot.lock();
        if (!shot) continue;
        if (signatureOf(*shot) == it->signature) continue;
        // Out of date: the old frames no longer match and would only be read
        // by mistake; gone now, made again once the edits settle.
        const QString folder = folderFor(*shot);
        if (!folder.isEmpty()) QDir(folder).removeRecursively();
        it->state = State::None;
        emit stateChanged(it.key());
        if (!m_changed.contains(it.key())) m_changed.append(it.key());
        any = true;
    }
    if (any || !m_changed.isEmpty()) m_settle.start();
}

void PreRenderManager::startNext()
{
    if (m_running || !m_runningId.isEmpty() || m_queue.isEmpty()) return;
    const QString id = m_queue.takeFirst();
    Entry& entry = m_entries[id];
    const auto shot = entry.shot.lock();
    if (!shot) {
        m_entries.remove(id);
        startNext();
        return;
    }
    const QString folder = folderFor(*shot);
    QDir(folder).removeRecursively();
    if (folder.isEmpty() || !QDir().mkpath(folder)) {
        setState(id, State::Failed);
        emit failed(id, QCoreApplication::translate("PreRenderTask", "Could not create the pre-render directory."));
        startNext();
        return;
    }
    setState(id, State::Preparing);
    m_runningId = id;
    entry.signature = signatureOf(*shot);
    const double fps = double(shot->fpsNumerator()) / qMax(1, shot->fpsDenominator());
    entry.total = qMax(1, qRound(shot->durationSeconds() * fps));
    entry.done = 0;

    // The shot rendered once over transparency at its own size, every frame
    // into the folder (the timeline cache's files, keyed by render state).
    auto* task = new RenderManager(this);
    task->setComposition(shot);
    task->setMediaManager(m_media);
    task->setPreRenderDirectory(m_root);
    task->setDiskCacheDirectory(folder);
    m_running = task;
    connect(task, &RenderManager::playbackCacheProgress, this, [this, id](int done, int total) {
        if (m_runningId != id) return;
        Entry& running = m_entries[id];
        running.done = done;
        running.total = qMax(1, total);
        running.state = State::InProgress;
        emit stateChanged(id);
    });
    connect(task, &RenderManager::playbackCacheFailed, this, [this, id](const QString& reason) {
        if (m_runningId == id) finishRunning(false, reason);
    });
    connect(task, &RenderManager::playbackCacheFinished, this, [this, id] {
        if (m_runningId != id) return;
        const Entry& running = m_entries.value(id);
        // A cache run that gives up emits Finished before Failed.
        if (running.done < running.total) return;
        // The frames are there: now the audio.
        if (RenderManager* frames = m_running.data()) {
            frames->disconnect(this);
            m_running = nullptr;
            QTimer::singleShot(0, frames, [frames] { frames->cancelAll(); frames->deleteLater(); });
        }
        mixAudio(id);
    });
    task->startPlaybackCache(0.0, QSize(qMax(1, shot->width()), qMax(1, shot->height())), true, true);
}

void PreRenderManager::finishRunning(bool completed, const QString& reason)
{
    const QString id = m_runningId;
    m_runningId.clear();
    // A mix still going belongs to this run: its result is dropped.
    ++m_generation;
    if (RenderManager* task = m_running.data()) {
        task->disconnect(this);
        m_running = nullptr;
        // Not from inside its own signal.
        QTimer::singleShot(0, task, [task] { task->cancelAll(); task->deleteLater(); });
    }
    if (id.isEmpty()) return;
    if (completed) {
        setState(id, State::Done);
    } else if (!reason.isEmpty()) {
        setState(id, State::Failed);
        emit failed(id, reason);
    }
    QTimer::singleShot(0, this, &PreRenderManager::startNext);
}

PreRenderManager::State PreRenderManager::state(const QString& shotId) const
{
    return m_entries.value(shotId).state;
}

int PreRenderManager::framesDone(const QString& shotId) const
{
    return m_entries.value(shotId).done;
}

int PreRenderManager::framesTotal(const QString& shotId) const
{
    return m_entries.value(shotId).total;
}

QString PreRenderManager::statusText(const QString& shotId) const
{
    switch (state(shotId)) {
    case State::Queued:
        return QCoreApplication::translate("PreRenderProgressLabel", "The pre-render is in the queue.");
    case State::Preparing:
        return QCoreApplication::translate("PreRenderProgressLabel", "The pre-render is being prepared.");
    case State::InProgress:
        return QCoreApplication::translate("PreRenderProgressLabel", "Pre-rendering is in progress.");
    case State::Done:
        return QCoreApplication::translate("PreRenderProgressLabel", "The media has been pre-rendered.");
    case State::Failed:
    case State::None:
        break;
    }
    return QString();
}

} // namespace render
} // namespace openvegas
