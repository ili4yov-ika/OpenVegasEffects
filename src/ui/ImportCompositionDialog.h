#pragma once

#include <QDialog>
#include <QStringList>
#include <QVector>

#include "project/VegfxSerializer.h"

class QListWidget;
class QToolButton;

namespace openvegas {
namespace ui {

// The reference's ImportCompositionDialog (biff::ui::media, FUN_1407880a0 /
// FUN_140787100): which of the composite shots a project or composite shot
// file holds are to be imported. "Project Name:" shows the file, the shots
// are listed with a check box each under "Select Composite Shots", and
// Import takes the checked ones. Shown when the file holds more than one.
class ImportCompositionDialog : public QDialog
{
    Q_OBJECT

public:
    ImportCompositionDialog(const QString& fileName,
                            const QVector<project::CompositeShotInfo>& shots,
                            QWidget* parent = nullptr);

    QStringList selectedIds() const;

private:
    void updateImportButton();

    QListWidget* m_list = nullptr;
    QToolButton* m_import = nullptr;
};

} // namespace ui
} // namespace openvegas
