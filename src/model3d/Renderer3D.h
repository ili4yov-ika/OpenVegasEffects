#pragma once

#include <QColor>
#include <QHash>
#include <QImage>
#include <memory>

#include <QMatrix4x4>
#include <QPointF>
#include <QSize>
#include <QVector>
#include <QVector3D>
#include <QVector4D>

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
    double fieldOfViewDegrees = 39.6;   // vertical
    double nearPlane = 1.0;
    double farPlane = 100000.0;
    // A view of the scene along an axis (the Viewer's Front, Top, ...):
    // parallel rays, `orthographicHeight` scene units from the bottom of the
    // picture to its top, whatever its pixel size.
    bool orthographic = false;
    double orthographicHeight = 0.0;

    QMatrix4x4 viewProjection(const QSize& canvas) const;
};

// One light of the scene, as Flux's lighting shader takes it (the GLSL
// Illuminant function; its uniforms set by Flux.dll 0x1804a5100 from what
// 0x180483820 reads off the light layer), in scene units rather than the eye
// space the shader works in.
struct Light
{
    // The reference's numbers (LightLayer's LightType, the shader's
    // lightType and lightFallOff).
    enum class Type { Ambient = 1, Point = 2, Spot = 3, Directional = 4 };
    enum class Falloff { None = 1, Linear = 2, Curve = 3 };

    Type type = Type::Point;
    QVector3D position;                                  // lightPosition
    QVector3D direction = QVector3D(0.0f, 0.0f, -1.0f);  // where it points
    QColor color = QColor(255, 255, 255);
    // lightIntensity: the layer's Intensity percent / 100 * 2.5.
    double intensity = 2.5;
    Falloff falloff = Falloff::None;
    double reach = 1000.0;
    // spotParams: the cosine of half the cone, of the part of it the
    // feather leaves whole, and 1 / their difference (0 when they meet).
    double spotOuterCosine = 0.0;
    double spotInnerCosine = 0.0;
    double spotScale = 0.0;
    // Shadows: whether it casts them, their opacity and diffusion (0..1)
    // and colour.
    bool castShadows = false;
    double shadowOpacity = 0.5;
    double shadowDiffusion = 0.0;
    QColor shadowColor = QColor(0, 0, 0);

    // The shader's intensity for a layer's Intensity percent.
    static double intensityOf(double percent) { return percent / 100.0 * 2.5; }
    // spotParams for a Cone Angle (degrees) and Feather (percent):
    // cos(half), cos((1 - feather) * half), as Flux computes them.
    void setCone(double angleDegrees, double featherPercent);
};

// How a surface takes the lights: the layer's Material (Flux.dll
// 0x180474d80) - Illuminated, Receives Shadows, and ADSS, its matAmbient,
// matDiffuse, matSpecular and matShininess percentages / 100 - with the
// reference's defaults. Billboarded Lights lights it as if it faced each
// light (the shader's isIllum 0.8, Flux.dll 0x1804a4cf0). `shadowBias`
// scales how far behind a caster it may lie and still not be shadowed by it
// (a model's Self Shadow Bias; 1 for everything else).
struct SurfaceLighting
{
    bool illuminated = true;
    bool receiveShadows = true;
    bool billboardedLights = false;
    double shadowBias = 1.0;
    double ambient = 1.0;
    double diffuse = 0.5;
    double specular = 0.5;
    double shininess = 1.0;
    QColor emissive = QColor(0, 0, 0);
};

// A light's view of the scene's shadow casters, which a surface looks up to
// learn how much of the light it is in the shadow of.
class ShadowMap
{
public:
    virtual ~ShadowMap() = default;
    // shadowAmount at `position` (0 lit .. 1 in shadow), before the light's
    // Shadow Opacity.
    virtual double amountAt(const QVector3D& position) const = 0;
    // The same for a surface whose depth tolerance is `bias` times the usual
    // (SurfaceLighting::shadowBias).
    virtual double amountFor(const QVector3D& position, double bias) const
    {
        Q_UNUSED(bias);
        return amountAt(position);
    }
};

// What casts shadows: triangles in scene units, three corners each, and for
// a sheet where each corner falls on its picture (0..1, y down) - the clear
// parts of the picture (alpha 0.5 or less, which Flux's shadow depth pass
// discards) cast none. Without a picture the triangles are solid.
struct ShadowCaster
{
    QVector<QVector3D> corners;
    QVector<QPointF> uvs;
    QImage picture;
};

// The light's shadow maps of `casters`, as Flux's shadow pass makes them:
// one for a directional light (parallel, fitted to the casters) or a spot
// whose half cone has a cosine above 0.867, six - a cube about the light -
// for a point light or a wider spot; `size` texels square each. A surface
// looks one up with Flux's PCF: its own texel weighted 10 and a square of
// (2 * range + 1)^2 more, range = int(max(20 * Shadow Diffusion, 1) / 2 +
// 0.5), 1/2048 of the map apart, each in shadow where a caster is nearer the
// light than the surface. Null when the light casts no shadows or there is
// nothing to cast one.
std::shared_ptr<const ShadowMap> makeShadowMap(const Light& light, const QVector<ShadowCaster>& casters,
                                               int size);

struct Reflections;

// The lights a surface is drawn with, its Material, the lights' shadow
// maps (one per light, null where it casts none) and, for a model, what it
// reflects (null: nothing).
struct Lighting
{
    QVector<Light> lights;
    SurfaceLighting surface;
    QVector<std::shared_ptr<const ShadowMap>> shadows;
    std::shared_ptr<const Reflections> reflections;
};

// How a model's material answers each light, as Flux hands it to the
// shader (Flux.dll 0x180370e10, 0x1804a7460): a model's SpecularShininess is
// (1, the material's Shininess) - its specular colour taken whole and its
// own exponent, rather than the layer's Specular and Shininess - and the
// Cook-Torrance and physically based settings are the material's.
struct Reflectance
{
    IlluminationModel model = IlluminationModel::Phong;
    double specular = 1.0;      // ADSS.z
    double shininess = 1.0;     // ADSS.w
    // Metallic-Roughness and Specular-Glossiness: the surface's roughness
    // (its maps applied) and metalness.
    double roughness = 1.0;
    double metallic = 0.0;
    // Cook-Torrance: roughnessValue, specularReflectance, fresnelTerm.
    double cookTorranceRoughness = 0.05;
    double cookTorranceSpecularReflectance = 0.05;
    QVector3D cookTorranceFresnel = QVector3D(0.91318f, 0.92149f, 0.92452f);
};

// Flux's Illuminant, for a surface at `position` facing `normal` seen from
// `eye`: Phong for a sheet (and for a model's Phong material), the other
// illumination models for a model's material that has them (`reflectance`). Colours are
// straight RGBA in 0..1: `ambient`, `diffuse` and `specular` the surface's
// own (a sheet passes its pixel for all three, as the reference does),
// `emissive` added to a lit surface.
//
// No lights: the surface as it is, unlit. Otherwise each light adds, to an
// illuminated surface, an ambient light lightColor * intensity * ambient *
// ADSS.x and the others lightColor * (NdotL * diffuse * ADSS.y + ADSS.z *
// specular * pow(R.V, ADSS.w)) * intensity * attenuation - Linear falloff
// 1 - d / reach, Curve (reach - d)^2 / reach^2, nothing beyond reach, a
// spot's (cos - cosOuter) * spotScale outside its feathered part - the share
// of it in shadow taking the light's shadow colour instead; the sum is
// clamped. A surface that is not illuminated takes only the shadows,
// laid over it in their colours. Either way the alpha is the surface's.
//
// Cook-Torrance swaps the specular term for Fresnel(V.H) * G * Beckmann /
// (N.V * N.L) * specular * ADSS.z * specularReflectance. The physically
// based models first split the colour: Metallic-Roughness diffuse * (1 -
// 0.04) * (1 - metallic) and a specular colour from 0.04 to the base colour
// by metalness, Specular-Glossiness diffuse * (1 - the largest specular
// channel) and the specular colour itself; each light then gives N.L *
// ((1 - F) * diffuse / pi + F * visibility * GGX distribution) (Flux's
// GetPointShade), and an ambient light lights the split diffuse colour.
QVector4D illuminate(const Lighting& lighting, const QVector4D& ambient, const QVector4D& diffuse,
                     const QVector4D& specular, const QVector3D& emissive, const QVector3D& position,
                     const QVector3D& normal, const QVector3D& eye, const Reflectance* reflectance = nullptr);

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

// What a mesh shows at each pixel of renderMesh's picture, for the 3D
// scene's ambient occlusion: the nearest surface's point and normal in the
// scene (the normal turned to the eye, as a two-sided face shows it), its
// opacity (the material's and its maps', not how much of the pixel it
// covers) and its depth (NDC); a pixel showing nothing has an infinite one.
struct SurfaceBuffer
{
    QSize size;
    QVector<QVector3D> positions;
    QVector<QVector3D> normals;
    QVector<float> alphas;
    QVector<float> depths;

    void reset(const QSize& canvas);
    bool has(int index) const;
};

// Rasterises `mesh` into an image the size of the canvas, transparent where
// nothing was drawn so the result composites over the layers beneath.
//
// `modelMatrix` places the mesh in the scene - the layer's own 3D transform.
// Depth is resolved with a z-buffer, so faces do not depend on draw order.
// With `samples` above 1 it is multisampled as a GPU's MSAA target is:
// coverage and depth at each sample (samplePositions), the face shaded once
// per pixel, the samples averaged.
//
// `lighting` lights it as Flux does (illuminate): the material's diffuse
// colour (times its map) as the diffuse colour, its ambient colour (times the
// diffuse map and the occlusion map) as the ambient one, its specular colour
// (times its map) as the specular one, its emissive colour added to the
// layer's. Without lights it is drawn unlit, as the reference draws a model
// in a shot with no light layer.
//
// With `surfaces` it also notes what each pixel shows (SurfaceBuffer), over
// what the buffer holds from an earlier mesh drawn the same size, the nearer
// kept.
QImage renderMesh(const Mesh& mesh, const QMatrix4x4& modelMatrix, const Camera& camera,
                  const QSize& canvas, const Lighting& lighting, ShadingMode mode,
                  double opacity = 1.0, const Fog& fog = Fog(), int samples = 1,
                  SurfaceBuffer* surfaces = nullptr);

// A layer's ambient occlusion settings (its Material's Ambient Occlusion
// group) as Flux takes them (Flux.dll 0x180474a80): Samples, Sample Radius
// and Blur Radius as they are, Intensity, Depth Scale and Depth Falloff
// divided by 100, Bias as it is.
struct AmbientOcclusion
{
    int samples = 4;
    double sampleRadius = 15.0;
    double intensity = 0.5;
    double depthScale = 0.1;
    double bias = 0.01;
    int blurRadius = 5;
    double depthFalloff = 0.1;
};

// Flux's ConvertVec2LatLong: where a direction falls on a lat-long
// (equirectangular) picture - u = atan2(x, z) / pi / 2 + 0.5 (0 when x or z
// is 0), v = 1 - acos(y / |d|) / pi, so +Y is the top row and +Z the middle.
QVector2D latLongOf(const QVector3D& direction);

// The colour (0..1) a lat-long picture (RGBA8888, straight) shows along
// `direction`, bilinear and repeating both ways, as Flux binds a model's
// reflection map (GL_REPEAT on S and T, Flux.dll 0x180370e10) - so straight
// up or down it meets the other edge's row half way.
QVector3D environmentColor(const QImage& latLong, const QVector3D& direction);

// The light a lat-long picture throws on a surface facing each way, as
// Flux's diffuse environment term takes it (EnvironmentMapRenderData,
// Flux.dll 0x1804b9dc0): the picture projected on the nine spherical
// harmonics a channel (0x180435e50, scaled by 4 pi over the samples at
// 0x180436790) and turned into Ramamoorthi and Hanrahan's matrices
// (0x180437360), E(n) = (n, 1)^T M (n, 1) for a unit normal n.
struct Irradiance
{
    QMatrix4x4 red;
    QMatrix4x4 green;
    QMatrix4x4 blue;
    QVector3D at(const QVector3D& normal) const;
};
Irradiance irradianceOf(const QImage& latLong);

// What a model reflects, as Flux's model shader takes it in its last pass
// (Flux.dll 0x1804c1ce0; the amounts set at 0x180370e10):
// - `levels`: the lat-long reflection map round the model (a Spherical
//   material's), its mip levels each half the last (MaxReflectionLOD 4),
//   and its irradiance;
// - `planar`: a Planar material's map of the scene mirrored in its plane,
//   by material index, the shot's size and lined up with it.
// With either, a material adds - all times its diffuse alpha -
// - its irradiance on the normal (n, 1)^T M (n, 1), not below 0, times its
//   diffuse colour and Diffuse Reflectivity;
// - a Planar material with its map: the map's colour times its alpha, times
//   the specular colour and Specular Reflectivity;
// - otherwise IBLSpecularColor along the view reflected in the normal, times
//   Specular Reflectivity: Phong and Cook-Torrance the map times the
//   specular colour; Metallic-Roughness and Specular-Glossiness the map at
//   LOD roughness * 4 times (F * A + B), F = FresnelSchlickRoughness(N.V,
//   F0, roughness) and (A, B) the split-sum BRDF (Karis's approximation of
//   Flux's BRDF table here);
// - with Refraction Amount: the map along refract(to the eye, n, 1 / Index
//   of Refraction) - LOD 0 for Phong, roughness * 4 otherwise - times the
//   amount.
// The physically based models drop their unlit colour where a map is (the
// shader's useEnvMap).
struct Reflections
{
    QVector<QImage> levels;
    Irradiance irradiance;
    QHash<int, QImage> planar;

    // The map along `direction` at mip level `lod` (trilinear).
    QVector3D at(const QVector3D& direction, double lod) const;
    // A lat-long picture made a reflection map: its levels and irradiance.
    static Reflections fromLatLong(const QImage& latLong);
};

// The ambient occlusion of a picture of the scene (1 open, down to 0), as
// Flux's AmbientOcclusionRenderer draws it (Flux.dll 0x180478960; its
// shaders built at 0x180478030) from a G-buffer: each pixel's point and
// normal in eye space (the camera at the origin looking down -Z) and its
// opacity (`weights`). A pixel weighing less than 0.9 is left open; one
// weighing less than 0.8 occludes nothing.
//
// For a pixel at p with normal n (texture coordinates uv, 0..1 up from the
// bottom left), `samples` directions - (1, 0), (-1, 0), (0, 1), (0, -1),
// (1, 1), (-1, 1), (-1, -1), (1, -1), (0.7, 0.7), (-0.7, -0.65), each
// reflected about the pixel's random unit vector - scaled by
// rad = sampleRadius / p.z (texture units), four taps along each (a quarter
// and three quarters of it, and half and all of it turned 45 degrees); a tap
// meeting a point s adds max(0, n . normalize(s - p) - bias)
// / (1 + |s - p| depthScale) * intensity * 10. The sum, divided by
// samples squared, taken from 1 and clamped, is then blurred over
// (2 blurRadius + 1)^2 pixels: a Gaussian with sigma (2 blurRadius + 1) / 3,
// each tap weighed again by 1 / (depthFalloff + its difference in eye depth);
// a pixel showing nothing stays open.
QVector<float> ambientOcclusionMap(const QSize& size, const QVector<QVector3D>& positions,
                                   const QVector<QVector3D>& normals, const QVector<float>& weights,
                                   const AmbientOcclusion& settings);

// A model's texture as the project lets it be drawn (Project Settings
// "Model Texture Max Size", ProjectRenderSettings::modelTextureMaxSize): a
// larger one scaled smoothly to fit `maxSize` on its longer side, its shape
// kept; the same image when it is within that or `maxSize` is not positive.
QImage limitTextureSize(const QImage& image, int maxSize);

// The most samples renderMesh takes a pixel at.
constexpr int kMaxSamples = 32;
// Colour samples per pixel of a project's 3D antialiasing mode 1..9
// (ProjectRenderSettings::antialiasingMode: 4x/8x/16x/32x MSAA,
// 8x/8xQ/16x/16xQ/32x CSAA).
int antialiasingSamples(int mode);
// Where in a pixel (0..1, Y down) its samples lie: the standard GPU patterns
// for 4, 8 and 16, a rotated n-rooks grid for other counts.
QVector<QPointF> samplePositions(int samples);

// Camera that frames a bounding sphere of `radius` about `centre` head-on.
// Used for a preview of the model on its own.
Camera defaultCameraFor(const QVector3D& centre, double radius, const QSize& canvas);

// A camera's depth of field, as Flux's DepthOfFieldRenderer computes it.
// Its blur term (the GenerateBlurTerm / ConvertDepthMapToBlurTerm shaders):
// for a surface at eye depth z (scene pixels), with zScale = 1 / pixelsPerMM,
// d = |z| zScale + 50.25 and Zfocus = 1 / (1/f - 1/focus) + focus,
//   CoC = |lensDiameter f (Zfocus - d) / (Zfocus (d - f))|   (mm)
//   blur = clamp(clamp(CoC * pixelsPerMM * blurAmount, 0, maxCoC) / maxCoC, 0, 1)
// with f the camera's focal length (zoom * film width / frame width, mm),
// lensDiameter its aperture / pixelsPerMM and focus its Focus Distance /
// pixelsPerMM (FluxCamera's settings, Flux.dll 0x1803c1ac0); nothing behind
// the scene is blurred. The picture is then blurred across and down (the
// BoxBlurPass shader): each pixel over sizeCoC = (its neighbourhood's
// mean blur) * maxCoC / 2 pixels either side, every tap weighted by its own
// blur, so what is in focus does not bleed into what is not; around the
// scene, a blurred surface spreads into the empty pixels within its circle
// and fades there (a choice of this port: Flux blurs layer by layer). (Flux's
// neighbourhood is a pixel either side; its taps a quarter pixel apart, here
// whole pixels.)
struct DepthOfField
{
    bool enabled = false;
    double focalLength = 35.0;
    double lensDiameter = 6.25;
    double focusDistance = 600.0;
    double blurAmount = 1.0;
    // The largest circle of confusion, in pixels of the shot: a choice of
    // this port (Flux's value is not recovered).
    double maxCoC = 64.0;
    double pixelsPerMM = 2.8352;

    double blurTerm(double eyeDepth) const;
};

// Blurs a premultiplied RGBA picture of the 3D scene by `dof`; `distances`
// are each pixel's distance from the eye (infinite where the scene is not),
// turned into eye depth through `camera`; `scale` is the frame's pixels per
// pixel of the shot (a reduced preview blurs less).
void applyDepthOfField(QImage& premultiplied, const QVector<float>& distances, const Camera& camera,
                       const DepthOfField& dof, double scale);

// The camera a composite shot gets when it has no camera layer.
//
// It stands the distance back at which the z = 0 plane fills the frame exactly,
// so a 3D layer left at z = 0 is the same size as the 2D layer it was before
// its dimension was switched - which is what makes the switch a no-op until
// something is actually moved in depth.
Camera defaultCameraForCanvas(const QSize& canvas);

} // namespace model3d
} // namespace openvegas
