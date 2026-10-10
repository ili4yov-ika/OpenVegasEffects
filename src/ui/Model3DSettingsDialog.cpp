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

#include <QCoreApplication>
#include <QTimer>

#include "model3d/ModelLoader.h"
#include "ui/TimelineParameterEditor.h"

namespace openvegas {
namespace ui {

Model3DSettingsDialog::Model3DSettingsDialog(const QString& filePath,
                                             const model3d::ImportSettings& initial,
                                             QWidget* parent, const QVector<model3d::Material>& materials)
    : QDialog(parent)
    , m_filePath(filePath)
    , m_edits(materials)
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

    // Groups, Materials, Advanced, opening on Materials, as the reference's
    // (VegasEffects.exe 0x1404da7c0).
    auto* tabs = new QTabWidget(this);
    tabs->setObjectName(QStringLiteral("tabWidget"));
    tabs->addTab(buildGroupsTab(), tr("Groups"));
    tabs->addTab(buildMaterialsTab(), tr("Materials"));
    tabs->addTab(buildAdvancedTab(), tr("Advanced"));
    tabs->setCurrentIndex(1);
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
    m_materials->setHeaderLabels({tr("Title"), QString()});
    m_materials->setColumnCount(2);
    m_materials->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_materials->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    return m_materials;
}

QVector<model3d::Material> Model3DSettingsDialog::editedMaterials() const
{
    QVector<model3d::Material> edited;
    for (const model3d::Material& material : m_mesh.materials) {
        bool same = false;
        for (const model3d::Material& own : m_fileMaterials)
            if (own.name == material.name) same = model3d::sameMaterialSettings(own, material);
        if (!same) edited.append(material);
    }
    return edited;
}

void Model3DSettingsDialog::rebuildMaterialRows()
{
    m_materials->clear();
    m_materialReaders.clear();
    // Model3DMaterialDataDelegate's rows (VegasEffects.exe 0x1404fad30,
    // labels 0x1404f5cc0) for what the material's Illumination Model uses.
    const char* const context = "biff::ui::common::Model3DMaterialDataDelegate";
    const auto label = [context](const char* text) { return QCoreApplication::translate(context, text); };
    using Model = model3d::IlluminationModel;
    for (int m = 0; m < m_mesh.materials.size(); ++m) {
        const model3d::Material& material = m_mesh.materials.at(m);
        auto* top = new QTreeWidgetItem(m_materials, {material.name.isEmpty() ? tr("<unnamed>") : material.name});
        QFont bold = top->font(0); bold.setBold(true); top->setFont(0, bold);
        QPixmap swatch(14, 14);
        swatch.fill(material.diffuseColor);
        top->setIcon(0, QIcon(swatch));
        const auto row = [&](const char* text, const QString& key, plugin::EffectParameterSpec spec,
                             std::function<QVariant(const model3d::Material&)> get,
                             std::function<void(model3d::Material&, const QVariant&)> set, bool rebuild = false) {
            auto* item = new QTreeWidgetItem(top, {label(text)});
            spec.displayName = item->text(0);
            const auto read = [this, m, get]() -> QVariant {
                return m < m_mesh.materials.size() ? get(m_mesh.materials.at(m)) : QVariant();
            };
            const auto write = [this, m, set, read, rebuild](QVariant value) {
                if (m >= m_mesh.materials.size() || read().toString() == value.toString()) return;
                set(m_mesh.materials[m], value);
                // Other rows belong to another model: drawn again once this
                // editor has finished with its signal.
                if (rebuild) QTimer::singleShot(0, this, [this] { rebuildMaterialRows(); });
            };
            m_materials->setItemWidget(item, 1, timelineParameterEditor(
                m_materials, spec, QStringLiteral("materialRow_%1_%2").arg(m).arg(key), read, write, m_materialReaders));
        };
        const auto percent = [](double maximum = 100.0) {
            plugin::EffectParameterSpec spec;
            spec.type = QStringLiteral("double"); spec.minimum = 0.0; spec.maximum = maximum;
            spec.decimals = 1; spec.unit = QStringLiteral("%");
            return spec;
        };
        const auto number = [](double minimum, double maximum, int decimals) {
            plugin::EffectParameterSpec spec;
            spec.type = QStringLiteral("double"); spec.minimum = minimum; spec.maximum = maximum;
            spec.decimals = decimals;
            return spec;
        };
        const auto colorSpec = [] { plugin::EffectParameterSpec spec; spec.type = QStringLiteral("color"); return spec; };
        const auto colorRow = [&](const char* text, const QString& key, QColor model3d::Material::*member) {
            row(text, key, colorSpec(),
                [member](const model3d::Material& x) { return QVariant((x.*member).name()); },
                [member](model3d::Material& x, const QVariant& v) { x.*member = QColor(v.toString()); });
        };
        const auto percentRow = [&](const char* text, const QString& key, double model3d::Material::*member,
                                    double maximum = 100.0) {
            row(text, key, percent(maximum),
                [member](const model3d::Material& x) { return QVariant(x.*member * 100.0); },
                [member](model3d::Material& x, const QVariant& v) { x.*member = v.toDouble() / 100.0; });
        };
        const auto numberRow = [&](const char* text, const QString& key, double model3d::Material::*member,
                                   double minimum, double maximum, int decimals) {
            row(text, key, number(minimum, maximum, decimals),
                [member](const model3d::Material& x) { return QVariant(x.*member); },
                [member](model3d::Material& x, const QVariant& v) { x.*member = v.toDouble(); });
        };

        plugin::EffectParameterSpec models;
        models.type = QStringLiteral("choice");
        models.choices = {QCoreApplication::translate("biff::ui::common::Model3DMaterialDataDelegate", "Phong"), QCoreApplication::translate("biff::ui::common::Model3DMaterialDataDelegate", "Cook-Torrance"), QCoreApplication::translate("biff::ui::common::Model3DMaterialDataDelegate", "Metallic-Roughness"), QCoreApplication::translate("biff::ui::common::Model3DMaterialDataDelegate", "Specular-Glossiness")};
        models.choiceValues = {QStringLiteral("0"), QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3")};
        row(QT_TRANSLATE_NOOP("biff::ui::common::Model3DMaterialDataDelegate", "Illumination Model"),
            QStringLiteral("illuminationModel"), models,
            [](const model3d::Material& x) { return QVariant(QString::number(int(x.illuminationModel))); },
            [](model3d::Material& x, const QVariant& v) { x.illuminationModel = model3d::IlluminationModel(v.toInt()); },
            true);
        const Model model = material.illuminationModel;
        const bool physical = model == Model::MetallicRoughness || model == Model::SpecularGlossiness;
        colorRow(QT_TRANSLATE_NOOP("biff::ui::common::Model3DMaterialDataDelegate", "Diffuse Color"),
                 QStringLiteral("diffuseColor"), &model3d::Material::diffuseColor);
        if (model != Model::MetallicRoughness)
            colorRow(QT_TRANSLATE_NOOP("biff::ui::common::Model3DMaterialDataDelegate", "Specular Color"),
                     QStringLiteral("specularColor"), &model3d::Material::specularColor);
        if (!physical)
            colorRow(QT_TRANSLATE_NOOP("biff::ui::common::Model3DMaterialDataDelegate", "Ambient Color"),
                     QStringLiteral("ambientColor"), &model3d::Material::ambientColor);
        colorRow(QT_TRANSLATE_NOOP("biff::ui::common::Model3DMaterialDataDelegate", "Emissive Color"),
                 QStringLiteral("emissiveColor"), &model3d::Material::emissiveColor);
        if (model == Model::Phong) {
            numberRow(QT_TRANSLATE_NOOP("biff::ui::common::Model3DMaterialDataDelegate", "Shininess"),
                      QStringLiteral("shininess"), &model3d::Material::shininess, 0.0, 10000.0, 1);
        } else if (model == Model::CookTorrance) {
            percentRow(QT_TRANSLATE_NOOP("biff::ui::common::Model3DMaterialDataDelegate", "Roughness"),
                       QStringLiteral("cookTorranceRoughness"), &model3d::Material::cookTorranceRoughness);
            percentRow(QT_TRANSLATE_NOOP("biff::ui::common::Model3DMaterialDataDelegate", "Specular Reflectance"),
                       QStringLiteral("specularReflectance"), &model3d::Material::cookTorranceSpecularReflectance,
                       10000.0);
            plugin::EffectParameterSpec fresnel = number(0.0, 1.0, 3);
            fresnel.type = QStringLiteral("point3d");
            row(QT_TRANSLATE_NOOP("biff::ui::common::Model3DMaterialDataDelegate", "Fresnel"),
                QStringLiteral("fresnel"), fresnel,
                [](const model3d::Material& x) {
                    return QVariant(QStringLiteral("%1,%2,%3").arg(x.cookTorranceFresnel.x()).arg(x.cookTorranceFresnel.y())
                                        .arg(x.cookTorranceFresnel.z()));
                },
                [](model3d::Material& x, const QVariant& v) {
                    const QStringList parts = v.toString().split(QLatin1Char(','));
                    if (parts.size() == 3)
                        x.cookTorranceFresnel = QVector3D(parts[0].toFloat(), parts[1].toFloat(), parts[2].toFloat());
                });
        } else if (model == Model::MetallicRoughness) {
            percentRow(QT_TRANSLATE_NOOP("biff::ui::common::Model3DMaterialDataDelegate", "Metallic Strength"),
                       QStringLiteral("metallic"), &model3d::Material::metallic);
            percentRow(QT_TRANSLATE_NOOP("biff::ui::common::Model3DMaterialDataDelegate", "Roughness Strength"),
                       QStringLiteral("roughness"), &model3d::Material::roughness);
            percentRow(QT_TRANSLATE_NOOP("biff::ui::common::Model3DMaterialDataDelegate", "Clear Coat Strength"),
                       QStringLiteral("clearCoat"), &model3d::Material::clearCoat);
            percentRow(QT_TRANSLATE_NOOP("biff::ui::common::Model3DMaterialDataDelegate", "Clear Coat Roughness Strength"),
                       QStringLiteral("clearCoatRoughness"), &model3d::Material::clearCoatRoughness);
        } else {
            percentRow(QT_TRANSLATE_NOOP("biff::ui::common::Model3DMaterialDataDelegate", "SG Glossiness Strength"),
                       QStringLiteral("glossiness"), &model3d::Material::glossiness);
        }
        percentRow(QT_TRANSLATE_NOOP("biff::ui::common::Model3DMaterialDataDelegate", "Opacity"),
                   QStringLiteral("opacity"), &model3d::Material::opacity);
        plugin::EffectParameterSpec alphaModes;
        alphaModes.type = QStringLiteral("choice");
        alphaModes.choices = {QCoreApplication::translate("biff::ui::common::Model3DMaterialDataDelegate", "Guess"), QCoreApplication::translate("biff::ui::common::Model3DMaterialDataDelegate", "Opaque"), QCoreApplication::translate("biff::ui::common::Model3DMaterialDataDelegate", "Blend"), QCoreApplication::translate("biff::ui::common::Model3DMaterialDataDelegate", "Mask")};
        alphaModes.choiceValues = {QStringLiteral("0"), QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3")};
        row(QT_TRANSLATE_NOOP("biff::ui::common::Model3DMaterialDataDelegate", "Alpha Mode"),
            QStringLiteral("alphaMode"), alphaModes,
            [](const model3d::Material& x) { return QVariant(QString::number(int(x.alphaMode))); },
            [](model3d::Material& x, const QVariant& v) { x.alphaMode = model3d::AlphaMode(v.toInt()); });
        percentRow(QT_TRANSLATE_NOOP("biff::ui::common::Model3DMaterialDataDelegate", "Alpha Cut Off"),
                   QStringLiteral("alphaCutOff"), &model3d::Material::alphaCutOff);
        percentRow(QT_TRANSLATE_NOOP("biff::ui::common::Model3DMaterialDataDelegate", "Occlusion Strength"),
                   QStringLiteral("occlusionStrength"), &model3d::Material::occlusionStrength);
        numberRow(QT_TRANSLATE_NOOP("biff::ui::common::Model3DMaterialDataDelegate", "Max Bump Depth"),
                  QStringLiteral("maxBumpDepth"), &model3d::Material::bumpScale, 0.0, 100.0, 2);
        percentRow(QT_TRANSLATE_NOOP("biff::ui::common::Model3DMaterialDataDelegate", "Diffuse Reflectivity"),
                   QStringLiteral("diffuseReflectivity"), &model3d::Material::diffuseReflectivity);
        percentRow(QT_TRANSLATE_NOOP("biff::ui::common::Model3DMaterialDataDelegate", "Specular Reflectivity"),
                   QStringLiteral("specularReflectivity"), &model3d::Material::specularReflectivity);
        numberRow(QT_TRANSLATE_NOOP("biff::ui::common::Model3DMaterialDataDelegate", "Index of Refraction"),
                  QStringLiteral("refractionIndex"), &model3d::Material::indexOfRefraction, 0.0, 10.0, 3);
        percentRow(QT_TRANSLATE_NOOP("biff::ui::common::Model3DMaterialDataDelegate", "Refraction Amount"),
                   QStringLiteral("refractionAmount"), &model3d::Material::refractionAmount);
        top->setExpanded(m_mesh.materials.size() == 1);
    }
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
    // Edits made so far survive reading the file again.
    if (m_meshValid) m_edits = editedMaterials();
    const model3d::LoadResult loaded = model3d::loadModel(m_filePath, settings());
    m_meshValid = loaded.ok;
    m_mesh = loaded.mesh;
    m_fileMaterials = m_mesh.materials;
    model3d::applyMaterialSettings(m_mesh, m_edits);

    m_groups->clear();
    rebuildMaterialRows();

    if (!loaded.ok) {
        m_summary->setText(loaded.message);
        return;
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
