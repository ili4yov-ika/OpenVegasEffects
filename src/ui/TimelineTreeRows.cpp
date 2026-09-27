#include "ui/Theme.h"
#include "ui/TimelineWidget.h"
#include "plugin/PluginManager.h"
#include "media/MediaManager.h"
#include <QCheckBox>
#include <QCoreApplication>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QScrollBar>
#include <QSettings>
#include <QSpinBox>
#include <QSignalBlocker>
#include <QToolButton>
#include <QTreeWidgetItemIterator>

namespace openvegas::ui {
namespace {
QString itemPath(QTreeWidgetItem* item) {
    QStringList path;
    while (item) {
        path.prepend(QString::number(item->parent() ? item->parent()->indexOfChild(item) : item->treeWidget()->indexOfTopLevelItem(item)));
        item = item->parent();
    }
    return path.join('/');
}
QToolButton* iconButton(QWidget* parent, const QString& name, const QString& icon, const QString& tip) {
    auto* b = new QToolButton(parent); b->setObjectName(name);
    if (!icon.isEmpty()) b->setIcon(QIcon(":/icons/" + icon + ".svg")); b->setIconSize(QSize(14, 14));
    b->setFixedSize(20, 20); b->setAutoRaise(true); b->setToolTip(tip); b->setAccessibleName(tip);
    return b;
}
}
void TimelineWidget::rebuildTree()
{
    if (!m_tree || m_rebuilding) return;
    m_rebuilding = true;
    const int generation = ++m_treeGeneration;
    const QSignalBlocker blocker(m_tree);
    QHash<QString, bool> expanded;
    const QString selection = m_tree->currentItem() ? itemPath(m_tree->currentItem()) : QString();
    const int scroll = m_tree->verticalScrollBar()->value();
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it) expanded.insert(itemPath(*it), (*it)->isExpanded());
    m_valueReaders.clear(); m_tree->clear();
    if (!m_comp) { m_rebuilding = false; syncLanes(); return; }
    for (int i = 0; i < m_comp->layers().size(); ++i) {
        const auto& layer = m_comp->layers()[i];
        auto* item = new QTreeWidgetItem(m_tree, {QStringLiteral("%1. %2 [%3]").arg(i + 1).arg(layer.name,
            QCoreApplication::translate("LayerKind", composition::layerKindName(layer.kind)))});
        item->setData(0, Qt::UserRole, i);
        item->setFirstColumnSpanned(true);
        auto* header = new QWidget(m_tree);
        header->setObjectName("timelineLayerHeader");
        header->setStyleSheet("QWidget#timelineLayerHeader { background:#222222; }");
        auto* line = new QHBoxLayout(header); line->setContentsMargins(0, 0, 0, 0); line->setSpacing(3);
        auto* lock = iconButton(header, QStringLiteral("timelineLock_%1").arg(i), layer.locked ? "lock-on" : "lock-off", tr("Lock layer"));
        lock->setCheckable(true); lock->setChecked(layer.locked); line->addWidget(lock);
        connect(lock, &QToolButton::clicked, this, [this, i](bool on) {
            editLayer(i, tr("Lock layer"), [on](composition::Layer& l) { l.locked = on; }, true);
        });
        auto* eye = iconButton(header, QStringLiteral("timelineVisible_%1").arg(i), layer.visible ? "eye-on" : "eye-off", tr("Layer visibility"));
        eye->setStyleSheet("QToolButton:checked { background:transparent; }");
        eye->setCheckable(true); eye->setChecked(layer.visible); eye->setEnabled(!layer.locked); line->addWidget(eye);
        connect(eye, &QToolButton::clicked, this, [this, i, eye](bool on) {
            editLayer(i, tr("Layer visibility"), [on](composition::Layer& l) { l.visible = on; });
            eye->setIcon(QIcon(on ? ":/icons/eye-on.svg" : ":/icons/eye-off.svg"));
        });
        auto* color = iconButton(header, QStringLiteral("timelineLabelColor_%1").arg(i), QString(), tr("Layer label color"));
        color->setFixedSize(12, 12); color->setStyleSheet("background:" + layer.labelColor.name()); line->addWidget(color);
        color->setEnabled(!layer.locked);
        connect(color, &QToolButton::clicked, this, [this, i, color] {
            QMenu menu(this);
            const char* names[] = {"Red", "Orange", "Yellow", "Green", "Cyan", "Blue", "Purple", "Pink"};
            const char* colors[] = {"#c94b4b", "#d9823b", "#d1b849", "#55a868", "#4aa6a6", "#4f78b8", "#8662b0", "#b95f8a"};
            const QSettings settings;
            for (int n = 0; n < 8; ++n) {
                const QString prefix = QStringLiteral("Options/Labels/%1/").arg(n + 1);
                auto* action = menu.addAction(settings.value(prefix + "Name",
                    QCoreApplication::translate("openvegas::ui::OptionsDialog", names[n])).toString());
                const QColor labelColor(settings.value(prefix + "Color", colors[n]).toString());
                action->setData(labelColor);
                QPixmap swatch(12, 12); swatch.fill(labelColor); action->setIcon(QIcon(swatch));
            }
            menu.addSeparator();
            auto* custom = menu.addAction(tr("Custom color…"));
            const auto* chosen = menu.exec(color->mapToGlobal(QPoint(0, color->height())));
            if (!chosen) return;
            const QColor c = chosen == custom
                ? interfaceColor(m_comp->layers()[i].labelColor, this, tr("Layer label color"))
                : chosen->data().value<QColor>();
            if (c.isValid()) editLayer(i, tr("Layer label color"), [c](composition::Layer& l) { l.labelColor = c; });
        });
        auto* name = new QLineEdit(layer.name, header);
        name->setObjectName(QStringLiteral("timelineLayerName_%1").arg(i)); name->setMinimumWidth(30);
        name->setToolTip(item->text(0)); name->setEnabled(!layer.locked); line->addWidget(name, 1);
        connect(name, &QLineEdit::selectionChanged, this, [this, item, generation] {
            if (!m_rebuilding && generation == m_treeGeneration) m_tree->setCurrentItem(item);
        });
        connect(name, &QLineEdit::editingFinished, this, [this, i, name] {
            if (name->text().trimmed().isEmpty()) { name->setText(m_comp->layers()[i].name); return; }
            editLayer(i, tr("Rename layer"), [name](composition::Layer& l) { l.name = name->text(); });
        });
        auto* dimension = iconButton(header, QStringLiteral("timelineDimension_%1").arg(i), "effect-2d", tr("Layer dimension: 2D / 3D"));
        dimension->setCheckable(true); dimension->setChecked(layer.dimension == composition::LayerDimension::ThreeD);
        dimension->setEnabled(!layer.locked && layer.kind != composition::LayerKind::Model3D && layer.kind != composition::LayerKind::Camera && layer.kind != composition::LayerKind::Light);
        connect(dimension, &QToolButton::clicked, this, [this, i](bool on) { editLayer(i, tr("Layer dimension"), [on](composition::Layer& l) {
            l.dimension = on ? composition::LayerDimension::ThreeD : composition::LayerDimension::TwoD;
        }, true); });
        line->addWidget(dimension);
        auto* blend = new QComboBox(header); blend->setObjectName(QStringLiteral("timelineBlend_%1").arg(i));
        blend->addItems(composition::blendModeNames()); blend->setCurrentText(layer.blendMode);
        blend->setFixedWidth(112); blend->setEnabled(!layer.locked); line->addWidget(blend);
        connect(blend, &QComboBox::currentTextChanged, this, [this, i](const QString& mode) {
            editLayer(i, tr("Layer blend mode"), [mode](composition::Layer& l) { l.blendMode = mode; });
        });
        m_tree->setItemWidget(item, 0, header);
        const auto group = [this, item, i](const QString& text, const QString& name, bool plus) {
            auto* row = new QTreeWidgetItem(item, {text}); row->setData(0, Qt::UserRole, i);
            if (plus) {
                auto* wrapper = new QWidget(m_tree); auto* layout = new QHBoxLayout(wrapper);
                layout->setContentsMargins(0, 0, 0, 0); layout->addStretch();
                auto* add = iconButton(wrapper, name + QString::number(i), "add", tr("Add %1").arg(text));
                add->setEnabled(!m_comp->layers()[i].locked); layout->addWidget(add);
                m_tree->setItemWidget(row, 1, wrapper);
                if (name == "timelineAddTrack_") {
                    connect(add, &QToolButton::clicked, this, [this, i] {
                        QSize frameSize(m_comp->width(), m_comp->height());
                        const auto& layer = m_comp->layers().at(i);
                        if (m_media && !layer.clips.isEmpty()) {
                            const QSize sourceSize = m_media->assetById(layer.clips.first().mediaId).frameSize();
                            if (sourceSize.isValid() && !sourceSize.isEmpty()) frameSize = sourceSize;
                        }
                        editLayer(i, tr("Add motion track"), [frameSize](composition::Layer& target) {
                            composition::MotionTrack track;
                            track.name = QObject::tr("Track %1").arg(target.motionTracks.size() + 1);
                            track.point = QPointF(frameSize.width() * .5, frameSize.height() * .5);
                            target.motionTracks.append(track);
                        }, true);
                    });
                } else if (name == "timelineAddMask_") {
                    connect(add, &QToolButton::clicked, this, [this, i] {
                        const QSizeF size(m_comp->width() * .5, m_comp->height() * .5);
                        addMaskToLayer(i, composition::MaskShape::Rectangle,
                            QRectF(QPointF((m_comp->width() - size.width()) * .5,
                                          (m_comp->height() - size.height()) * .5), size));
                    });
                } else connect(add, &QToolButton::clicked, this, [this, i, name, add] { showEffectMenu(i, name == "timelineAddBehavior_", add); });
            }
            return row;
        };
        auto* tracks = group(tr("Tracks"), "timelineAddTrack_", true);
        for (int t = 0; t < layer.motionTracks.size(); ++t) {
            const auto& track = layer.motionTracks.at(t);
            auto* trackRow = new QTreeWidgetItem(tracks, {track.name});
            trackRow->setData(0, Qt::UserRole, i);
            auto* controls = new QWidget(m_tree);
            auto* tl = new QHBoxLayout(controls); tl->setContentsMargins(0, 0, 0, 0); tl->setSpacing(3);
            auto* enabled = new QCheckBox(controls);
            enabled->setObjectName(QStringLiteral("timelineTrackEnabled_%1_%2").arg(i).arg(t));
            enabled->setChecked(track.enabled); enabled->setToolTip(tr("Enable track")); tl->addWidget(enabled);
            const auto coordinate = [controls, tl](const QString& name, double value, const QString& tip) {
                auto* spin = new QDoubleSpinBox(controls); spin->setObjectName(name);
                spin->setRange(-32768.0, 32768.0); spin->setDecimals(1); spin->setValue(value);
                spin->setToolTip(tip); spin->setFixedWidth(72); tl->addWidget(spin); return spin;
            };
            auto* x = coordinate(QStringLiteral("timelineTrackX_%1_%2").arg(i).arg(t), track.point.x(), tr("Tracking point X"));
            auto* y = coordinate(QStringLiteral("timelineTrackY_%1_%2").arg(i).arg(t), track.point.y(), tr("Tracking point Y"));
            auto* sample = new QSpinBox(controls); sample->setObjectName(QStringLiteral("timelineTrackSample_%1_%2").arg(i).arg(t));
            sample->setRange(2, 64); sample->setValue(track.sampleRadius); sample->setSuffix(tr(" px sample")); sample->setToolTip(tr("Sample radius")); tl->addWidget(sample);
            auto* search = new QSpinBox(controls); search->setObjectName(QStringLiteral("timelineTrackSearch_%1_%2").arg(i).arg(t));
            search->setRange(2, 256); search->setValue(track.searchRadius); search->setSuffix(tr(" px search")); search->setToolTip(tr("Search radius")); tl->addWidget(search);
            auto* analyze = iconButton(controls, QStringLiteral("timelineTrackAnalyze_%1_%2").arg(i).arg(t), "play", tr("Analyze forward"));
            analyze->setEnabled(!layer.locked && !layer.clips.isEmpty()); tl->addWidget(analyze);
            auto* remove = iconButton(controls, QStringLiteral("timelineTrackRemove_%1_%2").arg(i).arg(t), "close", tr("Remove track")); tl->addWidget(remove);
            m_tree->setItemWidget(trackRow, 1, controls);
            const auto editTrack = [this, i, t](const QString& title, const std::function<void(composition::MotionTrack&)>& fn, bool rebuild = false) {
                editLayer(i, title, [t, fn](composition::Layer& target) {
                    if (t >= 0 && t < target.motionTracks.size()) fn(target.motionTracks[t]);
                }, rebuild);
            };
            connect(enabled, &QCheckBox::toggled, this, [editTrack](bool value) { editTrack(QObject::tr("Enable track"), [value](composition::MotionTrack& target) { target.enabled = value; }); });
            connect(x, &QDoubleSpinBox::valueChanged, this, [editTrack](double value) { editTrack(QObject::tr("Set tracking point"), [value](composition::MotionTrack& target) { target.point.setX(value); target.xCurve.clear(); target.yCurve.clear(); }); });
            connect(y, &QDoubleSpinBox::valueChanged, this, [editTrack](double value) { editTrack(QObject::tr("Set tracking point"), [value](composition::MotionTrack& target) { target.point.setY(value); target.xCurve.clear(); target.yCurve.clear(); }); });
            connect(sample, &QSpinBox::valueChanged, this, [editTrack](int value) { editTrack(QObject::tr("Set sample radius"), [value](composition::MotionTrack& target) { target.sampleRadius = value; }); });
            connect(search, &QSpinBox::valueChanged, this, [editTrack](int value) { editTrack(QObject::tr("Set search radius"), [value](composition::MotionTrack& target) { target.searchRadius = value; }); });
            connect(analyze, &QToolButton::clicked, this, [this, i, t] { emit motionTrackingRequested(i, t); });
            connect(remove, &QToolButton::clicked, this, [this, i, t] { editLayer(i, tr("Remove motion track"), [t](composition::Layer& target) { if (t < target.motionTracks.size()) target.motionTracks.removeAt(t); }, true); });
            m_valueReaders.append([this, i, t, x, y] {
                if (!m_comp || i >= m_comp->layers().size()
                    || t >= m_comp->layers()[i].motionTracks.size()) return;
                const double fps = m_comp->fpsDenominator() > 0
                    ? double(m_comp->fpsNumerator()) / m_comp->fpsDenominator() : 30.0;
                const QPointF value = m_comp->layers()[i].motionTracks[t].pointAt(qRound(playhead() * fps));
                const QSignalBlocker bx(x), by(y);
                x->setValue(value.x()); y->setValue(value.y());
            });
        }
        tracks->setExpanded(!layer.motionTracks.isEmpty());
        auto* masks = group(tr("Masks"), "timelineAddMask_", true);
        for (int m = 0; m < layer.masks.size(); ++m) {
            const auto& mask = layer.masks.at(m);
            auto* maskRow = new QTreeWidgetItem(masks, {mask.name});
            maskRow->setData(0, Qt::UserRole, i);
            auto* controls = new QWidget(m_tree);
            auto* ml = new QHBoxLayout(controls); ml->setContentsMargins(0, 0, 0, 0); ml->setSpacing(3);
            auto* enabled = new QCheckBox(controls); enabled->setObjectName(QStringLiteral("timelineMaskEnabled_%1_%2").arg(i).arg(m));
            enabled->setChecked(mask.enabled); enabled->setToolTip(tr("Enable mask")); ml->addWidget(enabled);
            auto* shape = new QComboBox(controls); shape->setObjectName(QStringLiteral("timelineMaskShape_%1_%2").arg(i).arg(m));
            shape->addItems({tr("Rectangle"), tr("Rounded Rectangle"), tr("Ellipse"), tr("Polygon"), tr("Star"), tr("Freehand")});
            shape->setCurrentIndex(int(mask.shape)); shape->setToolTip(tr("Mask shape")); ml->addWidget(shape);
            auto number = [controls, ml](const QString& name, double value, double min, double max, const QString& tip) {
                auto* spin = new QDoubleSpinBox(controls); spin->setObjectName(name); spin->setRange(min, max);
                spin->setDecimals(1); spin->setValue(value); spin->setToolTip(tip); spin->setFixedWidth(72); ml->addWidget(spin); return spin;
            };
            auto* opacity = number(QStringLiteral("timelineMaskOpacity_%1_%2").arg(i).arg(m), mask.opacity * 100.0, 0, 100, tr("Opacity (%)"));
            auto* feather = number(QStringLiteral("timelineMaskFeather_%1_%2").arg(i).arg(m), mask.feather, 0, 1000, tr("Feather (px)"));
            auto* expansion = number(QStringLiteral("timelineMaskExpansion_%1_%2").arg(i).arg(m), mask.expansion, -1000, 1000, tr("Expansion (px)"));
            auto* invert = new QCheckBox(tr("Invert"), controls); invert->setObjectName(QStringLiteral("timelineMaskInvert_%1_%2").arg(i).arg(m)); invert->setChecked(mask.inverted); ml->addWidget(invert);
            auto* remove = iconButton(controls, QStringLiteral("timelineMaskRemove_%1_%2").arg(i).arg(m), "close", tr("Remove mask")); ml->addWidget(remove);
            m_tree->setItemWidget(maskRow, 1, controls);
            const auto editMask = [this, i, m](const QString& title, const std::function<void(composition::LayerMask&)>& fn, bool rebuild = false) {
                editLayer(i, title, [m, fn](composition::Layer& l) { if (m < l.masks.size()) fn(l.masks[m]); }, rebuild);
            };
            connect(enabled, &QCheckBox::toggled, this, [editMask](bool value) { editMask(QObject::tr("Enable mask"), [value](composition::LayerMask& x) { x.enabled = value; }); });
            connect(shape, &QComboBox::currentIndexChanged, this, [editMask](int value) { editMask(QObject::tr("Mask shape"), [value](composition::LayerMask& x) { x.shape = composition::MaskShape(value); }); });
            connect(opacity, &QDoubleSpinBox::valueChanged, this, [editMask](double value) { editMask(QObject::tr("Mask opacity"), [value](composition::LayerMask& x) { x.opacity = value / 100.0; }); });
            connect(feather, &QDoubleSpinBox::valueChanged, this, [editMask](double value) { editMask(QObject::tr("Mask feather"), [value](composition::LayerMask& x) { x.feather = value; }); });
            connect(expansion, &QDoubleSpinBox::valueChanged, this, [editMask](double value) { editMask(QObject::tr("Mask expansion"), [value](composition::LayerMask& x) { x.expansion = value; }); });
            connect(invert, &QCheckBox::toggled, this, [editMask](bool value) { editMask(QObject::tr("Invert mask"), [value](composition::LayerMask& x) { x.inverted = value; }); });
            connect(remove, &QToolButton::clicked, this, [this, i, m] { editLayer(i, tr("Remove mask"), [m](composition::Layer& l) { if (m < l.masks.size()) l.masks.removeAt(m); }, true); });
        }
        masks->setExpanded(!layer.masks.isEmpty());
        auto* effects = group(tr("Effects"), "timelineAddEffect_", true);
        auto* transform = group(tr("Transform"), QString(), false);
        buildTransformRows(transform, i);
        bool hasTransformKeys = false;
        for (const auto property : composition::transformPropertiesFor(layer.dimension)) {
            if (layer.transform.isAnimated(property, layer.dimension)) {
                hasTransformKeys = true;
                break;
            }
        }
        // On a fresh load there is no old expansion map to restore. Opening
        // an animated branch makes its keyframes visible immediately instead
        // of leaving them hidden behind a collapsed Transform group.
        transform->setExpanded(hasTransformKeys);
        if (layer.kind == composition::LayerKind::Model3D && m_media) {
            auto* models = group(tr("Models"), QString(), false);
            for (const auto& node : m_media->modelMesh(layer.modelAssetId).nodeNames) {
                auto* model = new QTreeWidgetItem(models, {node});
                model->setData(0, Qt::UserRole, i);
            }
        }
        auto* behaviors = group(tr("Behaviors"), "timelineAddBehavior_", true);
        for (int c = 0; c < layer.clips.size(); ++c) {
            addEffectRows(effects, i, c, false);
            addEffectRows(behaviors, i, c, true);
        }
        effects->setExpanded(true);
        behaviors->setExpanded(true);
        item->setExpanded(true);
    }
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
        auto* row = *it; const QString path = itemPath(row);
        row->setSizeHint(0, QSize(0, 23));
        if (expanded.contains(path)) row->setExpanded(expanded.value(path));
        if (path == selection) m_tree->setCurrentItem(row);
    }
    m_tree->verticalScrollBar()->setValue(scroll);
    m_rebuilding = false;
    applySearch(); refreshValues(); syncLanes();
}

void TimelineWidget::addEffectRows(QTreeWidgetItem* parent, int l, int c, bool behaviors)
{
    const auto& clip = m_comp->layers()[l].clips[c];
    const auto behaviorIds = m_pluginManager
        ? m_pluginManager->pluginIdsByKind(plugin::PluginKind::BehaviorEffect)
        : QVector<plugin::PluginId>{};
    for (int e = 0; e < clip.effects.size(); ++e) {
        const auto& effect = clip.effects[e];
        if (behaviorIds.contains(effect.pluginId) != behaviors) continue;
        const auto spec = m_pluginManager ? m_pluginManager->spec(effect.pluginId) : plugin::EffectSpec{};
        auto* effectRow = new QTreeWidgetItem(parent, {effect.name});
        effectRow->setCheckState(0, effect.enabled ? Qt::Checked : Qt::Unchecked);
        effectRow->setData(0, Qt::UserRole, l); effectRow->setData(0, Qt::UserRole + 1, c); effectRow->setData(0, Qt::UserRole + 2, e);
        effectRow->setToolTip(0, spec.renderable ? effect.name : spec.unavailableReason);
        auto* presetRow = new QTreeWidgetItem(effectRow, {QStringLiteral("•")});
        auto* presets = new QComboBox(m_tree);
        presets->setObjectName(QStringLiteral("timelinePreset_%1_%2_%3").arg(l).arg(c).arg(e));
        presets->addItem(tr("Preset"), "none"); presets->addItem(tr("Reset to defaults"), "reset");
        QSettings settings;
        const QString key = "TimelinePresets/" + effect.pluginId.value();
        settings.beginGroup(key);
        for (const auto& name : settings.childKeys()) presets->addItem(name, "load:" + name);
        settings.endGroup();
        presets->addItem(tr("Save preset…"), "save"); presets->addItem(tr("Remove effect"), "remove");
        presets->setEnabled(!m_comp->layers()[l].locked);
        m_tree->setItemWidget(presetRow, 1, presets);
        connect(presets, &QComboBox::activated, this, [this, presets, l, c, e, spec, key](int idx) {
            const QString action = presets->itemData(idx).toString();
            if (action == "save") {
                bool ok = false;
                const QString name = QInputDialog::getText(this, tr("Save preset"), tr("Name"), QLineEdit::Normal, QString(), &ok).trimmed();
                if (ok && !name.isEmpty() && !name.contains('/')) {
                    QSettings().setValue(key + '/' + name, m_comp->layers()[l].clips[c].effects[e].parameterValues); scheduleRefresh();
                }
            } else if (action != "none") {
                editLayer(l, action == "remove" ? tr("Remove effect") : tr("Apply preset"), [=](composition::Layer& layer) {
                    if (action == "remove") { layer.clips[c].effects.removeAt(e); return; }
                    auto& effect = layer.clips[c].effects[e];
                    if (action == "reset") { effect.parameterValues.clear(); for (const auto& p : spec.parameters) effect.parameterValues.append(p.defaultValue); }
                    else if (action.startsWith("load:")) effect.parameterValues = QSettings().value(key + '/' + action.mid(5)).toStringList();
                    effect.animation.clear();
                }, true);
            }
            presets->setCurrentIndex(0);
        });
        QHash<QString, QTreeWidgetItem*> groups;
        const int count = qMax(spec.parameters.size(), effect.parameterValues.size());
        for (int p = 0; p < count; ++p) {
            const auto param = spec.parameters.value(p);
            QTreeWidgetItem* parameterParent = effectRow;
            if (!param.group.isEmpty()) {
                if (!groups.contains(param.group)) { auto* g = new QTreeWidgetItem(effectRow, {param.group}); g->setExpanded(true); groups.insert(param.group, g); }
                parameterParent = groups[param.group];
            }
            auto* row = new QTreeWidgetItem(parameterParent, {param.displayName.isEmpty() ? tr("Parameter %1").arg(p + 1) : param.displayName});
            row->setData(0, Qt::UserRole, l); row->setData(0, Qt::UserRole + 1, c); row->setData(0, Qt::UserRole + 2, e);
            if (param.type != "label") {
                row->setData(0, Qt::UserRole + 3, p);
                row->setIcon(0, QIcon(effect.isAnimated(p) ? ":/icons/key-frame-full.svg" : ":/icons/key-frame-off.svg"));
                row->setToolTip(0, tr("Toggle keyframe at the playhead"));
                buildParameterEditor(row, l, c, e, p);
            }
            row->setBackground(0, QColor(53, 53, 53)); row->setBackground(1, QColor(53, 53, 53));
        }
        effectRow->setExpanded(true);
    }
}
}
