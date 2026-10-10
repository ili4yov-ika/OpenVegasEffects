#pragma once

#include <QLineF>
#include <QPointF>
#include <QVector>

#include <functional>
#include <optional>

#include "composition/Layer.h"
#include "model3d/Renderer3D.h"
#include "ui/ViewerOverlay.h"

namespace openvegas {
namespace composition {
class Composition;
}

namespace ui {

// The cameras, lights and points of a 3D shot in the Viewer, as the
// reference's CameraLayerControl, LightLayerControl and PointLayerControl
// show them (VegasEffects.exe vtables 0x141328af0, 0x141328658,
// 0x141327ff0), in the colour its controls draw with (0.25, 0.5, 1 -
// FUN_140182d40) and 1.5 pixels wide:
//  - a camera: its frustum (FluxCamera::FrustumLineData), to the frame it
//    sees where the shot fits it; not the camera a view looks through;
//  - a light: its Position handle (OriginControl) - the reference's light
//    icon, hover and checked as its own; a point light with a falloff, the
//    circle of its Reach (PointLightReachControl, a 100-sided ring); a spot,
//    its cone (SpotLightControl: the whole cone and its unfeathered part,
//    250 units long); a directional light, the way it shines;
//  - a point: its Position handle (the reference's crosshairs).
// The overlay draws in every view of the scene - the Viewer's Top, Front,
// Perspective and the others with a camera of their own - through that
// view's camera. With the Select tool a press on a handle selects the layer
// and dragging it moves the layer parallel to the view at its depth, one
// step of History.
class SceneObjectOverlay : public ViewerOverlay
{
public:
    struct Scene
    {
        composition::Composition* composition = nullptr;
        int frame = 0;
        int selectedLayer = -1;
    };
    using SceneProvider = std::function<Scene()>;
    // The camera a view shows the scene through, the canvas being the shot's
    // own size; `shotCamera` when it is the shot's camera itself (its active
    // camera view), whose own frustum is then not drawn.
    struct ViewCamera
    {
        model3d::Camera camera;
        bool shotCamera = false;
    };
    using CameraProvider = std::function<std::optional<ViewCamera>(int view)>;
    using Picked = std::function<void(int layer)>;
    using DragStep = std::function<void()>;

    SceneObjectOverlay(SceneProvider scene, CameraProvider camera, Picked picked, DragStep began,
                       DragStep changed, DragStep finished);

    bool isActive() const override;
    bool acceptsTool(int tool) const override;
    bool inSceneViews() const override { return true; }
    void paint(QPainter& painter, const ViewerMapping& mapping) override;
    bool mousePress(ViewerPointerEvent& event) override;
    void mouseMove(ViewerPointerEvent& event) override;
    void mouseRelease(ViewerPointerEvent& event) override;
    void hover(ViewerPointerEvent& event) override;

    // What a view shows, on the canvas: the lines, and each handle's layer
    // and place.
    struct Handle
    {
        int layer = -1;
        QPointF position;
        bool light = false;
    };
    struct Drawing
    {
        QVector<QLineF> lines;
        QVector<Handle> handles;
    };
    Drawing drawing(int view) const;
    // The layer whose handle is at a canvas point of a view (within the
    // handle's eight pixels at `pixelsPerCanvas`), or -1.
    int layerAt(int view, const QPointF& canvasPos, double pixelsPerCanvas = 1.0) const;

private:
    SceneProvider m_scene;
    CameraProvider m_camera;
    Picked m_picked;
    DragStep m_began;
    DragStep m_changed;
    DragStep m_finished;

    int m_hovered = -1;
    bool m_dragging = false;
    bool m_moved = false;
    int m_dragLayer = -1;
    QPointF m_pressCanvas;
    composition::LayerTransform m_before;
    QMatrix4x4 m_viewProjection;
    double m_depth = 0.0;
    QSizeF m_canvas;
};

} // namespace ui
} // namespace openvegas
