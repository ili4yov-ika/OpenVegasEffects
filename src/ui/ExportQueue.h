#pragma once

#include "project/VegfxSerializer.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVector>

#include <memory>

namespace openvegas {
namespace composition {
class Composition;
}
namespace media {
class MediaManager;
}
namespace render {
class ExportJob;
}
namespace ui {

// One task of the export queue - a row of the reference's ExportTaskItemModel
// (Name, Preset, Output, Progress, Duration, Elapsed).
struct ExportTask
{
    // The biff::ui::common::AsyncTask states the queue shows.
    enum class State { Ready, Started, Finished, Error, Cancelled };

    QString id;
    QString name;            // the shot's name
    QString shotId;          // which shot of the snapshot is exported
    QString preset;          // the Export panel preset it was queued with
    QString outputPath;
    int firstFrame = 0;
    int lastFrame = 0;       // inclusive
    double frameRate = 30.0;
    // The project as it was when the task was queued (the reference's export
    // snapshot), so later edits do not change what the task exports.
    QString snapshotPath;
    State state = State::Ready;
    int framesDone = 0;
    qint64 elapsedMilliseconds = 0;
    QString message;

    int frameCount() const { return qMax(1, lastFrame - firstFrame + 1); }
    double durationSeconds() const { return frameRate > 0.0 ? frameCount() / frameRate : 0.0; }
};

// The export queue (the reference's ExportTaskManager): tasks are run one
// after the other while the queue is started, each from its snapshot, by a
// render::ExportJob; the list is kept in a tasks file so it survives a
// restart, and a finished task's snapshot is deleted.
class ExportQueue : public QObject
{
    Q_OBJECT

public:
    explicit ExportQueue(QObject* parent = nullptr);
    ~ExportQueue() override;

    void setMediaManager(std::shared_ptr<media::MediaManager> media) { m_media = std::move(media); }
    void setPreRenderDirectory(const QString& directory) { m_preRenderDirectory = directory; }
    void setHardwareEncoding(bool on) { m_hardwareEncoding = on; }
    // Where snapshots are written (Options' Default Snapshot Directory).
    void setSnapshotDirectory(const QString& directory) { m_snapshotDirectory = directory; }
    // The tasks file; empty keeps the queue in memory only.
    void setTasksFile(const QString& path) { m_tasksFile = path; }
    QString tasksFile() const { return m_tasksFile; }

    // Queues `shot` (the root or one of `project`'s shots) from frame `first`
    // to `last`: the project is written to a snapshot first. Returns the task
    // id, empty when the snapshot cannot be written.
    QString addTask(const composition::Composition& project, const composition::Composition& shot,
                    const media::MediaManager& media, const QString& preset,
                    const QString& outputPath, int firstFrame, int lastFrame,
                    const project::ProjectSaveOptions& options = {});

    const QVector<ExportTask>& tasks() const { return m_tasks; }
    int indexOf(const QString& id) const;
    // Cancels the running one if it is among them; snapshots go too.
    void removeTasks(const QStringList& ids);
    void removeFinishedTasks();
    // Copies, Ready, with snapshots of their own.
    void duplicateTasks(const QStringList& ids);

    bool isRunning() const { return m_running; }
    void start();
    // Stops after cancelling the running task, which goes back to Ready.
    void suspend();

    bool load();
    bool save() const;

signals:
    void tasksChanged();
    void taskChanged(int row);
    void runningChanged(bool running);
    // Every task has run: the reference's "Beep speaker on completion".
    void queueFinished();

private:
    void runNext();
    void jobFinished(const QString& id, bool ok, const QString& message);
    QString writeSnapshot(const composition::Composition& project,
                          const media::MediaManager& media,
                          const project::ProjectSaveOptions& options) const;

    QVector<ExportTask> m_tasks;
    std::shared_ptr<media::MediaManager> m_media;
    QString m_preRenderDirectory;
    QString m_snapshotDirectory;
    QString m_tasksFile;
    bool m_hardwareEncoding = true;
    bool m_running = false;
    QPointer<render::ExportJob> m_job;
    QString m_jobTaskId;
};

QString exportTaskStateText(ExportTask::State state);

} // namespace ui
} // namespace openvegas
