#pragma once

#include <QPointF>
#include <QVector>
#include <QWidget>

#include <memory>

#include "composition/Composition.h"
#include "ui/TimelineWidget.h"

class QScrollBar;

namespace openvegas {
namespace ui {

// The value graph: page 1 of the timeline's stack.
//
// The reference puts the value curves on a canvas of their own
// (ControlPanelValueGraphGraphicsView, objectName "graphicsViewValueGraph") on
// the second page of stackedWidgetTimelines, and the "Value Graph" button
// switches to it. This is that page: a full-height plot with time on X and
// value on Y, where a keyframe is a draggable node and its bezier handles are
// draggable manipulators.
//
// The time axis is not its own: it mirrors the track canvas's zoom and scroll,
// which is what keeps the two pages showing the same span of the timeline.
// Only the value axis belongs to this view.
//
// Written as a QWidget rather than the reference's QGraphicsView because the
// track canvas beside it is one too - a scene-graph page next to a painted one
// would need its own scrolling and zoom mechanics, and the two would drift.
class TimelineValueGraphView : public QWidget
{
    Q_OBJECT

public:
    explicit TimelineValueGraphView(QWidget* parent = nullptr);

    void setComposition(std::shared_ptr<composition::Composition> comp);
    // Rows of the layer tree, as the track canvas gets them: the animated ones
    // are what this plots.
    void setLanes(const QVector<TimelineLane>& lanes);
    // Lane whose curve is edited. -1 lets the view pick the first animated one,
    // so switching to the graph always lands on something.
    void setSelectedLane(int laneIndex);
    void setPlayheadPosition(double timeSeconds);

    // Time axis, pushed from the track canvas so both pages agree.
    void setViewport(double pixelsPerSecond, int hScrollOffset, double contentWidth);

    // Reference "Graph Auto Zoom": fits the value axis to the curves on show.
    // With it off the range is kept and Ctrl+wheel zooms it by hand.
    void setAutoZoom(bool on);
    bool autoZoom() const { return m_autoZoom; }

signals:
    void timeScrubbed(double timeSeconds);
    // Horizontal scroll or zoom asked for here; the track canvas owns both, so
    // the widget above forwards these to it and the change comes back through
    // setViewport().
    void scrollRequested(int contentX);
    void zoomRequested(double factor, int anchorContentX);
    // Live during a node or handle drag, and once when it finishes.
    void keyFramesDragged();
    void keyFramesEdited();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    // What a press landed on.
    enum class Grab
    {
        None,
        Node,             // the keyframe itself: drag moves it in time and value
        IncomingHandle,   // bezier manipulator on the arriving side
        OutgoingHandle,   // bezier manipulator on the leaving side
        Ruler,            // scrubbing the time axis
    };

    // The lane actually plotted with editable nodes.
    int activeLane() const;
    composition::KeyFrameList* activeCurve() const;
    // Every animated lane, drawn faintly behind the active one so the graph
    // shows the property in context rather than in isolation.
    QVector<int> animatedLanes() const;

    double frameRate() const;
    double pixelsPerSecond() const { return m_pixelsPerSecond; }
    // Plot area, i.e. everything below the ruler and right of the value gutter.
    QRect plotRect() const;
    double contentXForFrame(int frame) const;
    int frameAtX(int widgetX) const;
    double yForValue(double value) const;
    double valueAtY(double y) const;
    // Recomputes the value axis from the curves on show. Only does anything
    // while Auto Zoom is on.
    void refreshValueRange();
    // Screen position of a keyframe node and of its two handles.
    QPointF nodePos(const composition::KeyFrame& key) const;
    bool handlePositions(const composition::KeyFrameList& curve, int index,
                         QPointF* incoming, QPointF* outgoing) const;
    // Hit test in screen space; fills the grabbed part and the key's index.
    Grab hitTest(const QPoint& pos, int* keyIndex) const;
    void layoutScrollbar();
    void syncScrollbar();

    std::shared_ptr<composition::Composition> m_comp;
    QVector<TimelineLane> m_lanes;
    int m_selectedLane = -1;
    double m_playhead = 0.0;

    double m_pixelsPerSecond = 40.0;
    int m_hOffset = 0;
    double m_contentWidth = 0.0;

    bool m_autoZoom = true;
    double m_valueLow = 0.0;
    double m_valueHigh = 1.0;

    Grab m_grab = Grab::None;
    int m_grabKeyIndex = -1;
    // Key being dragged, addressed by id so a move in time does not lose it.
    int m_grabKeyId = 0;

    QScrollBar* m_hScroll = nullptr;
    int m_scrollbarHeight = 0;
};

} // namespace ui
} // namespace openvegas
