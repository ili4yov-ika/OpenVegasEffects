#include "ui/ExportQueueView.h"

#include "app/Settings.h"
#include "ui/ExportQueue.h"
#include "ui/PromptMessage.h"

#include <QCoreApplication>
#include <QDir>
#include <QDesktopServices>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <cmath>

namespace openvegas {
namespace ui {
namespace {

enum Column { NameColumn, PresetColumn, OutputColumn, ProgressColumn, DurationColumn, ElapsedColumn };

QString model(const char* text)
{
    return QCoreApplication::translate("biff::ui::exporter::ExportTaskItemModel", text);
}

QString action(const char* text)
{
    return QCoreApplication::translate("ExportTaskActions", text);
}

// For lupdate: the strings model() and action() look up.
[[maybe_unused]] const char* const kQueueStrings[] = {
    QT_TRANSLATE_NOOP("biff::ui::exporter::ExportTaskItemModel", "Name"),
    QT_TRANSLATE_NOOP("biff::ui::exporter::ExportTaskItemModel", "Preset"),
    QT_TRANSLATE_NOOP("biff::ui::exporter::ExportTaskItemModel", "Output"),
    QT_TRANSLATE_NOOP("biff::ui::exporter::ExportTaskItemModel", "Progress"),
    QT_TRANSLATE_NOOP("biff::ui::exporter::ExportTaskItemModel", "Duration"),
    QT_TRANSLATE_NOOP("biff::ui::exporter::ExportTaskItemModel", "Elapsed"),
    QT_TRANSLATE_NOOP("biff::ui::exporter::ExportTaskItemModel", "<No Preset>"),
    QT_TRANSLATE_NOOP("biff::ui::exporter::ExportTaskItemModel", "The name of the timeline"),
    QT_TRANSLATE_NOOP("biff::ui::exporter::ExportTaskItemModel", "The name of the preset assigned to this task"),
    QT_TRANSLATE_NOOP("biff::ui::exporter::ExportTaskItemModel", "The path to the file or directory to create for the export"),
    QT_TRANSLATE_NOOP("biff::ui::exporter::ExportTaskItemModel", "The state and progress of the task"),
    QT_TRANSLATE_NOOP("biff::ui::exporter::ExportTaskItemModel", "The length of the timeline to be exported"),
    QT_TRANSLATE_NOOP("biff::ui::exporter::ExportTaskItemModel", "The time taken to export the task so far"),
    QT_TRANSLATE_NOOP("ExportTaskActions", "Start Exporting"),
    QT_TRANSLATE_NOOP("ExportTaskActions", "Reveal Output"),
    QT_TRANSLATE_NOOP("ExportTaskActions", "Duplicate Task(s)"),
    QT_TRANSLATE_NOOP("ExportTaskActions", "Remove Task(s)"),
    QT_TRANSLATE_NOOP("ExportTaskActions", "Remove Finished Task(s)"),
    QT_TRANSLATE_NOOP("ExportTaskActions", "Select All"),
};

} // namespace

ExportTimeFormat exportTimeFormatFromSettings()
{
    const QString stored = app::Settings::optionSettings()
                               .value(QStringLiteral("Options/TimeFormat"), QStringLiteral("Timecode")).toString();
    if (stored == QLatin1String("Natural") || stored == QLatin1String("1")) return ExportTimeFormat::Natural;
    if (stored == QLatin1String("Seconds") || stored == QLatin1String("2")) return ExportTimeFormat::Seconds;
    return ExportTimeFormat::Timecode;
}

QString formatExportTime(double seconds, double frameRate, ExportTimeFormat format)
{
    seconds = qMax(0.0, seconds);
    switch (format) {
    case ExportTimeFormat::Seconds:
        return QStringLiteral("%1 s").arg(seconds, 0, 'f', 1);
    case ExportTimeFormat::Natural: {
        const qint64 whole = qint64(std::floor(seconds));
        const qint64 h = whole / 3600, m = (whole % 3600) / 60, s = whole % 60;
        if (h > 0) return QStringLiteral("%1h %2m %3s").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'));
        if (m > 0) return QStringLiteral("%1m %2s").arg(m).arg(s, 2, 10, QLatin1Char('0'));
        return QStringLiteral("%1s").arg(s);
    }
    case ExportTimeFormat::Timecode:
    default: {
        const double fps = frameRate > 0.0 ? frameRate : 30.0;
        const int framesPerSecond = qMax(1, int(std::lround(fps)));
        const qint64 total = qint64(std::floor(seconds * fps + 1e-6));
        const qint64 whole = total / framesPerSecond;
        return QStringLiteral("%1:%2:%3:%4")
            .arg(whole / 3600, 2, 10, QLatin1Char('0')).arg((whole % 3600) / 60, 2, 10, QLatin1Char('0'))
            .arg(whole % 60, 2, 10, QLatin1Char('0')).arg(total % framesPerSecond, 2, 10, QLatin1Char('0'));
    }
    }
}

ExportQueueView::ExportQueueView(ExportQueue* queue, QWidget* parent)
    : QWidget(parent), m_queue(queue), m_format(exportTimeFormatFromSettings())
{
    setObjectName(QStringLiteral("exportQueueView"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* bar = new QHBoxLayout;
    auto* title = new QLabel(QCoreApplication::translate("ExportPanelWidget", "Queue"), this);
    QFont bold = title->font();
    bold.setBold(true);
    title->setFont(bold);
    m_start = new QPushButton(this);
    m_start->setObjectName(QStringLiteral("pushButtonStartExporting"));
    m_start->setToolTip(QCoreApplication::translate("ExportPanelWidget", "Start of stop the export queue"));
    bar->addWidget(title);
    bar->addStretch();
    bar->addWidget(m_start);
    layout->addLayout(bar);

    m_stack = new QStackedWidget(this);
    m_empty = new QLabel(QCoreApplication::translate(
        "biff::ui::exporter::ExportTaskTreeView",
        "To queue an export, click the 'Export Content Area' or 'Export In-to-Out Area' buttons on the timeline(s) to be exported.\n\n"
        "You can queue exports from multiple projects by opening each one in turn and adding timelines to the queue."), m_stack);
    m_empty->setObjectName(QStringLiteral("labelEmptyQueue"));
    m_empty->setWordWrap(true);
    m_empty->setAlignment(Qt::AlignCenter);
    m_tree = new QTreeWidget(m_stack);
    m_tree->setObjectName(QStringLiteral("treeViewTasks"));
    m_tree->setRootIsDecorated(false);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setToolTip(QCoreApplication::translate("ExportPanelWidget",
                                                   "A list of export tasks. Right click here for additional options"));
    m_tree->setHeaderLabels({model("Name"), model("Preset"), model("Output"), model("Progress"),
                             model("Duration"), model("Elapsed")});
    const char* const tips[] = {"The name of the timeline", "The name of the preset assigned to this task",
                                "The path to the file or directory to create for the export",
                                "The state and progress of the task",
                                "The length of the timeline to be exported", "The time taken to export the task so far"};
    for (int column = 0; column < 6; ++column)
        m_tree->headerItem()->setToolTip(column, model(tips[column]));
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(OutputColumn, QHeaderView::Stretch);
    m_stack->addWidget(m_empty);
    m_stack->addWidget(m_tree);
    layout->addWidget(m_stack, 1);

    connect(m_start, &QPushButton::clicked, this, [this] {
        if (!m_queue) return;
        if (m_queue->isRunning()) m_queue->suspend();
        else m_queue->start();
    });
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &ExportQueueView::showContextMenu);
    if (m_queue) {
        connect(m_queue, &ExportQueue::tasksChanged, this, &ExportQueueView::rebuild);
        connect(m_queue, &ExportQueue::taskChanged, this, &ExportQueueView::updateRow);
        connect(m_queue, &ExportQueue::runningChanged, this, &ExportQueueView::updateStartButton);
    }
    rebuild();
}

void ExportQueueView::setTimeFormat(ExportTimeFormat format)
{
    m_format = format;
    rebuild();
}

QStringList ExportQueueView::selectedIds() const
{
    QStringList ids;
    for (const QTreeWidgetItem* item : m_tree->selectedItems()) ids.append(item->data(0, Qt::UserRole).toString());
    return ids;
}

void ExportQueueView::rebuild()
{
    const QStringList selected = selectedIds();
    m_tree->clear();
    if (m_queue) {
        for (const ExportTask& task : m_queue->tasks()) {
            auto* item = new QTreeWidgetItem(m_tree);
            item->setData(0, Qt::UserRole, task.id);
            if (selected.contains(task.id)) item->setSelected(true);
        }
        for (int row = 0; row < m_queue->tasks().size(); ++row) updateRow(row);
    }
    m_stack->setCurrentWidget(m_tree->topLevelItemCount() > 0 ? static_cast<QWidget*>(m_tree)
                                                                : static_cast<QWidget*>(m_empty));
    updateStartButton();
}

void ExportQueueView::updateRow(int row)
{
    if (!m_queue || row < 0 || row >= m_queue->tasks().size() || row >= m_tree->topLevelItemCount()) return;
    const ExportTask& task = m_queue->tasks().at(row);
    QTreeWidgetItem* item = m_tree->topLevelItem(row);
    item->setText(NameColumn, task.name);
    item->setText(PresetColumn, task.preset.isEmpty() ? model("<No Preset>") : task.preset);
    item->setText(OutputColumn, task.outputPath);
    item->setToolTip(OutputColumn, task.outputPath);
    QString progress = exportTaskStateText(task.state);
    if (task.state == ExportTask::State::Started)
        progress = QStringLiteral("%1%").arg(100 * task.framesDone / qMax(1, task.frameCount()));
    item->setText(ProgressColumn, progress);
    item->setToolTip(ProgressColumn, task.message);
    item->setText(DurationColumn, formatExportTime(task.durationSeconds(), task.frameRate, m_format));
    item->setText(ElapsedColumn, task.elapsedMilliseconds > 0
                                     ? formatExportTime(task.elapsedMilliseconds / 1000.0, task.frameRate, m_format)
                                     : QString());
}

void ExportQueueView::updateStartButton()
{
    const bool running = m_queue && m_queue->isRunning();
    m_start->setText(running ? QCoreApplication::translate("biff::ui::exporter::ExportPanelWidget", "Suspend Exporting")
                             : QCoreApplication::translate("biff::ui::exporter::ExportPanelWidget", "Start Exporting"));
    bool anyReady = false;
    if (m_queue)
        for (const ExportTask& task : m_queue->tasks())
            anyReady = anyReady || task.state == ExportTask::State::Ready;
    m_start->setEnabled(running || anyReady);
}

void ExportQueueView::removeSelected()
{
    const QStringList ids = selectedIds();
    if (!m_queue || ids.isEmpty()) return;
    const QString text = QCoreApplication::translate("biff::ui::exporter::ExportTaskTreeView",
                                                     "Are you sure you want to cancel and remove the selected task(s)?")
        + QStringLiteral("\n\n")
        + QCoreApplication::translate("biff::ui::exporter::ExportTaskTreeView", "This cannot be undone.");
    if (showPrompt(this, QStringLiteral("RemovingExportTasks"), QMessageBox::Question,
                   action("Remove Task(s)"), text, QMessageBox::Yes | QMessageBox::No, QMessageBox::No,
                   {}, QMessageBox::Yes)
        != QMessageBox::Yes)
        return;
    m_queue->removeTasks(ids);
}

void ExportQueueView::showContextMenu(const QPoint& position)
{
    if (!m_queue) return;
    QMenu menu(this);
    const QStringList ids = selectedIds();
    QAction* start = menu.addAction(m_queue->isRunning()
        ? QCoreApplication::translate("biff::ui::exporter::ExportPanelWidget", "Suspend Exporting")
        : action("Start Exporting"));
    menu.addSeparator();
    QAction* reveal = menu.addAction(action("Reveal Output"));
    reveal->setEnabled(ids.size() == 1);
    QAction* duplicate = menu.addAction(action("Duplicate Task(s)"));
    duplicate->setEnabled(!ids.isEmpty());
    QAction* remove = menu.addAction(action("Remove Task(s)"));
    remove->setEnabled(!ids.isEmpty());
    QAction* removeFinished = menu.addAction(action("Remove Finished Task(s)"));
    menu.addSeparator();
    QAction* selectAll = menu.addAction(action("Select All"));
    QAction* chosen = menu.exec(m_tree->viewport()->mapToGlobal(position));
    if (chosen == start) {
        if (m_queue->isRunning()) m_queue->suspend();
        else m_queue->start();
    } else if (chosen == reveal && ids.size() == 1) {
        const int row = m_queue->indexOf(ids.first());
        if (row >= 0) {
            const QFileInfo output(m_queue->tasks().at(row).outputPath);
            if (!output.exists() && !output.dir().exists()) {
                QMessageBox::warning(this, action("Reveal Output"),
                                     QCoreApplication::translate("biff::ui::exporter::ExportTaskTreeView",
                                                                 "The exported file or directory does not exist."));
            } else {
                emit revealRequested(output.exists() ? output.absoluteFilePath() : output.absolutePath());
                QDesktopServices::openUrl(QUrl::fromLocalFile(output.absolutePath()));
            }
        }
    } else if (chosen == duplicate) {
        m_queue->duplicateTasks(ids);
    } else if (chosen == remove) {
        removeSelected();
    } else if (chosen == removeFinished) {
        m_queue->removeFinishedTasks();
    } else if (chosen == selectAll) {
        m_tree->selectAll();
    }
}

} // namespace ui
} // namespace openvegas
