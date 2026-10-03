#pragma once

#include <QColor>
#include <QImage>
#include <QMatrix4x4>
#include <QSize>
#include <QVector3D>

#include "model3d/Mesh.h"

namespace openvegas {
namespace model3d {

// The camera a 3D layer is seen through.
//
// The reference requires one: "3D composite shots must have at least one
// camera. All 3D layers will be converted to 2D or removed if all cameras are
// removed." A composition without a CameraLayer therefore gets the default
// below rather than nothing to look through.
struct Camera
{
    QVector3D position = QVector3D(0.0f, 0.0f, 1000.0f);
    QVector3D target;
    QVector3D up = QVector3D(0.0f, 1.0f, 0.0f);
    double fieldOfViewDegrees = 39.6;   // the 50 mm equivalent a camera opens at
    double nearPlane = 1.0;
    double farPlane = 100000.0;

    QMatrix4x4 viewProjection(const QSize& canvas) const;
};

// The light a model is shaded by. One directional light stands for the
// reference's "defaultLight", which is what a composite shot without a
// LightLayer renders with.
struct Light
{
    QVector3D direction = QVector3D(-0.4f, -0.6f, -1.0f);   // travelling direction
    QColor color = QColor(255, 255, 255);
    double intensity = 1.0;
    // Reference "Ambient Color" on the material is modulated by this, so a
    // surface facing away is dim rather than black.
    double ambient = 0.25;
};

// A composite shot's fog (CompositionAsset/RenderSettings Fog*), applied the
// way Flux's ComputeFoggedFragment does to every 3D fragment: by its distance
// d from the eye, the surface keeps the share
//   Linear          (far - d) / (far - near)
//   Exponential     exp(-density * d * 0.001)
//   Exponential^2   exp(-density^2 * d^2 * 0.001)
// of its colour, clamped to 0..1, and the rest becomes the fog colour
// (mix(fogColor * alpha, colour, factor), alpha kept).
struct Fog
{
    enum class Falloff { Linear = 0, Exponential = 1, ExponentialSquared = 2 };
    bool enabled = false;
    double nearDistance = 900.0;
    double farDistance = 2000.0;
    double density = 1.0;
    Falloff falloff = Falloff::Linear;
    QColor color = Qt::black;

    // Share of the surface's own colour left at `distance`; 1 without fog.
    double factor(double distance) const;
    // Fogs one straight-alpha RGBA pixel seen at `distance`.
    void apply(uchar* rgba, double distance) const;
};

// How the model is drawn. The reference's viewer offers the same choice
// between a solid preview and a wireframe one while a model is being placed.
enum class ShadingMode
{
    Shaded,
    Wireframe,
};

// Rasterises `mesh` into an image the size of the canvas, transparent where
// nothing was drawn so the result composites over the layers beneath.
//
// `modelMatrix` places the mesh in the scene - the layer's own 3D transform.
// Depth is resolved with a z-buffer, so faces do not depend on draw order.
QImage renderMesh(const Mesh& mesh, const QMatrix4x4& modelMatrix, const Camera& camera,
                  const QSize& canvas, const Light& light, ShadingMode mode,
                  double opacity = 1.0, const Fog& fog = Fog());

// Camera that frames a bounding sphere of `radius` about `centre` head-on.
// Used for a preview of the model on its own.
Camera defaultCameraFor(const QVector3D& centre, double radius, const QSize& canvas);

// The camera a composite shot gets when it has no camera layer.
//
// It stands the distance back at which the z = 0 plane fills the frame exactly,
// so a 3D layer left at z = 0 is the same size as the 2D layer it was before
// its dimension was switched - which is what makes the switch a no-op until
// something is actually moved in depth.
Camera defaultCameraForCanvas(const QSize& canvas);

} // namespace model3d
} // namespace openvegas
