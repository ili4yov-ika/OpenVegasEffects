#include "ui/TimelineWidget.h"
#include "composition/CompositionState.h"
#include "ui/TimelineParameterEditor.h"
#include "ui/TimelineValueGraphView.h"
#include "plugin/PluginManager.h"
#include <QDataStream>
#include <QDateTime>
#include <QTreeWidgetItemIterator>
#include <QTimer>
#include <QUndoStack>
#include <QMenu>
#include <QPolygonF>
#include <QSettings>
#include <QScrollBar>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>

namespace openvegas::ui {
namespace {
using namespace composition;
struct CompositionSettings {
    QString name;
    int width, height, numerator, denominator;
    double duration;
    explicit CompositionSettings(const Composition& c)
        : name(c.name()), width(c.width()), height(c.height()), numerator(c.fpsNumerator()),
          denominator(c.fpsDenominator()), duration(c.durationSeconds()) {}
    void apply(Composition& c) const {
        c.setName(name); c.setSize(width, height); c.setFrameRate(numerator, denominator); c.setDurationSeconds(duration);
    }
};
class CompositionSettingsEdit : public QUndoCommand {
public:
    CompositionSettingsEdit(std::shared_ptr<Composition> comp, CompositionSettings before,
                            CompositionSettings after, std::function<void()> notify)
        : QUndoCommand(QObject::tr("Composition properties")), m_comp(std::move(comp)),
          m_before(before), m_after(after), m_notify(std::move(notify)) {}
    void undo() override { m_before.apply(*m_comp); m_notify(); }
    void redo() override { m_after.apply(*m_comp); m_notify(); }
private:
    std::shared_ptr<Composition> m_comp;
    CompositionSettings m_before, m_after;
    std::function<void()> m_notify;
};
class TimelineEdit : public QUndoCommand {
public:
    TimelineEdit(std::shared_ptr<Composition> comp, QVector<Layer> before, QVector<Layer> after,
                 QString title, std::function<void()> notify)
        : QUndoCommand(title), m_comp(std::move(comp)), m_before(std::move(before)),
          m_after(std::move(after)), m_notify(std::move(notify)) {}
    void redo() override { if (m_first) { m_first = false; return; } apply(m_after); }
    void undo() override { apply(m_before); }
private:
    void apply(const QVector<Layer>& layers) {
        while (!m_comp->layers().isEmpty()) m_comp->removeLayer(m_comp->layers().size() - 1);
        for (const auto& layer : layers) m_comp->insertLayer(m_comp->layers().size(), layer);
        m_notify();
    }
    bool m_first = true;
    std::shared_ptr<Composition> m_comp;
    QVector<Layer> m_before, m_after;
    std::function<void()> m_notify;
};

}

void TimelineWidget::beginModelEdit()
{
    if (m_comp) { m_editBefore = m_comp->layers(); m_editPending = true; }
}
void TimelineWidget::finishModelEdit(const QString& title, bool rebuild)
{
    if (!m_comp || !m_editPending) return;
    m_editPending = false;
    const bool changed = composition::layerState(m_editBefore) != composition::layerState(m_comp->layers());
    if (changed && m_undoStack) {
        QPointer<TimelineWidget> guard(this);
        m_undoStack->push(new TimelineEdit(m_comp, m_editBefore, m_comp->layers(), title, [guard] {
            if (!guard) return;
            guard->refreshKeyFrames(); emit guard->keyFramesChanged();
        }));
    }
    m_editBefore.clear();
    if (!changed) return;
    if (rebuild) scheduleRefresh(); else refreshValues();
    m_canvas->update(); m_valueGraphView->update();
    emit keyFramesChanged();
}
void TimelineWidget::editLayer(int index, const QString& title,
    const std::function<void(composition::Layer&)>& edit, bool rebuild)
{
    if (m_rebuilding || !m_comp || index < 0 || index >= m_comp->layers().size()) return;
    beginModelEdit(); edit(m_comp->layerRef(index)); finishModelEdit(title, rebuild);
}
void TimelineWidget::scheduleRefresh()
{
    if (m_refreshPending) return;
    m_refreshPending = true;
    QTimer::singleShot(0, this, [this] { m_refreshPending = false; rebuildTree(); });
}
void TimelineWidget::refreshValues()
{
    if (m_rebuilding) return;
    for (const auto& read : m_valueReaders) read();
    updateKeyButtons();
}
bool TimelineWidget::eventFilter(QObject* object, QEvent* event)
{
    if (object == m_canvas && event->type() == QEvent::Resize) {
        // The splitter settles after the outer page stack has been laid out.
        // Re-align the key controls whenever its track page changes width.
        QTimer::singleShot(0, this, &TimelineWidget::syncHeaderToTree);
    }
    if (object == m_valueGraphView && event->type() == QEvent::MouseButtonPress) beginModelEdit();
    // Single-letter editor shortcuts must leave text/numeric entry intact.
    if (event->type() == QEvent::ShortcutOverride) {
        auto* focus = QApplication::focusWidget();
        if (focus && isAncestorOf(focus) && (qobject_cast<QLineEdit*>(focus) || qobject_cast<QAbstractSpinBox*>(focus))) {
            auto* key = static_cast<QKeyEvent*>(event);
            if (key->modifiers() == Qt::NoModifier || key->modifiers() == Qt::ShiftModifier) {
                event->accept(); return true;
            }
        }
    }
    return QDockWidget::eventFilter(object, event);
}
void TimelineWidget::applySearch()
{
    if (!m_tree) return;
    const QString filter = m_search->text().trimmed();
    std::function<bool(QTreeWidgetItem*, bool)> visit = [&](QTreeWidgetItem* item, bool ancestor) {
        const bool match = ancestor || filter.isEmpty() || item->text(0).contains(filter, Qt::CaseInsensitive);
        bool childMatch = false;
        for (int i = 0; i < item->childCount(); ++i) childMatch = visit(item->child(i), match) || childMatch;
        item->setHidden(!match && !childMatch);
        if (!filter.isEmpty() && childMatch) item->setExpanded(true);
        return match || childMatch;
    };
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) visit(m_tree->topLevelItem(i), false);
    syncLanes();
}

void TimelineWidget::buildParameterEditor(QTreeWidgetItem* row, int layerIndex, int clipIndex, int effectIndex, int p)
{
    const auto& effect = m_comp->layers()[layerIndex].clips[clipIndex].effects[effectIndex];
    const auto spec = m_pluginManager ? m_pluginManager->spec(effect.pluginId) : plugin::EffectSpec{};
    plugin::EffectParameterSpec param = spec.parameters.value(p);
    if (param.name.isEmpty()) {
        param.name = QString::number(p); param.displayName = row->text(0);
        bool numeric = false; effect.parameterValues.value(p).toDouble(&numeric);
        param.type = numeric ? "double" : "string";
    }
    if (param.type == "layer") {
        param.choices = {QString()};
        param.choiceValues = {QStringLiteral("00000000-0000-0000-0000-000000000000")};
        for (const auto& layer : m_comp->layers()) {
            param.choices.append(layer.name);
            param.choiceValues.append(layer.id.value());
        }
    }
    // Text's original enum strings remain its serialization vocabulary.
    if (effect.pluginId.value() == "text") {
        if (param.name == "strokeOrder") param.choices = {"Over Fill", "Under Fill", "Centered"};
        if (param.name == "caps") param.choices = {"None", "All Caps", "Small Caps", "Start Case", "Lower Case"};
        if (param.name == "script") param.choices = {"None", "Superscript", "Subscript"};
        if (param.name == "alignH") param.choices = {"Left", "Center", "Right", "Left Justify", "Center Justify", "Right Justify", "Justify"};
        if (param.name == "alignV") param.choices = {"Top", "Middle", "Bottom"};
    }
    const auto read = [this, layerIndex, clipIndex, effectIndex, p, param]() -> QVariant {
        auto* clip = m_comp ? m_comp->clipAt(layerIndex, clipIndex) : nullptr;
        if (!clip || effectIndex >= clip->effects.size()) return {};
        const auto value = clip->effects[effectIndex].parameterAt(p, currentFrame());
        return value.isValid() ? value : QVariant(param.defaultValue);
    };
    const int generation = m_treeGeneration;
    const auto write = [this, row, layerIndex, clipIndex, effectIndex, p, param, read, generation](QVariant value) {
        if (m_rebuilding || generation != m_treeGeneration) return;
        if (read().toString() == value.toString() || m_comp->layers()[layerIndex].locked) return;
        m_tree->setCurrentItem(row);
        editLayer(layerIndex, tr("Set %1").arg(param.displayName), [=](composition::Layer& layer) {
            auto& effect = layer.clips[clipIndex].effects[effectIndex];
            if (effect.isAnimated(p)) {
                auto& curve = effect.animation[p];
                if (auto* key = curve.keyAt(currentFrame())) key->value = value;
                else curve.set(currentFrame(), value);
            } else {
                if (effect.parameterValues.size() <= p) effect.parameterValues.resize(p + 1);
                effect.parameterValues[p] = value.toString();
            }
        });
    };
    auto* editor = timelineParameterEditor(m_tree, param,
        QStringLiteral("timelineParam_%1_%2_%3_%4").arg(layerIndex).arg(clipIndex).arg(effectIndex).arg(p), read, write, m_valueReaders);
    editor->setEnabled(!m_comp->layers()[layerIndex].locked && spec.renderable);
    m_tree->setItemWidget(row, 1, editor);
}

void TimelineWidget::buildTransformRows(QTreeWidgetItem* parent, int index)
{
    const auto& layer = m_comp->layers()[index];
    for (auto prop : composition::transformPropertiesFor(layer.dimension)) {
        auto* row = new QTreeWidgetItem(parent, {QCoreApplication::translate("Transform", composition::transformPropertyName(prop, layer.dimension))});
        row->setData(0, Qt::UserRole, index);
        row->setData(0, Qt::UserRole + 4, int(prop));
        if (layer.transform.curve(prop, 0)) row->setIcon(0, QIcon(":/icons/key-frame-off.svg"));
        auto* axes = new QWidget(m_tree);
        auto* layout = new QHBoxLayout(axes); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(2);
        for (int axis = 0; axis < composition::axisCount(prop, layer.dimension); ++axis) {
            plugin::EffectParameterSpec spec;
            spec.type = "double"; spec.decimals = 1; spec.displayName = row->text(0);
            if (prop == composition::TransformProperty::Opacity || prop == composition::TransformProperty::Scale) spec.unit = "%";
            if (prop == composition::TransformProperty::Opacity) { spec.minimum = 0; spec.maximum = 100; }
            auto read = [this, index, prop, axis]() -> QVariant {
                const auto& l = m_comp->layers()[index];
                return prop == composition::TransformProperty::Opacity ? l.transform.opacityAt(currentFrame(), l.opacity) * 100.
                    : l.transform.valueAt(prop, axis, currentFrame());
            };
            const int generation = m_treeGeneration;
            auto write = [this, row, index, prop, axis, read, generation](QVariant value) {
                if (m_rebuilding || generation != m_treeGeneration) return;
                if (read().toDouble() == value.toDouble() || m_comp->layers()[index].locked) return;
                m_tree->setCurrentItem(row);
                editLayer(index, tr("Set %1").arg(row->text(0)), [=](composition::Layer& l) {
                    auto* curve = l.transform.curve(prop, axis);
                    if (curve && !curve->isEmpty()) curve->set(currentFrame(), value.toDouble());
                    else composition::setLayerTransformValue(l, prop, axis, value.toDouble());
                });
            };
            layout->addWidget(timelineParameterEditor(axes, spec,
                QStringLiteral("timelineTransform_%1_%2_%3").arg(index).arg(int(prop)).arg(axis), read, write, m_valueReaders), 1);
        }
        axes->setEnabled(!layer.locked);
        m_tree->setItemWidget(row, 1, axes);
    }
}

QVector<composition::KeyFrameList*> TimelineWidget::selectedCurves()
{
    QVector<composition::KeyFrameList*> curves;
    if (!m_comp || !m_tree->currentItem()) return curves;
    auto* row = m_tree->currentItem();
    if (!row->data(0, Qt::UserRole).isValid()) return curves;
    const int index = row->data(0, Qt::UserRole).toInt();
    if (index < 0 || index >= m_comp->layers().size() || m_comp->layers()[index].locked) return curves;
    if (row->data(0, Qt::UserRole + 4).isValid()) {
        auto& layer = m_comp->layerRef(index);
        const auto prop = composition::TransformProperty(row->data(0, Qt::UserRole + 4).toInt());
        for (int axis = 0; axis < composition::axisCount(prop, layer.dimension); ++axis)
            if (auto* c = layer.transform.curve(prop, axis)) curves.append(c);
    } else if (auto* curve = selectedCurve()) curves.append(curve);
    return curves;
}
void TimelineWidget::updateKeyButtons()
{
    if (!m_toggleKeyButton || !m_tree) return;
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
        auto* item = *it;
        if (item->icon(0).isNull()) continue;
        const int l = item->data(0, Qt::UserRole).toInt();
        if (!m_comp || l < 0 || l >= m_comp->layers().size()) continue;
        const auto& layer = m_comp->layers()[l];
        bool animated = false;
        if (item->data(0, Qt::UserRole + 4).isValid()) animated = layer.transform.isAnimated(
            composition::TransformProperty(item->data(0, Qt::UserRole + 4).toInt()), layer.dimension);
        else if (item->data(0, Qt::UserRole + 3).isValid()) animated = layer.clips[item->data(0, Qt::UserRole + 1).toInt()]
            .effects[item->data(0, Qt::UserRole + 2).toInt()].isAnimated(item->data(0, Qt::UserRole + 3).toInt());
        item->setIcon(0, QIcon(animated ? ":/icons/key-frame-full.svg" : ":/icons/key-frame-off.svg"));
    }
    const auto curves = selectedCurves();
    const composition::KeyFrame* key = nullptr;
    for (auto* curve : curves) if ((key = curve->at(currentFrame()))) break;
    auto* row = m_tree->currentItem();
    const bool parameter = row && (row->data(0, Qt::UserRole + 3).isValid() || !curves.isEmpty());
    m_toggleKeyButton->setEnabled(parameter && m_comp && !m_comp->layers()[row->data(0, Qt::UserRole).toInt()].locked);
    m_toggleKeyButton->setCheckable(true);
    m_toggleKeyButton->setChecked(key != nullptr);
    m_toggleKeyButton->setIcon(QIcon(key ? ":/icons/key-frame-full.svg"
                                     : ":/icons/key-frame-off.svg"));
    for (auto* button : m_keyTypeButtons) {
        button->setEnabled(key != nullptr);
        button->setChecked(key && button->property("temporalType").toInt() == int(key->temporal));
    }
}

void TimelineWidget::showEffectMenu(int layer, bool behaviors, QWidget* anchor)
{
    if (!m_comp || !m_pluginManager || layer < 0 || layer >= m_comp->layers().size()) return;
    const auto* selected = m_tree->currentItem();
    const int clip = selected && selected->data(0, Qt::UserRole).toInt() == layer
        && selected->data(0, Qt::UserRole + 1).isValid()
        ? selected->data(0, Qt::UserRole + 1).toInt() : 0;
    QMenu menu(this);
    auto ids = behaviors
        ? m_pluginManager->pluginIdsByKind(plugin::PluginKind::BehaviorEffect)
        : m_pluginManager->allPluginIds();
    const auto behaviorIds = m_pluginManager->pluginIdsByKind(plugin::PluginKind::BehaviorEffect);
    std::sort(ids.begin(), ids.end(), [this](const auto& a, const auto& b) {
        return m_pluginManager->spec(a).displayName < m_pluginManager->spec(b).displayName;
    });
    for (const auto& id : ids) {
        const auto spec = m_pluginManager->spec(id);
        if (id.value() == "text" || (!behaviors && behaviorIds.contains(id))) continue;
        auto* action = menu.addAction(spec.displayName);
        action->setEnabled(spec.renderable && !m_comp->layers()[layer].locked && !m_comp->layers()[layer].clips.isEmpty());
        action->setToolTip(spec.unavailableReason);
        connect(action, &QAction::triggered, this, [this, layer, clip, spec] {
            editLayer(layer, tr("Add %1").arg(spec.displayName), [spec, clip](composition::Layer& l) {
                if (clip < 0 || clip >= l.clips.size()) return;
                composition::Effect e; e.pluginId = spec.id; e.name = spec.displayName;
                for (const auto& param : spec.parameters) e.parameterValues.append(param.defaultValue);
                l.clips[clip].effects.append(e);
            }, true);
        });
    }
    if (menu.isEmpty()) { auto* item = menu.addAction(tr("No compatible effects installed")); item->setEnabled(false); }
    menu.setToolTipsVisible(true);
    menu.exec(anchor->mapToGlobal(QPoint(0, anchor->height())));
}

void TimelineWidget::editCompositionProperties()
{
    if (!m_comp) return;
    QDialog dialog(this); dialog.setWindowTitle(tr("Composite Shot Properties"));
    dialog.setObjectName("timelineCompositionProperties");
    auto* form = new QFormLayout(&dialog);
    auto* name = new QLineEdit(m_comp->name(), &dialog); name->setObjectName("compositionName");
    form->addRow(tr("Name"), name);
    const auto integer = [&](const QString& label, const char* object, int value, int maximum) {
        auto* editor = new QSpinBox(&dialog); editor->setObjectName(object);
        editor->setRange(1, maximum); editor->setValue(value); form->addRow(label, editor); return editor;
    };
    auto* width = integer(tr("Width (px)"), "compositionWidth", m_comp->width(), 16384);
    auto* height = integer(tr("Height (px)"), "compositionHeight", m_comp->height(), 16384);
    auto* numerator = integer(tr("Frame rate numerator"), "compositionFpsNumerator", m_comp->fpsNumerator(), 240000);
    auto* denominator = integer(tr("Frame rate denominator"), "compositionFpsDenominator", m_comp->fpsDenominator(), 10000);
    auto* duration = new QDoubleSpinBox(&dialog); duration->setObjectName("compositionDuration");
    duration->setDecimals(3); duration->setRange(.001, 86400); duration->setValue(m_comp->durationSeconds());
    form->addRow(tr("Duration (seconds)"), duration);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;
    CompositionSettings before(*m_comp), after(before);
    after.name = name->text().trimmed().isEmpty() ? before.name : name->text().trimmed();
    after.width = width->value(); after.height = height->value(); after.duration = duration->value();
    after.numerator = numerator->value(); after.denominator = denominator->value();
    if (after.name == before.name && after.width == before.width && after.height == before.height
        && after.duration == before.duration && after.numerator == before.numerator && after.denominator == before.denominator) return;
    const QPointer<TimelineWidget> self(this);
    auto notify = [self] {
        if (!self) return;
        self->m_canvas->setComposition(self->m_comp); self->m_valueGraphView->setComposition(self->m_comp);
        self->setPlayheadPosition(qMin(self->playhead(), self->m_comp->durationSeconds()));
        self->refreshValues();
        emit self->compositionPropertiesChanged();
    };
    if (m_undoStack) m_undoStack->push(new CompositionSettingsEdit(m_comp, before, after, notify));
    else { after.apply(*m_comp); notify(); }
}

void TimelineWidget::setCompositionDuration(double seconds)
{
    if (!m_comp || !qIsFinite(seconds)) {
        return;
    }
    const double fps = m_comp->fpsDenominator() > 0
                           ? double(m_comp->fpsNumerator()) / m_comp->fpsDenominator()
                           : 30.0;
    const double duration = qMax(fps > 0.0 ? 1.0 / fps : 0.001, seconds);
    CompositionSettings before(*m_comp), after(before);
    if (qFuzzyCompare(before.duration + 1.0, duration + 1.0)) {
        return;
    }
    after.duration = duration;
    const QPointer<TimelineWidget> self(this);
    auto notify = [self] {
        if (!self) return;
        self->m_canvas->setComposition(self->m_comp);
        self->m_valueGraphView->setComposition(self->m_comp);
        self->setPlayheadPosition(qMin(self->playhead(), self->m_comp->durationSeconds()));
        self->refreshValues();
        emit self->compositionPropertiesChanged();
    };
    if (m_undoStack) {
        m_undoStack->push(new CompositionSettingsEdit(m_comp, before, after, notify));
    } else {
        after.apply(*m_comp);
        notify();
    }
}

void TimelineWidget::selectAllRows()
{
    if (!m_tree) {
        return;
    }
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->selectAll();
}

void TimelineWidget::addMaskToLayer(int layerIndex, composition::MaskShape shape,
                                    const QRectF& bounds)
{
    if (bounds.isEmpty()) return;
    editLayer(layerIndex, tr("Add mask"), [shape, bounds](composition::Layer& layer) {
        composition::LayerMask mask;
        mask.name = QObject::tr("Mask %1").arg(layer.masks.size() + 1);
        mask.shape = shape;
        mask.bounds = bounds.normalized();
        layer.masks.append(mask);
    }, true);
}

void TimelineWidget::addFreehandMaskToLayer(int layerIndex, const QVector<QPointF>& points)
{
    if (points.size() < 3) return;
    editLayer(layerIndex, tr("Add freehand mask"), [points](composition::Layer& layer) {
        composition::LayerMask mask;
        mask.name = QObject::tr("Mask %1").arg(layer.masks.size() + 1);
        mask.shape = composition::MaskShape::Freehand;
        mask.points = points;
        mask.bounds = QPolygonF(points).boundingRect();
        layer.masks.append(mask);
    }, true);
}

void TimelineWidget::setMotionTrackData(int layerIndex, int trackIndex,
                                        const composition::KeyFrameList& xCurve,
                                        const composition::KeyFrameList& yCurve)
{
    editLayer(layerIndex, tr("Analyze motion track"), [=](composition::Layer& layer) {
        if (trackIndex < 0 || trackIndex >= layer.motionTracks.size()) return;
        layer.motionTracks[trackIndex].xCurve = xCurve;
        layer.motionTracks[trackIndex].yCurve = yCurve;
    }, true);
}
}
