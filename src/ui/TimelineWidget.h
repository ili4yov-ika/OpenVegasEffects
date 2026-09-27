#pragma once

#include <QDockWidget>
#include <QTreeWidget>
#include <QElapsedTimer>
#include <QVector>
#include <functional>

#include <memory>

#include "composition/Composition.h"

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
class QScrollBar;
class QSplitter;
class QTreeWidgetItem;
class QUndoStack;
class QSlider;

namespace openvegas {
namespace media {
class MediaManager;
}
namespace plugin {
class PluginManager;
}
namespace ui {

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

    bool isParameterRow() const { return parameterIndex >= 0; }
    bool isTransformRow() const { return transformProp >= 0; }
};

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

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void pickClipAt(const QPoint& pos);

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
    // Applies the Viewer transport's Timeline Duration edit through the same
    // composition command path as Composite Shot Properties.
    void setCompositionDuration(double seconds);
    void selectAllRows();
    void addMaskToLayer(int layerIndex, composition::MaskShape shape, const QRectF& bounds);
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
    void compositionTabActivated(int index);
    void compositionTabCloseRequested(int index);

private:
    void beginModelEdit();
    void finishModelEdit(const QString& title, bool rebuild = false);
    void editLayer(int index, const QString& title, const std::function<void(composition::Layer&)>& edit,
                   bool rebuild = false);
    void scheduleRefresh();
    void refreshValues();
    void applySearch();
    void updateKeyButtons();
    void showEffectMenu(int layer, bool behaviors, QWidget* anchor);
    void buildTransformRows(QTreeWidgetItem* parent, int layer);
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
    TimelineCanvas* m_canvas = nullptr;
    QLineEdit* m_timecode = nullptr;
    QLineEdit* m_search = nullptr;
    std::shared_ptr<composition::Composition> m_comp;
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
