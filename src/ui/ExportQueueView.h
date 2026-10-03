#pragma once

#include <QWidget>

class QLabel;
class QPushButton;
class QStackedWidget;
class QTreeWidget;

namespace openvegas {
namespace ui {

class ExportQueue;

// Options > Export > Time Format, as the reference's ExportSettingsWidget
// lists it: how Duration and Elapsed are shown.
enum class ExportTimeFormat { Timecode = 0, Natural = 1, Seconds = 2 };
ExportTimeFormat exportTimeFormatFromSettings();
QString formatExportTime(double seconds, double frameRate, ExportTimeFormat format);

// The export queue as the reference's Export panel shows it
// (ExportTaskTreeView over ExportTaskItemModel): one row per task with Name,
// Preset, Output, Progress, Duration and Elapsed; Start / Suspend Exporting;
// and the ExportTaskActions in the context menu.
class ExportQueueView : public QWidget
{
    Q_OBJECT

public:
    explicit ExportQueueView(ExportQueue* queue, QWidget* parent = nullptr);

    void setTimeFormat(ExportTimeFormat format);
    QStringList selectedIds() const;
    // Asks first (Options/Prompts/RemovingExportTasks), as the reference does.
    void removeSelected();

signals:
    void revealRequested(const QString& path);

private:
    void rebuild();
    void updateRow(int row);
    void updateStartButton();
    void showContextMenu(const QPoint& position);

    ExportQueue* m_queue = nullptr;
    ExportTimeFormat m_format = ExportTimeFormat::Timecode;
    QStackedWidget* m_stack = nullptr;
    QLabel* m_empty = nullptr;
    QTreeWidget* m_tree = nullptr;
    QPushButton* m_start = nullptr;
};

} // namespace ui
} // namespace openvegas
