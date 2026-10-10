#pragma once

#include <QPointF>
#include <QString>

#include <functional>
#include <memory>

#include "composition/Layer.h"
#include "core/Identifier.h"
#include "model3d/Mesh.h"
#include "ui/ViewerOverlay.h"

namespace openvegas {
namespace composition {
class Composition;
}
namespace media {
class MediaManager;
}

namespace ui {

// The selected 3D model layer's nodes in the Viewer. The selected node is
// outlined by the edges of its triangles (and its children's), as Flux's
// FluxSelect::RenderSelectedWireFrameTriangles outlines a selection; a click
// on the model picks the node of the nearest face under the pointer, as
// FluxSelect::Select finds what is there; a drag that starts on the selected
// node moves it, parallel to the screen at its depth. Anywhere else the
// press is left to the tool, so the layer itself is still dragged as before.
class ModelNodeOverlay : public ViewerOverlay
{
public:
    struct Target
    {
        const composition::Composition* composition = nullptr;
        composition::Layer* layer = nullptr;   // a Model3D layer, or nothing to show
        int frame = 0;
        QString node;                          // the selected node's path; empty for none
    };
    using TargetProvider = std::function<Target()>;
    // A click picked this node ("" when it was on the model but no node).
    using Picked = std::function<void(const QString& path)>;
    // A drag of the node starts, moves it, and ends (one Undo step).
    using DragStep = std::function<void()>;

    ModelNodeOverlay(TargetProvider target, std::shared_ptr<media::MediaManager> media,
                     Picked picked, DragStep began, DragStep changed, DragStep finished);

    bool isActive() const override;
    bool acceptsTool(int tool) const override;
    void paint(QPainter& painter, const ViewerMapping& mapping) override;
    bool mousePress(ViewerPointerEvent& event) override;
    void mouseMove(ViewerPointerEvent& event) override;
    void mouseRelease(ViewerPointerEvent& event) override;

    // The node of the nearest face at a canvas point; "" over the model's own
    // faces, a null string when nothing of the model is there.
    QString nodeAt(const QPointF& canvasPos) const;
    // The selected node's outline: its triangles' edges on the canvas.
    QVector<QLineF> outline() const;

private:
    struct Scene
    {
        const model3d::Mesh* mesh = nullptr;
        QVector<QMatrix4x4> nodeMatrices;   // model space
        QMatrix4x4 viewProjection;          // camera * layer
        QSizeF canvas;
        bool valid = false;
    };
    Scene sceneFor(const Target& target) const;
    const model3d::Mesh* meshFor(const composition::Layer& layer) const;
    // Screen point (canvas pixels) and NDC depth of a model-space point under
    // a node's matrix; false behind the eye.
    static bool project(const Scene& scene, int node, const QVector3D& point, QPointF* screen,
                        double* depth);

    TargetProvider m_target;
    std::shared_ptr<media::MediaManager> m_media;
    Picked m_picked;
    DragStep m_began;
    DragStep m_changed;
    DragStep m_finished;

    // The model's geometry, read again when it is re-imported.
    mutable model3d::Mesh m_mesh;
    mutable QString m_meshId;
    mutable quint64 m_meshRevision = 0;

    bool m_dragging = false;
    bool m_moved = false;
    QPointF m_pressCanvas;
    QString m_dragNode;
    composition::LayerTransform m_before;
    QMatrix4x4 m_toNdc;     // node position (parent space) to clip space
    double m_depth = 0.0;   // NDC depth of the node's position at the press
};

} // namespace ui
} // namespace openvegas
