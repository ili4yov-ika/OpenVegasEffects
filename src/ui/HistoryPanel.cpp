#include "ui/HistoryPanel.h"
#include "ui_History.h"

#include <QListWidget>
#include <QListWidgetItem>
#include <QHBoxLayout>
#include <QIcon>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

namespace openvegas {
namespace ui {

HistoryPanel::HistoryPanel(QWidget* parent)
    : QDockWidget(parent)
{
    Ui::HistoryPanel form;
    form.setupUi(this);
    m_undoButton = form.toolButtonUndo;
    m_redoButton = form.toolButtonRedo;
    m_clearButton = form.toolButtonClear;
    m_list = form.listViewHistory;
    m_undoButton->setIcon(QIcon(QStringLiteral(":/icons/undo.svg")));
    m_redoButton->setIcon(QIcon(QStringLiteral(":/icons/redo.svg")));

    connect(m_list, &QListWidget::itemActivated, this, &HistoryPanel::onItemActivated);
    connect(m_list, &QListWidget::itemClicked, this, &HistoryPanel::onItemActivated);
    connect(m_undoButton, &QToolButton::clicked, this, &HistoryPanel::undoRequested);
    connect(m_redoButton, &QToolButton::clicked, this, &HistoryPanel::redoRequested);
    connect(m_clearButton, &QToolButton::clicked, this, &HistoryPanel::clearRequested);
    setCanUndo(false);
    setCanRedo(false);
    m_clearButton->setEnabled(false);
}

void HistoryPanel::addEntry(const QString& description)
{
    m_list->addItem(description);
    m_anchorIndex = m_list->count() - 1;
    m_list->setCurrentRow(m_anchorIndex);
    m_clearButton->setEnabled(m_list->count() > 1);
}

void HistoryPanel::setCurrentIndex(int index)
{
    // Moves the marker without emitting undoRequested / redoRequested: this is
    // the stack telling the panel where it is, not the user asking to travel.
    if (index < 0 || index >= m_list->count()) {
        return;
    }
    m_anchorIndex = index;
    m_list->setCurrentRow(index);
    // Steps past the current index are still on the stack but not applied, so
    // they are dimmed rather than removed - the same way the reference shows
    // history you have undone past.
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem* row = m_list->item(i);
        QFont f = row->font();
        f.setItalic(i > index);
        row->setFont(f);
        row->setForeground(i > index ? palette().placeholderText().color()
                                     : palette().text().color());
    }
}

void HistoryPanel::clear()
{
    m_list->clear();
    m_anchorIndex = -1;
    m_clearButton->setEnabled(false);
    setCanUndo(false);
    setCanRedo(false);
}

int HistoryPanel::count() const
{
    return m_list->count();
}

void HistoryPanel::setCanUndo(bool canUndo)
{
    m_undoButton->setEnabled(canUndo);
}

void HistoryPanel::setCanRedo(bool canRedo)
{
    m_redoButton->setEnabled(canRedo);
}

void HistoryPanel::onItemActivated(QListWidgetItem* item)
{
    if (!item) {
        return;
    }
    goToIndex(m_list->row(item));
}

void HistoryPanel::goToIndex(int index)
{
    const int total = m_list->count();
    if (index < 0 || index >= total) {
        return;
    }
    const int delta = index - m_anchorIndex;
    if (delta < 0) {
        for (int i = 0; i < -delta; ++i) {
            emit undoRequested();
        }
    } else if (delta > 0) {
        for (int i = 0; i < delta; ++i) {
            emit redoRequested();
        }
    }
    m_anchorIndex = index;
    m_list->setCurrentRow(index);
}

} // namespace ui
} // namespace openvegas
