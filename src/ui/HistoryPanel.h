#pragma once

#include <QDockWidget>

#include <QStringList>

class QListWidget;
class QListWidgetItem;
class QToolButton;

namespace openvegas {
namespace ui {

// Mirrors the reference "History" panel (type 256): journal of user actions
// with undo/redo playback. Class name from reference RTTI: HistoryPanel;
// widget object name "listViewHistory".
class HistoryPanel : public QDockWidget
{
    Q_OBJECT

public:
    explicit HistoryPanel(QWidget* parent = nullptr);

    void addEntry(const QString& description);
    // Reflects the undo stack's position without asking it to move.
    void setCurrentIndex(int index);
    void clear();
    int count() const;
    void setCanUndo(bool canUndo);
    void setCanRedo(bool canRedo);

signals:
    void undoRequested();
    void redoRequested();
    void clearRequested();

private:
    void onItemActivated(QListWidgetItem* item);
    void goToIndex(int index);

    QListWidget* m_list = nullptr;
    QToolButton* m_undoButton = nullptr;
    QToolButton* m_redoButton = nullptr;
    QToolButton* m_clearButton = nullptr;
    int m_anchorIndex = -1;
};

} // namespace ui
} // namespace openvegas
