#pragma once

#include <QVector3D>
#include <QDockWidget>
#include <QTreeWidget>
#include <QElapsedTimer>
#include <QVector>
#include <functional>

#include <memory>

#include "composition/Composition.h"
#include "ui/CompositionSettingsDialog.h"
#include "plugin/EffectSpec.h"

class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QResizeEvent;
class QSpacerItem;
class QStackedWidget;
class QTabBar;
class QToolButton;
class QVBoxLayout;
class QMouseEvent;
class QPainter;
class QScrollBar;
class QSplitter;
class QTreeWidgetItem;
class QUndoStack;
class QSlider;

namespace openvegas {
namespace media {
class MediaManager;
class WaveformCache;
}
namespace plugin {
class PluginManager;
}
namespace model3d {
struct Mesh;
}
namespace ui {

// Tree rows of a model node (its row and its Transform rows) carry the
// node's path (model3d::Mesh::nodePath) under this role.
constexpr int kModelNodeRole = Qt::UserRole + 6;
// A row of a model animation's keyed property: the instance's id, and which
// property (ModelAnimationProperty).
constexpr int kModelAnimationRole = Qt::UserRole + 8;
constexpr int kModelAnimationPropertyRole = Qt::UserRole + 9;
// A row of a light's, a camera lens's or a Material's property that
// animates: the reference's name of it (composition::keyedLayerProperty).
constexpr int kLayerPropertyRole = Qt::UserRole + 10;
// The keyed properties of a model animation instance, as its rows show them.
enum class ModelAnimationProperty { Strength = 0, Playback = 1 };

// A property row of a layer group (TimelineTreeRows.cpp).
struct TimelinePropertyRow;

// One horizontal lane of the track area, matching a visible row of the layer
// tree so the two halves line up the way the reference Editor does.
struct TimelineLane
{
    int y = 0;            // viewport-relative top edge
    int height = 0;
    int layerIndex = -1;  // >= 0 only for the layer's own row

    // Set for a parameter row, so the lane can show its keyframes the way the
    // reference draws diamonds beside each animated property.
    int clipIndex = -1;
    int effectIndex = -1;
    int parameterIndex = -1;

    // Transform property of the layer (composition::TransformProperty cast to
    // int), for the rows under Transform. Those keyframes live on the layer's
    // LayerTransform, not on an effect, so they need their own marker.
    int transformProp = -1;
    // For a Transform row of a model node: the node's path. Its keys are on
    // the node's transform, not the layer's.
    QString modelNode;
    // For a Strength or Playback row of a model animation: the instance's
    // id and the property (ModelAnimationProperty cast to int).
    QString modelAnimation;
    int animationProperty = -1;
    // For a row of a light's, a lens's or a Material's property: its name.
    QString layerProperty;

    bool isParameterRow() const { return parameterIndex >= 0; }
    bool isTransformRow() const { return transformProp >= 0; }
    // A row with one curve of its own: a model animation's or a layer
    // property's.
    bool isAnimationRow() const { return animationProperty >= 0 || !layerProperty.isEmpty(); }
};

// The curve of a model animation's property: its Strength or Playback keys.
inline composition::KeyFrameList* modelAnimationCurve(composition::Layer& layer, const QString& instance,
                                                     int property)
{
    for (composition::ModelAnimationInstance& animation : layer.modelAnimations) {
        if (animation.id.value() != instance) continue;
        return property == int(ModelAnimationProperty::Playback) ? &animation.playbackCurve
                                                                 : &animation.strengthCurve;
    }
    return nullptr;
}
inline const composition::KeyFrameList* modelAnimationCurve(const composition::Layer& layer,
                                                           const QString& instance, int property)
{
    return modelAnimationCurve(const_cast<composition::Layer&>(layer), instance, property);
}
// The one curve of an animation row: a model animation's Strength or
// Playback, or a light's, a lens's or a Material's property.
inline composition::KeyFrameList* laneAnimationCurve(composition::Layer& layer, const TimelineLane& lane)
{
    if (!lane.layerProperty.isEmpty()) return composition::keyedLayerProperty(layer, lane.layerProperty).curve;
    return modelAnimationCurve(layer, lane.modelAnimation, lane.animationProperty);
}
inline const composition::KeyFrameList* laneAnimationCurve(const composition::Layer& layer,
                                                          const TimelineLane& lane)
{
    return laneAnimationCurve(const_cast<composition::Layer&>(layer), lane);
}

// The transform a lane's keys are on - its layer's, or a model node's (null
// while the node has none) - and the dimension its axes follow.
inline const composition::LayerTransform* laneTransform(const composition::Layer& layer,
                                                        const TimelineLane& lane)
{
    if (lane.modelNode.isEmpty()) return &layer.transform;
    const composition::ModelNodeTransform* node = layer.modelNode(lane.modelNode);
    return node ? &node->transform : nullptr;
}
inline composition::LayerTransform* laneTransform(composition::Layer& layer, const TimelineLane& lane)
{
    if (lane.modelNode.isEmpty()) return &layer.transform;
    composition::ModelNodeTransform* node = layer.modelNode(lane.modelNode);
    return node ? &node->transform : nullptr;
}
inline composition::LayerDimension laneDimension(const composition::Layer& layer,
                                                 const TimelineLane& lane)
{
    return lane.modelNode.isEmpty() ? layer.dimension : composition::LayerDimension::ThreeD;
}
inline const composition::KeyFrameList* laneCurve(const composition::Layer& layer, const TimelineLane& lane,
                                                  composition::TransformProperty prop, int axis)
{
    const composition::LayerTransform* transform = laneTransform(layer, lane);
    return transform ? transform->curve(prop, axis) : nullptr;
}
inline composition::KeyFrameList* laneCurve(composition::Layer& layer, const TimelineLane& lane,
                                            composition::TransformProperty prop, int axis)
{
    composition::LayerTransform* transform = laneTransform(layer, lane);
    return transform ? transform->curve(prop, axis) : nullptr;
}

// Height of the time ruler across the top of the track canvas. The value
// graph draws the same ruler, so both pages of the stack line up on time.
constexpr int kTimelineRulerHeight = 30;

// Animation curves a lane stands for: one per axis for a Transform row, the
// single parameter curve for an effect parameter row, and none for a layer or
// group row. Both the track canvas and the value graph resolve a row through
// this, so a lane always means the same thing in either view.
QVector<composition::KeyFrameList*> laneCurves(composition::Composition* comp,
                                               const TimelineLane& lane);

class TimelineValueGraphView;

// Editor tools of the timeline's left strip, with the reference's own command
// ids from its shortcut table (category 3, "Editor sequence timeline"). Order
// is the reference's own: Select, Hand, Slice, Slip, Slide, Ripple, Roll,
// Rate Stretch, Track Select.
enum class EditorTool
{
    Select,       // 2005, V
    Hand,         // 2006, H
    Slice,        // 2008, C
    Slip,         // 2009, Y
    Slide,        // 2010, Shift+U
    Ripple,       // 2011, R
    Roll,         // 2012, E
    Stretch,      // 2014, S  - "Rate Stretch Tool"
    TrackSelect,  // 2015, A  - "Track Select Forwards Tool"
};

// Renders the timeline ruler + layer tracks (independent of the header bar).
class TimelineCanvas : public QWidget
{
    Q_OBJECT

public:
    explicit TimelineCanvas(QWidget* parent = nullptr);

    void setComposition(std::shared_ptr<composition::Composition> comp);
    void setPlayheadPosition(double timeSeconds);
    double pixelsPerSecond() const;
    double zoom() const { return m_zoom; }

    // Full pixel width of the timeline content (duration x pixels-per-second),
    // used to size the horizontal scroll bar.
    double contentWidth() const;

    // Current horizontal scroll offset, in content pixels.
    int hScrollOffset() const { return m_hOffset; }

    // Keeps a given content X under the same screen X while the zoom changes,
    // so zooming stays anchored to the playhead (or the mouse cursor).
    void setZoom(double factor, int anchorContentX = -1);
    // Zooms so the whole composition fits the visible width - what the
    // reference's magnifier beside Value Graph does.
    void zoomToFit();

    double playhead() const { return m_playhead; }

    // Lanes come from the layer tree; the canvas draws one strip per visible
    // tree row so an expanded effect pushes the track rows down in step.
    void setLanes(const QVector<TimelineLane>& lanes);
    void setSelection(int layer, int clip) { m_selLayer = layer; m_selClip = clip; update(); }
    // Media lookup and peak cache for the waveforms drawn inside audio and
    // video clips; without them clips are drawn plain.
    void setWaveformSource(std::shared_ptr<media::MediaManager> media,
                           media::WaveformCache* cache);

    // Scroll offset in content pixels, clamped to the scroll bar's range.
    void setScrollOffset(int contentX);

    // Active editor tool. This is what decides what a drag on a clip means:
    // without it the strip's buttons only lit up, and every tool behaved like
    // Select.
    void setTool(EditorTool tool);
    EditorTool tool() const { return m_tool; }

    // Snapping of clip edges to the playhead and to neighbouring clip edges.
    void setSnapEnabled(bool on) { m_snap = on; }
    bool snapEnabled() const { return m_snap; }

signals:
    void editStarted();
    void verticalScrollRequested(int delta);
    void keySelected(const TimelineLane& lane, int frame);
    void timeScrubbed(double timeSeconds);
    // A tool changed the clips: their times, lengths or count. The widget
    // rebuilds its rows and the project is marked dirty.
    void clipsEdited();
    // Zoom or horizontal scroll changed. The value graph is a separate page of
    // the same stack and has to follow the track view's time axis exactly, so
    // it listens for this rather than keeping a time axis of its own.
    void viewportChanged();
    void clipSelected(int layerIndex, int clipIndex);
    // Live during a drag: the key has a new frame, but the row structure has
    // not changed, so listeners should only re-render.
    void keyFrameDragged();
    // The drag finished - a good moment to rebuild the rows and their values.
    void keyFramesEdited();
    // An Effects panel row was dropped on a clip at a composition time.
    void effectDropped(int layerIndex, int clipIndex, const QString& pluginId, double seconds);
    // A Media panel row dropped at a composition time, above the layer it
    // was let go over (-1: on top).
    void mediaDropped(const QString& filePath, int layerIndex, double seconds);
    void compositeShotDropped(const QString& shotId, int layerIndex, double seconds);
    // Make (or remove) the pre-render of a composite-shot clip's shot.
    void shotPreRenderRequested(int layerIndex, int clipIndex, bool make);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void pickClipAt(const QPoint& pos);
    void drawClipWaveform(QPainter& painter, const composition::Clip& clip, const QRect& rect,
                          double pps);
    // A transition window edge that sets its length (not one shared with a
    // clip edge, which stays a trim handle).
    bool transitionHandleAt(const QPoint& pos, int* layerIndex, int* clipIndex,
                            int* effectIndex) const;
    // Resizes the dragged transition so its moving edge sits at `seconds`.
    bool dragTransitionTo(double seconds);

    // Which part of which clip is under a point. `edge` says whether the
    // pointer is over the clip's left or right trim handle, which is what
    // separates a move from a trim and a roll from a slide.
    enum class ClipEdge
    {
        None,
        Left,
        Right,
    };
    bool clipAt(const QPoint& pos, int* layerIndex, int* clipIndex, ClipEdge* edge) const;
    // The bar of a layer without clips (its span, Layer::spanStartSeconds)
    // under a point, and whether the pointer is on one of its ends.
    bool spanAt(const QPoint& pos, int* layerIndex, ClipEdge* edge) const;
    // Moves the dragged span (its body grabbed) or trims it (an end) by
    // `deltaSeconds` from where the drag started.
    bool applySpanDrag(double deltaSeconds);
    // Screen x of a time, and the time at a screen x - one place, so painting,
    // hit-testing and the tools cannot drift apart.
    double timeAtX(int widgetX) const;
    int xForTime(double seconds) const;
    // Nearest interesting time to `seconds` when snapping is on: the playhead
    // or any clip edge within a few pixels. Returns `seconds` unchanged when
    // snapping is off or nothing is close.
    double snapTime(double seconds, int ignoreLayer, int ignoreClip) const;
    // Applies the active tool to a drag that has moved `deltaSeconds` from
    // where it started. Returns true when the model changed.
    bool applyToolDrag(double deltaSeconds);
    // Captures the geometry a drag starts from, for the clip and its neighbours.
    void beginClipDrag(int layerIndex, int clipIndex, ClipEdge edge, double pressTime);
    // Cuts the clip under the pointer in two at the time clicked.
    bool sliceClipAt(const QPoint& pos);
    // Track Select Forwards: picks the first clip at or after the click.
    void selectFromHere(const QPoint& pos);
    void updateToolCursor(const QPoint& pos);
    // Keyframe under the cursor, if any. Reports which lane it belongs to and
    // the frame it currently sits on.
    bool keyFrameAt(const QPoint& pos, int* laneIndex, int* frame) const;
    // Frames carrying a key on this lane: one curve for an effect parameter,
    // the union of both axes for a point-valued transform property.
    QVector<int> keyFrameLocations(const TimelineLane& lane) const;
    // Moves every key the lane shows at fromFrame onto toFrame. Refuses when a
    // different key already occupies the target, so a drag cannot silently
    // swallow one.
    bool moveKeyFrames(const TimelineLane& lane, int fromFrame, int toFrame);
    // Interpolation type for every key the lane shows at this frame. Mirrors
    // the reference's per-type keyframe buttons (toolButtonKeyFrameType*).
    bool setKeyFrameType(const TimelineLane& lane, int frame, composition::TemporalType type);
    // Type of the key this lane shows at a frame, for drawing its marker.
    composition::TemporalType keyFrameTypeAt(const TimelineLane& lane, int frame) const;
    int frameAtContentX(double contentX) const;
    double frameRate() const;
    QString formatTimecode(double seconds) const;
    int usableHeight() const;
    void layoutScrollbar();
    void syncScrollbar();
    void scrollTo(int value);
    void ensurePlayheadVisible();

    std::shared_ptr<media::MediaManager> m_media;
    media::WaveformCache* m_waveforms = nullptr;
    // Transition whose length is being dragged (-1 when none).
    int m_transitionLayer = -1;
    int m_transitionClip = -1;
    int m_transitionEffect = -1;
    std::shared_ptr<composition::Composition> m_comp;
    QVector<TimelineLane> m_lanes;
    double m_playhead = 0.0;
    double m_zoom = 1.0;
    int m_hOffset = 0;
    int m_selLayer = -1;
    int m_selClip = -1;
    // Keyframe drag in progress: the lane it belongs to and the frame it has
    // been dragged to so far (-1 when nothing is being dragged).
    int m_dragLane = -1;
    int m_dragFrame = -1;

    EditorTool m_tool = EditorTool::Select;
    bool m_snap = true;
    // Clip drag in progress: which clip, which edge was grabbed, where the
    // press was in time, and the clip's geometry when it started - every tool
    // works from the original values rather than accumulating rounding.
    int m_dragClipLayer = -1;
    int m_dragClipIndex = -1;
    ClipEdge m_dragEdge = ClipEdge::None;
    double m_dragStartTime = 0.0;
    double m_dragOriginalStart = 0.0;
    // Frames the dragged layer's keys have been moved by so far.
    int m_dragKeyShift = 0;
    // Span drag in progress: the layer, the end grabbed, and the span as it
    // was at the press (its end resolved, and whether it ran to the shot's).
    int m_dragSpanLayer = -1;
    ClipEdge m_dragSpanEdge = ClipEdge::None;
    double m_dragSpanStart = 0.0;
    double m_dragSpanEnd = 0.0;
    bool m_dragSpanOpen = false;
    double m_dragOriginalDuration = 0.0;
    double m_dragOriginalSource = 0.0;
    double m_dragOriginalSpeed = 1.0;
    // Neighbour geometry, for the tools that move two clips at once.
    double m_dragPrevStart = 0.0;
    double m_dragPrevDuration = 0.0;
    double m_dragNextStart = 0.0;
    double m_dragNextDuration = 0.0;
    double m_dragNextSource = 0.0;
    // Starts of every clip after the dragged one, as they were at the press:
    // Ripple offsets them from these rather than from where they are now.
    QVector<double> m_dragFollowStarts;
    // Hand tool: where the grab started, so the view follows the pointer
    // one-to-one instead of drifting.
    bool m_handPanning = false;
    int m_handAnchorX = 0;
    int m_handAnchorOffset = 0;
    QScrollBar* m_hScroll = nullptr;
    int m_scrollbarHeight = 0;
};

// QTreeWidget with a public top inset, so its first row can start below the
// canvas ruler and the two columns stay aligned.
class TimelineTree : public QTreeWidget
{
    Q_OBJECT

public:
    explicit TimelineTree(QWidget* parent = nullptr) : QTreeWidget(parent) {}

    void setTopInset(int px) { setViewportMargins(0, px, 0, 0); }

signals:
    void layoutShifted();
    // The reference puts a small round toggle left of an animatable property;
    // clicking it (rather than the row) is what arms the keyframe.
    void decorationClicked(QTreeWidgetItem* item);

protected:
    void mousePressEvent(QMouseEvent* event) override;

    void scrollContentsBy(int dx, int dy) override
    {
        QTreeWidget::scrollContentsBy(dx, dy);
        emit layoutShifted();
    }
    void resizeEvent(QResizeEvent* event) override
    {
        QTreeWidget::resizeEvent(event);
        emit layoutShifted();
    }
};

class TimelineWidget : public QDockWidget
{
    Q_OBJECT

public:
    explicit TimelineWidget(QWidget* parent = nullptr);

    void setComposition(std::shared_ptr<composition::Composition> comp);
    void setCompositionTabs(const QStringList& names, int currentIndex);
    // Start and open compositions occupy one footer strip in the reference.
    // The StartPanel remains the controller for its form, while its content is
    // hosted by this stack so QMainWindow does not draw a second dock tab bar.
    void setStartPage(QWidget* page);
    bool isStartPageVisible() const;
    // Selects the layer row when another panel (Track) changes the global
    // selection. The tree then emits the usual clipSelected signal, keeping
    // every inspector on the same path.
    void selectLayer(int layerIndex);
    // Makes a model node's row current (the layer's row for an empty path).
    void selectModelNode(int layerIndex, const QString& path);

    // Needed to resolve effect parameter names for the layer tree.
    void setPluginManager(plugin::PluginManager* pluginManager);

    // Needed to list a 3D model layer's nodes under its Models group.
    void setMediaManager(std::shared_ptr<media::MediaManager> media);
    void setUndoStack(QUndoStack* stack) { m_undoStack = stack; }

    // The tool set is declared above, beside TimelineCanvas, because the canvas
    // is what acts on it.
    EditorTool tool() const { return m_tool; }
    void setTool(EditorTool tool);

    // Snap is a timeline command in the reference (id 2007, Shift+S), not a
    // viewer one - it belongs beside the editor tools.
    bool snapEnabled() const { return m_snap; }
    void setSnapEnabled(bool enabled);

    void setPlayheadPosition(double timeSeconds);
    double playhead() const;
    void setZoom(double factor);
    double zoom() const;
    double pixelsPerSecond() const;

public slots:
    void showStartPage();
    void showTimelinePage();
    void zoomIn();
    void zoomOut();

    // Keyframe navigation from the toolbar. They act on the parameter row
    // selected in the tree; with none selected they consider every animated
    // parameter in the composition.
    void goToPreviousKeyFrame();
    void goToNextKeyFrame();
    void toggleSelectedKeyFrame();

    // Rebuilds the lane tree after something outside the timeline changed the
    // animation - the Controls panel arming a parameter, for instance. A plain
    // update() only repaints the track area, leaving the row's keyframe icon
    // showing the previous state.
    void refreshKeyFrames();
    // Drops the "New Layer" list, for the reference's Ctrl+Alt+N.
    void openNewLayerMenu();
    void editCompositionProperties();
    // The editor timeline's format, which Composite Shot Properties' Match
    // Timeline takes.
    void setMatchFormat(int width, int height, int fpsNumerator, int fpsDenominator);
    // Applies the Viewer transport's Timeline Duration edit through the same
    // composition command path as Composite Shot Properties.
    void setCompositionDuration(double seconds);
    void selectAllRows();
    void addMaskToLayer(int layerIndex, composition::MaskShape shape, const QRectF& bounds);
    // The row's 2D/3D toggle. Going 3D in a shot without a camera first asks
    // to add one (CameraRule); Cancel leaves the layer as it was.
    void setLayerDimension(int layerIndex, composition::LayerDimension dimension);
    void addFreehandMaskToLayer(int layerIndex, const QVector<QPointF>& points);
    void setMotionTrackData(int layerIndex, int trackIndex,
                            const composition::KeyFrameList& xCurve,
                            const composition::KeyFrameList& yCurve);

protected:
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* object, QEvent* event) override;

public:

signals:
    void timeScrubbed(double timeSeconds);
    void clipSelected(int layerIndex, int clipIndex);
    void layersSelected(const QVector<int>& layerIndices);
    // A row of a model node (or one of its Transform rows) became current:
    // that node's path; empty for any other row.
    void modelNodeSelected(int layerIndex, const QString& path);
    // Carries the kind chosen in the button's menu; the reference opens the
    // same list from its "Open New Layer Menu" command.
    void newLayerRequested(composition::LayerKind kind);
    void searchFilterChanged(const QString& filter);
    void keyFramesChanged();
    void toolChanged(EditorTool tool);
    void snapChanged(bool enabled);
    void valueGraphToggled(bool on);
    void exportRequested();
    void preRenderRequested();
    void compositionPropertiesChanged();
    void motionTrackingRequested(int layerIndex, int trackIndex);
    void makeCompositeShotRequested();
    // Make (or remove) the pre-render of a composite-shot clip's shot.
    void shotPreRenderRequested(int layerIndex, int clipIndex, bool make);
    void compositionTabActivated(int index);
    void compositionTabCloseRequested(int index);
    void mediaDropped(const QString& filePath, int layerIndex, double seconds);
    // A file's animations were taken into Media (the "+" menu's 3D Model
    // Animation..., as the reference makes a Model3DAnimationAsset of it).
    void modelAnimationImported();
    void compositeShotDropped(const QString& shotId, int layerIndex, double seconds);

public:
    // Adds an effect from the Effects panel to a clip as one undoable edit; a
    // transition lands on the clip edge nearer `seconds`. Returns the new
    // effect's index, or -1 when the clip is missing or its layer locked.
    int addEffect(int layerIndex, int clipIndex, const plugin::EffectSpec& spec, double seconds);
    // Insert 3D Model Animation Instance: animation `index` of `filePath`
    // (empty: the model's own file) playing on a model layer, as one
    // undoable edit. False when there is no such animation or the layer is
    // locked.
    bool insertModelAnimation(int layerIndex, const QString& filePath, int index);

private:
    void beginModelEdit();
    void finishModelEdit(const QString& title, bool rebuild = false);
    void editLayer(int index, const QString& title, const std::function<void(composition::Layer&)>& edit,
                   bool rebuild = false);
    // Override Materials of a model layer: each material's diffuse map source.
    void addMaterialRows(int layerIndex, const model3d::Mesh& mesh, QTreeWidgetItem* parent);
    // A light layer's Light group (LightLayerGroupFactory) and a 3D layer's
    // Material group (AssetLayerGroupFactory): one editor a property.
    void addLightRows(int layerIndex, QTreeWidgetItem* parent);
    void addSurfaceRows(int layerIndex, QTreeWidgetItem* parent);
    // A camera layer's Lens group (CameraLayerGroupFactory).
    void addCameraRows(int layerIndex, QTreeWidgetItem* parent);
    // The Transform group's Alignment, and - as it asks for them - its
    // Alignment Axis or Alignment Layer.
    void addAlignmentRows(int layerIndex, QTreeWidgetItem* parent);
    void addPropertyRows(int layerIndex, QTreeWidgetItem* parent, const QVector<TimelinePropertyRow>& rows,
                         const QString& objectNamePrefix);
    // Animations of a model layer: each instance's Strength, Loop, Custom
    // Playback and Playback, its properties and its removal.
    void addModelAnimationRows(int layerIndex, QTreeWidgetItem* parent);
    // The Animations "+": the model's own animations, or one from a file.
    void showModelAnimationMenu(int layerIndex, QWidget* anchor);
    void scheduleRefresh();
    void refreshValues();
    void applySearch();
    void updateKeyButtons();
    void showEffectMenu(int layer, bool behaviors, QWidget* anchor);
    void buildTransformRows(QTreeWidgetItem* parent, int layer);
    // A Transform row of the layer, or of one of its model nodes when
    // `modelNode` is given (`pivot` is where that node's position and anchor
    // point start).
    void buildTransformRow(QTreeWidgetItem* parent, int layer, composition::TransformProperty prop,
                           const QString& modelNode = QString(), const QVector3D& pivot = QVector3D());
    void buildParameterEditor(QTreeWidgetItem* row, int layer, int clip, int effect, int parameter);
    QVector<composition::KeyFrameList*> selectedCurves();
    QUndoStack* m_undoStack = nullptr;
    QVector<composition::Layer> m_editBefore;
    bool m_editPending = false;
    bool m_rebuilding = false;
    int m_treeGeneration = 0;
    bool m_refreshPending = false;
    QVector<std::function<void()>> m_valueReaders;
    QSlider* m_zoomSlider = nullptr;
    QVector<QToolButton*> m_keyTypeButtons;
    bool m_framesDisplay = false;
    void updateTimecodeLabel(double timeSeconds);
    // Rebuilds the layer tree (reference structure: layer -> Tracks / Masks /
    // Effects -> <effect> -> parameters, then Transform and Behaviors).
    void rebuildTree();
    void addEffectRows(QTreeWidgetItem* parent, int layerIndex, int clipIndex, bool behaviors);
    void toggleKeyFrame(QTreeWidgetItem* item);
    // Every keyframe position relevant to the current selection, in frames.
    QVector<int> keyFrameLocations() const;
    // Curve of the parameter row selected in the tree, or nullptr when the
    // selection is not an animated parameter.
    composition::KeyFrameList* selectedCurve();
    // Applies one of the reference's six temporal types to the key sitting at
    // the playhead.
    void setSelectedKeyFrameType(composition::TemporalType type);
    int currentFrame() const;
    void seekToFrame(int frame);
    // Kept as the code-built fallback for tooling that instantiates the panel
    // without the Designer form; the application uses Timeline.ui.
    QWidget* buildToolStrip(QWidget* parent);
    void updateToolButtons();
    // Recomputes the track-area lanes from the tree's visible rows.
    void syncLanes();

    TimelineTree* m_tree = nullptr;
    QSplitter* m_splitter = nullptr;
    plugin::PluginManager* m_pluginManager = nullptr;
    std::shared_ptr<media::MediaManager> m_media;
    media::WaveformCache* m_waveforms = nullptr;
    TimelineCanvas* m_canvas = nullptr;
    QLineEdit* m_timecode = nullptr;
    QLineEdit* m_search = nullptr;
    std::shared_ptr<composition::Composition> m_comp;
    CompositionSettingsDialog::Values m_matchFormat{QString(), 0};
    EditorTool m_tool = EditorTool::Select;
    QVector<QToolButton*> m_toolButtons;
    QToolButton* m_snapButton = nullptr;
    QToolButton* m_toggleKeyButton = nullptr;
    // Two pages of one stack, as the reference's stackedWidgetTimelines: the
    // track canvas and the value graph. The Value Graph button switches
    // between them; only one is ever visible.
    // Mirrors the track canvas's zoom and scroll onto the value graph.
    void syncValueGraphViewport();

    QStackedWidget* m_timelineStack = nullptr;
    TimelineValueGraphView* m_valueGraphView = nullptr;

    QStackedWidget* m_panelPages = nullptr;
    QWidget* m_startPage = nullptr;
    QVBoxLayout* m_startPageLayout = nullptr;
    QToolButton* m_startTabButton = nullptr;
    QToolButton* m_panelMenuButton = nullptr;

    QToolButton* m_valueGraphButton = nullptr;
    QToolButton* m_graphAutoZoomButton = nullptr;
    QToolButton* m_newLayerButton = nullptr;
    QTabBar* m_compositionTabs = nullptr;
    // Header row plus the spacer that pushes the keyframe group out to the
    // start of the track area; see syncHeaderToTree().
    QHBoxLayout* m_header = nullptr;
    QSpacerItem* m_headerSpacer = nullptr;
    QWidget* m_toolStrip = nullptr;
    // Row scrollbar at the right edge of the panel, driving the tree's own
    // (hidden) vertical bar - the reference puts it there rather than between
    // the layer controls and the tracks.
    QScrollBar* m_vScroll = nullptr;
    void syncHeaderToTree();
    bool m_snap = true;
};

} // namespace ui
} // namespace openvegas
