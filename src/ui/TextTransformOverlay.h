#pragma once

#include <QPolygonF>
#include <QRectF>
#include <QString>
#include <QTransform>

#include <functional>

#include "composition/Layer.h"
#include "composition/TextStyle.h"
#include "ui/ViewerOverlay.h"

namespace openvegas::ui {

// The frame drawn round the selected text layer in the Viewer: drag inside
// it to move the text, a handle to scale it (corners keep the proportions
// unless Shift is held, Alt scales about the middle), the knob above the top
// edge to turn it (Shift snaps to 15 degrees). Works under the Select and
// Text tools, so text can be placed and arranged without changing tool.
//
// Values are written at the current frame - into the property's keyframes
// when it is animated - while dragging, and each finished drag is reported
// once so it becomes a single Undo step.
class TextTransformOverlay : public ViewerOverlay
{
public:
    struct Target
    {
        composition::Layer* layer = nullptr;   // null: nothing to frame
        composition::TextStyle style;
        int frame = 0;
        QSizeF canvasSize;
    };
    enum class Handle
    {
        None, Move, TopLeft, Top, TopRight, Right, BottomRight, Bottom, BottomLeft, Left, Rotate,
    };

    using TargetProvider = std::function<Target()>;
    using Changed = std::function<void()>;
    using Committed = std::function<void(const composition::LayerTransform& before,
                                         const composition::LayerTransform& after,
                                         const QString& title)>;

    // Selects the text layer under a canvas point (other than the framed
    // one); true when the selection changed.
    using Picker = std::function<bool(const QPointF& canvasPos)>;

    TextTransformOverlay(TargetProvider target, Changed changed, Committed committed,
                         Picker picker = {});

    // Where a text target sits on the canvas (corners as canvasQuad), for
    // picking text that is not selected.
    static QPolygonF quadFor(const Target& target);

    bool isActive() const override;
    bool acceptsTool(int tool) const override;
    void paint(QPainter& painter, const ViewerMapping& mapping) override;
    bool mousePress(ViewerPointerEvent& event) override;
    void mouseMove(ViewerPointerEvent& event) override;
    void mouseRelease(ViewerPointerEvent& event) override;
    void hover(ViewerPointerEvent& event) override;

    // The text's box in canvas pixels: top-left, top-right, bottom-right,
    // bottom-left as the layer transform places it. Empty without a target.
    QPolygonF canvasQuad() const;
    Handle handleAt(const QPointF& widgetPos, const ViewerMapping& mapping) const;
    bool isDragging() const { return m_drag != Handle::None; }

private:
    struct Geometry
    {
        QRectF local;          // the text's bounds in its own (box) space
        QTransform toCanvas;   // what the renderer applies to that space
        bool valid = false;
    };
    static Geometry geometryOf(const Target& target);
    QPointF handleWidget(Handle handle, const Geometry& geometry,
                         const ViewerMapping& mapping) const;
    void applyDrag(const QPointF& canvasPos, Qt::KeyboardModifiers modifiers);

    TargetProvider m_target;
    Changed m_changed;
    Committed m_committed;
    Picker m_picker;

    Handle m_drag = Handle::None;
    Handle m_hover = Handle::None;
    Target m_start;
    Geometry m_startGeometry;
    composition::LayerTransform m_before;
    QPointF m_pressCanvas;
    bool m_moved = false;
};

} // namespace openvegas::ui
