#pragma once

#include "ui/AutoSave.h"

#include <QDialog>

class QTreeWidget;
class QPushButton;

namespace openvegas {
namespace ui {

// The reference's AutoSaveRecoveryDialog ("Recovered Projects", FUN_1402cf970):
// the auto-saves found in the auto-save folder, newest first, with Open
// Project, Save Project (a copy to a place the user picks), Delete Project and
// Cancel. Accepting means "open the selected one".
class RecoveredProjectsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit RecoveredProjectsDialog(QWidget* parent = nullptr);

    // The entry to open once the dialog is accepted.
    autosave::Entry selected() const { return m_selected; }
    int count() const;

private:
    void reload();
    autosave::Entry current() const;
    void saveCopy();
    void deleteCurrent();
    void updateButtons();

    QTreeWidget* m_list = nullptr;
    QPushButton* m_open = nullptr;
    QPushButton* m_save = nullptr;
    QPushButton* m_delete = nullptr;
    QVector<autosave::Entry> m_entries;
    autosave::Entry m_selected;
};

} // namespace ui
} // namespace openvegas
