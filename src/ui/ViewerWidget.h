#pragma once

#include <QColor>
#include <QImage>
#include <QPoint>
#include <QSize>
#include <QString>
#include <QVector>
#include <QWidget>

class QAction;
class QToolButton;
class QContextMenuEvent;

namespace openvegas {
namespace ui {

class ViewScaleButton;

// Schematic (2D) viewer with a layout manager, ported from the reference
// `biff::ui::viewer::ViewerLayoutManager` (abstract) + concrete
// `FourViewLayoutManager` / `RowViewLayoutManager`.
//
// Layout modes mirror the reference LayoutType enum:
//   Single     : one full view (default)
//   Row        : two side-by-side views (RowViewLayoutManager)
//   TwoOverOne : a row of two above a single full-width view
//   Four       : 2x2 grid of views (FourViewLayoutManager)
//   RowFour    : four views in a single row
// The five come from the reference's own layout managers rather than from any
// list of ids. They used to be attributed to the Layout panel's
// toolButtonLayout1/2/2_1/2_2/4, which turned out to belong to its Scopes panel
// (builder FUN_1402915c0 is ScopesPanelWidget, tooltips "One Scope" ... "Four
// Scopes") - see RE_wnd_Layout.md section 10.
//
// Each view renders the current composited frame; exactly one sub-view is the
// "active" view (reference: SetActiveViewInLayout) and is highlighted. The
// background can be a checkerboard (reference: PaintCheckerboard /
// checkBoxShowCheckerboard2D, setting "ShowCheckerboard2D").
class ViewerWidget : public QWidget
{
    Q_OBJECT

public:
    enum class ViewerLayout
    {
        Single,     // layout-1   : 1 view
        Row,        // layout-2   : 2 views side by side
        TwoOverOne, // layout-2-1 : 2 views over 1
        Four,       // layout-2-2 : 2x2 grid
        RowFour,    // layout-4   : 4 views in a row
    };
    Q_ENUM(ViewerLayout)

    // Preview resolutions offered by the reference's toolButtonPlaybackQuality
    // (141320cf8; its menu builder FUN_140a88320 spells the four out as
    // "Antialiased", "Full", "1/2", "1/4"). Antialiased renders larger than the
    // frame and shrinks the result, which is why it heads the list rather than
    // sitting beside Full.
    enum class Downsample
    {
        Antialiased,
        Full,
        Half,
        Quarter,
    };
    Q_ENUM(Downsample)

    // The four built-in quality profiles, by the names carried in the
    // reference's own profile records (FUN_1403452a0 / FUN_1403451a0 /
    // FUN_1403450a0 / FUN_140344fa0, each pairing a GUID with a name).
    enum class QualityProfile
    {
        Final,
        Draft,
        Quick,
        Fastest,
    };
    Q_ENUM(QualityProfile)

    // Reference "Color Channels" submenu of the viewer Options menu
    // (FUN_1408d4b80): RGB, RGB Straight, Red, Green, Blue, Alpha.
    enum class ColorChannel
    {
        Rgb,
        RgbStraight,
        Red,
        Green,
        Blue,
        Alpha,
    };
    Q_ENUM(ColorChannel)

    // Reference viewer tools. The strip itself carries five buttons - Select,
    // Hand, Text, Shape and Pen - with the shape variants behind a flyout on
    // the shape button; their names come from the reference's own strings
    // ("Rectangle Mask Tool", "Rounded Rect Mask Tool", "Ellipse Mask Tool",
    // "Polygon Mask Tool", "Star Mask Tool", "Freehand Mask Tool").
    enum class ViewerTool
    {
        Select,      // V - pick / select (activates a view)
        Hand,        // H - pan the active view
        Text,        // T - add text
        Rectangle,   // R - shape flyout, default entry
        RoundedRect, // shape flyout
        Ellipse,     // E - shape flyout
        Polygon,     // shape flyout
        Star,        // shape flyout
        Orbit,       // reference "Orbit Tool" - turns the selected 3D layer
        Freehand,    // F - mask pen (reference toolButtonMaskPen)
        VectorPath,  // reference toolButtonVectorPath, foot of the strip
    };
    Q_ENUM(ViewerTool)

    explicit ViewerWidget(QWidget* parent = nullptr);

    void setFrame(const QImage& image);
    QImage frame() const { return m_frame; }

    ViewerLayout layout() const { return m_layout; }
    int activeView() const { return m_activeView; }
    int viewCount() const;
    QPointF canvasDeltaForViewDelta(const QPointF& viewDelta) const;

    bool checkerboardEnabled() const { return m_checkerboard; }

    // Reference "Show mouse coordinates" (checkBoxShowMouseCoordinates on its
    // Preferences Viewer page): the pointer's position in image space, drawn
    // over the canvas. Re-read from the settings when the dialog closes.
    void setShowMouseCoordinates(bool on);
    bool showMouseCoordinates() const { return m_showMouseCoordinates; }
    int zoomPercent() const; // -1 when fit
    void setProjectSize(const QSize& size);

    ViewerTool tool() const { return m_tool; }

    // True for the entries of the shape flyout, which share one strip button.
    static bool isShapeTool(ViewerTool tool);

    // The viewer's own view options - layout presets, checkerboard and zoom.
    // Built here so the connections stay inside this class, but handed out so
    // the layout can place them where the reference does: at the right end of
    // the transport row under the canvas, not in a bar of their own on top.
    QWidget* createViewOptionsBar(QWidget* parent);

    // Timecode overlay (HH:MM:SS:FF) drawn on the active view.
    void setFrameRate(int numerator, int denominator);
    void setTimecode(double seconds);

    // --- view options (reference toolButtonOption / toolButtonPlaybackQuality)

    ColorChannel colorChannel() const { return m_colorChannel; }
    // Colour the frame is seen against. The reference's checkerboard shader
    // tints from it: the plain squares are this colour and the lit ones
    // mix(background, white, 0.15).
    QColor backgroundColor() const { return m_backgroundColor; }

    bool showMotionPath() const { return m_showMotionPath; }
    // The selected layer's position track in canvas pixels - origin top-left,
    // the space the renderer composites in - where `path` is the sampled curve
    // and `keys` the keyframed points on it. Fed by the window, which is the
    // only thing that knows what is selected; the viewer only has to place
    // canvas pixels on screen, which it already does for the frame.
    void setMotionPath(const QVector<QPointF>& path, const QVector<QPointF>& keys);

    Downsample playbackResolution() const { return m_playbackResolution; }
    Downsample pausedResolution() const { return m_pausedResolution; }
    QualityProfile playbackQuality() const { return m_playbackQuality; }
    QualityProfile pausedQuality() const { return m_pausedQuality; }

    // What the renderer should do right now. `playing` picks between the
    // playback and paused halves of the quality button, which is the whole
    // point of the reference keeping two of each.
    double renderScale(bool playing) const;
    bool renderEffects(bool playing) const;

    // Reference labels, used for the menu, the button face and the stored
    // setting alike so all three agree.
    static QString downsampleLabel(Downsample mode);
    static Downsample downsampleFromLabel(const QString& label);
    static QString qualityLabel(QualityProfile profile);
    static QualityProfile qualityFromLabel(const QString& label);

    bool fullScreenPreviewActive() const;
    // The reference registers command 5026 once, centrally, and hangs it off
    // whatever menu wants it. The same action object is therefore handed out
    // rather than duplicated: two QActions on Ctrl+Shift+F would be an
    // ambiguous shortcut and neither would fire.
    QAction* fullScreenPreviewAction() const { return m_fullScreenAction; }

public slots:
    void setLayout(ViewerLayout layout);
    void setActiveView(int index);
    void setCheckerboardEnabled(bool enabled);
    void setZoomPercent(int percent); // -1 fit
    void zoomToFit() { setZoomPercent(-1); }
    void zoomIn();
    void zoomOut();
    void setTool(ViewerTool tool);
    void setColorChannel(ColorChannel channel);
    void setBackgroundColor(const QColor& color);
    void setShowMotionPath(bool on);
    void setPlaybackResolution(Downsample mode);
    void setPausedResolution(Downsample mode);
    void setPlaybackQuality(QualityProfile profile);
    void setPausedQuality(QualityProfile profile);
    // Reference command 5026, "Toggle Full Screen Preview" (Ctrl+Shift+F): the
    // frame alone on a screen of its own, with the editor left behind.
    void toggleFullScreenPreview();
    void setFullScreenPreview(bool on);
    // Follows the transport so the button face and the render size can switch
    // between the playback and paused halves.
    void setPlaying(bool playing);

signals:
    void layoutChanged(ViewerLayout layout);
    void activeViewChanged(int index);
    void zoomPercentChanged(int percent);
    void checkerboardChanged(bool enabled);
    void toolChanged(ViewerTool tool);
    // Viewer manipulation of the selected layer, in view pixels dragged. The
    // viewer knows nothing about the composition, so it reports the gesture
    // and the window applies it to whichever layer is selected.
    void layerOrbited(QPointF delta);
    void layerMoved(QPointF delta);
    // Arrow-key nudge of the selected layer, in composition pixels rather than
    // view pixels - the reference registers these under its Viewer category as
    // "Move position left/right/up/down by 1 pixel" (commands 1107-1110) and by
    // 10 pixels with Shift (1111-1114), and a pixel there means a pixel of the
    // frame whatever the viewer is zoomed to.
    void layerNudged(QPointF delta);
    // A quality-button choice changed: the window re-renders at the new size or
    // with effects switched off.
    void renderQualityChanged();
    void colorChannelChanged(ColorChannel channel);
    void showMotionPathChanged(bool on);
    void textCreationRequested(QPointF canvasPosition);
    void textEditRequested();
    void textContextMenuRequested(const QPoint& globalPosition);
    void maskCreationRequested(int shape, QRectF canvasBounds);
    void freehandMaskCreationRequested(QVector<QPointF> canvasPoints);

protected:
    void paintEvent(QPaintEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void buildToolStrip();
    // The reference's viewer Options menu, FUN_1408d93d0.
    QWidget* createOptionsButton(QWidget* parent);
    // The reference's toolButtonPlaybackQuality menu, FUN_140a88320.
    QWidget* createQualityButton(QWidget* parent);
    void updateQualityButtonText();
    // Applies the Color Channels choice to a frame about to be drawn.
    QImage channelFiltered(const QImage& source) const;
    void paintMotionPath(QPainter& painter, int viewIndex) const;
    // Applies the flyout choice to the shape button's icon and tooltip.
    void updateShapeButton();
    QRect viewRect(int index) const;
    QRect toolStripRect() const;
    QRect imageRectForView(int index) const;
    QPoint imagePos(int viewIndex, const QPoint& widgetPos) const;
    QString formatTimecode(double seconds) const;
    void updateToolbarButtons();
    void updateToolButtons();
    int viewAt(const QPoint& widgetPos) const;
    void placeText(int viewIndex, const QPoint& widgetPos);

    QImage m_frame;
    QSize m_projectSize{1920, 1080};

    ViewerLayout m_layout = ViewerLayout::Single;
    int m_activeView = 0;
    bool m_checkerboard = true;
    int m_zoomPercent = -1; // -1 => fit

    ViewerTool m_tool = ViewerTool::Select;
    QPoint m_panOffset; // applied to the active view when Hand tool pans
    QPoint m_rubberStart;
    QPoint m_rubberCurrent;
    QPoint m_lastMouse;
    int m_dragView = -1;
    bool m_handPanning = false;
    // A drag with Select or Orbit is turning or moving the selected layer.
    bool m_layerDragging = false;
    // Pointer position in image space and whether it is over the canvas at all,
    // for the coordinate overlay.
    bool m_showMouseCoordinates = false;
    bool m_mouseInside = false;
    QPoint m_mouseImagePos;
    bool m_rubberBanding = false;
    bool m_freehandDrawing = false;
    QVector<QPoint> m_freehandPoints;

    int m_fpsNumerator = 30;
    int m_fpsDenominator = 1;
    double m_timecode = 0.0;
    bool m_hasTimecode = false;

    // View options, all mirroring reference state (see the enums above).
    ColorChannel m_colorChannel = ColorChannel::Rgb;
    QColor m_backgroundColor{24, 24, 28};
    bool m_showMotionPath = false;
    QVector<QPointF> m_motionPath;      // canvas pixels
    QVector<QPointF> m_motionPathKeys;  // canvas pixels
    // Channel-filtered copy of the frame, rebuilt only when the frame or the
    // channel changes - a per-pixel pass on every repaint would cost more than
    // the render it is showing.
    mutable QImage m_channelFrame;
    mutable ColorChannel m_channelFrameChannel = ColorChannel::Rgb;
    mutable qint64 m_channelFrameKey = 0;
    Downsample m_playbackResolution = Downsample::Full;
    Downsample m_pausedResolution = Downsample::Full;
    QualityProfile m_playbackQuality = QualityProfile::Final;
    QualityProfile m_pausedQuality = QualityProfile::Final;
    bool m_playing = false;
    QToolButton* m_qualityButton = nullptr;
    QWidget* m_fullScreenPreview = nullptr;
    QAction* m_fullScreenAction = nullptr;

    ViewScaleButton* m_scaleButton = nullptr;
    QWidget* m_toolStrip = nullptr;
    QVector<QToolButton*> m_toolButtons;
    // Shape flyout: one strip button whose menu switches between the five mask
    // shapes, remembering the last one so a click reuses it.
    QToolButton* m_shapeButton = nullptr;
    ViewerTool m_shapeTool = ViewerTool::Rectangle;
};

} // namespace ui
} // namespace openvegas
