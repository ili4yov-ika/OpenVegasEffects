#pragma once

#include <QPointF>
#include <QPointer>
#include <QRectF>
#include <QSizeF>
#include <Qt>

class QKeyEvent;
class QPainter;
class QPoint;

namespace openvegas::ui {

class ViewerWidget;
class Viewer360View;

// How one viewer view shows the composition: canvas pixels (origin top-left,
// the space the renderer composites in) against widget pixels. In 360 mode
// the canvas is an equirectangular frame seen through a perspective lens, so
// the mapping goes through the lens rather than a scale.
struct ViewerMapping
{
    QRectF viewRect;     // the view's area in the widget
    QRectF imageRect;    // where the flat canvas sits (pan included)
    QSizeF canvasSize;   // composition pixels
    const Viewer360View* sphere = nullptr;

    bool isValid() const { return !viewRect.isEmpty() && !canvasSize.isEmpty(); }
    bool isSpherical() const { return sphere != nullptr; }
    // Widget pixels per canvas pixel (flat views).
    double scale() const;
    QPointF toWidget(const QPointF& canvas, bool* visible = nullptr) const;
    QPointF toCanvas(const QPointF& widget) const;
};

struct ViewerPointerEvent
{
    QPointF widgetPos;
    QPointF canvasPos;
    Qt::MouseButton button = Qt::NoButton;
    Qt::MouseButtons buttons;
    Qt::KeyboardModifiers modifiers;
    ViewerMapping mapping;
    // Set by the overlay that handled the event.
    Qt::CursorShape cursor = Qt::ArrowCursor;
    bool setsCursor = false;
};

// Something drawn over the Viewer's frame that can take the pointer and the
// keyboard: the text transform frame, and the custom UI native plugins draw
// in the reference's viewer (CustomUIRender/MouseEvent/KeyEvent/...; the
// MotionTrack feature picker is one). Overlays see events before the tool
// does; whatever they leave goes to the tool as before.
class ViewerOverlay
{
public:
    // Leaves the viewer it is installed on.
    virtual ~ViewerOverlay();

    // Whether the overlay is shown and asks for events at all right now.
    virtual bool isActive() const { return true; }
    // The tools the overlay works under; the Hand tool always pans.
    virtual bool acceptsTool(int tool) const;

    virtual void paint(QPainter& painter, const ViewerMapping& mapping) { Q_UNUSED(painter); Q_UNUSED(mapping); }
    // A press the overlay takes starts a drag that ends with mouseRelease.
    virtual bool mousePress(ViewerPointerEvent& event) { Q_UNUSED(event); return false; }
    virtual void mouseMove(ViewerPointerEvent& event) { Q_UNUSED(event); }
    virtual void mouseRelease(ViewerPointerEvent& event) { Q_UNUSED(event); }
    // Pointer movement without a drag; sets the cursor it wants.
    virtual void hover(ViewerPointerEvent& event) { Q_UNUSED(event); }
    virtual bool mouseDoubleClick(ViewerPointerEvent& event) { Q_UNUSED(event); return false; }
    virtual bool keyPress(QKeyEvent* event) { Q_UNUSED(event); return false; }
    virtual bool keyRelease(QKeyEvent* event) { Q_UNUSED(event); return false; }
    virtual void focusChanged(bool focused) { Q_UNUSED(focused); }
    virtual bool contextMenu(const QPoint& globalPos, ViewerPointerEvent& event)
    {
        Q_UNUSED(globalPos); Q_UNUSED(event); return false;
    }

    ViewerWidget* viewer() const;
    // Repaints the viewer, e.g. when the plugin asks for RedrawCustomUI.
    void requestRepaint() const;

private:
    friend class ViewerWidget;
    QPointer<QObject> m_viewer;
};

} // namespace openvegas::ui
