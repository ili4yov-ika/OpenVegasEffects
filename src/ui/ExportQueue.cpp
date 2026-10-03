#include "ui/ExportQueue.h"

#include "composition/Composition.h"
#include "media/MediaManager.h"
#include "render/ExportJob.h"

#include <QCoreApplication>
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QUuid>

#include <functional>

namespace openvegas {
namespace ui {
namespace {

const char* stateToken(ExportTask::State state)
{
    switch (state) {
    case ExportTask::State::Started: return "Started";
    case ExportTask::State::Finished: return "Finished";
    case ExportTask::State::Error: return "Error";
    case ExportTask::State::Cancelled: return "Cancelled";
    default: return "Ready";
    }
}

ExportTask::State stateFromToken(const QString& token)
{
    if (token == QLatin1String("Finished")) return ExportTask::State::Finished;
    if (token == QLatin1String("Error")) return ExportTask::State::Error;
    if (token == QLatin1String("Cancelled")) return ExportTask::State::Cancelled;
    // A task that was running when the application closed starts over.
    return ExportTask::State::Ready;
}

// The shot of a loaded snapshot with `id`: the root, one of its shots, or one
// only a layer nests.
std::shared_ptr<composition::Composition> findShot(const std::shared_ptr<composition::Composition>& root,
                                                   const QString& id)
{
    if (root->id().value() == id) return root;
    if (auto shot = root->compositeShot(core::Identifier(id))) return shot;
    std::shared_ptr<composition::Composition> found;
    std::function<void(const composition::Composition&)> visit = [&](const composition::Composition& comp) {
        for (const composition::Layer& layer : comp.layers()) {
            for (const composition::Clip& clip : layer.clips) {
                if (found || !clip.nestedComposition) continue;
                if (clip.nestedComposition->id().value() == id) found = clip.nestedComposition;
                else visit(*clip.nestedComposition);
            }
        }
    };
    visit(*root);
    return found;
}

} // namespace

QString exportTaskStateText(ExportTask::State state)
{
    // The reference's AsyncTask state names.
    switch (state) {
    case ExportTask::State::Started:
        return QCoreApplication::translate("biff::ui::common::AsyncTask", "Started");
    case ExportTask::State::Finished:
        return QCoreApplication::translate("biff::ui::common::AsyncTask", "Finished");
    case ExportTask::State::Error:
        return QCoreApplication::translate("biff::ui::common::AsyncTask", "Error");
    case ExportTask::State::Cancelled:
        return QCoreApplication::translate("biff::ui::common::AsyncTask", "Cancelled");
    default:
        return QCoreApplication::translate("biff::ui::common::AsyncTask", "Ready");
    }
}

ExportQueue::ExportQueue(QObject* parent) : QObject(parent) {}

ExportQueue::~ExportQueue()
{
    if (m_job) {
        m_job->disconnect(this);
        m_job->cancel();
    }
}

QString ExportQueue::writeSnapshot(const composition::Composition& project,
                                   const media::MediaManager& media,
                                   const project::ProjectSaveOptions& options) const
{
    const QString folder = m_snapshotDirectory.isEmpty()
        ? QDir::temp().filePath(QStringLiteral("OpenVegasExportSnapshots")) : m_snapshotDirectory;
    if (!QDir().mkpath(folder)) return QString();
    const QString path = QDir(folder).filePath(
        QStringLiteral("export-%1.vegfx").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    project::ProjectSaveOptions snapshot = options;
    snapshot.isAutoSave = false;
    snapshot.screenLayout.clear();
    // Absolute paths: the snapshot does not live beside the media.
    snapshot.useRelativePaths = false;
    return project::VegfxSerializer::saveToFile(path, project, media, snapshot).isSuccess() ? path : QString();
}

QString ExportQueue::addTask(const composition::Composition& project, const composition::Composition& shot,
                             const media::MediaManager& media, const QString& preset,
                             const QString& outputPath, int firstFrame, int lastFrame,
                             const project::ProjectSaveOptions& options)
{
    ExportTask task;
    task.snapshotPath = writeSnapshot(project, media, options);
    if (task.snapshotPath.isEmpty()) return QString();
    task.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    task.name = shot.name();
    task.shotId = shot.id().value();
    task.preset = preset;
    task.outputPath = outputPath;
    task.firstFrame = qMax(0, firstFrame);
    task.lastFrame = qMax(task.firstFrame, lastFrame);
    task.frameRate = double(shot.fpsNumerator()) / qMax(1, shot.fpsDenominator());
    m_tasks.append(task);
    save();
    emit tasksChanged();
    if (m_running && !m_job) runNext();
    return task.id;
}

int ExportQueue::indexOf(const QString& id) const
{
    for (int i = 0; i < m_tasks.size(); ++i)
        if (m_tasks.at(i).id == id) return i;
    return -1;
}

void ExportQueue::removeTasks(const QStringList& ids)
{
    if (ids.contains(m_jobTaskId) && m_job) {
        m_job->disconnect(this);
        m_job->cancel();
        m_job->deleteLater();
        m_job = nullptr;
        m_jobTaskId.clear();
    }
    for (int i = m_tasks.size() - 1; i >= 0; --i) {
        if (!ids.contains(m_tasks.at(i).id)) continue;
        QFile::remove(m_tasks.at(i).snapshotPath);
        m_tasks.removeAt(i);
    }
    save();
    emit tasksChanged();
    if (m_running) runNext();
}

void ExportQueue::removeFinishedTasks()
{
    QStringList finished;
    for (const ExportTask& task : m_tasks)
        if (task.state == ExportTask::State::Finished) finished.append(task.id);
    if (!finished.isEmpty()) removeTasks(finished);
}

void ExportQueue::duplicateTasks(const QStringList& ids)
{
    const QVector<ExportTask> current = m_tasks;
    for (const ExportTask& task : current) {
        if (!ids.contains(task.id)) continue;
        ExportTask copy = task;
        copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        copy.state = ExportTask::State::Ready;
        copy.framesDone = 0;
        copy.elapsedMilliseconds = 0;
        copy.message.clear();
        // A snapshot of its own, so either can finish and clean up.
        const QFileInfo source(task.snapshotPath);
        copy.snapshotPath = source.dir().filePath(
            QStringLiteral("export-%1.vegfx").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        if (!QFile::copy(task.snapshotPath, copy.snapshotPath)) continue;
        m_tasks.append(copy);
    }
    save();
    emit tasksChanged();
    if (m_running && !m_job) runNext();
}

void ExportQueue::start()
{
    if (m_running) return;
    m_running = true;
    emit runningChanged(true);
    runNext();
}

void ExportQueue::suspend()
{
    if (!m_running) return;
    m_running = false;
    if (m_job) {
        m_job->disconnect(this);
        m_job->cancel();
        m_job->deleteLater();
        m_job = nullptr;
        const int row = indexOf(m_jobTaskId);
        m_jobTaskId.clear();
        if (row >= 0) {
            ExportTask& task = m_tasks[row];
            task.state = ExportTask::State::Ready;
            task.framesDone = 0;
            emit taskChanged(row);
        }
    }
    save();
    emit runningChanged(false);
}

void ExportQueue::runNext()
{
    if (!m_running || m_job) return;
    int row = -1;
    for (int i = 0; i < m_tasks.size(); ++i) {
        if (m_tasks.at(i).state == ExportTask::State::Ready) {
            row = i;
            break;
        }
    }
    if (row < 0) {
        m_running = false;
        emit runningChanged(false);
        emit queueFinished();
        return;
    }
    ExportTask& task = m_tasks[row];
    // The snapshot, read back as the project it was.
    auto root = std::make_shared<composition::Composition>();
    media::MediaManager staged;
    std::shared_ptr<composition::Composition> shot;
    const core::Result loaded = project::VegfxSerializer::loadFromFile(task.snapshotPath, root.get(), &staged);
    if (loaded.isSuccess()) shot = findShot(root, task.shotId);
    if (!shot) {
        task.state = ExportTask::State::Error;
        task.message = loaded.isFailure()
            ? loaded.message()
            : QCoreApplication::translate("biff::ui::exporter::ExportTask",
                                          "The export project snapshot could not be loaded.");
        emit taskChanged(row);
        save();
        QMetaObject::invokeMethod(this, &ExportQueue::runNext, Qt::QueuedConnection);
        return;
    }
    render::ExportJob::Request request;
    request.composition = shot;
    request.media = m_media;
    request.outputPath = task.outputPath;
    request.firstFrame = task.firstFrame;
    request.lastFrame = task.lastFrame;
    request.preRenderDirectory = m_preRenderDirectory;
    request.hardwareEncoding = m_hardwareEncoding;
    auto* job = new render::ExportJob(request, this);
    m_job = job;
    m_jobTaskId = task.id;
    task.state = ExportTask::State::Started;
    task.framesDone = 0;
    task.message.clear();
    const QString id = task.id;
    connect(job, &render::ExportJob::progress, this, [this, id, job](int done, int) {
        const int at = indexOf(id);
        if (at < 0) return;
        m_tasks[at].framesDone = done;
        m_tasks[at].elapsedMilliseconds = job->elapsedMilliseconds();
        emit taskChanged(at);
    });
    connect(job, &render::ExportJob::finished, this, [this, id](bool ok, const QString& message) {
        jobFinished(id, ok, message);
    });
    emit taskChanged(row);
    job->start();
}

void ExportQueue::jobFinished(const QString& id, bool ok, const QString& message)
{
    const int row = indexOf(id);
    if (m_job) {
        m_job->deleteLater();
        m_job = nullptr;
    }
    m_jobTaskId.clear();
    if (row >= 0) {
        ExportTask& task = m_tasks[row];
        task.state = ok ? ExportTask::State::Finished : ExportTask::State::Error;
        task.message = message;
        if (ok) {
            task.framesDone = task.frameCount();
            // "These files are deleted automatically when the tasks finish."
            QFile::remove(task.snapshotPath);
        }
        emit taskChanged(row);
    }
    save();
    QMetaObject::invokeMethod(this, &ExportQueue::runNext, Qt::QueuedConnection);
}

bool ExportQueue::save() const
{
    if (m_tasksFile.isEmpty()) return true;
    QDomDocument doc;
    QDomElement root = doc.createElement(QStringLiteral("ExportTasks"));
    root.setAttribute(QStringLiteral("Version"), 1);
    doc.appendChild(root);
    for (const ExportTask& task : m_tasks) {
        QDomElement node = doc.createElement(QStringLiteral("Task"));
        node.setAttribute(QStringLiteral("ID"), task.id);
        node.setAttribute(QStringLiteral("Name"), task.name);
        node.setAttribute(QStringLiteral("ShotID"), task.shotId);
        node.setAttribute(QStringLiteral("Preset"), task.preset);
        node.setAttribute(QStringLiteral("Output"), task.outputPath);
        node.setAttribute(QStringLiteral("FirstFrame"), task.firstFrame);
        node.setAttribute(QStringLiteral("LastFrame"), task.lastFrame);
        node.setAttribute(QStringLiteral("FrameRate"), QString::number(task.frameRate, 'g', 10));
        node.setAttribute(QStringLiteral("Snapshot"), task.snapshotPath);
        node.setAttribute(QStringLiteral("State"), QString::fromLatin1(stateToken(task.state)));
        node.setAttribute(QStringLiteral("Elapsed"), task.elapsedMilliseconds);
        node.setAttribute(QStringLiteral("Message"), task.message);
        root.appendChild(node);
    }
    if (!QDir().mkpath(QFileInfo(m_tasksFile).absolutePath())) return false;
    QSaveFile file(m_tasksFile);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(doc.toByteArray(1));
    return file.commit();
}

bool ExportQueue::load()
{
    if (m_tasksFile.isEmpty()) return false;
    QFile file(m_tasksFile);
    if (!file.exists()) return true;
    QDomDocument doc;
    if (!file.open(QIODevice::ReadOnly) || !doc.setContent(file.readAll())
        || doc.documentElement().tagName() != QLatin1String("ExportTasks"))
        return false;
    m_tasks.clear();
    for (QDomElement node = doc.documentElement().firstChildElement(QStringLiteral("Task")); !node.isNull();
         node = node.nextSiblingElement(QStringLiteral("Task"))) {
        ExportTask task;
        task.id = node.attribute(QStringLiteral("ID"));
        task.name = node.attribute(QStringLiteral("Name"));
        task.shotId = node.attribute(QStringLiteral("ShotID"));
        task.preset = node.attribute(QStringLiteral("Preset"));
        task.outputPath = node.attribute(QStringLiteral("Output"));
        task.firstFrame = node.attribute(QStringLiteral("FirstFrame")).toInt();
        task.lastFrame = node.attribute(QStringLiteral("LastFrame")).toInt();
        task.frameRate = node.attribute(QStringLiteral("FrameRate"), QStringLiteral("30")).toDouble();
        task.snapshotPath = node.attribute(QStringLiteral("Snapshot"));
        task.state = stateFromToken(node.attribute(QStringLiteral("State")));
        task.elapsedMilliseconds = node.attribute(QStringLiteral("Elapsed")).toLongLong();
        task.message = node.attribute(QStringLiteral("Message"));
        if (task.state == ExportTask::State::Finished) task.framesDone = task.frameCount();
        if (!task.id.isEmpty()) m_tasks.append(task);
    }
    emit tasksChanged();
    return true;
}

} // namespace ui
} // namespace openvegas
