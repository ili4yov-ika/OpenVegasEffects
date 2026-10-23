#include "ui/NativeCustomUiOverlay.h"

#include <QKeyEvent>
#include <QPainter>

#include "ui/ViewerWidget.h"

// The Viewer overlay of a native module's own interface (MotionTrack's
// lasso and track points, a 360 effect's handles): while the selected effect
// has a custom UI, the overlay draws the image the module renders and hands
// it the Viewer's pointer, keys and focus in canvas pixels, the way the
// reference's viewer passes its events to a plugin.
namespace openvegas::ui {

namespace {

// The bit masks the module's event records use: Shift 1, Ctrl 2, Alt 4 and
// left 1, right 2, middle 4.
int modifierMask(Qt::KeyboardModifiers modifiers)
{
    return (modifiers.testFlag(Qt::ShiftModifier) ? 1 : 0)
           | (modifiers.testFlag(Qt::ControlModifier) ? 2 : 0)
           | (modifiers.testFlag(Qt::AltModifier) ? 4 : 0);
}

int buttonMask(Qt::MouseButtons buttons)
{
    return (buttons.testFlag(Qt::LeftButton) ? 1 : 0)
           | (buttons.testFlag(Qt::RightButton) ? 2 : 0)
           | (buttons.testFlag(Qt::MiddleButton) ? 4 : 0);
}

} // namespace

NativeCustomUiOverlay::NativeCustomUiOverlay(TargetProvider provider)
    : m_provider(std::move(provider))
{
}

NativeCustomUiOverlay::~NativeCustomUiOverlay()
{
    shutdown();
}

void NativeCustomUiOverlay::shutdown()
{
    if (!m_active.effectId.isValid()) return;
    plugin::nativeCustomUiShutdown(m_active.effectId, m_active.values, m_active.view);
    m_active = Target();
    m_pressed = false;
}

// Follows the selection: when the effect instance shown changes, the old
// one's UI is shut down (Notify teardown) and the new one's set up.
NativeCustomUiOverlay::Target NativeCustomUiOverlay::sync()
{
    Target target = m_provider ? m_provider() : Target();
    if (target.effectId.isValid() && !plugin::nativeEffectHasCustomUi(target.effectId)) {
        target = Target();
    }
    if (target.instanceKey != m_active.instanceKey || target.effectId != m_active.effectId) {
        shutdown();
        if (target.effectId.isValid()) {
            plugin::nativeCustomUiSetup(target.effectId, target.values, target.view);
        }
    }
    m_active = target;
    return target;
}

// Not in the 360 view: the module draws over the flat canvas only.
bool NativeCustomUiOverlay::isActive() const
{
    const ViewerWidget* owner = viewer();
    if (owner && owner->isSpherical()) return false;
    const Target target = m_provider ? m_provider() : Target();
    return target.effectId.isValid() && plugin::nativeEffectHasCustomUi(target.effectId);
}

bool NativeCustomUiOverlay::acceptsTool(int tool) const
{
    return tool == int(ViewerWidget::ViewerTool::Select);
}

// A Viewer pointer event as the module's record: the position in canvas
// pixels, the button that changed (1 left, 2 right, 3 middle) and the masks.
plugin::NativeCustomUiPointer NativeCustomUiOverlay::pointerFor(const ViewerPointerEvent& event,
                                                                bool pressed) const
{
    plugin::NativeCustomUiPointer pointer;
    pointer.position = event.canvasPos.toPoint();
    pointer.pressed = pressed;
    pointer.button = event.button == Qt::LeftButton ? 1
                   : event.button == Qt::RightButton ? 2
                   : event.button == Qt::MiddleButton ? 3 : 0;
    pointer.buttons = buttonMask(event.buttons);
    pointer.modifiers = modifierMask(event.modifiers);
    pointer.clicks = event.button != Qt::NoButton ? 1 : 0;
    return pointer;
}

// What the module answered: changed property values (or a request for
// background work) go to the owner to store as an edit, a redraw repaints the
// Viewer, and the cursor it asked for replaces the Viewer's.
void NativeCustomUiOverlay::apply(const plugin::NativeCustomUiResult& result,
                                  ViewerPointerEvent* event)
{
    if (m_active.resultHandler
        && (!result.values.isEmpty() || result.backgroundRequested)) {
        m_active.resultHandler(result);
    }
    if (result.redraw) requestRepaint();
    if (event && result.cursor != 0) {
        const int shape = plugin::nativeCursorShape(result.cursor);
        if (shape >= 0) {
            event->cursor = Qt::CursorShape(shape);
            event->setsCursor = true;
        }
    }
}

// The module renders its overlay at the Viewer's zoom; it is drawn over the
// frame's rectangle.
void NativeCustomUiOverlay::paint(QPainter& painter, const ViewerMapping& mapping)
{
    if (mapping.isSpherical() || mapping.imageRect.isEmpty()) return;
    Target target = sync();
    if (!target.effectId.isValid()) return;
    target.view.zoomX = target.view.zoomY = mapping.scale();
    const QImage overlay = plugin::nativeCustomUiRender(target.effectId, target.values, target.view);
    if (overlay.isNull()) return;
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(mapping.imageRect, overlay);
}

bool NativeCustomUiOverlay::mousePress(ViewerPointerEvent& event)
{
    if (event.mapping.isSpherical()) return false;
    Target target = sync();
    if (!target.effectId.isValid()) return false;
    target.view.zoomX = target.view.zoomY = event.mapping.scale();
    const auto result = plugin::nativeCustomUiMouse(target.effectId, target.values, target.view,
                                                    plugin::NativeCustomUiMouse::Press,
                                                    pointerFor(event, true));
    apply(result, &event);
    m_pressed = result.handled;
    return result.handled;
}

void NativeCustomUiOverlay::mouseMove(ViewerPointerEvent& event)
{
    Target target = sync();
    if (!target.effectId.isValid()) return;
    target.view.zoomX = target.view.zoomY = event.mapping.scale();
    apply(plugin::nativeCustomUiMouse(target.effectId, target.values, target.view,
                                      plugin::NativeCustomUiMouse::Move,
                                      pointerFor(event, m_pressed)),
          &event);
}

void NativeCustomUiOverlay::mouseRelease(ViewerPointerEvent& event)
{
    Target target = sync();
    m_pressed = false;
    if (!target.effectId.isValid()) return;
    target.view.zoomX = target.view.zoomY = event.mapping.scale();
    apply(plugin::nativeCustomUiMouse(target.effectId, target.values, target.view,
                                      plugin::NativeCustomUiMouse::Release,
                                      pointerFor(event, false)),
          &event);
}

void NativeCustomUiOverlay::hover(ViewerPointerEvent& event)
{
    // The reference viewer reports every move, so modules can track hover.
    if (event.mapping.isSpherical()) return;
    mouseMove(event);
}

bool NativeCustomUiOverlay::keyPress(QKeyEvent* event)
{
    const Target target = sync();
    if (!target.effectId.isValid() || event->isAutoRepeat()) return false;
    const quint32 keysym = plugin::nativeKeysym(event->key(), event->text());
    if (keysym == 0) return false;
    const auto result = plugin::nativeCustomUiKey(target.effectId, target.values, target.view,
                                                  plugin::NativeCustomUiKey::Press, keysym,
                                                  event->text());
    apply(result, nullptr);
    return result.handled;
}

bool NativeCustomUiOverlay::keyRelease(QKeyEvent* event)
{
    const Target target = sync();
    if (!target.effectId.isValid() || event->isAutoRepeat()) return false;
    const quint32 keysym = plugin::nativeKeysym(event->key(), event->text());
    if (keysym == 0) return false;
    const auto result = plugin::nativeCustomUiKey(target.effectId, target.values, target.view,
                                                  plugin::NativeCustomUiKey::Release, keysym,
                                                  event->text());
    apply(result, nullptr);
    return result.handled;
}

void NativeCustomUiOverlay::focusChanged(bool focused)
{
    const Target target = sync();
    if (!target.effectId.isValid()) return;
    apply(plugin::nativeCustomUiFocus(target.effectId, target.values, target.view, focused),
          nullptr);
}

bool NativeCustomUiOverlay::contextMenu(const QPoint& globalPos, ViewerPointerEvent& event)
{
    Q_UNUSED(globalPos);
    Q_UNUSED(event);
    const Target target = sync();
    if (!target.effectId.isValid()) return false;
    if (!plugin::nativeCustomUiHasContextMenu(target.effectId, target.values, target.view).handled) {
        return false;
    }
    const auto result = plugin::nativeCustomUiContextMenu(target.effectId, target.values,
                                                          target.view);
    apply(result, nullptr);
    return result.handled;
}

} // namespace openvegas::ui
