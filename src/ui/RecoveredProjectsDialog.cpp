#include "ui/RecoveredProjectsDialog.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace openvegas {
namespace ui {
namespace {

// Strings use the reference's own contexts (AutoSaveRecoveryDialog,
// biff::ui::MainAppWindow), so its catalogues translate them as well.

} // namespace

RecoveredProjectsDialog::RecoveredProjectsDialog(QWidget* parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("AutoSaveRecoveryDialog"));
    setWindowTitle(QCoreApplication::translate("biff::ui::common::AutoSaveRecoveryDialog",
                                               "Recovered Projects"));
    resize(560, 320);
    auto* layout = new QVBoxLayout(this);
    m_list = new QTreeWidget(this);
    m_list->setObjectName(QStringLiteral("treeRecoveredProjects"));
    m_list->setRootIsDecorated(false);
    const QString savedColumn = tr("Saved");
    m_list->setHeaderLabels({QCoreApplication::translate("AutoSaveRecoveryDialog", "Project"), savedColumn});
    m_list->header()->setStretchLastSection(false);
    m_list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_list->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    layout->addWidget(m_list, 1);

    auto* buttons = new QHBoxLayout;
    m_open = new QPushButton(QCoreApplication::translate("AutoSaveRecoveryDialog", "Open Project"), this);
    m_open->setObjectName(QStringLiteral("pushButtonOpenProject"));
    m_save = new QPushButton(QCoreApplication::translate("AutoSaveRecoveryDialog", "Save Project"), this);
    m_save->setObjectName(QStringLiteral("pushButtonSaveProject"));
    m_delete = new QPushButton(QCoreApplication::translate("AutoSaveRecoveryDialog", "Delete Project"), this);
    m_delete->setObjectName(QStringLiteral("pushButtonDeleteProject"));
    auto* cancel = new QPushButton(QCoreApplication::translate("AutoSaveRecoveryDialog", "Cancel"), this);
    cancel->setObjectName(QStringLiteral("pushButtonCancel"));
    buttons->addWidget(m_open);
    buttons->addWidget(m_save);
    buttons->addWidget(m_delete);
    buttons->addStretch();
    buttons->addWidget(cancel);
    layout->addLayout(buttons);
    m_open->setDefault(true);

    connect(m_open, &QPushButton::clicked, this, [this] {
        m_selected = current();
        if (!m_selected.file.isEmpty()) accept();
    });
    connect(m_save, &QPushButton::clicked, this, &RecoveredProjectsDialog::saveCopy);
    connect(m_delete, &QPushButton::clicked, this, &RecoveredProjectsDialog::deleteCurrent);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_list, &QTreeWidget::itemDoubleClicked, m_open, &QPushButton::click);
    connect(m_list, &QTreeWidget::currentItemChanged, this, &RecoveredProjectsDialog::updateButtons);
    reload();
}

int RecoveredProjectsDialog::count() const
{
    return m_entries.size();
}

void RecoveredProjectsDialog::reload()
{
    m_entries = autosave::entries();
    m_list->clear();
    for (const autosave::Entry& entry : m_entries) {
        const QString name = entry.projectPath.isEmpty()
            ? QCoreApplication::translate("biff::ui::MainAppWindow", "Recovered Untitled Project")
            : QFileInfo(entry.projectPath).fileName();
        auto* item = new QTreeWidgetItem(m_list, {name, QLocale().toString(entry.saved, QLocale::ShortFormat)});
        item->setToolTip(0, QDir::toNativeSeparators(entry.projectPath.isEmpty() ? entry.file
                                                                                 : entry.projectPath));
    }
    if (m_list->topLevelItemCount() > 0) m_list->setCurrentItem(m_list->topLevelItem(0));
    updateButtons();
}

autosave::Entry RecoveredProjectsDialog::current() const
{
    const int row = m_list->currentItem() ? m_list->indexOfTopLevelItem(m_list->currentItem()) : -1;
    return row >= 0 && row < m_entries.size() ? m_entries.at(row) : autosave::Entry();
}

void RecoveredProjectsDialog::saveCopy()
{
    const autosave::Entry entry = current();
    if (entry.file.isEmpty()) return;
    const QString start = entry.projectPath.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
        : QFileInfo(entry.projectPath).absolutePath();
    QString target = QFileDialog::getSaveFileName(
        this, QCoreApplication::translate("biff::ui::MainAppWindow", "Save Recovered Project"), start,
        QCoreApplication::translate("biff::ui::BiffFileFilter", "VEGAS Effects Projects (*.vegfx)"));
    if (target.isEmpty()) return;
    if (QFileInfo(target).suffix().isEmpty()) target += QStringLiteral(".vegfx");
    QFile::remove(target);
    if (!QFile::copy(entry.file, target)) {
        QMessageBox::warning(this, QCoreApplication::translate("biff::ui::MainAppWindow", "Save Recovered Project"),
                             QCoreApplication::translate("biff::ui::MainAppWindow", "The recovered project cannot be saved to the chosen location."));
    }
}

void RecoveredProjectsDialog::deleteCurrent()
{
    const autosave::Entry entry = current();
    if (entry.file.isEmpty()) return;
    QFile::remove(entry.file);
    reload();
}

void RecoveredProjectsDialog::updateButtons()
{
    const bool any = m_list->currentItem() != nullptr;
    m_open->setEnabled(any);
    m_save->setEnabled(any);
    m_delete->setEnabled(any);
}

} // namespace ui
} // namespace openvegas
