#include "ui/Theme.h"
#include "ui/TimelineRowDelegate.h"
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
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
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
        // The reference lists an effect collapsed under its open group; its
        // parameters unfold on demand (and stay as the user left them).
        effectRow->setExpanded(false);
    }
}
}
