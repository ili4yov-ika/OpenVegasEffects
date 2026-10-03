#pragma once

#include <QString>
#include <QStringList>

#include <functional>

#include "core/Identifier.h"
#include "plugin/NativeEffectRender.h"
#include "ui/ViewerOverlay.h"

namespace openvegas::ui {

// The custom UI a native effect draws in the Viewer while it is selected -
// the reference viewer's CustomUISetup/Render/MouseEvent/KeyEvent/... calls
// (MotionTrack's feature picker, BendGeometry's handles). The module sees a
// canvas-sized view: pointer positions are canvas pixels, its drawing is a
// canvas-sized overlay placed like the frame. Setup and Shutdown follow the
// effect the provider reports, so switching selection ends one module's UI
// before the next one starts.
class NativeCustomUiOverlay : public ViewerOverlay
{
public:
    struct Target
    {
        core::Identifier effectId;      // invalid: no custom UI right now
        QString instanceKey;            // changes when another effect instance is shown
        QStringList values;             // parameters at the current frame
        plugin::NativeCustomUiView view;
        // What the module's answers to the viewer's events go to: values it
        // set, background work it asked for (NativeInstanceHost).
        std::function<void(const plugin::NativeCustomUiResult&)> resultHandler;
    };
    using TargetProvider = std::function<Target()>;

    explicit NativeCustomUiOverlay(TargetProvider provider);
    ~NativeCustomUiOverlay() override;

    bool isActive() const override;
    bool acceptsTool(int tool) const override;
    void paint(QPainter& painter, const ViewerMapping& mapping) override;
    bool mousePress(ViewerPointerEvent& event) override;
    void mouseMove(ViewerPointerEvent& event) override;
    void mouseRelease(ViewerPointerEvent& event) override;
    void hover(ViewerPointerEvent& event) override;
    bool keyPress(QKeyEvent* event) override;
    bool keyRelease(QKeyEvent* event) override;
    void focusChanged(bool focused) override;
    bool contextMenu(const QPoint& globalPos, ViewerPointerEvent& event) override;

    // The effect whose UI is set up now (invalid when none).
    core::Identifier activeEffect() const { return m_active.effectId; }

private:
    // The current target, running Shutdown/Setup when it changed.
    Target sync();
    plugin::NativeCustomUiPointer pointerFor(const ViewerPointerEvent& event, bool pressed) const;
    void apply(const plugin::NativeCustomUiResult& result, ViewerPointerEvent* event);
    void shutdown();

    TargetProvider m_provider;
    Target m_active;
    bool m_pressed = false;
};

} // namespace openvegas::ui
