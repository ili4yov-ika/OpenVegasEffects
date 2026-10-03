#include "ui/LayerPanel.h"
#include "ui/CameraRule.h"
#include "ui/Theme.h"
#include "composition/CompositionState.h"
#include "render/RenderManager.h"
#include <QPointer>
#include <cmath>
#include <QSet>
#include <QResizeEvent>
#include "ui_Layer.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QUndoCommand>
#include <QUndoStack>
#include <QVBoxLayout>

namespace openvegas::ui {
namespace {
class LayerEditCommand final : public QUndoCommand
{
public:
    LayerEditCommand(std::shared_ptr<composition::Composition> composition, int index,
                     composition::Layer before, composition::Layer after,
                     std::function<void()> changed, const QString& title)
        : QUndoCommand(title), m_composition(std::move(composition)), m_index(index),
          m_id(before.id), m_before(std::move(before)), m_after(std::move(after)), m_changed(std::move(changed)) {}
    void undo() override { apply(m_before); }
    void redo() override { apply(m_after); }
private:
    void apply(const composition::Layer& value)
    {
        if (!m_composition) return;
        for (int i = 0; i < m_composition->layers().size(); ++i) {
            if (m_composition->layers()[i].id != m_id) continue;
            m_composition->layerRef(i) = value;
            if (m_changed) m_changed();
            break;
        }
    }
    std::shared_ptr<composition::Composition> m_composition;
    int m_index;
    core::Identifier m_id;
    composition::Layer m_before;
    composition::Layer m_after;
    std::function<void()> m_changed;
};

QString kindText(composition::LayerKind kind)
{
    return QCoreApplication::translate("LayerKind", composition::layerKindName(kind));
}
} // namespace

LayerPanel::LayerPanel(QWidget* parent) : QDockWidget(parent)
{
    Ui::LayerPanel form;
    form.setupUi(this);
    m_stack = form.stackedWidget;
    m_preview = form.layerPreview;
    connect(this, &QDockWidget::visibilityChanged, this, [this](bool on) { if (on) requestPreview(); });
    m_pageLayer = form.pageLayer;
    m_pageNoLayer = form.pageNoLayer;
    m_breadcrumb = form.labelBreadCrumbTrail;
    m_message = form.dummyWidget;
    m_kind = form.layerType;
    m_name = form.layerName;
    m_visible = form.layerVisible;
    m_muted = form.layerMuted;
    m_locked = form.layerLocked;
    m_blend = form.layerBlendMode;
    m_opacity = form.layerOpacity;
    m_cameraFov = form.layerCameraFov;
    m_cameraFovLabel = form.cameraFovLabel;
    m_dimension = form.layerDimension;
    m_parent = form.layerParent;
    m_labelColor = form.layerLabelColor;
    m_planeColorLabel = form.planeColorLabel;
    m_planeColor = form.layerPlaneColor;
    m_id = form.layerID;
    m_blend->addItems(composition::blendModeNames());
    m_dimension->setItemData(0, int(composition::LayerDimension::TwoD));
    m_dimension->setItemData(1, int(composition::LayerDimension::ThreeD));
    buildEditor(form.form);
    showMessage(tr("No Selection"));
}

void LayerPanel::buildEditor(QFormLayout* form)
{
    Q_UNUSED(form);

    connect(m_name, &QLineEdit::editingFinished, this, [this] {
        const QString value = m_name->text().trimmed();
        if (!m_updating && !value.isEmpty()) editLayer(tr("Set Layer Name"), [value](auto& l) { l.name = value; });
        else if (!m_updating) refresh();
    });
    connect(m_visible, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_updating) editLayer(tr("Set Layer Visibility"), [on](auto& l) { l.visible = on; });
    });
    connect(m_muted, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_updating) editLayer(tr("Set Layer Muting"), [on](auto& l) { l.muted = on; });
    });
    connect(m_locked, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_updating) editLayer(on ? tr("Lock Layer") : tr("Unlock Layer"), [on](auto& l) { l.locked = on; }, true);
    });
    connect(m_blend, &QComboBox::currentTextChanged, this, [this](const QString& value) {
        if (!m_updating) editLayer(tr("Set Layer Blend Mode"), [value](auto& l) { l.blendMode = value; });
    });
    connect(m_opacity, &QDoubleSpinBox::editingFinished, this, [this] {
        const double value = m_opacity->value() / 100.0;
        if (!m_updating) editLayer(tr("Set Layer Opacity"), [value](auto& l) { l.opacity = value; });
    });
    connect(m_dimension, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (m_updating || index < 0) return;
        const auto value = composition::LayerDimension(m_dimension->itemData(index).toInt());
        const int row = selectedLayer();
        if (value == composition::LayerDimension::ThreeD && m_composition && row >= 0
            && row < m_composition->layers().size() && !m_composition->layers().at(row).locked
            && !compositionIs3D(*m_composition)) {
            // A camera has to come first (CameraRule); both go in one record.
            if (!confirmAddCamera(this, AddCameraReason::SetDimension)) { refresh(); return; }
            const auto before = m_composition->layers();
            m_composition->insertLayer(m_composition->layers().size(), newCameraLayer(*m_composition));
            m_composition->layerRef(row).dimension = value;
            QPointer<LayerPanel> panel(this);
            const auto changed = [panel] { if (panel) { panel->refresh(); emit panel->layersModified(); } };
            if (m_undoStack) {
                m_undoStack->push(new LayerStackCommand(
                    m_composition, before, m_composition->layers(),
                    QCoreApplication::translate("CompositionTools", "Set Layer Dimension(s)"), changed));
            }
            changed();
            return;
        }
        editLayer(tr("Set Layer Dimension"), [value](auto& l) { l.dimension = value; });
    });
    connect(m_parent, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (m_updating || index < 0) return;
        const int candidate = m_parent->itemData(index).toInt();
        if (candidate >= 0 && wouldCreateParentCycle(candidate)) { refresh(); return; }
        const core::Identifier value = candidate >= 0 && m_composition
            ? m_composition->layers().at(candidate).id : core::Identifier();
        editLayer(tr("Set Layer Parent"), [value](auto& l) { l.parentLayerId = value; });
    });
    connect(m_cameraFov, &QDoubleSpinBox::editingFinished, this, [this] {
        if (!m_updating) editLayer(tr("Field of view"), [this](auto& l) { l.cameraFieldOfView = m_cameraFov->value(); });
    });
    connect(m_labelColor, &QPushButton::clicked, this, [this] { chooseColor(false); });
    connect(m_planeColor, &QPushButton::clicked, this, [this] { chooseColor(true); });
}

void LayerPanel::bindModel(std::shared_ptr<composition::Composition> composition)
{ m_composition = std::move(composition); refresh(); }
void LayerPanel::setSelection(int layerIndex)
{ setSelection(layerIndex < 0 ? QVector<int>() : QVector<int>{layerIndex}); }
void LayerPanel::setSelection(const QVector<int>& layerIndexes)
{ m_selection = layerIndexes; refresh(); }
int LayerPanel::selectedLayer() const
{ return m_selection.size() == 1 ? m_selection.first() : -1; }
void LayerPanel::showMessage(const QString& text)
{ ++m_previewGeneration; m_previewFrame = {}; m_preview->clear();
  m_message->setText(text); m_stack->setCurrentWidget(m_pageNoLayer); }

void LayerPanel::refresh()
{
    if (!m_composition) { showMessage(tr("No Composite Shot Selected")); return; }
    if (m_selection.isEmpty()) { showMessage(tr("No Selection")); return; }
    if (m_selection.size() > 1) { showMessage(tr("Multiple Selection")); return; }
    const int row = m_selection.first();
    if (row < 0 || row >= m_composition->layers().size()) { showMessage(tr("Invalid Layer Type")); return; }
    const auto& layer = m_composition->layers().at(row);
    m_updating = true;
    m_breadcrumb->setText(tr("%1 > %2").arg(m_composition->name(), layer.name));
    m_kind->setText(kindText(layer.kind)); m_name->setText(layer.name);
    m_visible->setChecked(layer.visible); m_muted->setChecked(layer.muted); m_locked->setChecked(layer.locked);
    m_blend->setCurrentText(layer.blendMode); m_opacity->setValue(layer.opacity * 100.0);
    m_dimension->setCurrentIndex(m_dimension->findData(int(layer.dimension)));
    m_parent->clear(); m_parent->addItem(tr("None"), -1);
    int parentIndex = 0;
    for (int i = 0; i < m_composition->layers().size(); ++i) {
        if (i == row || wouldCreateParentCycle(i)) continue;
        const auto& candidate = m_composition->layers().at(i);
        m_parent->addItem(candidate.name, i);
        if (candidate.id == layer.parentLayerId) parentIndex = m_parent->count() - 1;
    }
    m_parent->setCurrentIndex(parentIndex);
    updateColorButton(m_labelColor, layer.labelColor); updateColorButton(m_planeColor, layer.planeColor);
    const bool plane = layer.kind == composition::LayerKind::Plane;
    m_planeColorLabel->setVisible(plane); m_planeColor->setVisible(plane);
    m_cameraFov->setValue(layer.cameraFieldOfView);
    m_cameraFov->setVisible(layer.kind == composition::LayerKind::Camera);
    m_cameraFovLabel->setVisible(layer.kind == composition::LayerKind::Camera);
    m_id->setText(layer.id.value()); m_stack->setCurrentWidget(m_pageLayer);
    for (QWidget* editor : QList<QWidget*>{static_cast<QWidget*>(m_name), m_visible, m_muted,
                            m_blend, m_opacity, m_parent, m_labelColor, m_planeColor, m_cameraFov})
        editor->setEnabled(!layer.locked);
    const bool sceneObject = layer.kind == composition::LayerKind::Camera
        || layer.kind == composition::LayerKind::Light || layer.kind == composition::LayerKind::Model3D;
    m_dimension->setEnabled(!layer.locked && !sceneObject);
    m_updating = false;
    requestPreview();
}

void LayerPanel::editLayer(const QString& title, const std::function<void(composition::Layer&)>& change, bool allowLocked)
{
    const int row = selectedLayer();
    if (!m_composition || row < 0 || row >= m_composition->layers().size()) return;
    composition::Layer before = m_composition->layerCopy(row), after = before;
    if (before.locked && !allowLocked) { refresh(); return; }
    change(after);
    if (before.kind == composition::LayerKind::Camera || before.kind == composition::LayerKind::Light
        || before.kind == composition::LayerKind::Model3D) after.dimension = before.dimension;
    if (composition::layerState({before}) == composition::layerState({after})) { refresh(); return; }
    QPointer<LayerPanel> panel(this);
    auto changed = [panel] { if (panel) { panel->refresh(); emit panel->layersModified(); } };
    if (m_undoStack) m_undoStack->push(new LayerEditCommand(m_composition, row, before, after, changed, title));
    else { m_composition->layerRef(row) = after; changed(); }
}

void LayerPanel::chooseColor(bool plane)
{
    const int row = selectedLayer(); if (!m_composition || row < 0 || row >= m_composition->layers().size()) return;
    const QColor current = plane ? m_composition->layers().at(row).planeColor
                                 : m_composition->layers().at(row).labelColor;
    const QColor color = interfaceColor(current, this,
        plane ? tr("Plane Color") : tr("Set Layer Label"));
    if (!color.isValid()) return;
    editLayer(plane ? tr("Set Plane Color") : tr("Set Layer Label"),
              [plane, color](auto& l) { if (plane) l.planeColor = color; else l.labelColor = color; });
}

void LayerPanel::updateColorButton(QPushButton* button, const QColor& color)
{
    button->setText(color.name(QColor::HexRgb).toUpper());
    button->setStyleSheet(QStringLiteral("QPushButton { background:%1; color:%2; }")
        .arg(color.name(), color.lightness() < 128 ? QStringLiteral("white") : QStringLiteral("black")));
}

bool LayerPanel::wouldCreateParentCycle(int candidate) const
{
    const int selected = selectedLayer();
    if (!m_composition || selected < 0 || candidate < 0 || candidate >= m_composition->layers().size()) return false;
    core::Identifier id = m_composition->layers().at(candidate).id;
    const core::Identifier selectedId = m_composition->layers().at(selected).id;
    QSet<QString> visited;
    while (id.isValid()) {
        if (id == selectedId || visited.contains(id.value())) return true;
        visited.insert(id.value());
        bool found = false;
        for (const auto& layer : m_composition->layers()) {
            if (layer.id == id) { id = layer.parentLayerId; found = true; break; }
        }
        if (!found) break;
    }
    return false;
}
void LayerPanel::setMediaManager(std::shared_ptr<media::MediaManager> media)
{
    if (m_media != media && m_previewRenderer) { delete m_previewRenderer; m_previewRenderer = nullptr; }
    m_media = std::move(media);
    if (!m_previewRenderer) {
        m_previewRenderer = new render::RenderManager(this);
        connect(m_previewRenderer, &render::RenderManager::frameReady, this,
                [this](int generation, const QByteArray& rgba, QSize size) {
            if (generation != m_previewGeneration || size.isEmpty()
                || rgba.size() < qsizetype(size.width()) * size.height() * 4) return;
            m_previewFrame = QImage(reinterpret_cast<const uchar*>(rgba.constData()),
                size.width(), size.height(), size.width() * 4, QImage::Format_RGBA8888).copy();
            updatePreviewPixmap();
        });
    }
    m_previewRenderer->setMediaManager(m_media);
    requestPreview();
}

std::shared_ptr<composition::Composition> LayerPanel::previewComposition() const
{
    const int row = selectedLayer();
    if (!m_composition || row < 0 || row >= m_composition->layers().size()) return {};
    auto layer = m_composition->layerCopy(row);
    if (layer.kind == composition::LayerKind::Point || layer.kind == composition::LayerKind::Camera
        || layer.kind == composition::LayerKind::Light || layer.kind == composition::LayerKind::Grade) return {};
    if (layer.kind == composition::LayerKind::Media && m_media && !layer.clips.isEmpty()) {
        bool hasPicture = false;
        for (const auto& clip : layer.clips) {
            const auto asset = m_media->assetById(clip.mediaId);
            if (clip.nestedComposition || asset.kind() != media::MediaKind::Audio) { hasPicture = true; break; }
        }
        if (!hasPicture) return {};
    }
    layer.visible = true;
    layer.parentLayerId = {};
    layer.blendMode = QStringLiteral("Normal");
    auto snapshot = std::make_shared<composition::Composition>(*m_composition);
    QVector<composition::Layer> layers{layer};
    if (layer.kind == composition::LayerKind::Model3D) {
        for (const auto& scene : m_composition->layers())
            if (scene.kind == composition::LayerKind::Camera || scene.kind == composition::LayerKind::Light)
                layers.append(scene);
    }
    snapshot->setLayers(layers);
    return snapshot;
}

void LayerPanel::setCurrentTime(double seconds)
{
    if (!std::isfinite(seconds)) return;
    m_time = qMax(0.0, seconds);
    if (isVisible() && !visibleRegion().isEmpty()) requestPreview();
}

void LayerPanel::requestPreview()
{
    ++m_previewGeneration;
    const auto snapshot = previewComposition();
    if (!snapshot) { m_previewFrame = {}; m_preview->setText(tr("Invalid Layer Type")); return; }
    if (!m_previewRenderer || !m_media) return;
    m_previewRenderer->setComposition(snapshot);
    // At the shot's square shape (width x PAR), since the panel shows the
    // picture as it is, not as stored.
    QSize size = snapshot->displaySize();
    if (size.width() > 960 || size.height() > 540) size.scale(QSize(960,540), Qt::KeepAspectRatio);
    m_previewRenderer->requestFrame(m_previewGeneration, m_time, size, true);
}

void LayerPanel::updatePreviewPixmap()
{
    if (!m_previewFrame.isNull())
        m_preview->setPixmap(QPixmap::fromImage(m_previewFrame).scaled(m_preview->size(),
            Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void LayerPanel::resizeEvent(QResizeEvent* event)
{
    QDockWidget::resizeEvent(event);
    updatePreviewPixmap();
}

} // namespace openvegas::ui

