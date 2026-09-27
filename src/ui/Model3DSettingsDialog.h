#pragma once

#include <QDialog>

#include "model3d/Mesh.h"
#include "model3d/ModelImportSettings.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QTreeWidget;

namespace openvegas {
namespace ui {

// Import settings for a 3D model, and what the file turned out to contain.
//
// This is the reference's Model3DSettingsDialog: a path field over three tabs -
// Advanced, Materials and Groups - with an Update Preview button. The Advanced
// tab is its Model3DAdvancedPane, whose four groups (Coordinate System, 3D
// Model Unit/Scale, Normals, UV Mapping) and their controls are reproduced
// field for field from the reference's own widget names.
class Model3DSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    Model3DSettingsDialog(const QString& filePath, const model3d::ImportSettings& initial,
                          QWidget* parent = nullptr);

    model3d::ImportSettings settings() const;
    // Geometry the dialog last read with the settings shown. Reusing it saves
    // the caller a second parse of the same file.
    const model3d::Mesh& mesh() const { return m_mesh; }
    bool meshIsValid() const { return m_meshValid; }

private slots:
    // Re-reads the file with what the controls now say, and refreshes the
    // summary, the material list and the group list from the result.
    void updatePreview();

private:
    QWidget* buildAdvancedTab();
    QWidget* buildMaterialsTab();
    QWidget* buildGroupsTab();
    void applyToControls(const model3d::ImportSettings& settings);

    QString m_filePath;
    model3d::Mesh m_mesh;
    bool m_meshValid = false;

    QLineEdit* m_path = nullptr;
    QLabel* m_summary = nullptr;

    QCheckBox* m_flipYZAxis = nullptr;
    QCheckBox* m_centerAnchorPoint = nullptr;
    QComboBox* m_unit = nullptr;
    QDoubleSpinBox* m_singleUnitScale = nullptr;
    QCheckBox* m_autoNormalize = nullptr;
    QComboBox* m_normalMethod = nullptr;
    QDoubleSpinBox* m_smoothingAngle = nullptr;
    QCheckBox* m_unifyNormals = nullptr;
    QCheckBox* m_flipNormals = nullptr;
    QCheckBox* m_flipUVCoordinates = nullptr;

    QTreeWidget* m_materials = nullptr;
    QTreeWidget* m_groups = nullptr;
};

} // namespace ui
} // namespace openvegas
