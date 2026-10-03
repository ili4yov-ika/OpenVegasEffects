#include "ui/ImportCompositionDialog.h"

#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace openvegas {
namespace ui {

ImportCompositionDialog::ImportCompositionDialog(const QString& fileName,
                                                 const QVector<project::CompositeShotInfo>& shots,
                                                 QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("ImportCompositionDialog"));
    setWindowTitle(tr("Import Composite Shot"));
    resize(400, 307);
    setMinimumSize(400, 300);

    auto* layout = new QVBoxLayout(this);
    auto* grid = new QGridLayout;
    auto* nameLabel = new QLabel(tr("Project Name:"), this);
    auto* name = new QLineEdit(fileName, this);
    name->setObjectName(QStringLiteral("lineEditProjectName"));
    name->setReadOnly(true);
    name->setCursorPosition(0);
    nameLabel->setBuddy(name);
    grid->addWidget(nameLabel, 0, 0);
    grid->addWidget(name, 0, 1);

    auto* group = new QGroupBox(this);
    group->setObjectName(QStringLiteral("groupBoxSelectCompositeShots"));
    auto* groupLayout = new QVBoxLayout(group);
    auto* heading = new QLabel(tr("Select Composite Shots"), group);
    heading->setObjectName(QStringLiteral("labelSelectCompositeShots"));
    QFont bold = heading->font();
    bold.setBold(true);
    heading->setFont(bold);
    groupLayout->addWidget(heading);
    m_list = new QListWidget(group);
    m_list->setObjectName(QStringLiteral("listViewCompositeShots"));
    m_list->setSelectionMode(QAbstractItemView::NoSelection);
    m_list->setTextElideMode(Qt::ElideMiddle);
    for (const project::CompositeShotInfo& shot : shots) {
        auto* item = new QListWidgetItem(shot.name.isEmpty() ? shot.id : shot.name, m_list);
        item->setData(Qt::UserRole, shot.id);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
        if (shot.size.isValid())
            item->setToolTip(QStringLiteral("%1x%2").arg(shot.size.width()).arg(shot.size.height()));
    }
    groupLayout->addWidget(m_list);
    grid->addWidget(group, 1, 0, 1, 2);
    layout->addLayout(grid);

    auto* footer = new QHBoxLayout;
    auto* cancel = new QToolButton(this);
    cancel->setObjectName(QStringLiteral("toolButtonCancel"));
    cancel->setText(tr("Cancel"));
    m_import = new QToolButton(this);
    m_import->setObjectName(QStringLiteral("toolButtonImport"));
    m_import->setText(tr("Import"));
    footer->addWidget(cancel);
    footer->addStretch();
    footer->addWidget(m_import);
    layout->addLayout(footer);

    connect(cancel, &QToolButton::clicked, this, &QDialog::reject);
    connect(m_import, &QToolButton::clicked, this, &QDialog::accept);
    connect(m_list, &QListWidget::itemChanged, this, &ImportCompositionDialog::updateImportButton);
    updateImportButton();
}

QStringList ImportCompositionDialog::selectedIds() const
{
    QStringList ids;
    for (int row = 0; row < m_list->count(); ++row) {
        const QListWidgetItem* item = m_list->item(row);
        if (item->checkState() == Qt::Checked) ids.append(item->data(Qt::UserRole).toString());
    }
    return ids;
}

void ImportCompositionDialog::updateImportButton()
{
    m_import->setEnabled(!selectedIds().isEmpty());
}

} // namespace ui
} // namespace openvegas
