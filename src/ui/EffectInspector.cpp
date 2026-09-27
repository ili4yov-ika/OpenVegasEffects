#include "ui/EffectInspector.h"
#include "ui_Controls.h"

#include "composition/CompositionState.h"
#include "plugin/PluginManager.h"
#include "ui/TimelineParameterEditor.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QInputDialog>
#include <QPointer>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QTimer>
#include <QUndoCommand>
#include <QUndoStack>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace openvegas::ui {
namespace {
constexpr double kPi = 3.14159265358979323846;
QString itemPath(QTreeWidgetItem* item)
{
    QStringList result;
    while (item) {
        result.prepend(QString::number(item->parent() ? item->parent()->indexOfChild(item)
                                                      : item->treeWidget()->indexOfTopLevelItem(item)));
        item = item->parent();
    }
    return result.join('/');
}

QString expandedKey(QTreeWidgetItem* item)
{
    return itemPath(item) + QLatin1Char('|') + item->text(0);
}

class ColorWheelEditor final : public QWidget
{
public:
    ColorWheelEditor(QWidget* parent, std::function<void(QColor)> changed)
        : QWidget(parent), m_changed(std::move(changed))
    {
        setFixedSize(112, 112); setCursor(Qt::CrossCursor);
    }
    void setColor(const QColor& color)
    {
        if (!color.isValid()) return;
        m_color = color; update();
    }
protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
        const QRect wheel = rect().adjusted(5, 5, -5, -5);
        if (m_wheel.size() != wheel.size()) {
            m_wheel = QImage(wheel.size(), QImage::Format_ARGB32_Premultiplied);
            const QPointF center(m_wheel.width() / 2., m_wheel.height() / 2.);
            const double radius = qMin(center.x(), center.y()) - 1.;
            for (int y = 0; y < m_wheel.height(); ++y) for (int x = 0; x < m_wheel.width(); ++x) {
                const double dx = x - center.x(), dy = y - center.y();
                const double saturation = std::sqrt(dx * dx + dy * dy) / radius;
                if (saturation > 1.) { m_wheel.setPixelColor(x, y, Qt::transparent); continue; }
                double hue = std::atan2(dy, dx) / (2. * kPi); if (hue < 0.) hue += 1.;
                m_wheel.setPixelColor(x, y, QColor::fromHsvF(hue, saturation, 1.));
            }
        }
        painter.drawImage(wheel.topLeft(), m_wheel);
        painter.setPen(QPen(QColor("#111"), 2)); painter.drawEllipse(wheel.adjusted(0, 0, -1, -1));
        qreal hue = m_color.hsvHueF(); if (hue < 0.) hue = 0.;
        const double angle = hue * 2. * kPi;
        const double radius = wheel.width() / 2. * m_color.hsvSaturationF();
        const QPointF marker = wheel.center() + QPointF(std::cos(angle) * radius, std::sin(angle) * radius);
        painter.setPen(QPen(Qt::black, 3)); painter.setBrush(Qt::white); painter.drawEllipse(marker, 4, 4);
    }
    void mousePressEvent(QMouseEvent* event) override { setFromPoint(event->position()); }
    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (event->buttons().testFlag(Qt::LeftButton)) setFromPoint(event->position());
    }
private:
    void setFromPoint(const QPointF& point)
    {
        const QPointF delta = point - rect().center();
        const double radius = qMax(1., width() / 2. - 5.);
        const double saturation = qBound(0., std::hypot(delta.x(), delta.y()) / radius, 1.);
        double hue = std::atan2(delta.y(), delta.x()) / (2. * kPi); if (hue < 0.) hue += 1.;
        setColor(QColor::fromHsvF(hue, saturation, 1.));
        if (m_changed) m_changed(m_color);
    }
    QColor m_color = Qt::white;
    QImage m_wheel;
    std::function<void(QColor)> m_changed;
};

class ControlsEditCommand final : public QUndoCommand
{
public:
    ControlsEditCommand(std::shared_ptr<composition::Composition> comp,
                        QVector<composition::Layer> before, QVector<composition::Layer> after,
                        QString title, QString mergeKey, std::function<void()> notify)
        : QUndoCommand(std::move(title)), m_comp(std::move(comp)), m_before(std::move(before)),
          m_after(std::move(after)), m_mergeKey(std::move(mergeKey)),
          m_notify(std::move(notify)), m_time(QDateTime::currentMSecsSinceEpoch()) {}

    void redo() override
    {
        if (m_first) { m_first = false; return; }
        apply(m_after);
    }
    void undo() override { apply(m_before); }
    int id() const override { return 0x4354524c; }
    bool mergeWith(const QUndoCommand* command) override
    {
        const auto* other = dynamic_cast<const ControlsEditCommand*>(command);
        if (!other || other->m_comp != m_comp || other->m_mergeKey != m_mergeKey
            || other->m_time - m_time > 750) return false;
        m_after = other->m_after;
        m_time = other->m_time;
        return true;
    }

private:
    void apply(const QVector<composition::Layer>& layers)
    {
        while (!m_comp->layers().isEmpty()) m_comp->removeLayer(m_comp->layers().size() - 1);
        for (const auto& layer : layers) m_comp->insertLayer(m_comp->layers().size(), layer);
        m_notify();
    }
    std::shared_ptr<composition::Composition> m_comp;
    QVector<composition::Layer> m_before, m_after;
    QString m_mergeKey;
    std::function<void()> m_notify;
    qint64 m_time = 0;
    bool m_first = true;
};

QToolButton* smallButton(QWidget* parent, const QString& name, const QString& icon,
                         const QString& tooltip)
{
    auto* button = new QToolButton(parent);
    button->setObjectName(name);
    if (!icon.isEmpty()) button->setIcon(QIcon(icon));
    button->setIconSize(QSize(13, 13));
    button->setFixedSize(20, 20);
    button->setAutoRaise(true);
    button->setToolTip(tooltip);
    return button;
}
} // namespace

EffectInspector::EffectInspector(QWidget* parent) : QDockWidget(parent)
{
    Ui::EffectInspector form;
    form.setupUi(this);
    QWidget* panel = form.ControlsPanelWidget;
    m_selectionTitle = form.controlsSelectionTitle;
    auto* pin = form.controlsPin;
    pin->setIcon(QIcon(":/icons/pin.svg"));
    form.clockIcon->setPixmap(QIcon(":/icons/clock.svg").pixmap(14, 14));
    m_timeLabel = form.controlsCurrentTime;
    QFont timeFont = m_timeLabel->font(); timeFont.setBold(true); m_timeLabel->setFont(timeFont);
    form.filterIcon->setPixmap(QIcon(":/icons/filter.svg").pixmap(13, 13));
    m_search = form.controlsSearch;
    auto* collapse = form.controlsCollapseAll;
    auto* expand = form.controlsExpandAll;
    collapse->setIcon(QIcon(":/icons/list.svg"));
    expand->setIcon(QIcon(":/icons/grid.svg"));
    m_tree = form.controlsTree;
    m_tree->header()->setStretchLastSection(true);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_tree->setColumnWidth(0, 150);

    panel->setStyleSheet(R"(
        QWidget#ControlsPanelWidget { background:#222; }
        QWidget#ControlsPanelWidget QTreeWidget { background:#222; border:0; }
        QWidget#ControlsPanelWidget QTreeWidget::item { min-height:22px; padding:0 2px; border-bottom:1px solid #292929; }
        QWidget#ControlsPanelWidget QTreeWidget::item:selected { background:#566873; }
        QWidget#ControlsPanelWidget QLineEdit, QWidget#ControlsPanelWidget QComboBox,
        QWidget#ControlsPanelWidget QAbstractSpinBox { background:transparent; border:0; border-radius:0; padding:0 3px; min-height:18px; }
        QWidget#ControlsPanelWidget QLineEdit:focus, QWidget#ControlsPanelWidget QAbstractSpinBox:focus { background:#1b1b1b; }
        QWidget#ControlsPanelWidget QToolButton { padding:0; min-width:18px; min-height:18px; }
    )");

    connect(m_search, &QLineEdit::textChanged, this, &EffectInspector::applySearch);
    connect(collapse, &QToolButton::clicked, m_tree, &QTreeWidget::collapseAll);
    connect(expand, &QToolButton::clicked, m_tree, &QTreeWidget::expandAll);
    connect(m_tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
        if (m_building || column != 0 || !item->data(0, Qt::UserRole + 2).isValid()) return;
        const int effectIndex = item->data(0, Qt::UserRole + 2).toInt();
        const bool enabled = item->checkState(0) == Qt::Checked;
        editModel(tr("Enable effect"), QStringLiteral("effect-enabled-%1").arg(effectIndex),
                  [=](composition::Layer& layer) {
                      if (m_clipIndex >= 0 && m_clipIndex < layer.clips.size()
                          && effectIndex >= 0 && effectIndex < layer.clips[m_clipIndex].effects.size())
                          layer.clips[m_clipIndex].effects[effectIndex].enabled = enabled;
                  }, false, true);
    });
    updateHeader();
}

void EffectInspector::setPluginManager(plugin::PluginManager* manager)
{
    m_pluginManager = manager;
    refresh();
}

void EffectInspector::setComposition(std::shared_ptr<composition::Composition> comp)
{
    m_comp = std::move(comp);
    m_layerIndex = -1; m_clipIndex = -1;
    refresh();
}

void EffectInspector::setSelection(int layerIndex, int clipIndex)
{
    auto* pin = findChild<QToolButton*>("controlsPin");
    if (pin && pin->isChecked() && m_layerIndex >= 0) return;
    m_layerIndex = layerIndex;
    m_followPlayheadClip = clipIndex < 0;
    m_clipIndex = clipIndex;
    if (m_comp && m_layerIndex >= 0 && m_layerIndex < m_comp->layers().size()) {
        const auto& clips = m_comp->layers()[m_layerIndex].clips;
        if ((m_clipIndex < 0 || m_clipIndex >= clips.size()) && !clips.isEmpty()) {
            m_clipIndex = 0;
            for (int i = 0; i < clips.size(); ++i)
                if (m_time >= clips[i].startSeconds && m_time < clips[i].endSeconds()) { m_clipIndex = i; break; }
        }
    }
    refresh();
}

void EffectInspector::updateHeader()
{
    if (!m_selectionTitle || !m_timeLabel) return;
    if (!m_comp || m_layerIndex < 0 || m_layerIndex >= m_comp->layers().size()) {
        m_selectionTitle->setText(tr("No layer selected"));
    } else {
        const auto& layer = m_comp->layers()[m_layerIndex];
        const QString clip = m_clipIndex >= 0 && m_clipIndex < layer.clips.size()
            ? QString::number(m_clipIndex + 1) : tr("Layer");
        m_selectionTitle->setText(QStringLiteral("%1 > %2").arg(layer.name, clip));
    }
    const double fps = m_comp ? double(m_comp->fpsNumerator()) / qMax(1, m_comp->fpsDenominator()) : 30.;
    const int nominal = qMax(1, qRound(fps));
    const int frames = qMax(0, qRound(m_time * fps));
    const int seconds = frames / nominal;
    m_timeLabel->setText(QStringLiteral("%1;%2;%3;%4")
        .arg(seconds / 3600, 2, 10, QLatin1Char('0')).arg(seconds / 60 % 60, 2, 10, QLatin1Char('0'))
        .arg(seconds % 60, 2, 10, QLatin1Char('0')).arg(frames % nominal, 2, 10, QLatin1Char('0')));
}

void EffectInspector::refresh()
{
    QHash<QString, bool> expanded;
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it) expanded[expandedKey(*it)] = (*it)->isExpanded();
    m_building = true;
    m_valueReaders.clear();
    m_tree->clear();
    updateHeader();
    if (!m_comp || m_layerIndex < 0 || m_layerIndex >= m_comp->layers().size()) {
        new QTreeWidgetItem(m_tree, {tr("Select a layer in the Timeline")});
        m_building = false;
        return;
    }
    const auto& layer = m_comp->layers()[m_layerIndex];
    buildLayerProperties(layer);
    buildTracks(layer);
    buildMasks(layer);
    buildEffects(layer);
    buildTransformSection(layer, currentFrame());
    buildEffects(layer, true);
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
        auto* item = *it;
        item->setSizeHint(0, QSize(0, 22));
        if (expanded.contains(expandedKey(item))) item->setExpanded(expanded[expandedKey(item)]);
    }
    m_building = false;
    applySearch();
    refreshValues();
}

void EffectInspector::buildTracks(const composition::Layer& layer)
{
    auto* root = new QTreeWidgetItem(m_tree, {tr("Tracks")});
    for (int index = 0; index < layer.motionTracks.size(); ++index) {
        const composition::MotionTrack& track = layer.motionTracks.at(index);
        auto* row = new QTreeWidgetItem(root, {track.name});
        auto* controls = new QWidget(m_tree);
        auto* layout = new QHBoxLayout(controls);
        layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(3);
        auto* enabled = new QCheckBox(controls);
        enabled->setObjectName(QStringLiteral("controlsTrackEnabled_%1").arg(index));
        enabled->setChecked(track.enabled); enabled->setToolTip(tr("Enable track"));
        layout->addWidget(enabled);
        const auto coordinate = [controls, layout](const QString& name, double value, const QString& tip) {
            auto* editor = new QDoubleSpinBox(controls); editor->setObjectName(name);
            editor->setRange(-32768.0, 32768.0); editor->setDecimals(1); editor->setValue(value);
            editor->setFixedWidth(68); editor->setToolTip(tip); layout->addWidget(editor); return editor;
        };
        auto* x = coordinate(QStringLiteral("controlsTrackX_%1").arg(index), track.point.x(), tr("Tracking point X"));
        auto* y = coordinate(QStringLiteral("controlsTrackY_%1").arg(index), track.point.y(), tr("Tracking point Y"));
        auto* analyze = smallButton(controls, QStringLiteral("controlsTrackAnalyze_%1").arg(index),
                                    QStringLiteral(":/icons/play.svg"), tr("Analyze forward"));
        layout->addWidget(analyze);
        m_tree->setItemWidget(row, 1, controls);
        connect(enabled, &QCheckBox::toggled, this, [this, index](bool value) {
            editModel(tr("Enable track"), QStringLiteral("track-enabled-%1").arg(index),
                [=](composition::Layer& target) { if (index < target.motionTracks.size()) target.motionTracks[index].enabled = value; });
        });
        const auto setCoordinate = [this, index](int axis, double value) {
            editModel(tr("Set tracking point"), QStringLiteral("track-point-%1-%2").arg(index).arg(axis),
                [=](composition::Layer& target) {
                    if (index >= target.motionTracks.size()) return;
                    if (axis == 0) target.motionTracks[index].point.setX(value);
                    else target.motionTracks[index].point.setY(value);
                    target.motionTracks[index].xCurve.clear(); target.motionTracks[index].yCurve.clear();
                }, true);
        };
        connect(x, &QDoubleSpinBox::valueChanged, this, [setCoordinate](double value) { setCoordinate(0, value); });
        connect(y, &QDoubleSpinBox::valueChanged, this, [setCoordinate](double value) { setCoordinate(1, value); });
        connect(analyze, &QToolButton::clicked, this, [this, index] { emit motionTrackingRequested(m_layerIndex, index); });
        m_valueReaders.append([this, index, x, y] {
            const composition::Layer* current = layerRef();
            if (!current || index >= current->motionTracks.size()) return;
            const QPointF value = current->motionTracks[index].pointAt(currentFrame());
            const QSignalBlocker bx(x), by(y); x->setValue(value.x()); y->setValue(value.y());
        });
    }
    root->setExpanded(!layer.motionTracks.isEmpty());
}

void EffectInspector::buildLayerProperties(const composition::Layer& layer)
{
    auto* root = new QTreeWidgetItem(m_tree, {tr("Layer Properties")});
    root->setExpanded(true);
    const bool editable = !layer.locked;
    const auto addRow = [this, root](const QString& label, QWidget* editor, const QString& name) {
        auto* row = new QTreeWidgetItem(root, {label}); row->setData(0, Qt::UserRole, m_layerIndex);
        editor->setObjectName(name); m_tree->setItemWidget(row, 1, editor); return row;
    };
    auto* visible = new QCheckBox(m_tree); visible->setChecked(layer.visible); visible->setEnabled(editable);
    addRow(tr("Visible"), visible, "controlsLayerVisible");
    connect(visible, &QCheckBox::toggled, this, [this](bool on) {
        editModel(tr("Layer visibility"), "layer-visible", [=](composition::Layer& l) { l.visible = on; });
    });
    auto* dimension = new QComboBox(m_tree); dimension->addItems({"2D", "3D"});
    dimension->setCurrentIndex(layer.dimension == composition::LayerDimension::ThreeD);
    dimension->setEnabled(editable && layer.kind != composition::LayerKind::Model3D
                          && layer.kind != composition::LayerKind::Camera
                          && layer.kind != composition::LayerKind::Light);
    addRow(tr("Dimension"), dimension, "controlsLayerDimension");
    connect(dimension, &QComboBox::currentIndexChanged, this, [this](int index) {
        editModel(tr("Layer dimension"), "layer-dimension", [=](composition::Layer& l) {
            l.dimension = index ? composition::LayerDimension::ThreeD : composition::LayerDimension::TwoD;
        }, false, true);
    });
    auto* blend = new QComboBox(m_tree); blend->addItems(composition::blendModeNames());
    blend->setCurrentText(layer.blendMode); blend->setEnabled(editable);
    addRow(tr("Blend"), blend, "controlsLayerBlend");
    connect(blend, &QComboBox::currentTextChanged, this, [this](const QString& value) {
        editModel(tr("Layer blend mode"), "layer-blend", [=](composition::Layer& l) { l.blendMode = value; });
    });
}

void EffectInspector::buildMasks(const composition::Layer& layer)
{
    auto* root = new QTreeWidgetItem(m_tree, {tr("Masks")});
    root->setExpanded(!layer.masks.isEmpty());
    auto* add = smallButton(m_tree, "controlsAddMask", ":/icons/add.svg", tr("Add mask"));
    add->setEnabled(!layer.locked);
    m_tree->setItemWidget(root, 1, add);
    connect(add, &QToolButton::clicked, this, [this] {
        if (!m_comp) return;
        const QRectF bounds(m_comp->width() * .25, m_comp->height() * .25,
                            m_comp->width() * .5, m_comp->height() * .5);
        editModel(tr("Add mask"), "add-mask", [bounds](composition::Layer& target) {
            composition::LayerMask mask;
            mask.name = QObject::tr("Mask %1").arg(target.masks.size() + 1);
            mask.bounds = bounds;
            target.masks.append(mask);
        }, false, true);
    });

    const QStringList shapes{tr("Rectangle"), tr("Rounded Rectangle"), tr("Ellipse"),
                             tr("Polygon"), tr("Star"), tr("Freehand")};
    for (int index = 0; index < layer.masks.size(); ++index) {
        const composition::LayerMask& mask = layer.masks.at(index);
        auto* row = new QTreeWidgetItem(root, {mask.name});
        auto* controls = new QWidget(m_tree);
        auto* layout = new QHBoxLayout(controls);
        layout->setContentsMargins(0, 0, 1, 0);
        layout->setSpacing(3);
        auto* enabled = new QCheckBox(controls);
        enabled->setObjectName(QStringLiteral("controlsMaskEnabled_%1").arg(index));
        enabled->setChecked(mask.enabled);
        enabled->setEnabled(!layer.locked);
        enabled->setToolTip(tr("Enable mask"));
        layout->addWidget(enabled);
        auto* shape = new QComboBox(controls);
        shape->setObjectName(QStringLiteral("controlsMaskShape_%1").arg(index));
        shape->addItems(shapes);
        shape->setCurrentIndex(int(mask.shape));
        shape->setEnabled(!layer.locked);
        layout->addWidget(shape, 1);
        const auto number = [controls, layout](const QString& name, double value,
                                               double minimum, double maximum,
                                               const QString& suffix) {
            auto* editor = new QDoubleSpinBox(controls);
            editor->setObjectName(name); editor->setRange(minimum, maximum);
            editor->setDecimals(1); editor->setValue(value); editor->setSuffix(suffix);
            editor->setFixedWidth(76); layout->addWidget(editor); return editor;
        };
        auto* opacity = number(QStringLiteral("controlsMaskOpacity_%1").arg(index),
                               mask.opacity * 100.0, 0.0, 100.0, QStringLiteral("%"));
        auto* feather = number(QStringLiteral("controlsMaskFeather_%1").arg(index),
                               mask.feather, 0.0, 1000.0, QStringLiteral(" px"));
        auto* expansion = number(QStringLiteral("controlsMaskExpansion_%1").arg(index),
                                 mask.expansion, -1000.0, 1000.0, QStringLiteral(" px"));
        opacity->setEnabled(!layer.locked); feather->setEnabled(!layer.locked);
        expansion->setEnabled(!layer.locked);
        auto* invert = new QCheckBox(tr("Invert"), controls);
        invert->setObjectName(QStringLiteral("controlsMaskInvert_%1").arg(index));
        invert->setChecked(mask.inverted); invert->setEnabled(!layer.locked);
        layout->addWidget(invert);
        auto* remove = smallButton(controls,
            QStringLiteral("controlsMaskRemove_%1").arg(index),
            QStringLiteral(":/text-icons/remove.svg"), tr("Remove mask"));
        remove->setEnabled(!layer.locked); layout->addWidget(remove);
        m_tree->setItemWidget(row, 1, controls);

        const auto editMask = [this, index](const QString& title, const QString& key,
                                            const std::function<void(composition::LayerMask&)>& edit,
                                            bool rebuild = false) {
            editModel(title, key, [index, edit](composition::Layer& target) {
                if (index < target.masks.size()) edit(target.masks[index]);
            }, false, rebuild);
        };
        connect(enabled, &QCheckBox::toggled, this, [editMask](bool value) {
            editMask(QObject::tr("Enable mask"), "mask-enabled",
                     [value](auto& target) { target.enabled = value; });
        });
        connect(shape, &QComboBox::currentIndexChanged, this, [editMask](int value) {
            editMask(QObject::tr("Mask shape"), "mask-shape",
                     [value](auto& target) { target.shape = composition::MaskShape(value); });
        });
        connect(opacity, &QDoubleSpinBox::valueChanged, this, [editMask](double value) {
            editMask(QObject::tr("Mask opacity"), "mask-opacity",
                     [value](auto& target) { target.opacity = value / 100.0; });
        });
        connect(feather, &QDoubleSpinBox::valueChanged, this, [editMask](double value) {
            editMask(QObject::tr("Mask feather"), "mask-feather",
                     [value](auto& target) { target.feather = value; });
        });
        connect(expansion, &QDoubleSpinBox::valueChanged, this, [editMask](double value) {
            editMask(QObject::tr("Mask expansion"), "mask-expansion",
                     [value](auto& target) { target.expansion = value; });
        });
        connect(invert, &QCheckBox::toggled, this, [editMask](bool value) {
            editMask(QObject::tr("Invert mask"), "mask-invert",
                     [value](auto& target) { target.inverted = value; });
        });
        connect(remove, &QToolButton::clicked, this, [this, index] {
            editModel(tr("Remove mask"), "remove-mask", [index](composition::Layer& target) {
                if (index < target.masks.size()) target.masks.removeAt(index);
            }, false, true);
        });
    }
}

void EffectInspector::buildEffects(const composition::Layer& layer, bool behaviors)
{
    auto* root = new QTreeWidgetItem(m_tree, {behaviors ? tr("Behaviors") : tr("Effects")});
    root->setExpanded(true);
    auto* add = smallButton(m_tree, behaviors ? "controlsAddBehavior" : "controlsAddEffect",
                            ":/icons/add.svg", behaviors ? tr("Add behavior") : tr("Add effect"));
    add->setEnabled(!layer.locked && m_pluginManager && m_clipIndex >= 0 && m_clipIndex < layer.clips.size());
    m_tree->setItemWidget(root, 1, add);
    connect(add, &QToolButton::clicked, this, [this, add, behaviors] {
        if (!m_pluginManager) return;
        QMenu menu(this);
        auto ids = behaviors
            ? m_pluginManager->pluginIdsByKind(plugin::PluginKind::BehaviorEffect)
            : m_pluginManager->allPluginIds();
        std::sort(ids.begin(), ids.end(), [this](const auto& a, const auto& b) {
            return m_pluginManager->spec(a).displayName < m_pluginManager->spec(b).displayName;
        });
        for (const auto& id : ids) {
            const auto spec = m_pluginManager->spec(id);
            if (id.value() == "text"
                || (spec.kind == plugin::PluginKind::BehaviorEffect) != behaviors) continue;
            auto* action = menu.addAction(spec.displayName);
            action->setEnabled(spec.renderable);
            action->setToolTip(spec.unavailableReason);
            connect(action, &QAction::triggered, this, [this, spec] {
                editModel(tr("Add %1").arg(spec.displayName), "add-effect", [=](composition::Layer& l) {
                    if (m_clipIndex < 0 || m_clipIndex >= l.clips.size()) return;
                    composition::Effect effect; effect.pluginId = spec.id; effect.name = spec.displayName;
                    for (const auto& param : spec.parameters) effect.parameterValues.append(param.defaultValue);
                    l.clips[m_clipIndex].effects.append(effect);
                }, false, true);
            });
        }
        if (menu.isEmpty()) { auto* none = menu.addAction(tr("No compatible effects installed")); none->setEnabled(false); }
        menu.exec(add->mapToGlobal(QPoint(0, add->height())));
    });
    if (m_clipIndex < 0 || m_clipIndex >= layer.clips.size()) return;
    const auto& clip = layer.clips[m_clipIndex];
    const auto behaviorIds = m_pluginManager
        ? m_pluginManager->pluginIdsByKind(plugin::PluginKind::BehaviorEffect)
        : QVector<plugin::PluginId>{};
    for (int e = 0; e < clip.effects.size(); ++e) {
        const auto& effect = clip.effects[e];
        if (behaviorIds.contains(effect.pluginId) != behaviors) continue;
        const auto spec = m_pluginManager ? m_pluginManager->spec(effect.pluginId) : plugin::EffectSpec{};
        auto* effectItem = new QTreeWidgetItem(root, {effect.name});
        effectItem->setCheckState(0, effect.enabled ? Qt::Checked : Qt::Unchecked);
        effectItem->setData(0, Qt::UserRole, m_layerIndex);
        effectItem->setData(0, Qt::UserRole + 1, m_clipIndex);
        effectItem->setData(0, Qt::UserRole + 2, e);
        effectItem->setExpanded(true);
        effectItem->setToolTip(0, spec.renderable ? spec.description : spec.unavailableReason);

        auto* presetItem = new QTreeWidgetItem(effectItem, {QStringLiteral("•")});
        auto* presets = new QComboBox(m_tree); presets->setObjectName(QStringLiteral("controlsPreset_%1").arg(e));
        presets->addItem(tr("Preset"), "none"); presets->addItem(tr("Reset to defaults"), "reset");
        const QString settingsKey = "TimelinePresets/" + effect.pluginId.value();
        QSettings settings; settings.beginGroup(settingsKey);
        for (const auto& name : settings.childKeys()) presets->addItem(name, "load:" + name);
        settings.endGroup();
        presets->addItem(tr("Save preset…"), "save"); presets->addItem(tr("Remove effect"), "remove");
        presets->setEnabled(!layer.locked);
        m_tree->setItemWidget(presetItem, 1, presets);
        connect(presets, &QComboBox::activated, this, [this, presets, e, spec, settingsKey](int index) {
            const QPointer<QComboBox> presetGuard(presets);
            const QString action = presets->itemData(index).toString();
            if (action == "save") {
                bool ok = false;
                const QString name = QInputDialog::getText(this, tr("Save preset"), tr("Name"),
                    QLineEdit::Normal, QString(), &ok).trimmed();
                if (ok && !name.isEmpty() && !name.contains('/')) {
                    if (auto* effect = effectAt(e)) QSettings().setValue(settingsKey + '/' + name, effect->parameterValues);
                    const QPointer<EffectInspector> self(this);
                    QTimer::singleShot(0, this, [self] { if (self) self->refresh(); });
                }
            } else if (action != "none") {
                editModel(action == "remove" ? tr("Remove effect") : tr("Apply preset"),
                          QStringLiteral("effect-preset-%1").arg(e), [=](composition::Layer& l) {
                    if (m_clipIndex < 0 || m_clipIndex >= l.clips.size() || e >= l.clips[m_clipIndex].effects.size()) return;
                    if (action == "remove") { l.clips[m_clipIndex].effects.removeAt(e); return; }
                    auto& edited = l.clips[m_clipIndex].effects[e];
                    if (action == "reset") {
                        edited.parameterValues.clear();
                        for (const auto& param : spec.parameters) edited.parameterValues.append(param.defaultValue);
                    } else if (action.startsWith("load:")) {
                        edited.parameterValues = QSettings().value(settingsKey + '/' + action.mid(5)).toStringList();
                    }
                    edited.animation.clear();
                }, false, true);
            }
            if (presetGuard) presetGuard->setCurrentIndex(0);
        });

        if (effect.pluginId.value() == QLatin1String("openvegas.builtin.color-wheels")
            && spec.parameters.size() >= 12) {
            auto* wheelItem = new QTreeWidgetItem(effectItem, {tr("Color Wheels")});
            wheelItem->setFirstColumnSpanned(true);
            auto* wheelPanel = new QWidget(m_tree);
            wheelPanel->setObjectName("controlsColorWheels");
            auto* grid = new QGridLayout(wheelPanel);
            grid->setContentsMargins(18, 3, 3, 4); grid->setSpacing(4);
            const struct Wheel { const char* label; int hue; int saturation; int color; } wheels[] = {
                {QT_TR_NOOP("Master"), -1, -1, 2}, {QT_TR_NOOP("Highlights"), 4, 5, -1},
                {QT_TR_NOOP("Midtones"), 7, 8, -1}, {QT_TR_NOOP("Shadows"), 10, 11, -1},
            };
            for (int index = 0; index < 4; ++index) {
                const auto wheel = wheels[index];
                auto* label = new QLabel(tr(wheel.label), wheelPanel); label->setAlignment(Qt::AlignCenter);
                auto* editor = new ColorWheelEditor(wheelPanel, [this, e, wheel](const QColor& color) {
                    if (wheel.color >= 0) { applyParamValue(e, wheel.color, color.name()); return; }
                    editModel(tr("Edit color wheel"), QStringLiteral("color-wheel-%1-%2").arg(e).arg(wheel.hue),
                              [=](composition::Layer& layer) {
                        auto& effect = layer.clips[m_clipIndex].effects[e];
                        while (effect.parameterValues.size() <= wheel.saturation) effect.parameterValues.append(QString());
                        effect.parameterValues[wheel.hue] = QString::number(color.hsvHueF() * 360., 'f', 2);
                        effect.parameterValues[wheel.saturation] = QString::number(color.hsvSaturationF(), 'f', 3);
                    });
                });
                editor->setObjectName(QStringLiteral("controlsColorWheel_%1").arg(index));
                editor->setEnabled(!layer.locked);
                const auto update = [this, editor = QPointer<ColorWheelEditor>(editor), e, wheel] {
                    if (!editor) return; const auto* effect = effectAt(e); if (!effect) return;
                    QColor color;
                    if (wheel.color >= 0) color = QColor(effect->parameterAt(wheel.color, currentFrame()).toString());
                    else color = QColor::fromHsvF(std::fmod(effect->parameterAt(wheel.hue, currentFrame()).toDouble() / 360. + 1., 1.),
                                                  qBound(0., effect->parameterAt(wheel.saturation, currentFrame()).toDouble(), 1.), 1.);
                    editor->setColor(color);
                };
                m_valueReaders.append(update); update();
                grid->addWidget(label, 0, index); grid->addWidget(editor, 1, index);
            }
            m_tree->setItemWidget(wheelItem, 0, wheelPanel);
        }

        QHash<QString, QTreeWidgetItem*> groups;
        for (int p = 0; p < spec.parameters.size(); ++p) {
            plugin::EffectParameterSpec param = spec.parameters[p];
            if (param.type == "layer") {
                param.choices = {tr("None")};
                for (const auto& candidate : m_comp->layers()) param.choices.append(candidate.name);
            }
            QTreeWidgetItem* parent = effectItem;
            if (!param.group.isEmpty()) {
                if (!groups.contains(param.group)) {
                    groups[param.group] = new QTreeWidgetItem(effectItem, {param.group});
                    groups[param.group]->setExpanded(param.group == "Master Controls");
                }
                parent = groups[param.group];
            }
            auto* row = new QTreeWidgetItem(parent, {param.displayName});
            row->setData(0, Qt::UserRole, m_layerIndex); row->setData(0, Qt::UserRole + 1, m_clipIndex);
            row->setData(0, Qt::UserRole + 2, e); row->setData(0, Qt::UserRole + 3, p);
            auto* wrapper = new QWidget(m_tree); auto* layout = new QHBoxLayout(wrapper);
            layout->setContentsMargins(0, 0, 1, 0); layout->setSpacing(2);
            const QString object = QStringLiteral("controlsParam_%1_%2_%3_%4").arg(m_layerIndex).arg(m_clipIndex).arg(e).arg(p);
            auto read = [this, e, p, fallback = param.defaultValue]() -> QVariant {
                auto* effect = effectAt(e); if (!effect) return fallback;
                const QVariant value = effect->parameterAt(p, currentFrame());
                return value.isValid() ? value : QVariant(fallback);
            };
            const QString type = param.type.toLower();
            if ((type == "int" || type == "double" || type == "float" || type == "angle")
                && std::isfinite(param.minimum) && std::isfinite(param.maximum)
                && param.maximum > param.minimum && param.maximum - param.minimum < 1000000.) {
                auto* slider = new QSlider(Qt::Horizontal, wrapper);
                slider->setObjectName(QStringLiteral("controlsSlider_%1_%2").arg(e).arg(p));
                slider->setRange(0, 1000); slider->setMinimumWidth(50);
                const auto update = [slider = QPointer<QSlider>(slider), read, param] {
                    if (!slider) return; const QSignalBlocker block(slider);
                    slider->setValue(qRound((read().toDouble() - param.minimum) * 1000. / (param.maximum - param.minimum)));
                };
                m_valueReaders.append(update); update();
                connect(slider, &QSlider::valueChanged, this, [this, e, p, param, type](int position) {
                    const double value = param.minimum + position * (param.maximum - param.minimum) / 1000.;
                    applyParamValue(e, p, type == "int" ? QVariant(qRound(value)) : QVariant(value));
                });
                slider->setEnabled(!layer.locked && spec.renderable); layout->addWidget(slider, 2);
            }
            auto* editor = timelineParameterEditor(wrapper, param, object, read,
                [this, e, p](QVariant value) { applyParamValue(e, p, value); }, m_valueReaders);
            editor->setEnabled(!layer.locked && spec.renderable);
            editor->setMaximumWidth(145);
            layout->addWidget(editor, 1);
            if (param.type != "label" && param.type != "button") {
                auto* key = smallButton(wrapper, QStringLiteral("controlsParamKey_%1_%2").arg(e).arg(p),
                                        ":/icons/key-frame-off.svg", tr("Add or remove a keyframe at the playhead"));
                key->setCheckable(true); key->setEnabled(!layer.locked);
                auto update = [this, key = QPointer<QToolButton>(key), e, p] {
                    if (!key) return; const auto* effect = effectAt(e);
                    bool present = false;
                    if (effect) {
                        const auto it = effect->animation.constFind(p);
                        present = it != effect->animation.cend() && it->at(currentFrame());
                    }
                    const QSignalBlocker block(key); key->setChecked(present);
                    key->setIcon(QIcon(present ? ":/icons/key-frame-full.svg" : ":/icons/key-frame-off.svg"));
                };
                m_valueReaders.append(update); update();
                connect(key, &QToolButton::clicked, this, [this, e, p](bool on) { toggleParamKeyFrame(e, p, on); });
                layout->addWidget(key);
            }
            m_tree->setItemWidget(row, 1, wrapper);
        }
    }
}

void EffectInspector::buildTransformSection(const composition::Layer& layer, int frame)
{
    auto* root = new QTreeWidgetItem(m_tree, {layer.dimension == composition::LayerDimension::ThreeD
                                              ? tr("World Transform") : tr("Transform")});
    root->setExpanded(true);
    for (const auto prop : composition::transformPropertiesFor(layer.dimension)) {
        plugin::EffectParameterSpec spec; spec.type = "double"; spec.minimum = -100000; spec.maximum = 100000;
        spec.decimals = 1; spec.step = 1;
        if (prop == TransformProp::Opacity) { spec.minimum = 0; spec.maximum = 100; spec.unit = "%"; }
        else if (prop == TransformProp::Scale) spec.unit = "%";
        else if (prop == TransformProp::Orientation || prop == TransformProp::RotationX
                 || prop == TransformProp::RotationY || prop == TransformProp::Rotation) {
            spec.minimum = -36000; spec.maximum = 36000; spec.unit = QString(QChar(0x00b0));
        }
        const QString label = QCoreApplication::translate("Transform", composition::transformPropertyName(prop, layer.dimension));
        auto* row = new QTreeWidgetItem(root, {label});
        row->setData(0, Qt::UserRole, m_layerIndex); row->setData(0, Qt::UserRole + 4, int(prop));
        auto* wrapper = new QWidget(m_tree); auto* layout = new QHBoxLayout(wrapper);
        layout->setContentsMargins(0, 0, 1, 0); layout->setSpacing(2);
        if (prop == TransformProp::Opacity) {
            auto* slider = new QSlider(Qt::Horizontal, wrapper);
            slider->setObjectName("controlsTransformOpacitySlider"); slider->setRange(0, 1000);
            const auto update = [this, slider = QPointer<QSlider>(slider)] {
                if (!slider) return; const auto* selected = layerRef(); if (!selected) return;
                const QSignalBlocker block(slider);
                slider->setValue(qRound(transformValue(*selected, TransformProp::Opacity, 0, currentFrame()) * 10.));
            };
            m_valueReaders.append(update); update();
            connect(slider, &QSlider::valueChanged, this,
                    [this](int value) { applyTransformValue(TransformProp::Opacity, 0, value / 10.); });
            slider->setEnabled(!layer.locked); layout->addWidget(slider, 2);
        }
        for (int axis = 0; axis < composition::axisCount(prop, layer.dimension); ++axis) {
            const QString object = QStringLiteral("controlsTransform_%1_%2_%3").arg(m_layerIndex).arg(int(prop)).arg(axis);
            auto read = [this, prop, axis]() -> QVariant {
                const auto* selected = layerRef(); return selected ? transformValue(*selected, prop, axis, currentFrame()) : 0.;
            };
            auto* editor = timelineParameterEditor(wrapper, spec, object, read,
                [this, prop, axis](QVariant value) { applyTransformValue(prop, axis, value.toDouble()); }, m_valueReaders);
            editor->setEnabled(!layer.locked); editor->setMaximumWidth(145); layout->addWidget(editor, 1);
        }
        if (prop == TransformProp::Scale) {
            auto* link = smallButton(wrapper, "toolButtonLinked", ":/icons/link.svg", tr("Link scale axes"));
            link->setCheckable(true); link->setChecked(m_scaleLinked); link->setEnabled(!layer.locked);
            connect(link, &QToolButton::toggled, this, [this](bool on) { m_scaleLinked = on; });
            layout->addWidget(link);
        }
        if (layer.transform.curve(prop, 0)) {
            auto* key = smallButton(wrapper, QStringLiteral("controlsTransformKey_%1").arg(int(prop)),
                                    ":/icons/key-frame-off.svg", tr("Add or remove a keyframe at the playhead"));
            key->setCheckable(true); key->setEnabled(!layer.locked);
            auto update = [this, key = QPointer<QToolButton>(key), prop] {
                if (!key) return; const auto* selected = layerRef(); bool present = false;
                if (selected) for (int axis = 0; axis < composition::axisCount(prop, selected->dimension); ++axis) {
                    const auto* curve = selected->transform.curve(prop, axis); present |= curve && curve->at(currentFrame());
                }
                const QSignalBlocker block(key); key->setChecked(present);
                key->setIcon(QIcon(present ? ":/icons/key-frame-full.svg" : ":/icons/key-frame-off.svg"));
            };
            m_valueReaders.append(update); update();
            connect(key, &QToolButton::clicked, this, [this, prop](bool on) { toggleTransformKeyFrame(prop, on); });
            layout->addWidget(key);
        }
        m_tree->setItemWidget(row, 1, wrapper);
    }
    Q_UNUSED(frame);
}

void EffectInspector::applySearch()
{
    if (!m_tree || !m_search) return;
    const QString filter = m_search->text().trimmed();
    std::function<bool(QTreeWidgetItem*)> visit = [&](QTreeWidgetItem* item) {
        const bool own = filter.isEmpty() || item->text(0).contains(filter, Qt::CaseInsensitive);
        bool child = false;
        for (int i = 0; i < item->childCount(); ++i) child |= visit(item->child(i));
        item->setHidden(!own && !child);
        if (!filter.isEmpty() && child) item->setExpanded(true);
        return own || child;
    };
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) visit(m_tree->topLevelItem(i));
}

void EffectInspector::refreshValues()
{
    for (const auto& reader : m_valueReaders) reader();
    updateHeader();
}

void EffectInspector::setCurrentTime(double seconds)
{
    if (qFuzzyCompare(m_time + 1., seconds + 1.)) return;
    m_time = seconds;
    if (m_followPlayheadClip && m_comp && m_layerIndex >= 0 && m_layerIndex < m_comp->layers().size()) {
        const auto& clips = m_comp->layers()[m_layerIndex].clips;
        int resolved = clips.isEmpty() ? -1 : 0;
        for (int i = 0; i < clips.size(); ++i)
            if (m_time >= clips[i].startSeconds && m_time < clips[i].endSeconds()) { resolved = i; break; }
        if (resolved != m_clipIndex) { m_clipIndex = resolved; refresh(); return; }
    }
    refreshValues();
}

int EffectInspector::currentFrame() const
{
    if (!m_comp) return 0;
    return qRound(m_time * double(m_comp->fpsNumerator()) / qMax(1, m_comp->fpsDenominator()));
}

composition::Layer* EffectInspector::layerRef()
{
    return m_comp && m_layerIndex >= 0 && m_layerIndex < m_comp->layers().size()
        ? &m_comp->layerRef(m_layerIndex) : nullptr;
}

composition::Effect* EffectInspector::effectAt(int effectIndex)
{
    auto* layer = layerRef();
    if (!layer || m_clipIndex < 0 || m_clipIndex >= layer->clips.size()
        || effectIndex < 0 || effectIndex >= layer->clips[m_clipIndex].effects.size()) return nullptr;
    return &layer->clips[m_clipIndex].effects[effectIndex];
}

QVariant EffectInspector::typedValue(int effectIndex, int paramIndex, const QVariant& raw) const
{
    if (!m_comp || !m_pluginManager || m_layerIndex < 0 || m_layerIndex >= m_comp->layers().size()) return raw;
    const auto& layer = m_comp->layers()[m_layerIndex];
    if (m_clipIndex < 0 || m_clipIndex >= layer.clips.size() || effectIndex < 0
        || effectIndex >= layer.clips[m_clipIndex].effects.size()) return raw;
    const auto spec = m_pluginManager->spec(layer.clips[m_clipIndex].effects[effectIndex].pluginId);
    if (paramIndex < 0 || paramIndex >= spec.parameters.size()) return raw;
    const QString type = spec.parameters[paramIndex].type.toLower();
    if (type == "int") return raw.toInt();
    if (type == "double" || type == "float") return raw.toDouble();
    if (type == "bool" || type == "checkbox" || type == "button") {
        const QString value = raw.toString();
        return value == "1" || value.compare("true", Qt::CaseInsensitive) == 0;
    }
    return raw.toString();
}

double EffectInspector::transformValue(const composition::Layer& layer, TransformProp prop, int axis, int frame)
{
    return layer.transform.valueAt(prop, axis, frame);
}

void EffectInspector::editModel(const QString& title, const QString& mergeKey,
                                const std::function<void(composition::Layer&)>& edit,
                                bool keyFrames, bool rebuild)
{
    const auto* layer = layerRef();
    if (!layer || layer->locked) return;
    const auto before = m_comp->layers();
    // Re-acquire through the non-const accessor after taking the implicitly
    // shared snapshot, so QVector detaches before the edit touches its storage.
    edit(m_comp->layerRef(m_layerIndex));
    const auto after = m_comp->layers();
    if (composition::layerState(before) == composition::layerState(after)) return;
    const QPointer<EffectInspector> self(this);
    if (m_undoStack) m_undoStack->push(new ControlsEditCommand(m_comp, before, after, title, mergeKey, [self] {
        if (!self) return; self->refresh(); emit self->effectParamsChanged(); emit self->keyFramesChanged();
    }));
    emit effectParamsChanged();
    if (keyFrames) emit keyFramesChanged();
    if (rebuild) {
        QTimer::singleShot(0, this, [self] { if (self) self->refresh(); });
    } else refreshValues();
}

void EffectInspector::applyParamValue(int effectIndex, int paramIndex, const QVariant& raw)
{
    const QVariant value = typedValue(effectIndex, paramIndex, raw);
    const bool animated = effectAt(effectIndex) && effectAt(effectIndex)->isAnimated(paramIndex);
    editModel(tr("Edit effect parameter"), QStringLiteral("parameter-%1-%2").arg(effectIndex).arg(paramIndex),
              [=](composition::Layer& layer) {
        auto& effect = layer.clips[m_clipIndex].effects[effectIndex];
        auto curve = effect.animation.find(paramIndex);
        if (curve != effect.animation.end() && !curve->isEmpty()) curve->set(currentFrame(), value);
        else {
            while (effect.parameterValues.size() <= paramIndex) effect.parameterValues.append(QString());
            effect.parameterValues[paramIndex] = value.toString();
        }
    }, animated);
}

void EffectInspector::toggleParamKeyFrame(int effectIndex, int paramIndex, bool on)
{
    const QVariant held = effectAt(effectIndex) ? effectAt(effectIndex)->parameterAt(paramIndex, currentFrame()) : QVariant();
    const QVariant typed = typedValue(effectIndex, paramIndex, held);
    editModel(tr("Toggle effect keyframe"), QStringLiteral("parameter-key-%1-%2").arg(effectIndex).arg(paramIndex),
              [=](composition::Layer& layer) {
        auto& effect = layer.clips[m_clipIndex].effects[effectIndex];
        auto& curve = effect.animation[paramIndex];
        if (on) { if (curve.isEmpty()) curve.setDefaultValue(typed); curve.set(currentFrame(), typed); }
        else {
            curve.removeAt(currentFrame());
            if (curve.isEmpty()) {
                effect.animation.remove(paramIndex);
                while (effect.parameterValues.size() <= paramIndex) effect.parameterValues.append(QString());
                effect.parameterValues[paramIndex] = typed.toString();
            }
        }
    }, true);
}

void EffectInspector::applyTransformValue(TransformProp prop, int axis, double value)
{
    editModel(tr("Edit transform"), QStringLiteral("transform-%1-%2").arg(int(prop)).arg(axis),
              [=](composition::Layer& layer) {
        const int axes = composition::axisCount(prop, layer.dimension);
        const double old = layer.transform.valueAt(prop, axis, currentFrame());
        for (int target = 0; target < axes; ++target) {
            if (target != axis && !(prop == TransformProp::Scale && m_scaleLinked)) continue;
            const double targetValue = target == axis ? value
                : (qFuzzyIsNull(old) ? value : layer.transform.valueAt(prop, target, currentFrame()) * value / old);
            auto* curve = layer.transform.curve(prop, target);
            if (curve && !curve->isEmpty()) curve->set(currentFrame(), targetValue);
            else composition::setLayerTransformValue(layer, prop, target, targetValue);
        }
    }, layerRef() && layerRef()->transform.isAnimated(prop, layerRef()->dimension));
}

void EffectInspector::toggleTransformKeyFrame(TransformProp prop, bool on)
{
    editModel(tr("Toggle transform keyframe"), QStringLiteral("transform-key-%1").arg(int(prop)),
              [=](composition::Layer& layer) {
        for (int axis = 0; axis < composition::axisCount(prop, layer.dimension); ++axis) {
            auto* curve = layer.transform.curve(prop, axis); if (!curve) continue;
            const double held = layer.transform.valueAt(prop, axis, currentFrame());
            if (on) { if (curve->isEmpty()) curve->setDefaultValue(held); curve->set(currentFrame(), held); }
            else {
                curve->removeAt(currentFrame());
                if (curve->isEmpty()) composition::setLayerTransformValue(layer, prop, axis, held);
            }
        }
    }, true);
}

bool EffectInspector::hasAnimatedParameters() const
{
    if (!m_comp || m_layerIndex < 0 || m_layerIndex >= m_comp->layers().size()) return false;
    const auto& layer = m_comp->layers()[m_layerIndex];
    if (m_clipIndex >= 0 && m_clipIndex < layer.clips.size())
        for (const auto& effect : layer.clips[m_clipIndex].effects) if (!effect.animation.isEmpty()) return true;
    for (const auto prop : composition::transformPropertiesFor(layer.dimension))
        if (layer.transform.isAnimated(prop, layer.dimension)) return true;
    return false;
}

} // namespace openvegas::ui
