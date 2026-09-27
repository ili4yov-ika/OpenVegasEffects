#include "ui/TimelineValueGraphView.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QScrollBar>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace openvegas {
namespace ui {

namespace {

// Same base scale as the track canvas, so a pixel means the same span of time
// on either page before zoom is applied.
constexpr double kMinPixelsPerSecond = 1.0;
constexpr int kNodeRadius = 4;
constexpr int kHandleRadius = 3;
constexpr int kHitSlop = 6;
// Air left above and below the curves when Auto Zoom fits the value axis.
constexpr double kAutoZoomMargin = 0.12;

bool numericValue(const QVariant& value, double* out)
{
    bool ok = false;
    const double v = value.toDouble(&ok);
    if (ok) {
        *out = v;
    }
    return ok;
}

} // namespace

TimelineValueGraphView::TimelineValueGraphView(QWidget* parent)
    : QWidget(parent)
{
    // Reference objectName for the value graph canvas, so a stylesheet can
    // reach it and the widget tree reads like the original.
    setObjectName(QStringLiteral("graphicsViewValueGraph"));
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

    m_hScroll = new QScrollBar(Qt::Horizontal, this);
    m_hScroll->setSingleStep(20);
    connect(m_hScroll, &QScrollBar::valueChanged, this,
            [this](int value) { emit scrollRequested(value); });
}

void TimelineValueGraphView::setComposition(std::shared_ptr<composition::Composition> comp)
{
    m_comp = std::move(comp);
    refreshValueRange();
    syncScrollbar();
    update();
}

void TimelineValueGraphView::setLanes(const QVector<TimelineLane>& lanes)
{
    m_lanes = lanes;
    refreshValueRange();
    update();
}

void TimelineValueGraphView::setSelectedLane(int laneIndex)
{
    if (m_selectedLane == laneIndex) {
        return;
    }
    m_selectedLane = laneIndex;
    refreshValueRange();
    update();
}

void TimelineValueGraphView::setPlayheadPosition(double timeSeconds)
{
    if (qFuzzyCompare(m_playhead + 1.0, timeSeconds + 1.0)) {
        return;
    }
    m_playhead = qMax(0.0, timeSeconds);
    update();
}

void TimelineValueGraphView::setViewport(double pixelsPerSecond, int hScrollOffset,
                                         double contentWidth)
{
    m_pixelsPerSecond = qMax(kMinPixelsPerSecond, pixelsPerSecond);
    m_hOffset = qMax(0, hScrollOffset);
    m_contentWidth = qMax(0.0, contentWidth);
    syncScrollbar();
    update();
}

void TimelineValueGraphView::setAutoZoom(bool on)
{
    if (m_autoZoom == on) {
        return;
    }
    m_autoZoom = on;
    refreshValueRange();
    update();
}

double TimelineValueGraphView::frameRate() const
{
    if (!m_comp) {
        return 25.0;
    }
    const int den = m_comp->fpsDenominator() > 0 ? m_comp->fpsDenominator() : 1;
    const double fps = static_cast<double>(m_comp->fpsNumerator()) / den;
    return fps > 0.0 ? fps : 25.0;
}

QRect TimelineValueGraphView::plotRect() const
{
    const int bottom = qMax(kTimelineRulerHeight + 1, height() - m_scrollbarHeight);
    return QRect(0, kTimelineRulerHeight, width(), bottom - kTimelineRulerHeight);
}

double TimelineValueGraphView::contentXForFrame(int frame) const
{
    return (frame / frameRate()) * m_pixelsPerSecond;
}

int TimelineValueGraphView::frameAtX(int widgetX) const
{
    const double seconds = (widgetX + m_hOffset) / qMax(kMinPixelsPerSecond, m_pixelsPerSecond);
    return qMax(0, static_cast<int>(qRound(seconds * frameRate())));
}

double TimelineValueGraphView::yForValue(double value) const
{
    const QRect plot = plotRect();
    const double span = m_valueHigh - m_valueLow;
    if (plot.height() <= 0 || qFuzzyIsNull(span)) {
        return plot.center().y();
    }
    const double norm = (value - m_valueLow) / span;
    return plot.bottom() - norm * plot.height();
}

double TimelineValueGraphView::valueAtY(double y) const
{
    const QRect plot = plotRect();
    if (plot.height() <= 0) {
        return m_valueLow;
    }
    const double norm = (plot.bottom() - y) / plot.height();
    return m_valueLow + norm * (m_valueHigh - m_valueLow);
}

QVector<int> TimelineValueGraphView::animatedLanes() const
{
    QVector<int> out;
    if (!m_comp) {
        return out;
    }
    auto* comp = m_comp.get();
    for (int i = 0; i < m_lanes.size(); ++i) {
        const QVector<composition::KeyFrameList*> curves = laneCurves(comp, m_lanes.at(i));
        for (const composition::KeyFrameList* curve : curves) {
            if (curve && !curve->isEmpty()) {
                out.append(i);
                break;
            }
        }
    }
    return out;
}

int TimelineValueGraphView::activeLane() const
{
    const QVector<int> animated = animatedLanes();
    if (animated.contains(m_selectedLane)) {
        return m_selectedLane;
    }
    // Nothing usable is selected: fall back to the first animated row so the
    // page always opens on a curve rather than on an empty plot.
    return animated.isEmpty() ? -1 : animated.first();
}

composition::KeyFrameList* TimelineValueGraphView::activeCurve() const
{
    const int lane = activeLane();
    if (lane < 0 || !m_comp) {
        return nullptr;
    }
    // A point-valued transform row carries one curve per axis. The graph edits
    // one of them - the first that holds keys - because a node dragged in value
    // means one number, not a pair.
    const QVector<composition::KeyFrameList*> curves = laneCurves(m_comp.get(), m_lanes.at(lane));
    for (composition::KeyFrameList* curve : curves) {
        if (curve && !curve->isEmpty()) {
            return curve;
        }
    }
    return nullptr;
}

void TimelineValueGraphView::refreshValueRange()
{
    if (!m_autoZoom) {
        return;
    }
    // The active property alone decides the range, which is what the reference
    // auto-zooms to. Fitting every curve on show instead would squash the one
    // being edited flat as soon as another property used a wider scale -
    // opacity in percent beside a position in pixels, say.
    bool have = false;
    double lo = 0.0;
    double hi = 0.0;
    if (const composition::KeyFrameList* curve = activeCurve()) {
        const QVector<composition::KeyFrame> keys = curve->all();
        for (const composition::KeyFrame& key : keys) {
            double v = 0.0;
            if (!numericValue(key.value, &v)) {
                continue;
            }
            lo = have ? qMin(lo, v) : v;
            hi = have ? qMax(hi, v) : v;
            have = true;
        }
    }
    if (!have) {
        m_valueLow = 0.0;
        m_valueHigh = 100.0;
        return;
    }
    if (qFuzzyCompare(lo + 1.0, hi + 1.0)) {
        // A property held at one value still needs a finite axis to sit on.
        lo -= 1.0;
        hi += 1.0;
    }
    const double margin = (hi - lo) * kAutoZoomMargin;
    m_valueLow = lo - margin;
    m_valueHigh = hi + margin;
}

QPointF TimelineValueGraphView::nodePos(const composition::KeyFrame& key) const
{
    double value = 0.0;
    numericValue(key.value, &value);
    return QPointF(contentXForFrame(key.frame) - m_hOffset, yForValue(value));
}

// A handle is stored in the unit space of the segment it shapes: x is the
// fraction of the segment's duration, y the fraction of its value change. This
// turns one back into a point on screen. A segment whose two ends hold the same
// value is flat whatever the handles say - the model interpolates value by an
// eased t - so its handles sit on the flat line and only x means anything.
bool TimelineValueGraphView::handlePositions(const composition::KeyFrameList& curve, int index,
                                             QPointF* incoming, QPointF* outgoing) const
{
    const QVector<composition::KeyFrame> keys = curve.all();
    if (index < 0 || index >= keys.size()) {
        return false;
    }
    const composition::KeyFrame& key = keys.at(index);

    const auto pointFor = [this](const composition::KeyFrame& a, const composition::KeyFrame& b,
                                 const QPointF& unit) {
        double av = 0.0;
        double bv = 0.0;
        numericValue(a.value, &av);
        numericValue(b.value, &bv);
        const double frame = a.frame + unit.x() * (b.frame - a.frame);
        const double value = av + unit.y() * (bv - av);
        return QPointF((frame / frameRate()) * m_pixelsPerSecond - m_hOffset, yForValue(value));
    };

    if (incoming) {
        *incoming = QPointF();
        if (index > 0) {
            *incoming = pointFor(keys.at(index - 1), key, composition::incomingHandleFor(key));
        }
    }
    if (outgoing) {
        *outgoing = QPointF();
        if (index + 1 < keys.size()) {
            *outgoing = pointFor(key, keys.at(index + 1), composition::outgoingHandleFor(key));
        }
    }
    return true;
}

TimelineValueGraphView::Grab TimelineValueGraphView::hitTest(const QPoint& pos, int* keyIndex) const
{
    *keyIndex = -1;
    if (pos.y() < kTimelineRulerHeight) {
        return Grab::Ruler;
    }
    const composition::KeyFrameList* curve = activeCurve();
    if (!curve) {
        return Grab::None;
    }
    const QVector<composition::KeyFrame> keys = curve->all();

    // Handles first: they sit off the curve and would otherwise be shadowed by
    // the node they belong to when the segment is short.
    for (int i = 0; i < keys.size(); ++i) {
        QPointF incoming;
        QPointF outgoing;
        if (!handlePositions(*curve, i, &incoming, &outgoing)) {
            continue;
        }
        if (!incoming.isNull() && QLineF(incoming, pos).length() <= kHitSlop) {
            *keyIndex = i;
            return Grab::IncomingHandle;
        }
        if (!outgoing.isNull() && QLineF(outgoing, pos).length() <= kHitSlop) {
            *keyIndex = i;
            return Grab::OutgoingHandle;
        }
    }
    for (int i = 0; i < keys.size(); ++i) {
        if (QLineF(nodePos(keys.at(i)), pos).length() <= kHitSlop) {
            *keyIndex = i;
            return Grab::Node;
        }
    }
    return Grab::None;
}

void TimelineValueGraphView::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(24, 24, 28));

    const QRect plot = plotRect();
    const double fps = frameRate();

    // ---- time ruler, the same one the track canvas draws ------------------
    painter.setPen(QColor(60, 60, 66));
    painter.drawLine(0, kTimelineRulerHeight, width(), kTimelineRulerHeight);
    {
        // A tick every whole second that survives the current zoom.
        double step = 1.0;
        while (step * m_pixelsPerSecond < 60.0) {
            step *= 2.0;
        }
        painter.setPen(QColor(130, 130, 140));
        const double firstTime = m_hOffset / m_pixelsPerSecond;
        for (double t = std::floor(firstTime / step) * step;
             t * m_pixelsPerSecond - m_hOffset < width(); t += step) {
            const int x = static_cast<int>(t * m_pixelsPerSecond) - m_hOffset;
            if (x < 0) {
                continue;
            }
            painter.drawLine(x, kTimelineRulerHeight - 6, x, kTimelineRulerHeight);
            painter.drawText(x + 3, kTimelineRulerHeight - 9,
                             QStringLiteral("%1s").arg(t, 0, 'f', step < 1.0 ? 1 : 0));
        }
    }

    // ---- value axis: gridlines with their numbers --------------------------
    if (plot.height() > 0) {
        const int lines = 5;
        painter.setPen(QColor(44, 44, 50));
        for (int i = 0; i <= lines; ++i) {
            const int y = plot.top() + i * plot.height() / lines;
            painter.drawLine(0, y, width(), y);
        }
        painter.setPen(QColor(120, 120, 130));
        for (int i = 0; i <= lines; ++i) {
            const int y = plot.top() + i * plot.height() / lines;
            const double value = valueAtY(y);
            // Labels ride inside the plot so the time axis keeps starting at
            // x = 0 and stays aligned with the track page.
            painter.drawText(4, qBound(plot.top() + 10, y - 3, plot.bottom() - 2),
                             QString::number(value, 'f', 2));
        }
    }

    const composition::KeyFrameList* active = activeCurve();

    // ---- every animated curve, faintly, then the active one on top --------
    const auto drawCurve = [&](const composition::KeyFrameList* curve, bool isActive) {
        if (!curve || curve->isEmpty()) {
            return;
        }
        const QVector<composition::KeyFrame> keys = curve->all();
        if (keys.size() < 2) {
            return;
        }
        const int firstX = static_cast<int>(contentXForFrame(keys.first().frame)) - m_hOffset;
        const int lastX = static_cast<int>(contentXForFrame(keys.last().frame)) - m_hOffset;
        const int from = qMax(0, firstX);
        const int to = qMin(width() - 1, lastX);
        if (to <= from) {
            return;
        }
        QPolygonF polyline;
        polyline.reserve(to - from + 1);
        for (int x = from; x <= to; ++x) {
            const double seconds = (x + m_hOffset) / m_pixelsPerSecond;
            const int frameNo = static_cast<int>(qRound(seconds * fps));
            double v = 0.0;
            if (!numericValue(curve->valueAt(frameNo), &v)) {
                continue;
            }
            polyline.append(QPointF(x, yForValue(v)));
        }
        if (polyline.size() < 2) {
            return;
        }
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(isActive ? QColor(18, 176, 255) : QColor(90, 100, 115),
                            isActive ? 1.8 : 1.0));
        painter.drawPolyline(polyline);
    };

    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setClipRect(plot);

    if (m_comp) {
        auto* comp = m_comp.get();
        const QVector<int> animated = animatedLanes();
        for (int laneIndex : animated) {
            const QVector<composition::KeyFrameList*> curves =
                laneCurves(comp, m_lanes.at(laneIndex));
            for (const composition::KeyFrameList* curve : curves) {
                if (curve != active) {
                    drawCurve(curve, false);
                }
            }
        }
    }
    drawCurve(active, true);

    // ---- nodes and their bezier manipulators on the active curve ----------
    if (active) {
        const QVector<composition::KeyFrame> keys = active->all();
        for (int i = 0; i < keys.size(); ++i) {
            const QPointF node = nodePos(keys.at(i));
            QPointF incoming;
            QPointF outgoing;
            handlePositions(*active, i, &incoming, &outgoing);

            painter.setPen(QPen(QColor(200, 170, 90), 1.0));
            painter.setBrush(QColor(240, 205, 110));
            for (const QPointF& handle : {incoming, outgoing}) {
                if (handle.isNull()) {
                    continue;
                }
                painter.drawLine(node, handle);
                painter.drawEllipse(handle, kHandleRadius, kHandleRadius);
            }

            const bool held = keys.at(i).temporal == composition::TemporalType::Hold;
            painter.setPen(QPen(QColor(18, 176, 255), 1.4));
            painter.setBrush(held ? QColor(40, 44, 52) : QColor(240, 240, 245));
            painter.drawEllipse(node, kNodeRadius, kNodeRadius);
        }
    }

    // ---- playhead and the value it currently reads ------------------------
    const int playheadX = static_cast<int>(m_playhead * m_pixelsPerSecond) - m_hOffset;
    if (playheadX >= 0 && playheadX < width()) {
        painter.setPen(QPen(QColor(235, 235, 240), 1.0));
        painter.drawLine(playheadX, 0, playheadX, plot.bottom());
        if (active && !active->isEmpty()) {
            const int frameNo = static_cast<int>(qRound(m_playhead * fps));
            double v = 0.0;
            if (numericValue(active->valueAt(frameNo), &v)) {
                painter.setPen(QPen(QColor(18, 176, 255), 1.5));
                painter.setBrush(QColor(255, 255, 255));
                painter.drawEllipse(QPointF(playheadX, yForValue(v)), 3.0, 3.0);
            }
        }
    }

    painter.setClipping(false);
    painter.setRenderHint(QPainter::Antialiasing, false);

    if (!active) {
        painter.setPen(QColor(140, 140, 150));
        painter.drawText(plot, Qt::AlignCenter,
                         tr("Select an animated property to show its curve"));
    }
}

void TimelineValueGraphView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    int index = -1;
    const int lane = activeLane();
    if (lane >= 0 && m_comp && m_lanes[lane].layerIndex >= 0 && m_comp->layers()[m_lanes[lane].layerIndex].locked) return;
    m_grab = hitTest(event->pos(), &index);
    m_grabKeyIndex = index;
    m_grabKeyId = 0;

    if (m_grab == Grab::Ruler) {
        emit timeScrubbed(qMax(0.0, (event->pos().x() + m_hOffset) / m_pixelsPerSecond));
        event->accept();
        return;
    }
    if (m_grab != Grab::None) {
        if (const composition::KeyFrameList* curve = activeCurve()) {
            const QVector<composition::KeyFrame> keys = curve->all();
            if (index >= 0 && index < keys.size()) {
                m_grabKeyId = keys.at(index).id;
            }
        }
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void TimelineValueGraphView::mouseMoveEvent(QMouseEvent* event)
{
    if (m_grab == Grab::None) {
        // Cursor tells which manipulator is under the pointer before it is
        // grabbed, which is what makes the handles discoverable at all.
        int index = -1;
        const Grab over = hitTest(event->pos(), &index);
        setCursor(over == Grab::Node || over == Grab::IncomingHandle
                          || over == Grab::OutgoingHandle
                      ? Qt::SizeAllCursor
                      : Qt::ArrowCursor);
        QWidget::mouseMoveEvent(event);
        return;
    }

    if (m_grab == Grab::Ruler) {
        emit timeScrubbed(qMax(0.0, (event->pos().x() + m_hOffset) / m_pixelsPerSecond));
        event->accept();
        return;
    }

    composition::KeyFrameList* curve = activeCurve();
    if (!curve || m_grabKeyId == 0) {
        return;
    }
    composition::KeyFrame* key = curve->keyById(m_grabKeyId);
    if (!key) {
        return;
    }

    if (m_grab == Grab::Node) {
        // A node moves in both axes at once: X is the frame it sits on, Y the
        // value it holds. The move in time goes through moveById so the key
        // keeps its identity - and is refused when another key already owns
        // the target frame, which is what stops a drag swallowing one.
        const int target = frameAtX(event->pos().x());
        if (target != key->frame && !curve->contains(target)) {
            const int id = key->id;
            if (curve->moveById(id, target)) {
                key = curve->keyById(id);
            }
        }
        if (key) {
            key->value = QVariant(valueAtY(event->pos().y()));
        }
        refreshValueRange();
        emit keyFramesDragged();
        update();
        event->accept();
        return;
    }

    // A handle drag converts the cursor back into the unit space of the segment
    // it shapes, and switches the key to Manual Bezier: the preset types own
    // their handles, so a hand-placed one only means anything as a manual key.
    const QVector<composition::KeyFrame> keys = curve->all();
    const int index = m_grabKeyIndex;
    const bool incoming = (m_grab == Grab::IncomingHandle);
    const int otherIndex = incoming ? index - 1 : index + 1;
    if (index < 0 || index >= keys.size() || otherIndex < 0 || otherIndex >= keys.size()) {
        return;
    }
    const composition::KeyFrame& a = incoming ? keys.at(otherIndex) : keys.at(index);
    const composition::KeyFrame& b = incoming ? keys.at(index) : keys.at(otherIndex);

    const int frameSpan = b.frame - a.frame;
    if (frameSpan <= 0) {
        return;
    }
    const double cursorFrame =
        ((event->pos().x() + m_hOffset) / m_pixelsPerSecond) * frameRate();
    double unitX = (cursorFrame - a.frame) / frameSpan;

    double av = 0.0;
    double bv = 0.0;
    numericValue(a.value, &av);
    numericValue(b.value, &bv);
    const double valueSpan = bv - av;
    // A flat segment interpolates to the same number whatever the handle's y
    // is, so there is nothing to read back from the cursor's height - the
    // stored y is left alone rather than filled with a meaningless number.
    double unitY = incoming ? composition::incomingHandleFor(*key).y()
                            : composition::outgoingHandleFor(*key).y();
    if (!qFuzzyIsNull(valueSpan)) {
        unitY = (valueAtY(event->pos().y()) - av) / valueSpan;
    }

    // Handles stay inside their own segment, which is what keeps the curve a
    // function of time rather than a loop.
    unitX = qBound(0.0, unitX, 1.0);
    key->temporal = composition::TemporalType::ManualBezier;
    if (incoming) {
        key->incomingHandle = QPointF(unitX, unitY);
        if (key->handlesLocked) {
            key->outgoingHandle = QPointF(1.0 - unitX, 1.0 - unitY);
        }
    } else {
        key->outgoingHandle = QPointF(unitX, unitY);
        if (key->handlesLocked) {
            key->incomingHandle = QPointF(1.0 - unitX, 1.0 - unitY);
        }
    }
    emit keyFramesDragged();
    update();
    event->accept();
}

void TimelineValueGraphView::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_grab == Grab::None) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    const bool edited = m_grab != Grab::Ruler;
    m_grab = Grab::None;
    m_grabKeyIndex = -1;
    m_grabKeyId = 0;
    if (edited) {
        refreshValueRange();
        emit keyFramesEdited();
    }
    update();
    event->accept();
}

void TimelineValueGraphView::wheelEvent(QWheelEvent* event)
{
    const int delta = event->angleDelta().y();
    if (delta == 0) {
        QWidget::wheelEvent(event);
        return;
    }

    // Ctrl+wheel zooms time, Shift+wheel pans - the same bindings the track
    // canvas uses, forwarded to it because it owns the time axis. Alt+wheel
    // zooms the value axis, which only this page has, and only while Auto Zoom
    // is off: with it on the range is recomputed and a manual zoom would be
    // overwritten on the next repaint.
    if (event->modifiers() & Qt::ControlModifier) {
        const int anchorContentX = event->position().toPoint().x() + m_hOffset;
        emit zoomRequested(delta > 0 ? 1.25 : 0.8, anchorContentX);
        event->accept();
        return;
    }
    if (event->modifiers() & Qt::ShiftModifier) {
        emit scrollRequested(m_hOffset - delta);
        event->accept();
        return;
    }
    if ((event->modifiers() & Qt::AltModifier) && !m_autoZoom) {
        const double anchor = valueAtY(event->position().y());
        const double factor = delta > 0 ? 0.8 : 1.25;
        m_valueLow = anchor + (m_valueLow - anchor) * factor;
        m_valueHigh = anchor + (m_valueHigh - anchor) * factor;
        update();
        event->accept();
        return;
    }
    QWidget::wheelEvent(event);
}

void TimelineValueGraphView::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    layoutScrollbar();
    syncScrollbar();
}

void TimelineValueGraphView::layoutScrollbar()
{
    if (!m_hScroll) {
        return;
    }
    const int sbH = m_hScroll->sizeHint().height();
    m_hScroll->setGeometry(0, qMax(0, height() - sbH), width(), sbH);
    m_hScroll->show();
    m_scrollbarHeight = sbH;
}

void TimelineValueGraphView::syncScrollbar()
{
    if (!m_hScroll) {
        return;
    }
    const int viewW = qMax(1, width());
    const int maximum = qMax(0, static_cast<int>(m_contentWidth) - viewW);
    QSignalBlocker blocker(m_hScroll);   // mirroring the canvas is not a request
    m_hScroll->setRange(0, maximum);
    m_hScroll->setPageStep(viewW);
    m_hScroll->setValue(qBound(0, m_hOffset, maximum));
}

} // namespace ui
} // namespace openvegas
