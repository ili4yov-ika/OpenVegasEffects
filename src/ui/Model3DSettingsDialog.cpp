#include "ui/Model3DSettingsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "model3d/ModelLoader.h"

namespace openvegas {
namespace ui {

Model3DSettingsDialog::Model3DSettingsDialog(const QString& filePath,
                                             const model3d::ImportSettings& initial,
                                             QWidget* parent)
    : QDialog(parent)
    , m_filePath(filePath)
{
    setObjectName(QStringLiteral("Model3DSettingsDialog"));
    setWindowTitle(tr("3D Model Properties"));
    setModal(true);

    auto* root = new QVBoxLayout(this);

    // "Path:" over the tabs, as the reference lays it out.
    auto* pathRow = new QHBoxLayout();
    pathRow->addWidget(new QLabel(tr("Path:"), this));
    m_path = new QLineEdit(filePath, this);
    m_path->setObjectName(QStringLiteral("lineEditPath"));
    m_path->setReadOnly(true);
    pathRow->addWidget(m_path, 1);
    root->addLayout(pathRow);

    auto* tabs = new QTabWidget(this);
    tabs->addTab(buildAdvancedTab(), tr("Advanced"));
    tabs->addTab(buildMaterialsTab(), tr("Materials"));
    tabs->addTab(buildGroupsTab(), tr("Groups"));
    root->addWidget(tabs, 1);

    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    root->addWidget(m_summary);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto* update = buttons->addButton(tr("Update Preview"), QDialogButtonBox::ActionRole);
    update->setObjectName(QStringLiteral("toolButtonUpdatePreview"));
    connect(update, &QPushButton::clicked, this, &Model3DSettingsDialog::updatePreview);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);

    applyToControls(initial);
    // Read once on open so Materials and Groups have something in them before
    // anything is changed - the reference shows the model's own contents there
    // the moment the dialog appears.
    updatePreview();

    resize(520, 520);
}

QWidget* Model3DSettingsDialog::buildAdvancedTab()
{
    auto* page = new QWidget(this);
    page->setObjectName(QStringLiteral("Model3DAdvancedPane"));
    auto* layout = new QVBoxLayout(page);

    auto* coordinates = new QGroupBox(tr("Coordinate System"), page);
    auto* coordinateForm = new QFormLayout(coordinates);
    m_flipYZAxis = new QCheckBox(coordinates);
    m_centerAnchorPoint = new QCheckBox(coordinates);
    coordinateForm->addRow(tr("Flip YZ Axis:"), m_flipYZAxis);
    coordinateForm->addRow(tr("Center Anchor Point:"), m_centerAnchorPoint);
    layout->addWidget(coordinates);

    auto* unitScale = new QGroupBox(tr("3D Model Unit/Scale"), page);
    auto* unitForm = new QFormLayout(unitScale);
    m_unit = new QComboBox(unitScale);
    m_unit->addItems(model3d::unitNames());
    m_singleUnitScale = new QDoubleSpinBox(unitScale);
    m_singleUnitScale->setRange(0.0001, 100000.0);
    m_singleUnitScale->setDecimals(4);
    m_autoNormalize = new QCheckBox(unitScale);
    unitForm->addRow(tr("Unit:"), m_unit);
    unitForm->addRow(tr("Single Unit Scale:"), m_singleUnitScale);
    unitForm->addRow(tr("Auto Normalize:"), m_autoNormalize);
    layout->addWidget(unitScale);

    auto* normals = new QGroupBox(tr("Normals"), page);
    auto* normalForm = new QFormLayout(normals);
    m_normalMethod = new QComboBox(normals);
    m_normalMethod->addItems(model3d::normalMethodNames());
    m_smoothingAngle = new QDoubleSpinBox(normals);
    m_smoothingAngle->setRange(0.0, 180.0);
    m_smoothingAngle->setDecimals(1);
    // The reference spells the unit out in the field itself and explains the
    // control in a tooltip; both are worth keeping.
    m_smoothingAngle->setSuffix(tr(" Degrees"));
    m_smoothingAngle->setToolTip(tr("Angle threshold used for generating normals when using auto "
                                    "smoothing generation method."));
    m_unifyNormals = new QCheckBox(normals);
    m_flipNormals = new QCheckBox(normals);
    normalForm->addRow(tr("Method:"), m_normalMethod);
    normalForm->addRow(tr("Angle:"), m_smoothingAngle);
    normalForm->addRow(tr("Unify Normals:"), m_unifyNormals);
    normalForm->addRow(tr("Flip Normals:"), m_flipNormals);
    layout->addWidget(normals);

    auto* uv = new QGroupBox(tr("UV Mapping"), page);
    auto* uvForm = new QFormLayout(uv);
    m_flipUVCoordinates = new QCheckBox(uv);
    uvForm->addRow(tr("Flip UV Coordinates:"), m_flipUVCoordinates);
    layout->addWidget(uv);

    layout->addStretch(1);
    return page;
}

QWidget* Model3DSettingsDialog::buildMaterialsTab()
{
    m_materials = new QTreeWidget(this);
    m_materials->setObjectName(QStringLiteral("tabMaterials"));
    // The reference's Model3DMaterialItemModel names its first column "Title".
    m_materials->setHeaderLabels({tr("Title"), tr("Diffuse Color"), tr("Shininess"), tr("Opacity")});
    m_materials->setRootIsDecorated(false);
    m_materials->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    return m_materials;
}

QWidget* Model3DSettingsDialog::buildGroupsTab()
{
    m_groups = new QTreeWidget(this);
    m_groups->setObjectName(QStringLiteral("tabGroups"));
    m_groups->setHeaderLabels({tr("Title")});
    m_groups->setRootIsDecorated(false);
    m_groups->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    return m_groups;
}

void Model3DSettingsDialog::applyToControls(const model3d::ImportSettings& settings)
{
    m_flipYZAxis->setChecked(settings.flipYZAxis);
    m_centerAnchorPoint->setChecked(settings.centerAnchorPoint);
    m_unit->setCurrentIndex(int(settings.unit));
    m_singleUnitScale->setValue(settings.singleUnitScale);
    m_autoNormalize->setChecked(settings.autoNormalize);
    m_normalMethod->setCurrentIndex(int(settings.normalMethod));
    m_smoothingAngle->setValue(settings.autoSmoothingAngle);
    m_unifyNormals->setChecked(settings.unifyNormals);
    m_flipNormals->setChecked(settings.flipNormals);
    m_flipUVCoordinates->setChecked(settings.flipUVCoordinates);
}

model3d::ImportSettings Model3DSettingsDialog::settings() const
{
    model3d::ImportSettings settings;
    settings.flipYZAxis = m_flipYZAxis->isChecked();
    settings.centerAnchorPoint = m_centerAnchorPoint->isChecked();
    settings.unit = static_cast<model3d::ImportSettings::Unit>(qMax(0, m_unit->currentIndex()));
    settings.singleUnitScale = m_singleUnitScale->value();
    settings.autoNormalize = m_autoNormalize->isChecked();
    settings.normalMethod =
        static_cast<model3d::ImportSettings::NormalMethod>(qMax(0, m_normalMethod->currentIndex()));
    settings.autoSmoothingAngle = m_smoothingAngle->value();
    settings.unifyNormals = m_unifyNormals->isChecked();
    settings.flipNormals = m_flipNormals->isChecked();
    settings.flipUVCoordinates = m_flipUVCoordinates->isChecked();
    return settings;
}

void Model3DSettingsDialog::updatePreview()
{
    const model3d::LoadResult loaded = model3d::loadModel(m_filePath, settings());
    m_meshValid = loaded.ok;
    m_mesh = loaded.mesh;

    m_materials->clear();
    m_groups->clear();

    if (!loaded.ok) {
        m_summary->setText(loaded.message);
        return;
    }

    for (const model3d::Material& material : m_mesh.materials) {
        auto* row = new QTreeWidgetItem(m_materials);
        row->setText(0, material.name.isEmpty() ? tr("<unnamed>") : material.name);
        row->setText(1, material.diffuseColor.name(QColor::HexRgb));
        row->setIcon(1, QIcon());
        row->setText(2, QString::number(material.shininess, 'f', 1));
        row->setText(3, QString::number(material.opacity, 'f', 2));
        // A swatch says more than the hex does at a glance.
        QPixmap swatch(14, 14);
        swatch.fill(material.diffuseColor);
        row->setIcon(1, QIcon(swatch));
    }

    for (const QString& node : m_mesh.nodeNames) {
        auto* row = new QTreeWidgetItem(m_groups);
        row->setText(0, node);
    }

    const QString warning = loaded.message.isEmpty()
        ? QString()
        : QStringLiteral("\n") + tr("Warning:") + QLatin1Char('\n') + loaded.message;
    m_summary->setText(tr("%1: %n triangle(s), %2 material(s), %3 group(s).", nullptr,
                          m_mesh.triangleCount())
                           .arg(QFileInfo(m_filePath).fileName())
                           .arg(m_mesh.materials.size())
                           .arg(m_mesh.nodeNames.size())
                       + warning);
}

} // namespace ui
} // namespace openvegas
