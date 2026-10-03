#include "ui/TextTransformOverlay.h"

#include <QCoreApplication>
#include <QLineF>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

#include <cmath>

#include "render/TextRender.h"
#include "ui/ViewerWidget.h"

namespace openvegas::ui {

namespace {

using Handle = TextTransformOverlay::Handle;

const QColor kFrameColor(18, 176, 255);
constexpr double kHandleRadius = 6.0;
constexpr double kRotateKnobDistance = 24.0;

// Writes an animatable value at `frame`: into its keyframes when the property
// is animated (keeping the key's interpolation), as the static value when not.
void writeAtFrame(composition::KeyFrameList& curve, int frame, double value, double* staticValue)
{
    if (curve.isEmpty()) {
        *staticValue = value;
        return;
    }
    const composition::KeyFrame* key = curve.at(frame);
    curve.set(frame, value, key ? key->temporal : composition::TemporalType::Linear);
}

// x and y of a point property share one decision: animate both once either
// is, starting the other from its static value - as moving a layer does.
void writePairAtFrame(composition::KeyFrameList& xCurve, composition::KeyFrameList& yCurve,
                      QPointF& staticValue, int frame, const QPointF& value)
{
    if (xCurve.isEmpty() && yCurve.isEmpty()) {
        staticValue = value;
        return;
    }
    if (xCurve.isEmpty()) xCurve.setDefaultValue(staticValue.x());
    if (yCurve.isEmpty()) yCurve.setDefaultValue(staticValue.y());
    const auto type = [frame](const composition::KeyFrameList& curve) {
        const composition::KeyFrame* key = curve.at(frame);
        return key ? key->temporal : composition::TemporalType::Linear;
    };
    xCurve.set(frame, value.x(), type(xCurve));
    yCurve.set(frame, value.y(), type(yCurve));
}

QPointF localHandle(Handle handle, const QRectF& rect)
{
    switch (handle) {
    case Handle::TopLeft: return rect.topLeft();
    case Handle::Top: return QPointF(rect.center().x(), rect.top());
    case Handle::TopRight: return rect.topRight();
    case Handle::Right: return QPointF(rect.right(), rect.center().y());
    case Handle::BottomRight: return rect.bottomRight();
    case Handle::Bottom: return QPointF(rect.center().x(), rect.bottom());
    case Handle::BottomLeft: return rect.bottomLeft();
    case Handle::Left: return QPointF(rect.left(), rect.center().y());
    default: return rect.center();
    }
}

Handle opposite(Handle handle)
{
    switch (handle) {
    case Handle::TopLeft: return Handle::BottomRight;
    case Handle::Top: return Handle::Bottom;
    case Handle::TopRight: return Handle::BottomLeft;
    case Handle::Right: return Handle::Left;
    case Handle::BottomRight: return Handle::TopLeft;
    case Handle::Bottom: return Handle::Top;
    case Handle::BottomLeft: return Handle::TopRight;
    case Handle::Left: return Handle::Right;
    default: return Handle::None;
    }
}

bool isCorner(Handle handle)
{
    return handle == Handle::TopLeft || handle == Handle::TopRight
           || handle == Handle::BottomRight || handle == Handle::BottomLeft;
}

bool scalesX(Handle handle)
{
    return isCorner(handle) || handle == Handle::Left || handle == Handle::Right;
}

bool scalesY(Handle handle)
{
    return isCorner(handle) || handle == Handle::Top || handle == Handle::Bottom;
}

// Top-left, top-right, bottom-right, bottom-left - four corners, where
// QPolygonF(QRectF) would repeat the first to close the outline.
QPolygonF cornersOf(const QRectF& rect)
{
    return QPolygonF({rect.topLeft(), rect.topRight(), rect.bottomRight(), rect.bottomLeft()});
}

constexpr Handle kScaleHandles[] = {
    Handle::TopLeft, Handle::Top, Handle::TopRight, Handle::Right,
    Handle::BottomRight, Handle::Bottom, Handle::BottomLeft, Handle::Left,
};

// The resize cursor whose arrow is closest to the handle's direction.
Qt::CursorShape resizeCursor(const QPointF& direction)
{
    double degrees = qRadiansToDegrees(std::atan2(direction.y(), direction.x()));
    degrees = std::fmod(degrees + 180.0 + 22.5, 180.0);
    switch (int(degrees / 45.0)) {
    case 0: return Qt::SizeHorCursor;
    case 1: return Qt::SizeFDiagCursor;
    case 2: return Qt::SizeVerCursor;
    default: return Qt::SizeBDiagCursor;
    }
}

QString commandTitle(Handle handle)
{
    switch (handle) {
    case Handle::Move:
        return QCoreApplication::translate("openvegas::ui::TextTransformOverlay", "Move Text");
    case Handle::Rotate:
        return QCoreApplication::translate("openvegas::ui::TextTransformOverlay", "Rotate Text");
    default:
        return QCoreApplication::translate("openvegas::ui::TextTransformOverlay", "Scale Text");
    }
}

} // namespace

TextTransformOverlay::TextTransformOverlay(TargetProvider target, Changed changed,
                                           Committed committed, Picker picker)
    : m_target(std::move(target)), m_changed(std::move(changed)),
      m_committed(std::move(committed)), m_picker(std::move(picker))
{
}

QPolygonF TextTransformOverlay::quadFor(const Target& target)
{
    const Geometry geometry = geometryOf(target);
    return geometry.valid ? geometry.toCanvas.map(cornersOf(geometry.local)) : QPolygonF();
}

TextTransformOverlay::Geometry TextTransformOverlay::geometryOf(const Target& target)
{
    Geometry geometry;
    if (!target.layer || target.canvasSize.isEmpty() || target.style.text.isEmpty()) {
        return geometry;
    }
    // The same box, scale and placement RenderWorker::renderClip draws text
    // with: the frame-sized box centred on the layer origin, the style's
    // horizontal/vertical scale about that centre, then the layer transform.
    const double width = target.canvasSize.width();
    const double height = target.canvasSize.height();
    const QRectF box(-width / 2.0, -height / 2.0, width, height);
    QRectF bounds = render::styledTextBounds(box, target.style);
    bounds = QTransform::fromScale(qMax(0.01, target.style.horizontalScale / 100.0),
                                   qMax(0.01, target.style.verticalScale / 100.0))
                 .mapRect(bounds);
    if (bounds.isEmpty()) return geometry;
    const composition::LayerTransform& transform = target.layer->transform;
    const QPointF position = transform.positionAt(target.frame);
    const QPointF scale = transform.scaleAt(target.frame);
    QTransform toCanvas;
    toCanvas.translate(width / 2.0 + position.x(), height / 2.0 - position.y());
    toCanvas.rotate(transform.rotationAt(target.frame));
    toCanvas.scale(scale.x() / 100.0, scale.y() / 100.0);
    geometry.local = bounds;
    geometry.toCanvas = toCanvas;
    geometry.valid = true;
    return geometry;
}

bool TextTransformOverlay::isActive() const
{
    const ViewerWidget* owner = viewer();
    if (owner && owner->isSpherical()) return false;
    // With a picker the overlay listens even without a framed layer, so text
    // can be picked straight from the canvas.
    return m_drag != Handle::None || m_picker || (m_target && m_target().layer);
}

bool TextTransformOverlay::acceptsTool(int tool) const
{
    return tool == int(ViewerWidget::ViewerTool::Select)
           || tool == int(ViewerWidget::ViewerTool::Text);
}

QPolygonF TextTransformOverlay::canvasQuad() const
{
    const Geometry geometry = geometryOf(m_target ? m_target() : Target());
    if (!geometry.valid) return {};
    return geometry.toCanvas.map(cornersOf(geometry.local));
}

QPointF TextTransformOverlay::handleWidget(Handle handle, const Geometry& geometry,
                                           const ViewerMapping& mapping) const
{
    if (handle == Handle::Rotate) {
        // Outwards from the top edge, a fixed screen distance whatever the
        // zoom, along the box's own "up".
        const QPointF top = localHandle(Handle::Top, geometry.local);
        const QPointF topWidget = mapping.toWidget(geometry.toCanvas.map(top));
        const QPointF centreWidget = mapping.toWidget(geometry.toCanvas.map(geometry.local.center()));
        QPointF up = topWidget - centreWidget;
        const double length = std::hypot(up.x(), up.y());
        if (length < 1e-6) {
            const QPointF above = mapping.toWidget(geometry.toCanvas.map(top - QPointF(0, 1)));
            up = above - topWidget;
        }
        const double norm = std::hypot(up.x(), up.y());
        return norm > 1e-9 ? topWidget + up / norm * kRotateKnobDistance : topWidget;
    }
    return mapping.toWidget(geometry.toCanvas.map(localHandle(handle, geometry.local)));
}

TextTransformOverlay::Handle TextTransformOverlay::handleAt(const QPointF& widgetPos,
                                                            const ViewerMapping& mapping) const
{
    const Geometry geometry = geometryOf(m_target ? m_target() : Target());
    if (!geometry.valid || !mapping.isValid() || mapping.isSpherical()) return Handle::None;
    const auto near = [&](Handle handle, double radius) {
        return QLineF(handleWidget(handle, geometry, mapping), widgetPos).length() <= radius;
    };
    if (near(Handle::Rotate, kHandleRadius + 1.0)) return Handle::Rotate;
    for (Handle handle : kScaleHandles) {
        if (near(handle, kHandleRadius)) return handle;
    }
    QPolygonF quad;
    for (const QPointF& corner : geometry.toCanvas.map(cornersOf(geometry.local))) {
        quad << mapping.toWidget(corner);
    }
    return quad.containsPoint(widgetPos, Qt::OddEvenFill) ? Handle::Move : Handle::None;
}

void TextTransformOverlay::paint(QPainter& painter, const ViewerMapping& mapping)
{
    if (mapping.isSpherical()) return;
    const Geometry geometry = geometryOf(m_target ? m_target() : Target());
    if (!geometry.valid) return;
    QPolygonF quad;
    for (const QPointF& corner : geometry.toCanvas.map(cornersOf(geometry.local))) {
        quad << mapping.toWidget(corner);
    }
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(0, 0, 0, 140), 3.0));
    painter.drawPolygon(quad);
    painter.setPen(QPen(kFrameColor, 1.0));
    painter.drawPolygon(quad);

    const QPointF top = handleWidget(Handle::Top, geometry, mapping);
    const QPointF knob = handleWidget(Handle::Rotate, geometry, mapping);
    painter.drawLine(top, knob);
    painter.setBrush(Qt::white);
    for (Handle handle : kScaleHandles) {
        const QPointF centre = handleWidget(handle, geometry, mapping);
        painter.setPen(QPen(m_hover == handle ? QColor(255, 200, 40) : kFrameColor, 1.0));
        painter.drawRect(QRectF(centre.x() - 3.5, centre.y() - 3.5, 7.0, 7.0));
    }
    painter.setPen(QPen(m_hover == Handle::Rotate ? QColor(255, 200, 40) : kFrameColor, 1.0));
    painter.drawEllipse(knob, 4.0, 4.0);

    // The layer origin the text turns about.
    const QPointF origin = mapping.toWidget(geometry.toCanvas.map(QPointF(0.0, 0.0)));
    painter.setPen(QPen(kFrameColor, 1.0));
    painter.drawLine(origin - QPointF(5, 0), origin + QPointF(5, 0));
    painter.drawLine(origin - QPointF(0, 5), origin + QPointF(0, 5));
}

bool TextTransformOverlay::mousePress(ViewerPointerEvent& event)
{
    if (event.button != Qt::LeftButton || !m_target || event.mapping.isSpherical()) return false;
    Handle handle = handleAt(event.widgetPos, event.mapping);
    // A press on other text selects it and drags it straight away.
    if (handle == Handle::None && m_picker && m_picker(event.canvasPos)) {
        handle = handleAt(event.widgetPos, event.mapping) == Handle::Move ? Handle::Move
                                                                          : Handle::None;
        requestRepaint();
        if (handle == Handle::None) return true;   // selected; nothing to drag
    }
    const Target target = m_target();
    if (handle == Handle::None || !target.layer || target.layer->locked) return false;
    m_drag = handle;
    m_start = target;
    m_startGeometry = geometryOf(target);
    m_before = target.layer->transform;
    m_pressCanvas = event.canvasPos;
    m_moved = false;
    hover(event);
    return true;
}

void TextTransformOverlay::mouseMove(ViewerPointerEvent& event)
{
    if (m_drag == Handle::None) return;
    applyDrag(event.canvasPos, event.modifiers);
    m_moved = true;
    if (m_changed) m_changed();
    event.setsCursor = true;
    event.cursor = m_drag == Handle::Move ? Qt::SizeAllCursor
                 : m_drag == Handle::Rotate ? Qt::ClosedHandCursor
                                            : Qt::CrossCursor;
    requestRepaint();
}

void TextTransformOverlay::mouseRelease(ViewerPointerEvent& event)
{
    const Handle handle = m_drag;
    m_drag = Handle::None;
    if (handle == Handle::None || !m_moved) return;
    const Target target = m_target ? m_target() : Target();
    if (target.layer && m_committed) {
        m_committed(m_before, target.layer->transform, commandTitle(handle));
    }
    hover(event);
    requestRepaint();
}

void TextTransformOverlay::hover(ViewerPointerEvent& event)
{
    const Handle handle = m_drag != Handle::None ? m_drag
                                                 : handleAt(event.widgetPos, event.mapping);
    if (handle != m_hover) {
        m_hover = handle;
        requestRepaint();
    }
    if (handle == Handle::None) return;
    event.setsCursor = true;
    if (handle == Handle::Move) {
        event.cursor = Qt::SizeAllCursor;
    } else if (handle == Handle::Rotate) {
        event.cursor = Qt::PointingHandCursor;
    } else {
        const Geometry geometry = geometryOf(m_target ? m_target() : Target());
        const QPointF centre = event.mapping.toWidget(geometry.toCanvas.map(geometry.local.center()));
        event.cursor = resizeCursor(handleWidget(handle, geometry, event.mapping) - centre);
    }
}

void TextTransformOverlay::applyDrag(const QPointF& canvasPos, Qt::KeyboardModifiers modifiers)
{
    const Target target = m_target ? m_target() : Target();
    composition::Layer* layer = target.layer;
    if (!layer || !m_startGeometry.valid) return;
    // Every step starts again from the transform the drag began with, so the
    // result depends on where the pointer is, not on how it got there.
    layer->transform = m_before;
    composition::LayerTransform& transform = layer->transform;
    const int frame = m_start.frame;
    const double width = m_start.canvasSize.width();
    const double height = m_start.canvasSize.height();
    const QPointF position0 = m_before.positionAt(frame);
    const QPointF scale0 = m_before.scaleAt(frame) / 100.0;
    const double angle0 = m_before.rotationAt(frame);

    if (m_drag == Handle::Move) {
        const QPointF delta = canvasPos - m_pressCanvas;
        writePairAtFrame(transform.positionXCurve, transform.positionYCurve, transform.position,
                         frame, position0 + QPointF(delta.x(), -delta.y()));
        return;
    }

    if (m_drag == Handle::Rotate) {
        const QPointF origin = m_startGeometry.toCanvas.map(QPointF(0.0, 0.0));
        const QPointF from = m_pressCanvas - origin;
        const QPointF to = canvasPos - origin;
        if (std::hypot(to.x(), to.y()) < 1e-6 || std::hypot(from.x(), from.y()) < 1e-6) return;
        double angle = angle0 + qRadiansToDegrees(std::atan2(to.y(), to.x())
                                                  - std::atan2(from.y(), from.x()));
        if (modifiers.testFlag(Qt::ShiftModifier)) angle = std::round(angle / 15.0) * 15.0;
        writeAtFrame(transform.rotationCurve, frame, angle, &transform.rotationDegrees);
        return;
    }

    // Scale: the handle follows the pointer while the opposite handle (or
    // the middle, with Alt) stays where it is on the canvas.
    const QRectF& local = m_startGeometry.local;
    const QPointF handle = localHandle(m_drag, local);
    const QPointF fixed = modifiers.testFlag(Qt::AltModifier)
                              ? local.center() : localHandle(opposite(m_drag), local);
    const QPointF fixedCanvas = m_startGeometry.toCanvas.map(fixed);
    QTransform rotation;
    rotation.rotate(angle0);
    const QPointF reach = rotation.inverted().map(canvasPos - fixedCanvas);
    const double spanX = handle.x() - fixed.x();
    const double spanY = handle.y() - fixed.y();
    double sx = scale0.x();
    double sy = scale0.y();
    if (isCorner(m_drag) && !modifiers.testFlag(Qt::ShiftModifier)) {
        const QPointF span(scale0.x() * spanX, scale0.y() * spanY);
        const double length = span.x() * span.x() + span.y() * span.y();
        if (length > 1e-12) {
            const double factor = (reach.x() * span.x() + reach.y() * span.y()) / length;
            sx = scale0.x() * factor;
            sy = scale0.y() * factor;
        }
    } else {
        if (scalesX(m_drag) && std::abs(spanX) > 1e-9) sx = reach.x() / spanX;
        if (scalesY(m_drag) && std::abs(spanY) > 1e-9) sy = reach.y() / spanY;
    }
    // A handle dragged across its opposite does not mirror the text.
    const auto keepSide = [](double value, double start) {
        const double sign = start < 0.0 ? -1.0 : 1.0;
        return sign * qMax(0.01, value * sign);
    };
    sx = keepSide(sx, scale0.x());
    sy = keepSide(sy, scale0.y());
    QTransform placed;
    placed.rotate(angle0);
    placed.scale(sx, sy);
    const QPointF origin = fixedCanvas - placed.map(fixed);
    writePairAtFrame(transform.scaleXCurve, transform.scaleYCurve, transform.scalePercent, frame,
                     QPointF(sx * 100.0, sy * 100.0));
    writePairAtFrame(transform.positionXCurve, transform.positionYCurve, transform.position,
                     frame, QPointF(origin.x() - width / 2.0, height / 2.0 - origin.y()));
}

} // namespace openvegas::ui
