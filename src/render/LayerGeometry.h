#pragma once

#include <QMatrix4x4>
#include <QPolygonF>
#include <QRectF>
#include <QSize>
#include <QTransform>

#include "model3d/Mesh.h"
#include "model3d/Renderer3D.h"

namespace openvegas {
namespace composition {
class Composition;
struct Layer;
struct LayerTransform;
struct Clip;
struct Effect;
}
namespace media {
class MediaManager;
}

namespace render {

// The camera a shot's 3D layers are seen through: its first visible camera
// layer, or the default one the reference gives a composite shot without.
model3d::Camera sceneCameraOf(const composition::Composition* composition, const QSize& canvas,
                              int frame);
// The shot's lights at `frame`: every visible light layer as Flux reads it
// (0x180483820) and hands it to its shader (0x1804a5100) - standing at its
// position, pointing its turned -Z (layerRotationAt), with its colour,
// Intensity / 100 * 2.5, falloff, reach, a spot's cone and its shadow
// settings.
QVector<model3d::Light> sceneLightsOf(const composition::Composition* composition, int frame);
// How a 3D layer takes those lights at `frame`: its Material (Illuminated,
// Receives Shadows, Billboarded Lights, Ambient/Diffuse/Specular/Shininess /
// 100, Emissive). A model's are whole - ADSS 1 and no Emissive, its
// materials bringing their own (Flux.dll 0x180474a80) - and its Self Shadow
// Bias sets how far its surface may lie behind a caster and stay lit: Flux's
// selfShadowBias (v - 1) / 999 * 0.00999 + 0.00001 against that of 1.
model3d::SurfaceLighting surfaceLightingOf(const composition::Layer& layer, int frame);
// The Viewer's views of a shot (the reference's PanelRenderState::ViewType,
// its camera menu FUN_140937970): the 2D view, the shot's active camera, a
// free perspective camera and the six views along the axes.
enum class ViewType
{
    TwoD = 0,
    ActiveCamera = 1,
    Perspective = 2,
    Front = 3,
    Back = 4,
    Left = 5,
    Right = 6,
    Top = 7,
    Bottom = 8,
};

// Where a view's own camera stands (ViewState CameraPosition, its
// CameraRotation in radians, Z applied first, then Y, then X) and its CameraScale,
// how many picture pixels a scene unit takes in the axis views.
struct ViewCameraState
{
    QVector3D position;
    QVector3D rotation;
    double scale = 1.0;
};

// A view's camera as the reference first places it (PanelRenderState::
// AddViewState, Project.dll 1803c0170): the perspective one at (0, 100,
// 1000); Front a million units out on +Z looking back, Back on -Z turned
// half round, Left on -X and Right on +X turned a quarter, Top above and
// Bottom below.
ViewCameraState defaultViewCameraState(ViewType type);
// Whether the view has a camera of its own rather than the shot's.
bool viewHasOwnCamera(ViewType type);
// The camera `type` sees the shot through at `frame`: the shot's own for
// the 2D view and Active Camera, else the view's - perspective, or straight
// along its axis showing the project's height divided by its scale.
model3d::Camera viewCamera(ViewType type, const ViewCameraState& state,
                           const composition::Composition* composition, const QSize& canvas, int frame);
// How a view's camera is turned (its rotation, Z first).
QMatrix4x4 viewCameraAim(const ViewCameraState& state);
// The Viewer moving a view's own camera, as the reference's
// OrthoCameraController and PerspectiveCameraController do (VegasEffects.exe
// vtables 0x141325e70 / 0x141325dc0): `steps` wheel notches zoom an axis view
// by 1 + 0.1 a notch (its scale, at least 0.0002) about the point `anchor`
// picture pixels from the middle (x right, y down; the reference zooms at the
// pointer), and dolly the perspective camera 120 units a notch along where it
// looks; a drag of `canvasPixels` pans the camera across its view, 1 / scale
// units a pixel (a unit for the perspective camera), the scene following the
// pointer; an orbit turns the perspective camera 0.3 degrees a pixel about
// the scene's origin, keeping its distance (the reference's pivot is not
// recovered).
ViewCameraState zoomedView(ViewType type, ViewCameraState state, double steps,
                           const QPointF& anchor = QPointF());
ViewCameraState pannedView(ViewType type, ViewCameraState state, const QPointF& canvasPixels);
ViewCameraState orbitedView(ViewType type, ViewCameraState state, const QPointF& pixels);

// A 3D layer's turn, as the reference builds it (TransformationHelper::
// Rotation, Project.dll 0x180321170): its orientation as quaternions about X,
// then Y, then Z, then its rotations about X, Y and Z, every angle negated -
// so the Z turn is clockwise as the camera sees it, as in 2D.
QMatrix4x4 layerRotationAt(const composition::LayerTransform& transform, int frame);

// A 3D picture layer's sheet in the scene (layer transform, Y up): about the
// anchor for the turns, about the centre for the scale.
QMatrix4x4 layerPlaneMatrixAt(const composition::LayerTransform& transform, int frame);
// A 3D layer's - or a model node's - transform as a 4x4 (Y up): placed,
// aimed by its orientation, spun about its three axes, scaled, all about its
// anchor point, in the order the reference applies them.
QMatrix4x4 layerModelMatrixAt(const composition::LayerTransform& transform, int frame);
// Each node's matrix in the model's space at `frame`, its parents' included
// (Model3DTransformNode::ParentingTransformationAtTime): the identity for a
// node the layer did not change. Indexed like mesh.nodes.
QVector<QMatrix4x4> modelNodeMatrices(const composition::Layer& layer, const model3d::Mesh& mesh,
                                      int frame);
// What the layer's animations (Model3DAnimationAssetInstance) do to its
// model's nodes at `frame` of a shot at `fps`: each adds its Strength's share
// of its motion. `modelFile` is the model's own file, whose animations an
// instance without a file of its own plays. The identity for a node none
// moves.
QVector<QMatrix4x4> modelAnimationMatrices(const composition::Layer& layer, const model3d::Mesh& mesh,
                                           int frame, double fps, const QString& modelFile);
// The model as the layer shows it at `frame`: its skins carried by the
// animations' skeletons, then each node moved by the animations and its own
// transform (posedModelNodeMatrices) - what RenderWorker draws.
model3d::Mesh posedModelMesh(const composition::Layer& layer, const model3d::Mesh& mesh, int frame,
                             double fps, const QString& modelFile);
// The animations, then the nodes' own transforms: how the model is posed for
// model3d::Mesh::posed, as RenderWorker draws it and the Viewer outlines it.
QVector<QMatrix4x4> posedModelNodeMatrices(const composition::Layer& layer, const model3d::Mesh& mesh,
                                           int frame, double fps, const QString& modelFile);
// The clip's text effect, if it is a text clip.
const composition::Effect* textEffectOfClip(const composition::Clip& clip);

// Where a layer's picture lands on its shot's canvas (display pixels, Y down,
// from the top-left corner) at `frame`, as RenderWorker::renderClip and
// renderClipIn3D draw it: the content in its own units, centred on the layer
// origin (`local`: footage width x PAR by height, the text's glyph box, a
// plane's frame), and its four corners on the canvas (`quad`). A 2D layer
// carries the placement as `toCanvas`; a 3D one is seen through the shot's
// camera, and so is a model: the box of its posed points on the canvas. A
// grade layer covers the frame. Invalid for what draws nothing (camera,
// light, point, audio) and for a sheet behind the eye.
struct LayerGeometry
{
    bool valid = false;
    bool inScene = false;
    QRectF local;
    QTransform toCanvas;
    QPolygonF quad;
    // Where a 2D layer turns, from its position on the canvas (Y down): its
    // anchor point for footage and nested shots, nothing for planes and text.
    QPointF pivotOffset;
    // A model: scene units one canvas pixel covers at its centre's depth.
    double unitsPerPixel = 0.0;
    QRectF bounds() const { return quad.boundingRect(); }
};
LayerGeometry layerGeometry(const composition::Composition& composition,
                            const composition::Layer& layer, const media::MediaManager* media,
                            int frame);

// The transform that puts the layer's visual box (layerGeometry().bounds())
// at `box` instead, at `frame`: the position moves with the box's centre and
// the scale with its size - per axis for a layer turned by a multiple of 90
// degrees (axes swapped at 90/270), by the box's area otherwise. A 3D sheet
// moves by its on-screen shift taken back to its depth. False when the layer
// has no box.
bool fitLayerToBox(const composition::Composition& composition, const composition::Layer& layer,
                   const media::MediaManager* media, int frame, const QRectF& box,
                   QPointF* position, QPointF* scale);

} // namespace render
} // namespace openvegas
