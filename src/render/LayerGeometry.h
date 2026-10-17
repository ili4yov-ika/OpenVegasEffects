#pragma once

#include <QMatrix4x4>
#include <QPolygonF>
#include <optional>
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
// vtables 0x141325e70 / 0x141325dc0), for its tool modes (PanCameraToolMode,
// DollyCameraToolMode, RotateCameraToolMode, ZoomCameraToolMode,
// OrbitToolMode):
// - zoomedView: `steps` wheel notches zoom an axis view by 1 + 0.1 a notch
//   (its scale, at least 0.0002) about the point `anchor` picture pixels from
//   the middle (x right, y down; the reference zooms at the pointer), and
//   dolly the perspective camera 120 units a notch along where it looks;
// - pannedView: a drag of `canvasPixels` pans the camera across its view,
//   1 / scale units a pixel (a unit for the perspective camera), the scene
//   following the pointer;
// - orbitedView: the perspective camera turned 0.3 degrees a pixel about
//   `pivot` (the reference's orbit about the selected layers, or the
//   origin) - about the world's up across, about its own level side axis up
//   and down, never over the top - its view turning with it;
// - dolliedView: the perspective camera a unit a pixel of a vertical drag
//   along where it looks (up forward); an axis view zooms as dragZoomedView;
// - turnedView: the perspective camera turned where it stands, 0.1 degrees
//   a pixel - about its own X by the vertical drag, then its Y by the
//   horizontal one;
// - dragZoomedView: a vertical drag zooms the view - an axis view by
//   1 - 0.001 a pixel, the perspective one by 1 + 0.001 a pixel, at least
//   0.0002 either way (the perspective camera's field of view narrows as its
//   scale grows: tan(30 degrees) / scale is half its height).
ViewCameraState zoomedView(ViewType type, ViewCameraState state, double steps,
                           const QPointF& anchor = QPointF());
ViewCameraState pannedView(ViewType type, ViewCameraState state, const QPointF& canvasPixels);
ViewCameraState orbitedView(ViewType type, ViewCameraState state, const QPointF& pixels,
                            const QVector3D& pivot = QVector3D());
ViewCameraState dolliedView(ViewType type, ViewCameraState state, const QPointF& pixels);
ViewCameraState turnedView(ViewType type, ViewCameraState state, const QPointF& pixels);
ViewCameraState dragZoomedView(ViewType type, ViewCameraState state, const QPointF& pixels);

// A 3D layer's turn, as the reference builds it (TransformationHelper::
// Rotation, Project.dll 0x180321170): its orientation as quaternions about X,
// then Y, then Z, then its rotations about X, Y and Z, every angle negated -
// so the Z turn is clockwise as the camera sees it, as in 2D.
QMatrix4x4 layerRotationAt(const composition::LayerTransform& transform, int frame);

// The turn whose -Z points along `direction` with its Y kept towards the
// world's up - (-1, 0, 0) when it points straight up or down - as Alignment
// "Towards Target Position" aims a layer (TransformationHelper::
// AutoOrientAtTime, Project.dll 0x180323060: the inverse of the look-at
// FUN_180325550 builds from target - position and up (0, 1, 0)).
QMatrix4x4 aimAlong(const QVector3D& direction);
// The shortest turn taking `from` to `to` (FUN_180325a50 of Project.dll,
// about the origin): none when they agree, half a turn about `fallbackAxis`
// when they are opposite.
QMatrix4x4 turnFromTo(const QVector3D& from, const QVector3D& to, const QVector3D& fallbackAxis);
// What a layer's parent puts it under at `frame`
// (AbstractLayer::ParentTransformationAtTime, Project.dll 0x1802df0a0): the
// parent's world transform as its children take it - the parent's own
// parent's, then its position, its aim (a camera's, a light's or a point's
// Towards Target Position), its turn and its scale (not a camera's or a
// light's), a media or plane parent's anchor point taken off
// (AssetLayer::ParentingWorldTransformationAtTime 0x1802ea6b0) - or, for a
// 2D media, plane, text or point layer under a 3D parent, only a move to
// where the parent's origin shows in the frame through the shot's camera
// (ProjectPositionOntoLayer). The identity for a layer without a parent, or
// whose parent is missing or loops back. (The reference also converts the
// time into the parent's by their starts; here all layers share the shot's.)
QMatrix4x4 layerParentMatrixAt(const composition::Composition* composition, const composition::Layer& layer,
                               int frame);
// Where a layer stands in the scene at `frame`: its position under its
// parent.
QVector3D layerPositionAt(const composition::Composition* composition, const composition::Layer& layer, int frame);
// The turn in a matrix, its scale taken out.
QMatrix4x4 turnOf(const QMatrix4x4& matrix);
// A 2D layer's parent as a turn of the shot's canvas (`space` square units,
// Y down from the top left), to put before the layer's own placement.
QTransform layerCanvasParentAt(const composition::Composition* composition, const composition::Layer& layer,
                               int frame, const QSizeF& space);
// A model's placement in the scene: its parent's matrix, then
// layerModelMatrixAt.
QMatrix4x4 layerModelPlacementAt(const composition::Composition* composition, const composition::Layer& layer,
                                 int frame);
// A layer's turn in the scene at `frame` - its parent's turn included - with
// its Alignment:
// - a camera, a light or a point layer Towards Target Position aimed at its
//   Target (both under its parent) first, then turned by its own
//   orientation and rotations (position, AutoOrientAtTime, Rotation, as
//   CameraLayer/LightLayer/PointLayer::TransformationAtTime multiply them; a
//   point layer's +Z is what faces the target);
// - a 3D media, plane or text layer Towards Active Camera turned as the
//   shot's camera is, its sheet in the camera's plane and upright to it
//   (FluxCamera::OrientateToCamera, Flux.dll 0x180285a30);
// - Along Motion Path: its X, Y or -Z (Alignment Axis) turned the shortest
//   way along its motion from this frame to the next (TransformationHelper::
//   AlignToMotion 0x180323280, which looks 10 ms ahead; half a turn about
//   its -Z, or its X for the Z axis, when the motion runs against it);
// - Towards Layer: the side it faces with - a camera's and a light's -Z, a
//   picture's and a point's +Z - turned the shortest way to that layer's
//   position, then about that way until its up matches the other layer's
//   (TransformationHelper::AlignToTarget 0x180323650).
// Along Motion Path and Towards Layer turn the layer as it stands in the
// scene, by the positions under the parents.
// Otherwise layerRotationAt.
QMatrix4x4 layerTurnAt(const composition::Composition* composition, const composition::Layer& layer, int frame);
// A 2D layer's Rotation at `frame`, or - Along Motion Path - the angle that
// lays its X (or Y) along its motion to the next frame.
double layerAngleAt(const composition::Layer& layer, int frame);

// A 3D picture layer's sheet in the scene (layer transform, Y up): about the
// anchor for the turns, about the centre for the scale.
QMatrix4x4 layerPlaneMatrixAt(const composition::LayerTransform& transform, int frame);
// The same turned by `turn` instead of the layer's own rotation.
QMatrix4x4 layerPlaneMatrixAt(const composition::LayerTransform& transform, int frame, const QMatrix4x4& turn);
// A layer's sheet with its Alignment (layerTurnAt; Along Motion Path and
// Towards Layer turn it about its own centre once placed).
// Its Behaviors stand between its parent and its own placement, as the
// reference's world transform has them (ParentTransformationAtTime: the
// parent's world times the Behaviors' matrices; then the layer's own).
QMatrix4x4 layerSheetMatrixAt(const composition::Composition* composition, const composition::Layer& layer,
                              int frame, const media::MediaManager* media = nullptr);
// A 3D layer's - or a model node's - transform as a 4x4 (Y up): placed,
// aimed by its orientation, spun about its three axes, scaled, all about its
// anchor point, in the order the reference applies them.
QMatrix4x4 layerModelMatrixAt(const composition::LayerTransform& transform, int frame);
// The same turned by `turn` instead of the layer's own rotation (its
// Alignment: layerTurnAt).
QMatrix4x4 layerModelMatrixAt(const composition::LayerTransform& transform, int frame, const QMatrix4x4& turn);
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
// How a 2D layer's Behaviors move its picture at `frame`, as the renderer
// moves it (RenderWorker::applyClipBehaviors: MotionTrack's instance
// matrices, the verified native Behaviors in their order, then the simulated
// ones), on a canvas of the shot's display pixels (Y down). The Layout
// panel's box takes it in, as the reference's TransformWidget measures layers
// with AbstractLayer::WorldTransformationAtTime (Behaviors and AlignToTarget
// included; effects do not change it). Identity without any.
QTransform layerBehaviorTransformAt(const composition::Composition& composition,
                                    const media::MediaManager* media,
                                    const composition::Layer& layer, int frame);
// A 3D sheet's Behaviors in the scene: their matrices - from the frame's
// centre, Y up, in the shot's units - multiplied in the layer's order
// (AbstractLayer::ParentTransformationAtTime: B1 x B2 x ..., the last one
// nearest the layer). Identity for a 2D layer, for text drawn as a mesh by
// Geometry modules (its Behaviors stay on the drawn picture) and without any.
// `simulation`, when asked for, takes where its simulated Behaviors move the
// body (a translation in the scene): not among the Behaviors but the whole
// world's place (SimulateLayers / FUN_18036c010), applied after the parent.
QMatrix4x4 layerBehaviorMatrixAt(const composition::Composition& composition,
                                 const media::MediaManager* media,
                                 const composition::Layer& layer, int frame,
                                 QMatrix4x4* simulation = nullptr);
// The same for any layer, however it is drawn: what its children are put
// under along with it (ParentingWorldTransformationAtTime takes the parent's
// Behaviors in).
QMatrix4x4 layerOwnBehaviorMatrixAt(const composition::Composition& composition,
                                    const media::MediaManager* media,
                                    const composition::Layer& layer, int frame,
                                    QMatrix4x4* simulation = nullptr);
// Where the simulated body of `layer` is at `frame`, moved from where its
// world without the simulation puts it (plugin::simulateNativeBehaviorScene
// over every body of the shot). False when the layer is no body.
// Between frames (a motion blur sample) as FUN_18036c010 reads it: the
// body at the next whole frame taken back along its velocity, against the
// world's point in between. A run is kept per state of the shot and goes on
// from where it stopped (the shot's cache at +0x228).
bool layerSimulationOffsetAt(const composition::Composition& composition, const media::MediaManager* media,
                             const composition::Layer& layer, double frame, QVector3D* offset);
// A layer's world without any simulation - its own or its parents' - as
// SimulateLayers measures it (PreAlignToMotion with the simulation off):
// the parent's world x its Behaviors x its own placement.
QMatrix4x4 layerWorldWithoutSimulationAt(const composition::Composition* composition,
                                         const composition::Layer& layer, int frame);
// Above zero while a world without simulation is being measured: no
// simulation is run then, a parent's neither.
int& simulationSuppressed();
// Whether the layer has simulated Behaviors of its own - a body, whose
// matrix takes the place of its whole world, its parents' simulation
// included (the body's forces come to it instead).
bool layerIsSimulatedBody(const composition::Layer& layer);
// Added to the frame a simulated body is read at while a motion blur
// sample of a 3D sheet is drawn (its sheet matrix takes the body in).
double& simulationFrameShift();
// Whether a 3D layer's clip is drawn as a sheet in the scene (renderClipIn3D)
// - not text Geometry modules make a mesh of, nor a model.
bool layerDrawnAsSheet(const composition::Layer& layer, const composition::Clip* clip);

// Where a ray from `origin` along `direction` first meets the shot's
// visible 3D layers at `frame` - a media, plane or text sheet where its
// content is (layerGeometry's box on the layer's sheet), a model's posed
// triangles - as FluxSelect::ClickIntersect finds the point of the scene
// under a click (VegasEffects.exe 0x140953ad0, for Orbit Around Clicked
// Point); nothing when it meets none.
std::optional<QVector3D> sceneRayHit(const composition::Composition& composition, const media::MediaManager* media,
                                     int frame, const QVector3D& origin, const QVector3D& direction);

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
