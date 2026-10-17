#include "ui/ModelAnimationDialog.h"
#include "ui/Theme.h"
#include "ui/TimelineParameterEditor.h"
#include "ui/TimelineRowDelegate.h"
#include "ui/TimelineWidget.h"
#include "plugin/NativeEffectRender.h"
#include "plugin/PluginManager.h"
#include "media/MediaManager.h"
#include "model3d/ModelLoader.h"
#include "composition/CompositionState.h"
#include <QCheckBox>
#include <QCoreApplication>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QScrollBar>
#include <QSet>
#include <QSettings>
#include <QSpinBox>
#include <QSignalBlocker>
#include <QToolButton>
#include <QTreeWidgetItemIterator>
#include <algorithm>
#include <functional>

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
// A two-state header toggle: the reference swaps the icon rather than
// painting a checked background (lock/unlock, video-on/off, two-d/three-d).
QToolButton* toggleButton(QWidget* parent, const QString& name, bool on,
                          const QString& onIcon, const QString& offIcon, const QString& tip) {
    auto* b = iconButton(parent, name, on ? onIcon : offIcon, tip);
    b->setCheckable(true); b->setChecked(on); b->setIconSize(QSize(16, 16));
    QObject::connect(b, &QToolButton::toggled, b, [b, onIcon, offIcon](bool checked) {
        b->setIcon(QIcon(":/icons/" + (checked ? onIcon : offIcon) + ".svg"));
    });
    return b;
}

// The reference's InLineEdit on the layer row: it reads "N. name [Kind]"
// until double-clicked, then edits the bare name. Return or leaving the
// field commits, Escape restores the label. While it only shows the label
// a press falls through to the tree, so selecting (and Ctrl/Shift
// multi-selecting) a layer by its name works like anywhere else on the row.
class LayerNameEdit : public QLineEdit {
public:
    LayerNameEdit(const QString& label, const QString& name, bool editable, QWidget* parent)
        : QLineEdit(parent), m_label(label), m_name(name), m_editable(editable) { showLabel(); }
    std::function<void(const QString&)> renamed;
    void beginEdit() {
        if (!m_editable || !isReadOnly()) return;
        setReadOnly(false); setFocusPolicy(Qt::StrongFocus); setCursor(Qt::IBeamCursor);
        setText(m_name); selectAll(); setFocus(Qt::MouseFocusReason);
    }
protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (isReadOnly()) { event->ignore(); return; }
        QLineEdit::mousePressEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (isReadOnly()) { event->ignore(); return; }
        QLineEdit::mouseReleaseEvent(event);
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (isReadOnly()) { event->ignore(); return; }
        QLineEdit::mouseMoveEvent(event);
    }
    void mouseDoubleClickEvent(QMouseEvent* event) override {
        if (isReadOnly()) { beginEdit(); return; }
        QLineEdit::mouseDoubleClickEvent(event);
    }
    void keyPressEvent(QKeyEvent* event) override {
        if (!isReadOnly() && event->key() == Qt::Key_Escape) { showLabel(); return; }
        if (!isReadOnly() && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)) {
            commit(); return;
        }
        QLineEdit::keyPressEvent(event);
    }
    void focusOutEvent(QFocusEvent* event) override {
        if (!isReadOnly()) commit();
        QLineEdit::focusOutEvent(event);
    }
private:
    void commit() {
        const QString value = text().trimmed();
        showLabel();
        // The rename rebuilds the tree later (scheduleRefresh is queued), so
        // this widget is still alive when the callback returns.
        if (!value.isEmpty() && value != m_name && renamed) renamed(value);
    }
    void showLabel() {
        setReadOnly(true); setFocusPolicy(Qt::NoFocus); setCursor(Qt::ArrowCursor);
        setText(m_label); setCursorPosition(0); deselect();
    }
    QString m_label, m_name;
    bool m_editable = true;
};

// True when making `candidate` the parent of `child` would close a loop
// (the candidate already hangs below the child, directly or further down).
bool parentCycle(const composition::Composition& comp, int child, int candidate) {
    if (child < 0 || candidate < 0 || child >= comp.layers().size() || candidate >= comp.layers().size())
        return false;
    const core::Identifier childId = comp.layers().at(child).id;
    core::Identifier id = comp.layers().at(candidate).id;
    QSet<QString> visited;
    while (id.isValid()) {
        if (id == childId || visited.contains(id.value())) return true;
        visited.insert(id.value());
        const auto it = std::find_if(comp.layers().cbegin(), comp.layers().cend(),
                                     [&id](const composition::Layer& l) { return l.id == id; });
        if (it == comp.layers().cend()) break;
        id = it->parentLayerId;
    }
    return false;
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
        const QString label = QStringLiteral("%1. %2 [%3]").arg(i + 1).arg(layer.name,
            QCoreApplication::translate("LayerKind", composition::layerKindName(layer.kind)));
        auto* item = new QTreeWidgetItem(m_tree, {label});
        item->setData(0, Qt::UserRole, i);
        item->setFirstColumnSpanned(true);
        // The row text stays for search and accessibility; the header widget
        // draws it, so the delegate skips the item's own copy and the
        // transparent header lets the row's selection colour show through.
        item->setData(0, kTimelineHeaderRowRole, true);
        auto* header = new QWidget(m_tree);
        header->setObjectName("timelineLayerHeader");
        header->setStyleSheet(QStringLiteral(
            "QWidget#timelineLayerHeader { background:transparent; }"
            "QWidget#timelineLayerHeader QToolButton:checked { background:transparent; }"
            "QWidget#timelineLayerHeader QToolButton:checked:hover { background:#454545; }"
            "QWidget#timelineLayerHeader QComboBox::drop-down { border:0; width:16px; }"
            "QWidget#timelineLayerHeader QComboBox::down-arrow { image:url(:/icons/caret-down.svg); width:12px; height:12px; }"));
        auto* line = new QHBoxLayout(header); line->setContentsMargins(0, 0, 4, 0); line->setSpacing(3);
        // Order and icons follow the reference row: lock (unlock/lock), video
        // (video-on-checked/video-off), label, name, motion blur, two-d/three-d
        // and the parent picker.
        auto* lock = toggleButton(header, QStringLiteral("timelineLock_%1").arg(i), layer.locked,
                                  QStringLiteral("layer-locked"), QStringLiteral("layer-unlocked"), tr("Lock layer"));
        line->addWidget(lock);
        connect(lock, &QToolButton::clicked, this, [this, i](bool on) {
            editLayer(i, on ? tr("Lock Layer") : tr("Unlock Layer"), [on](composition::Layer& l) { l.locked = on; }, true);
        });
        auto* eye = toggleButton(header, QStringLiteral("timelineVisible_%1").arg(i), layer.visible,
                                 QStringLiteral("layer-visible"), QStringLiteral("layer-hidden"), tr("Layer visibility"));
        eye->setEnabled(!layer.locked); line->addWidget(eye);
        connect(eye, &QToolButton::clicked, this, [this, i](bool on) {
            editLayer(i, tr("Layer visibility"), [on](composition::Layer& l) { l.visible = on; });
        });
        auto* color = iconButton(header, QStringLiteral("timelineLabelColor_%1").arg(i), QString(), tr("Layer label color"));
        color->setFixedSize(14, 14);
        color->setStyleSheet(QStringLiteral("QToolButton { background:%1; border:1px solid #1b1b1b; }").arg(layer.labelColor.name()));
        line->addWidget(color);
        color->setEnabled(!layer.locked);
        connect(color, &QToolButton::clicked, this, [this, i, color] {
            QMenu menu(this);
            const char* names[] = {"Red", "Orange", "Yellow", "Green", "Cyan", "Blue", "Purple", "Pink"};
            const char* colors[] = {"#c94b4b", "#d9823b", "#d1b849", "#55a868", "#4aa6a6", "#4f78b8", "#8662b0", "#b95f8a"};
            const QSettings settings = app::Settings::optionSettings();
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
        auto* name = new LayerNameEdit(label, layer.name, !layer.locked, header);
        name->setObjectName(QStringLiteral("timelineLayerName_%1").arg(i)); name->setMinimumWidth(30);
        name->setToolTip(layer.locked ? label : tr("%1\nDouble-click to rename").arg(label));
        line->addWidget(name, 1);
        name->renamed = [this, i, generation](const QString& value) {
            if (generation != m_treeGeneration) return;
            editLayer(i, tr("Set Layer Name"), [value](composition::Layer& l) { l.name = value; }, true);
        };
        const bool sceneObject = layer.kind == composition::LayerKind::Model3D
            || layer.kind == composition::LayerKind::Camera || layer.kind == composition::LayerKind::Light;
        auto* blur = toggleButton(header, QStringLiteral("timelineMotionBlur_%1").arg(i), layer.motionBlur,
                                  QStringLiteral("layer-motion-blur-on"), QStringLiteral("layer-motion-blur"),
                                  tr("Motion Blur"));
        blur->setEnabled(!layer.locked && layer.kind != composition::LayerKind::Camera
                         && layer.kind != composition::LayerKind::Light);
        line->addWidget(blur);
        connect(blur, &QToolButton::clicked, this, [this, i](bool on) {
            editLayer(i, tr("Set Layer Motion Blur"), [on](composition::Layer& l) { l.motionBlur = on; });
        });
        auto* dimension = toggleButton(header, QStringLiteral("timelineDimension_%1").arg(i),
                                       layer.dimension == composition::LayerDimension::ThreeD,
                                       QStringLiteral("layer-3d"), QStringLiteral("layer-2d"),
                                       tr("Layer Dimensions"));
        dimension->setEnabled(!layer.locked && !sceneObject);
        connect(dimension, &QToolButton::clicked, this, [this, i](bool on) {
            setLayerDimension(i, on ? composition::LayerDimension::ThreeD : composition::LayerDimension::TwoD);
        });
        line->addWidget(dimension);
        // Parent: "None" or any layer that would not end up below this one.
        auto* parent = new QComboBox(header); parent->setObjectName(QStringLiteral("timelineParent_%1").arg(i));
        parent->setToolTip(tr("Parent")); parent->setAccessibleName(tr("Parent"));
        parent->addItem(tr("None"), -1);
        for (int p = 0; p < m_comp->layers().size(); ++p) {
            if (p == i || parentCycle(*m_comp, i, p)) continue;
            parent->addItem(QStringLiteral("%1. %2").arg(p + 1).arg(m_comp->layers().at(p).name), p);
            if (m_comp->layers().at(p).id == layer.parentLayerId) parent->setCurrentIndex(parent->count() - 1);
        }
        parent->setFixedWidth(112); parent->setEnabled(!layer.locked); line->addWidget(parent);
        connect(parent, &QComboBox::activated, this, [this, i, parent](int index) {
            const int p = parent->itemData(index).toInt();
            if (!m_comp || (p >= 0 && (p >= m_comp->layers().size() || parentCycle(*m_comp, i, p)))) return;
            const core::Identifier id = p >= 0 ? m_comp->layers().at(p).id : core::Identifier();
            editLayer(i, tr("Set Layer Parent(s)"), [id](composition::Layer& l) { l.parentLayerId = id; }, true);
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
                } else if (name == "timelineAddModelAnimation_") {
                    connect(add, &QToolButton::clicked, this, [this, i, add] { showModelAnimationMenu(i, add); });
                } else connect(add, &QToolButton::clicked, this, [this, i, name, add] { showEffectMenu(i, name == "timelineAddBehavior_", add); });
            }
            return row;
        };
        // AssetLayerGroupFactory builds Tracks/Masks, Transform and Behaviors
        // only for an asset with picture (AssetHadVideo) and adds an Audio
        // group for one with sound (AssetHadAudio); a song gets just Effects
        // and Audio.
        bool hasPicture = true, hasSound = false;
        if (layer.kind == composition::LayerKind::Media && m_media && !layer.clips.isEmpty()
            && !layer.clips.first().nestedComposition) {
            const auto asset = m_media->assetById(layer.clips.first().mediaId);
            if (asset.isValid()) {
                hasSound = asset.kind() == media::MediaKind::Audio
                    || (asset.kind() == media::MediaKind::Video && !asset.isImageSequence());
                hasPicture = asset.kind() != media::MediaKind::Audio;
            }
        }
        const auto addAudio = [&] {
            if (!hasSound) return;
            auto* audio = group(tr("Audio"), QString(), false);
            buildTransformRow(audio, i, composition::TransformProperty::AudioLevel);
            audio->setExpanded(layer.transform.isAnimated(composition::TransformProperty::AudioLevel));
        };
        if (!hasPicture) {
            auto* effects = group(tr("Effects"), "timelineAddEffect_", true);
            for (int c = 0; c < layer.clips.size(); ++c) addEffectRows(effects, i, c, false);
            effects->setExpanded(true);
            addAudio();
            item->setExpanded(true);
            continue;
        }
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
            auto* remove = iconButton(controls, QStringLiteral("timelineTrackRemove_%1_%2").arg(i).arg(t), "tab-close", tr("Remove track")); tl->addWidget(remove);
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
        // No "+" here: like the reference, masks are drawn with the Viewer's
        // mask tools (ViewerWidget::maskCreationRequested → addMaskToLayer).
        auto* masks = group(tr("Masks"), QString(), false);
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
            auto* remove = iconButton(controls, QStringLiteral("timelineMaskRemove_%1_%2").arg(i).arg(m), "tab-close", tr("Remove mask")); ml->addWidget(remove);
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
        addAlignmentRows(i, transform);
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
        if (layer.kind == composition::LayerKind::Camera) {
            auto* lens = group(QCoreApplication::translate("CameraLayerGroupFactory", "Lens"), QString(), false);
            addCameraRows(i, lens);
            lens->setExpanded(true);
        } else if (layer.kind == composition::LayerKind::Light) {
            auto* light = group(QCoreApplication::translate("LightLayerGroupFactory", "Light"), QString(), false);
            addLightRows(i, light);
            light->setExpanded(true);
        } else if (layer.kind == composition::LayerKind::Model3D
                   || (layer.dimension == composition::LayerDimension::ThreeD
                       && (layer.kind == composition::LayerKind::Media || layer.kind == composition::LayerKind::Plane
                           || layer.kind == composition::LayerKind::Text))) {
            addSurfaceRows(i, group(QCoreApplication::translate("AssetLayerGroupFactory", "Material"), QString(), false));
        }
        if (layer.kind == composition::LayerKind::Model3D && m_media) {
            // The model's nodes as the file nests them, each with the
            // Transform of its own (the reference's Model3DTransformNode).
            auto* models = group(tr("Models"), QString(), false);
            const model3d::Mesh mesh = m_media->modelMesh(layer.modelAssetId);
            QVector<QTreeWidgetItem*> nodeRows(mesh.nodes.size(), nullptr);
            for (int n = 0; n < mesh.nodes.size(); ++n) {
                const model3d::ModelNode& node = mesh.nodes.at(n);
                QTreeWidgetItem* parentRow =
                    node.parent >= 0 && node.parent < n && nodeRows.at(node.parent) ? nodeRows.at(node.parent) : models;
                auto* model = new QTreeWidgetItem(parentRow, {node.name});
                const QString path = mesh.nodePath(n);
                model->setData(0, Qt::UserRole, i);
                model->setData(0, kModelNodeRole, path);
                nodeRows[n] = model;
                auto* nodeTransform = new QTreeWidgetItem(model, {tr("Transform")});
                nodeTransform->setData(0, Qt::UserRole, i);
                nodeTransform->setData(0, kModelNodeRole, path);
                for (const auto property : composition::modelNodeTransformProperties())
                    buildTransformRow(nodeTransform, i, property, path, node.pivot);
                const composition::ModelNodeTransform* state = layer.modelNode(path);
                bool animated = false;
                for (const auto property : composition::modelNodeTransformProperties())
                    animated = animated || (state && state->transform.isAnimated(property, composition::LayerDimension::ThreeD));
                nodeTransform->setExpanded(animated);
            }
            if (mesh.nodes.isEmpty()) {
                for (const auto& nodeName : mesh.nodeNames) {
                    auto* model = new QTreeWidgetItem(models, {nodeName});
                    model->setData(0, Qt::UserRole, i);
                }
            }
            if (!mesh.materials.isEmpty()) addMaterialRows(i, mesh, group(
                QCoreApplication::translate("Model3DFactory", "Override Materials"), QString(), false));
            // The animations playing on it (Model3DAnimationAssetInstance).
            auto* animations = group(QCoreApplication::translate("Model3DFactory", "Animations"),
                                     "timelineAddModelAnimation_", true);
            addModelAnimationRows(i, animations);
            animations->setExpanded(!layer.modelAnimations.isEmpty());
        }
        auto* behaviors = group(tr("Behaviors"), "timelineAddBehavior_", true);
        for (int c = 0; c < layer.clips.size(); ++c) {
            addEffectRows(effects, i, c, false);
            addEffectRows(behaviors, i, c, true);
        }
        effects->setExpanded(true);
        behaviors->setExpanded(true);
        addAudio();
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

// One property row of a layer group: its label, an editor of `spec`'s kind
// reading `get` and writing through `set` as one undoable change - and, for a
// property that animates, the reference's name of it (`keyed`): the row then
// carries a key button and its lane the property's keys.
struct TimelinePropertyRow
{
    const char* context;
    const char* label;
    QString key;
    plugin::EffectParameterSpec spec;
    std::function<QVariant(const composition::Layer&, int frame)> get;
    std::function<void(composition::Layer&, const QVariant&, int frame)> set;
    QString title;   // the undo text; "Set <label>" when empty
    QString keyed = QString();
    bool rebuild = false;   // an edit that changes which rows there are
};

namespace {

plugin::EffectParameterSpec numberSpec(double minimum, double maximum, const QString& unit, int decimals = 1)
{
    plugin::EffectParameterSpec spec;
    spec.type = QStringLiteral("double");
    spec.minimum = minimum;
    spec.maximum = maximum;
    spec.unit = unit;
    spec.decimals = decimals;
    return spec;
}

plugin::EffectParameterSpec intSpec(int minimum, int maximum)
{
    plugin::EffectParameterSpec spec = numberSpec(minimum, maximum, QString(), 0);
    spec.type = QStringLiteral("int");
    return spec;
}

plugin::EffectParameterSpec choiceSpec(const char* context, std::initializer_list<std::pair<const char*, int>> items)
{
    plugin::EffectParameterSpec spec;
    spec.type = QStringLiteral("choice");
    for (const auto& item : items) {
        spec.choices << QCoreApplication::translate(context, item.first);
        spec.choiceValues << QString::number(item.second);
    }
    return spec;
}

plugin::EffectParameterSpec kindSpec(const char* type)
{
    plugin::EffectParameterSpec spec;
    spec.type = QString::fromLatin1(type);
    return spec;
}

QVariant boolValue(bool on) { return on ? QStringLiteral("true") : QStringLiteral("false"); }
bool isOn(const QVariant& value) { return value.toString() == QLatin1String("true") || value.toString() == QLatin1String("1"); }

// A row of a property that animates (composition::keyedLayerProperty), read
// and written at the playhead: a key there once it has keys. A colour's
// editor speaks "#rrggbb".
TimelinePropertyRow keyedRow(const char* context, const char* label, const char* key, const char* name,
                             const plugin::EffectParameterSpec& spec)
{
    const QString property = QString::fromLatin1(name);
    const bool colour = spec.type == QLatin1String("color");
    return {context, label, QString::fromLatin1(key), spec,
            [property, colour](const composition::Layer& layer, int frame) {
                const QVariant value = composition::keyedLayerPropertyAt(layer, property, frame);
                return colour ? QVariant(value.value<QColor>().name()) : value;
            },
            [property, colour](composition::Layer& layer, const QVariant& value, int frame) {
                composition::setKeyedLayerProperty(layer, property,
                                                   colour ? QVariant(QColor(value.toString())) : QVariant(value.toDouble()),
                                                   frame);
            },
            QString(), property};
}

// A static on/off row of a layer's Material.
TimelinePropertyRow flagRow(const char* label, const char* key, bool composition::SurfaceShading::*member)
{
    return {"AssetLayerGroupFactory", label, QString::fromLatin1(key), kindSpec("bool"),
            [member](const composition::Layer& layer, int) { return boolValue(layer.surface.*member); },
            [member](composition::Layer& layer, const QVariant& value, int) { layer.surface.*member = isOn(value); },
            QString()};
}

} // namespace

void TimelineWidget::addLightRows(int l, QTreeWidgetItem* parent)
{
    // The reference's Light group (LightLayerGroupFactory), in its order:
    // the kind of light and how it shines and casts shadows. Its colours
    // and numbers animate; Type, Cast Shadows and Falloff do not.
    using Light = composition::LightSettings;
    const auto light = [](composition::Layer& layer) -> Light& { return layer.light; };
    const QString percent = QStringLiteral("%");
    const char* group = "LightLayerGroupFactory";
    const QVector<TimelinePropertyRow> rows = {
        {group, QT_TRANSLATE_NOOP("LightLayerGroupFactory", "Type"), "type",
         choiceSpec("LightLayerGroupFactory", {{QT_TRANSLATE_NOOP("LightLayerGroupFactory", "Ambient"), 1},
                                               {QT_TRANSLATE_NOOP("LightLayerGroupFactory", "Point"), 2},
                                               {QT_TRANSLATE_NOOP("LightLayerGroupFactory", "Spot"), 3},
                                               {QT_TRANSLATE_NOOP("LightLayerGroupFactory", "Directional"), 4}}),
         [](const composition::Layer& layer, int) { return QVariant(QString::number(int(layer.light.type))); },
         [light](composition::Layer& layer, const QVariant& value, int) { light(layer).type = Light::Type(value.toInt()); },
         QCoreApplication::translate("DataUtilities", "Set Light Layer Type")},
        keyedRow(group, QT_TRANSLATE_NOOP("LightLayerGroupFactory", "Color"), "color", "color", kindSpec("color")),
        keyedRow(group, QT_TRANSLATE_NOOP("LightLayerGroupFactory", "Intensity"), "intensity", "intensity",
                 numberSpec(0.0, 10000.0, percent)),
        {group, QT_TRANSLATE_NOOP("LightLayerGroupFactory", "Cast Shadows"), "castShadows", kindSpec("bool"),
         [](const composition::Layer& layer, int) { return boolValue(layer.light.castShadows); },
         [light](composition::Layer& layer, const QVariant& value, int) { light(layer).castShadows = isOn(value); }, {}},
        {group, QT_TRANSLATE_NOOP("LightLayerGroupFactory", "Falloff"), "falloff",
         choiceSpec("LightLayerGroupFactory", {{QT_TRANSLATE_NOOP("LightLayerGroupFactory", "None"), 1},
                                               {QT_TRANSLATE_NOOP("LightLayerGroupFactory", "Linear"), 2},
                                               {QT_TRANSLATE_NOOP("LightLayerGroupFactory", "Curve"), 3}}),
         [](const composition::Layer& layer, int) { return QVariant(QString::number(int(layer.light.falloff))); },
         [light](composition::Layer& layer, const QVariant& value, int) { light(layer).falloff = Light::Falloff(value.toInt()); }, {}},
        keyedRow(group, QT_TRANSLATE_NOOP("LightLayerGroupFactory", "Reach"), "reach", "reach",
                 numberSpec(0.0, 1000000.0, QCoreApplication::translate("LightLayerGroupFactory", "px"))),
        keyedRow(group, QT_TRANSLATE_NOOP("LightLayerGroupFactory", "Cone Angle"), "coneAngle", "spotlightAngle",
                 numberSpec(0.0, 180.0, QStringLiteral("\u00b0"))),
        keyedRow(group, QT_TRANSLATE_NOOP("LightLayerGroupFactory", "Feather"), "feather", "spotlightFeather",
                 numberSpec(0.0, 100.0, percent)),
        keyedRow(group, QT_TRANSLATE_NOOP("LightLayerGroupFactory", "Shadow Opacity"), "shadowOpacity", "shadowOpacity",
                 numberSpec(0.0, 100.0, percent)),
        keyedRow(group, QT_TRANSLATE_NOOP("LightLayerGroupFactory", "Shadow Diffusion"), "shadowDiffusion",
                 "shadowDiffusion", numberSpec(0.0, 100.0, percent)),
        keyedRow(group, QT_TRANSLATE_NOOP("LightLayerGroupFactory", "Shadow Color"), "shadowColor", "shadowColor",
                 kindSpec("color")),
    };
    addPropertyRows(l, parent, rows, QStringLiteral("timelineLight_"));
}

void TimelineWidget::addCameraRows(int l, QTreeWidgetItem* parent)
{
    // The reference's Lens group: Zoom (the distance in pixels at which the
    // shot's frame fills the lens), Focus Distance, Aperture and Blur, which
    // the depth of field is drawn with and which all animate - and, first,
    // whether the camera has one.
    using Camera = composition::CameraSettings;
    const auto camera = [](composition::Layer& layer) -> Camera& { return layer.camera; };
    const QString px = QCoreApplication::translate("LightLayerGroupFactory", "px");
    const char* group = "CameraLayerGroupFactory";
    const QVector<TimelinePropertyRow> rows = {
        // HasDoF: whether the camera blurs what is out of focus.
        {"CompositionFactory", QT_TRANSLATE_NOOP("CompositionFactory", "Depth of Field"), "depthOfField",
         kindSpec("bool"),
         [](const composition::Layer& layer, int) { return boolValue(layer.camera.depthOfField); },
         [camera](composition::Layer& layer, const QVariant& value, int) { camera(layer).depthOfField = isOn(value); },
         QCoreApplication::translate("DataUtilities", "Set Camera Layer Depth of Field")},
        keyedRow(group, QT_TRANSLATE_NOOP("CameraLayerGroupFactory", "Zoom"), "zoom", "zoom",
                 numberSpec(0.0, 1000000.0, px)),
        keyedRow(group, QT_TRANSLATE_NOOP("CameraLayerGroupFactory", "Focus Distance"), "focusDistance",
                 "focusDistance", numberSpec(0.0, 1000000.0, px)),
        keyedRow(group, QT_TRANSLATE_NOOP("CameraLayerGroupFactory", "Aperture"), "aperture", "aperture",
                 numberSpec(0.0, 100000.0, px)),
        keyedRow(group, QT_TRANSLATE_NOOP("CameraLayerGroupFactory", "Blur"), "blur", "blurDoF",
                 numberSpec(0.0, 1000.0, QStringLiteral("%"))),
    };
    addPropertyRows(l, parent, rows, QStringLiteral("timelineCamera_"));
}

void TimelineWidget::addSurfaceRows(int l, QTreeWidgetItem* parent)
{
    // The part of the reference's Material group (AssetLayerGroupFactory,
    // VegasEffects.exe 0x1405e0130) the renderer takes, in its order:
    // whether lights and shadows reach the layer, whether it casts them
    // while hidden, a model's Self Shadow Bias, whether it receives and
    // casts reflections (and casts them while hidden), a text or model
    // layer's Environment Map (and its Layer), whether it receives and
    // casts ambient occlusion and the Ambient Occlusion group (a model's
    // with Include Semi Transparent Triangles), a media or plane layer's
    // Billboarded Lights and Shadows, but for a model, whose materials
    // bring their own, its Ambient, Diffuse, Specular, Shininess and
    // Emissive, and a media, plane or text layer's Specular Reflectivity (a
    // text layer's Diffuse Reflectivity too), which animate.
    using Surface = composition::SurfaceShading;
    const composition::LayerKind kind = m_comp->layers()[l].kind;
    const bool model = kind == composition::LayerKind::Model3D;
    const QString percent = QStringLiteral("%");
    const char* group = "AssetLayerGroupFactory";
    QVector<TimelinePropertyRow> rows = {
        flagRow(QT_TRANSLATE_NOOP("AssetLayerGroupFactory", "Illuminated"), "illuminated", &Surface::illuminated),
        flagRow(QT_TRANSLATE_NOOP("AssetLayerGroupFactory", "Receives Shadows"), "receiveShadows",
                &Surface::receiveShadows),
        flagRow(QT_TRANSLATE_NOOP("AssetLayerGroupFactory", "Casts Shadows"), "castShadows", &Surface::castShadows),
        flagRow(QT_TRANSLATE_NOOP("AssetLayerGroupFactory", "Cast Shadows If Visibility Is Off"),
                "castShadowsIfInvisible", &Surface::castShadowsIfInvisible),
    };
    if (model) {
        rows.append({group, QT_TRANSLATE_NOOP("AssetLayerGroupFactory", "Self Shadow Bias"), "selfShadowBias",
                     numberSpec(1.0, 1000.0, QString()),
                     [](const composition::Layer& layer, int) { return QVariant(layer.surface.selfShadowBias); },
                     [](composition::Layer& layer, const QVariant& value, int) {
                         layer.surface.selfShadowBias = qBound(1.0, value.toDouble(), 1000.0);
                     },
                     QString()});
    }
    rows.append(flagRow(QT_TRANSLATE_NOOP("AssetLayerGroupFactory", "Receives Reflections"), "receiveReflections",
                        &Surface::receiveReflections));
    rows.append(flagRow(QT_TRANSLATE_NOOP("AssetLayerGroupFactory", "Casts Reflections"), "castReflections",
                        &Surface::castReflections));
    rows.append(flagRow(QT_TRANSLATE_NOOP("AssetLayerGroupFactory", "Cast Reflections If Visibility Is Off"),
                        "castReflectionsIfInvisible", &Surface::castReflectionsIfInvisible));
    if (kind == composition::LayerKind::Text || model) {
        using Environment = Surface::EnvironmentMap;
        rows.append({group, QT_TRANSLATE_NOOP("AssetLayerGroupFactory", "Environment Map"), "environmentMap",
                     choiceSpec(group, {{QT_TRANSLATE_NOOP("AssetLayerGroupFactory", "None"), int(Environment::None)},
                                        {QT_TRANSLATE_NOOP("AssetLayerGroupFactory", "Use Composite Shot"),
                                         int(Environment::CompositeShot)},
                                        {QT_TRANSLATE_NOOP("AssetLayerGroupFactory", "Use Layer"), int(Environment::Layer)}}),
                     [](const composition::Layer& layer, int) {
                         return QVariant(QString::number(int(layer.surface.environmentMap)));
                     },
                     [](composition::Layer& layer, const QVariant& value, int) {
                         const int chosen = value.toInt();
                         if (chosen >= int(Environment::None) && chosen <= int(Environment::Layer))
                             layer.surface.environmentMap = Environment(chosen);
                     },
                     QString(), QString(), true});
        if (m_comp->layers()[l].surface.environmentMap == Environment::Layer) {
            // The other layers of the shot, by name.
            plugin::EffectParameterSpec layerSpec;
            layerSpec.type = QStringLiteral("choice");
            layerSpec.choices << QCoreApplication::translate("AssetLayerGroupFactory", "None");
            layerSpec.choiceValues << QString();
            const composition::Layer& own = m_comp->layers()[l];
            for (const composition::Layer& other : m_comp->layers()) {
                if (other.id == own.id) continue;
                layerSpec.choices << other.name;
                layerSpec.choiceValues << other.id.value();
            }
            rows.append({group, QT_TRANSLATE_NOOP("AssetLayerGroupFactory", "Layer"), "environmentMapLayer", layerSpec,
                         [](const composition::Layer& layer, int) { return QVariant(layer.surface.environmentMapLayer.value()); },
                         [](composition::Layer& layer, const QVariant& value, int) {
                             layer.surface.environmentMapLayer = core::Identifier(value.toString());
                         },
                         QString()});
        }
    }
    rows.append(flagRow(QT_TRANSLATE_NOOP("AssetLayerGroupFactory", "Receives Ambient Occlusion"),
                        "receiveAmbientOcclusion", &Surface::receiveAmbientOcclusion));
    rows.append(flagRow(QT_TRANSLATE_NOOP("AssetLayerGroupFactory", "Casts Ambient Occlusion"), "castAmbientOcclusion",
                        &Surface::castAmbientOcclusion));
    addPropertyRows(l, parent, rows, QStringLiteral("timelineSurface_"));
    rows.clear();

    // Ambient Occlusion: how the occlusion it receives is drawn, all static.
    const auto whole = [group](const char* label, const char* key, int Surface::*member, int low, int high) {
        return TimelinePropertyRow{group, label, QString::fromLatin1(key), intSpec(low, high),
                                   [member](const composition::Layer& layer, int) { return QVariant(layer.surface.*member); },
                                   [member, low, high](composition::Layer& layer, const QVariant& value, int) {
                                       layer.surface.*member = qBound(low, value.toInt(), high);
                                   },
                                   QString()};
    };
    const auto real = [group](const char* label, const char* key, double Surface::*member, double high,
                              const QString& unit, int decimals) {
        return TimelinePropertyRow{group, label, QString::fromLatin1(key), numberSpec(0.0, high, unit, decimals),
                                   [member](const composition::Layer& layer, int) { return QVariant(layer.surface.*member); },
                                   [member, high](composition::Layer& layer, const QVariant& value, int) {
                                       layer.surface.*member = qBound(0.0, value.toDouble(), high);
                                   },
                                   QString()};
    };
    QVector<TimelinePropertyRow> occlusion = {
        whole(QT_TRANSLATE_NOOP("AssetLayerGroupFactory", "Samples"), "aoSamples", &Surface::aoSamples, 4, 10),
        whole(QT_TRANSLATE_NOOP("AssetLayerGroupFactory", "Sample Radius"), "aoSampleRadius", &Surface::aoSampleRadius,
              0, 20),
        real(QT_TRANSLATE_NOOP("AssetLayerGroupFactory", "Intensity"), "aoIntensity", &Surface::aoIntensity, 100.0,
             percent, 1),
        real(QT_TRANSLATE_NOOP("AssetLayerGroupFactory", "Depth Scale"), "aoDepthScale", &Surface::aoDepthScale, 100.0,
             percent, 1),
        real(QT_TRANSLATE_NOOP("AssetLayerGroupFactory", "Bias"), "aoBias", &Surface::aoBias, 10.0, QString(), 3),
        whole(QT_TRANSLATE_NOOP("AssetLayerGroupFactory", "Blur Radius"), "aoBlurRadius", &Surface::aoBlurRadius, 1, 20),
        real(QT_TRANSLATE_NOOP("AssetLayerGroupFactory", "Depth Falloff"), "aoDepthFalloff", &Surface::aoDepthFalloff,
             100.0, percent, 1),
    };
    if (model)
        occlusion.append(flagRow(QT_TRANSLATE_NOOP("AssetLayerGroupFactory", "Include Semi Transparent Triangles"),
                                 "includeSemiTransparentTriangles", &Surface::includeSemiTransparentTriangles));
    auto* occlusionGroup =
        new QTreeWidgetItem(parent, {QCoreApplication::translate("AssetLayerGroupFactory", "Ambient Occlusion")});
    occlusionGroup->setData(0, Qt::UserRole, l);
    addPropertyRows(l, occlusionGroup, occlusion, QStringLiteral("timelineSurface_"));

    if (kind == composition::LayerKind::Media || kind == composition::LayerKind::Plane) {
        rows.append(flagRow(QT_TRANSLATE_NOOP("AssetLayerGroupFactory", "Billboarded Lights"), "billboardedLights",
                            &Surface::billboardedLights));
        rows.append(flagRow(QT_TRANSLATE_NOOP("AssetLayerGroupFactory", "Billboarded Shadows"), "billboardedShadows",
                            &Surface::billboardedShadows));
    }
    if (!model) {
        rows.append(keyedRow(group, QT_TRANSLATE_NOOP("AssetLayerGroupFactory", "Ambient"), "ambient", "matAmbient",
                             numberSpec(0.0, 1000.0, percent)));
        rows.append(keyedRow(group, QT_TRANSLATE_NOOP("AssetLayerGroupFactory", "Diffuse"), "diffuse", "matDiffuse",
                             numberSpec(0.0, 1000.0, percent)));
        rows.append(keyedRow(group, QT_TRANSLATE_NOOP("AssetLayerGroupFactory", "Specular"), "specular", "matSpecular",
                             numberSpec(0.0, 1000.0, percent)));
        rows.append(keyedRow(group, QT_TRANSLATE_NOOP("AssetLayerGroupFactory", "Shininess"), "shininess",
                             "matShininess", numberSpec(0.0, 10000.0, QString())));
        rows.append(keyedRow(group, QT_TRANSLATE_NOOP("AssetLayerGroupFactory", "Emissive"), "emissive",
                             "matEmissiveColor", kindSpec("color")));
        rows.append(keyedRow(group, QT_TRANSLATE_NOOP("AssetLayerGroupFactory", "Specular Reflectivity"),
                             "specularReflectivity", "specularReflectivity", numberSpec(0.0, 100.0, percent)));
    }
    if (kind == composition::LayerKind::Text)
        rows.append(keyedRow(group, QT_TRANSLATE_NOOP("AssetLayerGroupFactory", "Diffuse Reflectivity"),
                             "diffuseReflectivity", "diffuseReflectivity", numberSpec(0.0, 100.0, percent)));
    addPropertyRows(l, parent, rows, QStringLiteral("timelineSurface_"));
}

void TimelineWidget::addAlignmentRows(int l, QTreeWidgetItem* parent)
{
    // The reference's lists (VegasEffects.exe 0x14050fec0, CompositionModel):
    // a 2D picture layer None or Along Motion Path, turning its X or Y; a 3D
    // one also Towards Active Camera and Towards Layer, turning X, Y or Z; a
    // camera or a light None, Along Motion Path, Towards Target Position or
    // Towards Layer; a point layer None, Towards Target Position or Towards
    // Layer.
    using composition::AutoOrient;
    using composition::LayerKind;
    const composition::Layer& layer = m_comp->layers()[l];
    const bool picture = layer.kind == LayerKind::Media || layer.kind == LayerKind::Plane
                         || layer.kind == LayerKind::Text;
    const bool aims = layer.kind == LayerKind::Camera || layer.kind == LayerKind::Light;
    if (!picture && !aims && layer.kind != LayerKind::Point) return;
    const bool threeD = layer.dimension == composition::LayerDimension::ThreeD || aims;
    QVector<std::pair<const char*, int>> modes = {{QT_TRANSLATE_NOOP("CompositionModel", "None"), 0}};
    if (layer.kind != LayerKind::Point)
        modes.append({QT_TRANSLATE_NOOP("CompositionModel", "Along Motion Path"), int(AutoOrient::AlongMotionPath)});
    if (picture && threeD)
        modes.append({QT_TRANSLATE_NOOP("CompositionModel", "Towards Active Camera"),
                      int(AutoOrient::TowardsActiveCamera)});
    if (!picture)
        modes.append({QT_TRANSLATE_NOOP("CompositionModel", "Towards Target Position"),
                      int(AutoOrient::TowardsTargetPosition)});
    if (threeD)
        modes.append({QT_TRANSLATE_NOOP("CompositionModel", "Towards Layer"), int(AutoOrient::TowardsLayer)});
    plugin::EffectParameterSpec modeSpec;
    modeSpec.type = QStringLiteral("choice");
    for (const auto& mode : modes) {
        modeSpec.choices << QCoreApplication::translate("CompositionModel", mode.first);
        modeSpec.choiceValues << QString::number(mode.second);
    }
    QVector<TimelinePropertyRow> rows = {
        {"AssetLayerGroupFactory", QT_TRANSLATE_NOOP("AssetLayerGroupFactory", "Alignment"), "alignment", modeSpec,
         [](const composition::Layer& target, int) { return QVariant(QString::number(int(target.autoOrient))); },
         [](composition::Layer& target, const QVariant& value, int) { target.autoOrient = AutoOrient(value.toInt()); },
         QCoreApplication::translate("DataUtilities", "Set Layer Alignment"), QString(), true},
    };
    if (layer.autoOrient == AutoOrient::AlongMotionPath) {
        plugin::EffectParameterSpec axisSpec;
        axisSpec.type = QStringLiteral("choice");
        for (int axis = 1; axis <= (threeD ? 3 : 2); ++axis) {
            axisSpec.choices << QCoreApplication::translate("CompositionModel", axis == 1 ? "X" : axis == 2 ? "Y" : "Z");
            axisSpec.choiceValues << QString::number(axis);
        }
        rows.append({"AssetLayerGroupFactory", QT_TRANSLATE_NOOP("AssetLayerGroupFactory", "Alignment Axis"),
                     "alignmentAxis", axisSpec,
                     [](const composition::Layer& target, int) { return QVariant(QString::number(target.autoOrientAxis)); },
                     [](composition::Layer& target, const QVariant& value, int) {
                         target.autoOrientAxis = qBound(1, value.toInt(), 3);
                     },
                     QCoreApplication::translate("DataUtilities", "Set Layer Alignment Axis")});
    }
    if (layer.autoOrient == AutoOrient::TowardsLayer) {
        // The other layers of the shot, by name.
        plugin::EffectParameterSpec layerSpec;
        layerSpec.type = QStringLiteral("choice");
        layerSpec.choices << QCoreApplication::translate("CompositionModel", "None");
        layerSpec.choiceValues << QString();
        for (const composition::Layer& other : m_comp->layers()) {
            if (other.id == layer.id) continue;
            layerSpec.choices << other.name;
            layerSpec.choiceValues << other.id.value();
        }
        rows.append({"AssetLayerGroupFactory", QT_TRANSLATE_NOOP("AssetLayerGroupFactory", "Alignment Layer"),
                     "alignmentLayer", layerSpec,
                     [](const composition::Layer& target, int) { return QVariant(target.autoOrientLayer.value()); },
                     [](composition::Layer& target, const QVariant& value, int) {
                         target.autoOrientLayer = core::Identifier(value.toString());
                     },
                     QCoreApplication::translate("DataUtilities", "Set Layer Alignment Layer")});
    }
    addPropertyRows(l, parent, rows, QStringLiteral("timelineAlignment_"));
}

void TimelineWidget::addPropertyRows(int l, QTreeWidgetItem* parent, const QVector<TimelinePropertyRow>& rows,
                                     const QString& prefix)
{
    const composition::Layer& layer = m_comp->layers()[l];
    const int generation = m_treeGeneration;
    for (const TimelinePropertyRow& property : rows) {
        auto* item = new QTreeWidgetItem(parent, {QCoreApplication::translate(property.context, property.label)});
        item->setData(0, Qt::UserRole, l);
        if (!property.keyed.isEmpty()) {
            // Its key button, full while it has keys; its lane shows them.
            item->setData(0, kLayerPropertyRole, property.keyed);
            const composition::KeyedLayerProperty keyed =
                composition::keyedLayerProperty(m_comp->layerRef(l), property.keyed);
            item->setIcon(0, QIcon(keyed.curve && !keyed.curve->isEmpty() ? ":/icons/key-frame-full.svg"
                                                                          : ":/icons/key-frame-off.svg"));
            item->setToolTip(0, tr("Toggle keyframe at the playhead"));
        }
        plugin::EffectParameterSpec spec = property.spec;
        spec.displayName = item->text(0);
        const auto get = property.get;
        const auto set = property.set;
        const auto read = [this, l, get]() -> QVariant {
            if (!m_comp || l >= m_comp->layers().size()) return QVariant();
            return get(m_comp->layers()[l], currentFrame());
        };
        const QString title = property.title.isEmpty() ? tr("Set %1").arg(item->text(0)) : property.title;
        const bool rebuild = property.rebuild;
        const auto write = [this, l, read, set, generation, title, rebuild](QVariant value) {
            if (m_rebuilding || generation != m_treeGeneration || !m_comp || l >= m_comp->layers().size()) return;
            if (read().toString() == value.toString() || m_comp->layers()[l].locked) return;
            const int frame = currentFrame();
            editLayer(l, title, [set, value, frame](composition::Layer& target) { set(target, value, frame); },
                      rebuild);
        };
        QWidget* editor = timelineParameterEditor(m_tree, spec, prefix + QStringLiteral("%1_%2").arg(l).arg(property.key),
                                                  read, write, m_valueReaders);
        editor->setEnabled(!layer.locked);
        m_tree->setItemWidget(item, 1, editor);
    }
}

void TimelineWidget::addMaterialRows(int l, const model3d::Mesh& mesh, QTreeWidgetItem* parent)
{
    // Override Materials (the reference's Model3DFactory group, a
    // Model3DMaterialOverride per material): where each of a material's maps
    // comes from - the model's file, nowhere, or a layer's picture - and how
    // strongly the bump map turns the surface.
    const composition::Layer& layer = m_comp->layers()[l];
    using Override = composition::ModelMaterialOverride;
    using Source = Override::MapSource;
    struct MapRows { Override::Map map; const char* source; const char* layer; };
    static const MapRows kMaps[] = {
        {Override::Map::Diffuse, QT_TRANSLATE_NOOP("Model3DFactory", "Diffuse Map Source"),
         QT_TRANSLATE_NOOP("Model3DFactory", "Diffuse Map Layer")},
        {Override::Map::Specular, QT_TRANSLATE_NOOP("Model3DFactory", "Specular Map Source"),
         QT_TRANSLATE_NOOP("Model3DFactory", "Specular Map Layer")},
        {Override::Map::Normal, QT_TRANSLATE_NOOP("Model3DFactory", "Normal Map Source"),
         QT_TRANSLATE_NOOP("Model3DFactory", "Normal Map Layer")},
        {Override::Map::Bump, QT_TRANSLATE_NOOP("Model3DFactory", "Bump Map Source"),
         QT_TRANSLATE_NOOP("Model3DFactory", "Bump Map Layer")},
        {Override::Map::Emissive, QT_TRANSLATE_NOOP("Model3DFactory", "Emissive Map Source"),
         QT_TRANSLATE_NOOP("Model3DFactory", "Emissive Map Layer")},
        {Override::Map::Occlusion, QT_TRANSLATE_NOOP("Model3DFactory", "Occlusion Map Source"),
         QT_TRANSLATE_NOOP("Model3DFactory", "Occlusion Map Layer")},
        {Override::Map::Metallic, QT_TRANSLATE_NOOP("Model3DFactory", "Metallic Map Source"),
         QT_TRANSLATE_NOOP("Model3DFactory", "Metallic Map Layer")},
        {Override::Map::Roughness, QT_TRANSLATE_NOOP("Model3DFactory", "Roughness Map Source"),
         QT_TRANSLATE_NOOP("Model3DFactory", "Roughness Map Layer")},
        {Override::Map::SpecularGlossiness, QT_TRANSLATE_NOOP("Model3DFactory", "Specular-Glossiness Map Source"),
         QT_TRANSLATE_NOOP("Model3DFactory", "Specular-Glossiness Map Layer")},
    };
    for (int m = 0; m < mesh.materials.size(); ++m) {
        const QString name = mesh.materials.at(m).name;
        auto* row = new QTreeWidgetItem(parent, {name.isEmpty()
            ? QCoreApplication::translate("AssetLayerGroupFactory", "Material") : name});
        row->setData(0, Qt::UserRole, l);
        const Override* current = layer.materialOverride(name);
        const QString title = QCoreApplication::translate("DataUtilities", "Set 3D Model Material Override");
        const auto change = [this, l, name, title](const std::function<void(Override&)>& set) {
            editLayer(l, title, [name, set](composition::Layer& target) {
                Override* entry = target.materialOverride(name);
                if (!entry) {
                    target.materialOverrides.append(Override());
                    entry = &target.materialOverrides.last();
                    entry->material = name;
                }
                set(*entry);
            }, true);
        };
        for (const MapRows& rows : kMaps) {
            const Override::MapOverride state = current ? current->map(rows.map) : Override::MapOverride();
            const QString key = QString::fromLatin1(Override::mapKey(rows.map));
            auto* sourceRow = new QTreeWidgetItem(
                row, {QCoreApplication::translate("Model3DFactory", rows.source)});
            sourceRow->setData(0, Qt::UserRole, l);
            auto* sources = new QComboBox(m_tree);
            sources->setObjectName(QStringLiteral("timelineMaterialSource_%1_%2_%3").arg(l).arg(m).arg(key));
            sources->addItem(QCoreApplication::translate("Model3DFactory", "None"), int(Source::None));
            sources->addItem(QCoreApplication::translate("Model3DFactory", "From File"), int(Source::FromFile));
            sources->addItem(QCoreApplication::translate("Model3DFactory", "Use Layer"), int(Source::UseLayer));
            sources->setCurrentIndex(sources->findData(int(state.source)));
            sources->setEnabled(!layer.locked);
            m_tree->setItemWidget(sourceRow, 1, sources);
            const Override::Map map = rows.map;
            connect(sources, &QComboBox::activated, this, [sources, change, map](int index) {
                const Source chosen = Source(sources->itemData(index).toInt());
                change([chosen, map](Override& entry) { entry.map(map).source = chosen; });
            });

            auto* layerRow = new QTreeWidgetItem(
                row, {QCoreApplication::translate("Model3DFactory", rows.layer)});
            layerRow->setData(0, Qt::UserRole, l);
            auto* layers = new QComboBox(m_tree);
            layers->setObjectName(QStringLiteral("timelineMaterialLayer_%1_%2_%3").arg(l).arg(m).arg(key));
            layers->addItem(QCoreApplication::translate("Model3DFactory", "None"), QString());
            for (const composition::Layer& candidate : m_comp->layers()) {
                // A model's picture cannot be a map of a model.
                if (candidate.id == layer.id || candidate.kind == composition::LayerKind::Model3D) continue;
                layers->addItem(candidate.name, candidate.id.value());
            }
            layers->setCurrentIndex(qMax(0, layers->findData(state.layer.value())));
            layers->setEnabled(!layer.locked && state.source == Source::UseLayer);
            m_tree->setItemWidget(layerRow, 1, layers);
            connect(layers, &QComboBox::activated, this, [layers, change, map](int index) {
                const core::Identifier id(layers->itemData(index).toString());
                change([id, map](Override& entry) { entry.map(map).layer = id; });
            });
            if (rows.map != Override::Map::Bump) continue;
            auto* scaleRow = new QTreeWidgetItem(
                row, {QCoreApplication::translate("Model3DFactory", "Bump Map Scale")});
            scaleRow->setData(0, Qt::UserRole, l);
            auto* scale = new QDoubleSpinBox(m_tree);
            scale->setObjectName(QStringLiteral("timelineMaterialBumpScale_%1_%2").arg(l).arg(m));
            scale->setRange(0.0, 100.0); scale->setDecimals(2); scale->setSingleStep(0.1);
            scale->setValue(current ? current->bumpMapScale : 1.0);
            scale->setEnabled(!layer.locked);
            m_tree->setItemWidget(scaleRow, 1, scale);
            connect(scale, &QDoubleSpinBox::editingFinished, this, [scale, change] {
                const double value = scale->value();
                change([value](Override& entry) { entry.bumpMapScale = value; });
            });
        }
    }
}

bool TimelineWidget::insertModelAnimation(int l, const QString& filePath, int index)
{
    if (!m_comp || l < 0 || l >= m_comp->layers().size()) return false;
    const composition::Layer& layer = m_comp->layers()[l];
    if (layer.kind != composition::LayerKind::Model3D || layer.locked) return false;
    const QString modelFile = m_media ? m_media->assetById(layer.modelAssetId).sourcePath() : QString();
    const bool own = filePath.isEmpty()
        || QFileInfo(filePath).absoluteFilePath() == QFileInfo(modelFile).absoluteFilePath();
    QVector<model3d::ModelAnimation> available;
    if (own && m_media) {
        available = m_media->modelMesh(layer.modelAssetId).animations;
    } else if (const auto source = model3d::animationSource(filePath)) {
        available = source->animations;
    }
    if (index < 0 || index >= available.size()) return false;
    composition::ModelAnimationInstance instance;
    instance.name = available.at(index).name;
    instance.filePath = own ? QString() : QFileInfo(filePath).absoluteFilePath();
    instance.animationIndex = index;
    editLayer(l, QCoreApplication::translate("QObject", "Insert 3D Model Animation Instance"),
              [instance](composition::Layer& target) { target.modelAnimations.append(instance); }, true);
    return true;
}

void TimelineWidget::showModelAnimationMenu(int l, QWidget* anchor)
{
    if (!m_comp || l < 0 || l >= m_comp->layers().size()) return;
    const composition::Layer& layer = m_comp->layers()[l];
    QMenu menu(this);
    menu.setObjectName(QStringLiteral("timelineModelAnimationMenu"));
    if (m_media) {
        const QVector<model3d::ModelAnimation> own = m_media->modelMesh(layer.modelAssetId).animations;
        for (int a = 0; a < own.size(); ++a) {
            menu.addAction(own.at(a).name, this, [this, l, a] { insertModelAnimation(l, QString(), a); });
        }
        if (!own.isEmpty()) menu.addSeparator();
    }
    const QString modelFile = m_media ? m_media->assetById(layer.modelAssetId).sourcePath() : QString();
    menu.addAction(QCoreApplication::translate("biff::ui::media::MediaActions", "3D Model Animation..."),
                   this, [this, l, modelFile] {
        const QString chosen = QFileDialog::getOpenFileName(
            this, QCoreApplication::translate("biff::ui::media::MediaActions", "Import 3D Model Animation"),
            QFileInfo(modelFile).absolutePath(), model3d::animationFileFilter());
        if (chosen.isEmpty()) return;
        if (m_media && m_media->importModelAnimation(chosen).isSuccess()) emit modelAnimationImported();
        if (!insertModelAnimation(l, chosen, 0)) {
            QMessageBox::warning(this, QCoreApplication::translate("biff::ui::media::MediaActions",
                                                                   "Import 3D Model Animation"),
                                 QCoreApplication::translate("AnimationSettingsDialog",
                                                             "The animation could not be imported."));
        }
    });
    menu.exec(anchor ? anchor->mapToGlobal(QPoint(0, anchor->height())) : QCursor::pos());
}

void TimelineWidget::addModelAnimationRows(int l, QTreeWidgetItem* parent)
{
    // Each instance as the reference's Model3DFactory shows it - Strength,
    // Loop, Custom Playback and Playback - with its properties
    // (AnimationSettingsDialog) and its removal on the instance's row.
    const composition::Layer& layer = m_comp->layers()[l];
    const QString modelFile = m_media ? m_media->assetById(layer.modelAssetId).sourcePath() : QString();
    for (int a = 0; a < layer.modelAnimations.size(); ++a) {
        const composition::ModelAnimationInstance& instance = layer.modelAnimations.at(a);
        const core::Identifier id = instance.id;
        auto* row = new QTreeWidgetItem(parent, {instance.name.isEmpty()
            ? QCoreApplication::translate("DataUtilities", "3D Model Animation Instance") : instance.name});
        row->setData(0, Qt::UserRole, l);
        auto* controls = new QWidget(m_tree);
        auto* cl = new QHBoxLayout(controls); cl->setContentsMargins(0, 0, 0, 0); cl->setSpacing(3);
        cl->addStretch();
        auto* properties = iconButton(controls, QStringLiteral("timelineModelAnimationProperties_%1_%2").arg(l).arg(a),
                                      "settings", QCoreApplication::translate("Model3DAnimationAssetTreeWidget", "Show Properties"));
        properties->setEnabled(!layer.locked); cl->addWidget(properties);
        auto* remove = iconButton(controls, QStringLiteral("timelineModelAnimationRemove_%1_%2").arg(l).arg(a),
                                  "tab-close", QCoreApplication::translate("QObject", "Remove 3D Model Animation Instance"));
        remove->setEnabled(!layer.locked); cl->addWidget(remove);
        m_tree->setItemWidget(row, 1, controls);
        const auto change = [this, l, id](const QString& title,
                                          const std::function<void(composition::ModelAnimationInstance&)>& set,
                                          bool rebuild) {
            editLayer(l, title, [id, set](composition::Layer& target) {
                for (composition::ModelAnimationInstance& entry : target.modelAnimations)
                    if (entry.id == id) set(entry);
            }, rebuild);
        };
        const QString setProperty = QCoreApplication::translate("QObject", "Set Property");
        connect(remove, &QToolButton::clicked, this, [this, l, id] {
            editLayer(l, QCoreApplication::translate("QObject", "Remove 3D Model Animation Instance"),
                      [id](composition::Layer& target) {
                for (int k = 0; k < target.modelAnimations.size(); ++k)
                    if (target.modelAnimations.at(k).id == id) { target.modelAnimations.removeAt(k); break; }
            }, true);
        });
        connect(properties, &QToolButton::clicked, this, [this, instance, modelFile, change] {
            ModelAnimationDialog dialog(instance, modelFile, this);
            if (dialog.exec() != QDialog::Accepted) return;
            const composition::ModelAnimationInstance edited = dialog.instance();
            change(QCoreApplication::translate("QObject", "Edit 3D Model Animation Asset"),
                   [edited](composition::ModelAnimationInstance& target) {
                target.name = edited.name;
                target.filePath = edited.filePath;
                target.animationIndex = edited.animationIndex;
                target.frameRateOverridden = edited.frameRateOverridden;
                target.frameRate = edited.frameRate;
            }, true);
        });

        // A value that has keys is set at the playhead; otherwise its own.
        const auto keyed = [this](composition::KeyFrameList& curve, double& value, double next) {
            if (curve.isEmpty()) { value = next; return; }
            const double fps = m_comp && m_comp->fpsDenominator() > 0
                ? double(m_comp->fpsNumerator()) / m_comp->fpsDenominator() : 30.0;
            curve.set(qRound(playhead() * fps), next);
        };
        const auto addRow = [&](const char* label, QWidget* editor, int keyed = -1) {
            auto* line = new QTreeWidgetItem(row, {QCoreApplication::translate("Model3DFactory", label)});
            line->setData(0, Qt::UserRole, l);
            if (keyed >= 0) {
                // Strength and Playback animate: their keys show on the row's
                // lane, and its key button sets one at the playhead.
                line->setData(0, kModelAnimationRole, id.value());
                line->setData(0, kModelAnimationPropertyRole, keyed);
                const composition::KeyFrameList& curve = keyed == int(ModelAnimationProperty::Playback)
                    ? instance.playbackCurve : instance.strengthCurve;
                line->setIcon(0, QIcon(curve.isEmpty() ? ":/icons/key-frame-off.svg" : ":/icons/key-frame-full.svg"));
            }
            editor->setEnabled(editor->isEnabled() && !layer.locked);
            m_tree->setItemWidget(line, 1, editor);
        };
        auto* strength = new QDoubleSpinBox(m_tree);
        strength->setObjectName(QStringLiteral("timelineModelAnimationStrength_%1_%2").arg(l).arg(a));
        strength->setRange(0.0, 100.0); strength->setDecimals(1); strength->setSuffix(QStringLiteral(" %"));
        strength->setValue(instance.strength);
        addRow(QT_TRANSLATE_NOOP("Model3DFactory", "Strength"), strength, int(ModelAnimationProperty::Strength));
        connect(strength, &QDoubleSpinBox::editingFinished, this, [strength, change, setProperty, keyed] {
            const double value = strength->value();
            change(setProperty, [value, keyed](composition::ModelAnimationInstance& target) {
                keyed(target.strengthCurve, target.strength, value);
            }, false);
        });
        auto* loop = new QCheckBox(m_tree);
        loop->setObjectName(QStringLiteral("timelineModelAnimationLoop_%1_%2").arg(l).arg(a));
        loop->setChecked(instance.loop);
        addRow(QT_TRANSLATE_NOOP("Model3DFactory", "Loop"), loop);
        connect(loop, &QCheckBox::toggled, this, [change, setProperty](bool on) {
            change(setProperty, [on](composition::ModelAnimationInstance& target) { target.loop = on; }, false);
        });
        auto* custom = new QCheckBox(m_tree);
        custom->setObjectName(QStringLiteral("timelineModelAnimationCustomPlayback_%1_%2").arg(l).arg(a));
        custom->setChecked(instance.customPlayback);
        addRow(QT_TRANSLATE_NOOP("Model3DFactory", "Custom Playback"), custom);
        connect(custom, &QCheckBox::toggled, this, [change, setProperty](bool on) {
            change(setProperty, [on](composition::ModelAnimationInstance& target) { target.customPlayback = on; }, true);
        });
        auto* playback = new QDoubleSpinBox(m_tree);
        playback->setObjectName(QStringLiteral("timelineModelAnimationPlayback_%1_%2").arg(l).arg(a));
        playback->setRange(0.0, 1e6); playback->setDecimals(2);
        playback->setValue(instance.playback);
        playback->setEnabled(instance.customPlayback);
        addRow(QT_TRANSLATE_NOOP("Model3DFactory", "Playback"), playback, int(ModelAnimationProperty::Playback));
        connect(playback, &QDoubleSpinBox::editingFinished, this, [playback, change, setProperty, keyed] {
            const double value = playback->value();
            change(setProperty, [value, keyed](composition::ModelAnimationInstance& target) {
                keyed(target.playbackCurve, target.playback, value);
            }, false);
        });
        m_valueReaders.append([this, l, id, strength, playback] {
            if (!m_comp || l >= m_comp->layers().size()) return;
            const double fps = m_comp->fpsDenominator() > 0
                ? double(m_comp->fpsNumerator()) / m_comp->fpsDenominator() : 30.0;
            const int frame = qRound(playhead() * fps);
            for (const composition::ModelAnimationInstance& entry : m_comp->layers()[l].modelAnimations) {
                if (entry.id != id) continue;
                const QSignalBlocker bs(strength), bp(playback);
                if (!strength->hasFocus()) strength->setValue(entry.strengthAt(frame));
                if (!playback->hasFocus()) playback->setValue(entry.playbackAt(frame));
            }
        });
        row->setExpanded(true);
    }
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
        // The controls the module shows for its values, as in Controls
        // (EffectInspector::refreshModuleControls).
        const QString instanceKey =
            QStringLiteral("%1/%2/%3").arg(m_comp->layers()[l].id.value()).arg(c).arg(e);
        if (plugin::isNativeModule(effect.pluginId))
            plugin::refreshNativeControlStates(effect.pluginId, effect.parameterValues, instanceKey);
        const int count = qMax(spec.parameters.size(), effect.parameterValues.size());
        for (int p = 0; p < count; ++p) {
            const auto param = spec.parameters.value(p);
            if (!plugin::nativeControlShown(instanceKey, param.name, !param.hidden)) continue;
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
        // The reference lists an effect collapsed under its open group; its
        // parameters unfold on demand (and stay as the user left them).
        effectRow->setExpanded(false);
    }
}
}
