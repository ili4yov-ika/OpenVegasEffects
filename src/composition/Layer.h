#pragma once

#include "composition/Effect.h"

#include "core/Identifier.h"

#include <array>

#include <QColor>
#include <QCoreApplication>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <QVector>
#include <cmath>
#include <memory>

namespace openvegas {
namespace composition {

class Composition;

struct Clip
{
    core::Identifier mediaId;
    double startSeconds = 0.0;
    double durationSeconds = 0.0;
    QVector<Effect> effects;

    // Where inside the media this clip starts reading. The Slip tool moves the
    // content under a clip without moving the clip itself, and this is what it
    // moves: the reference's slip edit changes a clip's source in-point and
    // leaves its position and length alone.
    double sourceStartSeconds = 0.0;

    // Audio-clip properties (reference AudioObject's per-clip PropertyManager):
    // "audioLevel" (float, default 0) and "speed" (int, default 1). They exist
    // on every clip but only audio clips present them in the timeline.
    double audioLevel = 0.0;
    // Playback rate. The Rate Stretch tool changes a clip's length and this
    // together, so the same content plays over a different span of timeline.
    double speed = 1.0;

    // A composite-shot clip renders another composition as its source. The
    // shared pointer also gives nested shots stable identity while several
    // timeline tabs refer to the same model.
    std::shared_ptr<Composition> nestedComposition;
    // Kept separately while a project is being loaded; resolved to the shared
    // composition object after all embedded shots have been read.
    core::Identifier nestedCompositionId;

    double endSeconds() const { return startSeconds + durationSeconds; }
};

enum class MaskShape
{
    Rectangle,
    RoundedRectangle,
    Ellipse,
    Polygon,
    Star,
    Freehand,
    // A picture's alpha over `bounds` (imagePath; outside it nothing shows):
    // a Photoshop layer's raster masks as an import makes them.
    Image,
};

// A mask is stored in composition pixel coordinates. This matches the Viewer
// rulers and keeps the path stable while the layer itself is animated.
struct LayerMask
{
    core::Identifier id = core::Identifier(
        QUuid::createUuid().toString(QUuid::WithoutBraces));
    QString name = QStringLiteral("Mask");
    MaskShape shape = MaskShape::Rectangle;
    QRectF bounds;
    QVector<QPointF> points;
    bool enabled = true;
    bool inverted = false;
    double opacity = 1.0;
    double feather = 0.0;
    double expansion = 0.0;
    // For MaskShape::Image: the picture, a path media::readStillImage reads.
    QString imagePath;
};

// Blend modes offered for a layer. The list lives beside the model because a
// Layer stores its mode by name; RenderManager maps each of these onto a
// QPainter composition mode. Keep the two in step - a name with no mapping
// composites as plain source-over. Shown translated (blendModeDisplayName).
inline QStringList blendModeNames()
{
    return QStringList{
        QT_TRANSLATE_NOOP("BlendMode", "None"),        QT_TRANSLATE_NOOP("BlendMode", "Normal"),
        QT_TRANSLATE_NOOP("BlendMode", "Add"),         QT_TRANSLATE_NOOP("BlendMode", "Screen"),
        QT_TRANSLATE_NOOP("BlendMode", "Multiply"),    QT_TRANSLATE_NOOP("BlendMode", "Overlay"),
        QT_TRANSLATE_NOOP("BlendMode", "Lighten"),     QT_TRANSLATE_NOOP("BlendMode", "Darken"),
        QT_TRANSLATE_NOOP("BlendMode", "Color Dodge"), QT_TRANSLATE_NOOP("BlendMode", "Color Burn"),
        QT_TRANSLATE_NOOP("BlendMode", "Hard Light"),  QT_TRANSLATE_NOOP("BlendMode", "Soft Light"),
        QT_TRANSLATE_NOOP("BlendMode", "Difference"),  QT_TRANSLATE_NOOP("BlendMode", "Exclusion"),
    };
}

// The name a blend mode is shown by; the mode itself stays stored by its
// English name above.
inline QString blendModeDisplayName(const QString& mode)
{
    return QCoreApplication::translate("BlendMode", mode.toUtf8().constData());
}

// Whether a layer lives in the composition's plane or in its 3D scene.
//
// The reference exposes this per layer as the "Dimension" command in the layer
// context menu (141301938) and keeps the choice in the layer's "dimension"
// property. A 2D layer shows two axes for Position and Scale; a 3D one shows
// three and gains rotation about all three axes.
enum class LayerDimension
{
    TwoD,
    ThreeD,
};

// Animatable transform properties, in the order the reference lists them in the
// Controls panel and under Transform in the timeline layer tree
// (SAMPLES/screenshots/crop_screenshot.png). Shared so the inspector and the
// timeline resolve a property to its curve the same way.
//
// Orientation and the per-axis rotations come from the reference's own property
// names - "orientation", "rotationX", "rotationY", "rotationZ" - and are shown
// only for a 3D layer. Rotation is that rotationZ, which is why a 2D layer
// labels it plainly "Rotation" and a 3D one "Rotation (Z)".
enum class TransformProperty
{
    Opacity,
    AnchorPoint,
    Position,
    Scale,
    Orientation,
    RotationX,
    RotationY,
    Rotation,      // reference property "rotationZ"
    // Reference property "audioLevel" (dB). Not part of Transform: the
    // timeline lists it alone under the layer's Audio group, but it animates
    // through the same curve plumbing as the properties above.
    AudioLevel,
    // Reference property "target": the point a camera, a light or a point
    // layer turns to with Alignment "Towards Target Position". Only those
    // layers show it, after Position.
    Target,
};

// Properties every layer shows, in reference order.
inline QVector<TransformProperty> transformPropertiesFor(LayerDimension dimension)
{
    if (dimension == LayerDimension::ThreeD) {
        return {TransformProperty::Opacity,     TransformProperty::AnchorPoint,
                TransformProperty::Position,    TransformProperty::Scale,
                TransformProperty::Orientation, TransformProperty::RotationX,
                TransformProperty::RotationY,   TransformProperty::Rotation};
    }
    return {TransformProperty::Opacity, TransformProperty::AnchorPoint,
            TransformProperty::Position, TransformProperty::Scale, TransformProperty::Rotation};
}

// 2 for the point-valued properties, 3 once the layer is in the 3D scene, and
// 1 for the scalars. Orientation is always three angles.
inline int axisCount(TransformProperty prop, LayerDimension dimension = LayerDimension::TwoD)
{
    switch (prop) {
    case TransformProperty::AnchorPoint:
    case TransformProperty::Position:
    case TransformProperty::Scale:
        return dimension == LayerDimension::ThreeD ? 3 : 2;
    case TransformProperty::Orientation:
    case TransformProperty::Target:
        return 3;
    case TransformProperty::Opacity:
    case TransformProperty::RotationX:
    case TransformProperty::RotationY:
    case TransformProperty::Rotation:
    case TransformProperty::AudioLevel:
        break;
    }
    return 1;
}

// Source strings are marked under a shared "Transform" context so lupdate can
// extract them once and both the Controls panel and the timeline show the same
// wording. Translate at the call site with QCoreApplication::translate - a bare
// tr() taking this function's result is invisible to lupdate and would leave
// the labels untranslated.
// A 3D layer names the Z rotation "Rotation (Z)", because it sits beside the
// other two axes; a 2D layer has only the one and calls it "Rotation".
inline const char* transformPropertyName(TransformProperty prop,
                                         LayerDimension dimension = LayerDimension::TwoD)
{
    switch (prop) {
    case TransformProperty::Opacity:     return QT_TRANSLATE_NOOP("Transform", "Opacity");
    case TransformProperty::AnchorPoint: return QT_TRANSLATE_NOOP("Transform", "Anchor Point");
    case TransformProperty::Position:    return QT_TRANSLATE_NOOP("Transform", "Position");
    case TransformProperty::Scale:       return QT_TRANSLATE_NOOP("Transform", "Scale");
    case TransformProperty::Orientation: return QT_TRANSLATE_NOOP("Transform", "Orientation");
    case TransformProperty::RotationX:   return QT_TRANSLATE_NOOP("Transform", "Rotation (X)");
    case TransformProperty::RotationY:   return QT_TRANSLATE_NOOP("Transform", "Rotation (Y)");
    case TransformProperty::Rotation:
        return dimension == LayerDimension::ThreeD
            ? QT_TRANSLATE_NOOP("Transform", "Rotation (Z)")
            : QT_TRANSLATE_NOOP("Transform", "Rotation");
    // AssetLayerGroupFactory's label for audioLevel, under "Audio".
    case TransformProperty::AudioLevel:  return QT_TRANSLATE_NOOP("Transform", "Level");
    case TransformProperty::Target:      return QT_TRANSLATE_NOOP("Transform", "Target");
    }
    return "";
}

// Layer transform, read from the reference's per-layer <PropertyManager>
// (properties "anchorPoint", "position", "scale", "rotationZ").
//
// Its coordinate system has the origin at the centre of the composition and Y
// pointing up, so a position of (0, 440) puts the layer above the middle. Scale
// is a percentage of the media's own size, not of the canvas.
struct LayerTransform
{
    QPointF anchorPoint;                    // pivot, relative to the layer centre
    QPointF position;                       // offset from the composition centre
    QPointF scalePercent = {100.0, 100.0};
    double rotationDegrees = 0.0;           // reference property "rotationZ"

    // Third axis, used once the layer's dimension is 3D. Kept beside the 2D
    // fields rather than replacing them: the reference stores rotation as three
    // separate properties too, and a 2D layer simply never reads these.
    double positionZ = 0.0;
    double anchorPointZ = 0.0;
    double scaleZPercent = 100.0;
    double rotationXDegrees = 0.0;          // reference property "rotationX"
    double rotationYDegrees = 0.0;          // reference property "rotationY"
    // Reference property "orientation": three angles applied before the
    // per-axis rotations, which is what lets a layer be aimed and then spun.
    double orientationX = 0.0;
    double orientationY = 0.0;
    double orientationZ = 0.0;

    // Curves, set when the matching property carries an <Animation> block and
    // empty when it is static. A point property is kept as one curve per axis:
    // the keyframe model interpolates numbers, and the reference's own spatial
    // interpolation for points is not ported.
    KeyFrameList positionXCurve;
    KeyFrameList positionYCurve;
    KeyFrameList positionZCurve;
    KeyFrameList scaleXCurve;
    KeyFrameList scaleYCurve;
    KeyFrameList scaleZCurve;
    KeyFrameList rotationCurve;             // rotationZ
    KeyFrameList rotationXCurve;
    KeyFrameList rotationYCurve;
    KeyFrameList orientationXCurve;
    KeyFrameList orientationYCurve;
    KeyFrameList orientationZCurve;
    KeyFrameList opacityCurve;   // percent, as the reference stores it

    // "audioLevel" in dB (TransformProperty::AudioLevel). Its keys are
    // composition frames like every curve here; a clip's own level adds on.
    double audioLevel = 0.0;
    KeyFrameList audioLevelCurve;

    // "target" (TransformProperty::Target), a camera's, a light's or a point
    // layer's: (0, 0, 0) as the reference creates it.
    double targetX = 0.0;
    double targetY = 0.0;
    double targetZ = 0.0;
    KeyFrameList targetXCurve;
    KeyFrameList targetYCurve;
    KeyFrameList targetZCurve;

    // Curve backing one axis of a property, or nullptr when the model carries
    // none: Anchor Point has no curve and the serializer writes none, so it is
    // editable but not animatable.
    KeyFrameList* curve(TransformProperty prop, int axis)
    {
        switch (prop) {
        case TransformProperty::Opacity:   return &opacityCurve;
        case TransformProperty::Rotation:  return &rotationCurve;
        case TransformProperty::RotationX: return &rotationXCurve;
        case TransformProperty::RotationY: return &rotationYCurve;
        case TransformProperty::Position:
            return axis == 0 ? &positionXCurve : (axis == 1 ? &positionYCurve : &positionZCurve);
        case TransformProperty::Scale:
            return axis == 0 ? &scaleXCurve : (axis == 1 ? &scaleYCurve : &scaleZCurve);
        case TransformProperty::Orientation:
            return axis == 0 ? &orientationXCurve
                             : (axis == 1 ? &orientationYCurve : &orientationZCurve);
        case TransformProperty::AudioLevel: return &audioLevelCurve;
        case TransformProperty::Target:
            return axis == 0 ? &targetXCurve : (axis == 1 ? &targetYCurve : &targetZCurve);
        case TransformProperty::AnchorPoint: break;
        }
        return nullptr;
    }

    const KeyFrameList* curve(TransformProperty prop, int axis) const
    {
        return const_cast<LayerTransform*>(this)->curve(prop, axis);
    }

    bool isAnimated(TransformProperty prop,
                    LayerDimension dimension = LayerDimension::TwoD) const
    {
        for (int axis = 0; axis < axisCount(prop, dimension); ++axis) {
            const KeyFrameList* c = curve(prop, axis);
            if (c && !c->isEmpty()) {
                return true;
            }
        }
        return false;
    }

    static double curveValue(const KeyFrameList& curve, int frame, double fallback)
    {
        if (curve.isEmpty()) {
            return fallback;
        }
        bool ok = false;
        const double v = curve.valueAt(frame).toDouble(&ok);
        return ok ? v : fallback;
    }
    // A colour property's value at `frame`: its keys' colour, or its own.
    static QColor curveColor(const KeyFrameList& curve, int frame, const QColor& fallback)
    {
        if (curve.isEmpty()) return fallback;
        const QVariant value = curve.valueAt(frame);
        return value.typeId() == QMetaType::QColor ? value.value<QColor>() : fallback;
    }

    QPointF positionAt(int frame) const
    {
        return QPointF(curveValue(positionXCurve, frame, position.x()),
                       curveValue(positionYCurve, frame, position.y()));
    }
    QPointF scaleAt(int frame) const
    {
        return QPointF(curveValue(scaleXCurve, frame, scalePercent.x()),
                       curveValue(scaleYCurve, frame, scalePercent.y()));
    }
    double rotationAt(int frame) const
    {
        return curveValue(rotationCurve, frame, rotationDegrees);
    }

    // Third-axis reads, used by the 3D scene. They fall back to the static
    // fields exactly as the 2D ones do.
    double positionZAt(int frame) const { return curveValue(positionZCurve, frame, positionZ); }
    double scaleZAt(int frame) const { return curveValue(scaleZCurve, frame, scaleZPercent); }
    double rotationXAt(int frame) const
    {
        return curveValue(rotationXCurve, frame, rotationXDegrees);
    }
    double rotationYAt(int frame) const
    {
        return curveValue(rotationYCurve, frame, rotationYDegrees);
    }
    double orientationAt(int frame, int axis) const
    {
        switch (axis) {
        case 0: return curveValue(orientationXCurve, frame, orientationX);
        case 1: return curveValue(orientationYCurve, frame, orientationY);
        default: break;
        }
        return curveValue(orientationZCurve, frame, orientationZ);
    }

    // Value of one axis of a property at a frame, so an editor can read and a
    // renderer can sample without either repeating the switch above.
    double valueAt(TransformProperty prop, int axis, int frame) const
    {
        switch (prop) {
        case TransformProperty::Opacity:   return opacityAt(frame, 1.0) * 100.0;
        case TransformProperty::Rotation:  return rotationAt(frame);
        case TransformProperty::RotationX: return rotationXAt(frame);
        case TransformProperty::RotationY: return rotationYAt(frame);
        case TransformProperty::Orientation: return orientationAt(frame, axis);
        case TransformProperty::Position:
            return axis == 0 ? positionAt(frame).x()
                             : (axis == 1 ? positionAt(frame).y() : positionZAt(frame));
        case TransformProperty::Scale:
            return axis == 0 ? scaleAt(frame).x()
                             : (axis == 1 ? scaleAt(frame).y() : scaleZAt(frame));
        case TransformProperty::AnchorPoint:
            return axis == 0 ? anchorPoint.x() : (axis == 1 ? anchorPoint.y() : anchorPointZ);
        case TransformProperty::AudioLevel:
            return curveValue(audioLevelCurve, frame, audioLevel);
        case TransformProperty::Target:
            return targetAt(frame, axis);
        }
        return 0.0;
    }
    double targetAt(int frame, int axis) const
    {
        return axis == 0 ? curveValue(targetXCurve, frame, targetX)
                         : (axis == 1 ? curveValue(targetYCurve, frame, targetY) : curveValue(targetZCurve, frame, targetZ));
    }
    // `fallback` is the layer's static opacity, already normalised to 0..1.
    double opacityAt(int frame, double fallback) const
    {
        if (opacityCurve.isEmpty()) {
            return fallback;
        }
        return qBound(0.0, curveValue(opacityCurve, frame, fallback * 100.0) / 100.0, 1.0);
    }
};

// One node of a model layer's model, moved, turned and scaled on its own -
// the reference's Model3DTransformNode, whose properties are a layer's 3D
// transform (position, anchorPoint, scale, orientation, rotationX/Y/Z) in
// the model's units. Kept only for the nodes that were changed, by their
// path of names (model3d::Mesh::nodePath). Position and anchor point start
// at the node's pivot, so a new one leaves the node where it was.
struct ModelNodeTransform
{
    QString path;
    LayerTransform transform;
};

// One of a model's materials with a texture map taken from elsewhere - the
// reference's Model3DMaterialOverride ("Override Materials": for each map a
// source, None / From File / Use Layer, and a layer). Its properties are
// <map>MapSource and <map>MapLayerID for the diffuse, specular, normal, bump,
// emissive, occlusion, metallic, roughness and specular-glossiness maps, and
// bumpMapScale.
struct ModelMaterialOverride
{
    enum class MapSource { None = 0, FromFile = 1, UseLayer = 2 };
    enum class Map { Diffuse, Specular, Normal, Bump, Emissive, Occlusion,
                     Metallic, Roughness, SpecularGlossiness };
    static constexpr int kMapCount = 9;
    struct MapOverride
    {
        MapSource source = MapSource::FromFile;
        core::Identifier layer;               // with UseLayer: whose picture
    };
    QString material;                         // the material's name in the model
    std::array<MapOverride, kMapCount> maps;
    double bumpMapScale = 1.0;

    MapOverride& map(Map which) { return maps[size_t(which)]; }
    const MapOverride& map(Map which) const { return maps[size_t(which)]; }
    // The properties' stem: "diffuse" for diffuseMapSource / diffuseMapLayerID.
    static const char* mapKey(Map which)
    {
        switch (which) {
        case Map::Diffuse: return "diffuse";
        case Map::Specular: return "specular";
        case Map::Normal: return "normal";
        case Map::Bump: return "bump";
        case Map::Emissive: return "emissive";
        case Map::Occlusion: return "occlusion";
        case Map::Metallic: return "metallic";
        case Map::Roughness: return "roughness";
        case Map::SpecularGlossiness: return "specularGlossiness";
        }
        return "";
    }
};

// A light layer's own properties (LightLayer Version 4: its LightType, and
// the properties LightLayerGroupFactory shows in the timeline's Light group),
// with the reference's defaults (LightLayer property setup, Project.dll
// 1802fce00). The type numbers are the reference's, the ones Flux's lighting
// shader takes too. Its colours and numbers animate; Cast Shadows and
// Falloff are static (CreateProperty's static flag set), as the type is.
struct LightSettings
{
    enum class Type { Ambient = 1, Point = 2, Spot = 3, Directional = 4 };
    enum class Falloff { None = 1, Linear = 2, Curve = 3 };

    Type type = Type::Point;
    QColor color = QColor(255, 255, 255);
    double intensity = 100.0;           // percent
    KeyFrameList intensityCurve;
    bool castShadows = false;
    Falloff falloff = Falloff::None;
    double reach = 1000.0;              // pixels
    double coneAngle = 15.0;            // spotlightAngle, degrees
    double feather = 50.0;              // spotlightFeather, percent
    double shadowOpacity = 50.0;        // percent
    double shadowDiffusion = 0.0;       // percent
    QColor shadowColor = QColor(0, 0, 0);
    KeyFrameList colorCurve;
    KeyFrameList reachCurve;
    KeyFrameList coneAngleCurve;
    KeyFrameList featherCurve;
    KeyFrameList shadowOpacityCurve;
    KeyFrameList shadowDiffusionCurve;
    KeyFrameList shadowColorCurve;

    double intensityAt(int frame) const { return LayerTransform::curveValue(intensityCurve, frame, intensity); }
    QColor colorAt(int frame) const { return LayerTransform::curveColor(colorCurve, frame, color); }
    double reachAt(int frame) const { return LayerTransform::curveValue(reachCurve, frame, reach); }
    double coneAngleAt(int frame) const { return LayerTransform::curveValue(coneAngleCurve, frame, coneAngle); }
    double featherAt(int frame) const { return LayerTransform::curveValue(featherCurve, frame, feather); }
    double shadowOpacityAt(int frame) const
    {
        return LayerTransform::curveValue(shadowOpacityCurve, frame, shadowOpacity);
    }
    double shadowDiffusionAt(int frame) const
    {
        return LayerTransform::curveValue(shadowDiffusionCurve, frame, shadowDiffusion);
    }
    QColor shadowColorAt(int frame) const { return LayerTransform::curveColor(shadowColorCurve, frame, shadowColor); }
};

// A camera layer's lens and clipping, as the reference's CameraLayer keeps
// them: its "zoom" - the distance in pixels at which the shot's frame fills
// the lens, what the field of view follows from (CameraLayer::
// CalculateVerticalFOV, Project.dll 0x1802f08e0: 2 atan(height / 2 / zoom),
// kept between 0.001 rad and just under pi) - its "focusDistance",
// "aperture" and "blurDoF" (the timeline's Lens group,
// CameraLayerGroupFactory), whether depth of field is on (HasDoF), its
// NearClip and FarClip and its film size; with what "New Camera" gets
// (VegasEffects.exe 0x140a45760: a 35 mm lens, aperture 17.72, blur 100 %;
// CameraLayer's constructor: clipping 1 to 100000, film 36 x 20.25 mm).
struct CameraSettings
{
    // CameraLayer::HorizontalFieldOfView (0x1802f0230): the horizontal angles
    // of a 36 mm film behind 24, 28, 35, 50 and 80 mm lenses.
    static constexpr double kLensAngles[5] = {73.7398, 65.4705, 54.4322, 39.5978, 25.3608};
    static constexpr double kNewCameraLensAngle = 54.4322;

    double zoom = 0.0;          // pixels; 0 until it is given one
    KeyFrameList zoomCurve;
    double focusDistance = 0.0;
    double aperture = 17.72;
    double blur = 100.0;        // percent
    bool depthOfField = false;
    double nearClip = 1.0;
    double farClip = 100000.0;
    double filmWidth = 36.0;
    double filmHeight = 20.25;

    // The Lens group animates whole (CameraLayer's properties, Project.dll
    // 0x1802eec70, none static).
    KeyFrameList focusDistanceCurve;
    KeyFrameList apertureCurve;
    KeyFrameList blurCurve;

    double zoomAt(int frame) const { return LayerTransform::curveValue(zoomCurve, frame, zoom); }
    double focusDistanceAt(int frame) const
    {
        return LayerTransform::curveValue(focusDistanceCurve, frame, focusDistance);
    }
    double apertureAt(int frame) const { return LayerTransform::curveValue(apertureCurve, frame, aperture); }
    double blurAt(int frame) const { return LayerTransform::curveValue(blurCurve, frame, blur); }
    // CameraLayer::CalculateLensZoom: a lens of `angleDegrees` (horizontal)
    // across a frame `width` pixels wide.
    static double lensZoom(double angleDegrees, double width)
    {
        return width / (2.0 * std::tan(angleDegrees * 0.017453292519943295 * 0.5));
    }
    // The vertical field of view, in degrees, of a `zoom` for a frame
    // `height` pixels tall.
    static double verticalFieldOfView(double zoom, double height)
    {
        double radians = zoom > 0.0 ? 2.0 * std::atan(height * 0.5 / zoom) : 0.001;
        radians = std::max(radians, 0.001);
        if (radians >= 3.141592653589793) radians = 3.1315928;
        return radians * 57.29577951308232;
    }
    // The zoom that gives a vertical field of view, the other way round.
    static double zoomForVerticalFieldOfView(double degrees, double height)
    {
        return height * 0.5 / std::tan(degrees * 0.017453292519943295 * 0.5);
    }
};

// How a layer in the 3D scene takes lights and shadows: the reference's
// layer properties illuminated, castShadows, castShadowsIfLayerDisabled
// (it still casts them while its visibility is off), receiveShadows,
// billboardedLights (lit as if it faced each light), billboardedShadows (a
// layer that turns to face the camera turns to face the light for its
// shadow instead), shadowColor, a model's selfShadowBias (1..1000) and its
// material - matAmbient, matDiffuse, matSpecular, matShininess (percent;
// Flux divides them by 100) and matEmissiveColor, which animate - shown in
// the timeline's Material group (AssetLayerGroupFactory), with the defaults
// the reference creates them with (Project.dll 0x1802e52c0, 0x1802a5fc0).
// Its ambient occlusion, all static (Project.dll 0x180312a80, a model's
// 0x1802a5fc0; ranges from the static initialisers 0x180002a60...
// 0x180002d70): castAmbientOcclusion and receiveAmbientOcclusion (off), and
// the receiver's aoSamples (4, 4..10), aoSampleRadius (15, 0..20),
// aoIntensity (50, 0..100), aoDepthScale (10, 0..100), aoBias (0.01,
// 0..10), aoBlurRadius (5, 1..20), aoDepthFallOff (10, 0..100) and a
// model's includeSemiTransparentTriangles (on).
// Its reflections (Project.dll 0x1802e52c0, TextLayer 0x180312cf0, a
// model's 0x1802a5fc0): castReflections, castReflectionsIfLayerDisabled and
// receiveReflections (off, static); a media, plane or text layer's
// specularReflectivity (100) and a text layer's diffuseReflectivity (0),
// percent, 0..100, which animate; a text or model layer's
// environmentMapType (None 1000, Use Composite Shot 1001, Use Layer 1002)
// and environmentMapLayerID, static.
struct SurfaceShading
{
    // 1003, not offered by the Material rows: a project asset by its ID
    // (environmentMapLayerID), which Flux draws on its own as a render item
    // (FUN_1804bb850 -> FUN_1802ca540 / FUN_1802cebd0) - an environment
    // picture that is no layer of the shot.
    enum class EnvironmentMap { None = 1000, CompositeShot = 1001, Layer = 1002, Asset = 1003 };

    bool illuminated = true;
    bool castShadows = false;
    bool castShadowsIfInvisible = false;
    bool receiveShadows = true;
    bool billboardedLights = false;
    bool billboardedShadows = false;
    double selfShadowBias = 1.0;
    bool castAmbientOcclusion = false;
    bool receiveAmbientOcclusion = false;
    int aoSamples = 4;
    int aoSampleRadius = 15;
    double aoIntensity = 50.0;
    double aoDepthScale = 10.0;
    double aoBias = 0.01;
    int aoBlurRadius = 5;
    double aoDepthFalloff = 10.0;
    bool includeSemiTransparentTriangles = true;
    bool castReflections = false;
    bool castReflectionsIfInvisible = false;
    bool receiveReflections = false;
    double specularReflectivity = 100.0;
    double diffuseReflectivity = 0.0;
    KeyFrameList specularReflectivityCurve;
    KeyFrameList diffuseReflectivityCurve;
    EnvironmentMap environmentMap = EnvironmentMap::None;
    core::Identifier environmentMapLayer;
    QColor shadowColor = QColor(0, 0, 0);
    double ambient = 100.0;
    double diffuse = 50.0;
    double specular = 50.0;
    double shininess = 100.0;
    QColor emissive = QColor(0, 0, 0);
    KeyFrameList ambientCurve;
    KeyFrameList diffuseCurve;
    KeyFrameList specularCurve;
    KeyFrameList shininessCurve;
    KeyFrameList emissiveCurve;

    double ambientAt(int frame) const { return LayerTransform::curveValue(ambientCurve, frame, ambient); }
    double diffuseAt(int frame) const { return LayerTransform::curveValue(diffuseCurve, frame, diffuse); }
    double specularAt(int frame) const { return LayerTransform::curveValue(specularCurve, frame, specular); }
    double shininessAt(int frame) const { return LayerTransform::curveValue(shininessCurve, frame, shininess); }
    QColor emissiveAt(int frame) const { return LayerTransform::curveColor(emissiveCurve, frame, emissive); }
    double specularReflectivityAt(int frame) const
    {
        return LayerTransform::curveValue(specularReflectivityCurve, frame, specularReflectivity);
    }
    double diffuseReflectivityAt(int frame) const
    {
        return LayerTransform::curveValue(diffuseReflectivityCurve, frame, diffuseReflectivity);
    }
};

// An animation playing on a model layer - the reference's
// Model3DAnimationAssetInstance (Project.dll; XML Version 1: ID, Name,
// AssetID and a PropertyManager with "strength" (float, 100 by default),
// "loop" and "customPlayback" (both off) and "playback" (float, 0)) with what
// it keeps on its Model3DAnimationAsset (Version 3: Filename, AnimationIndex,
// OverrideFrameRate, FrameRate). The Timeline shows it under the layer's
// "Animations" as Strength, Loop, Custom Playback and Playback.
struct ModelAnimationInstance
{
    core::Identifier id = core::Identifier(QUuid::createUuid().toString(QUuid::WithoutBraces));
    QString name;
    QString filePath;              // the animation's file; empty: the model's own
    int animationIndex = 0;        // which of the file's animations
    bool frameRateOverridden = false;
    double frameRate = 24.0;       // with frameRateOverridden
    double strength = 100.0;       // percent of the motion
    KeyFrameList strengthCurve;
    bool loop = false;
    bool customPlayback = false;
    double playback = 0.0;         // with customPlayback: the animation's frame
    KeyFrameList playbackCurve;

    double strengthAt(int frame) const
    {
        return LayerTransform::curveValue(strengthCurve, frame, strength);
    }
    double playbackAt(int frame) const
    {
        return LayerTransform::curveValue(playbackCurve, frame, playback);
    }
    // Its frame rate as Model3DAnimationAsset::FrameRate gives it: the
    // override or the file's, rounded to three places, the rounded NTSC
    // rates read as the exact ones.
    double rate(double fileRate) const
    {
        double value = frameRateOverridden && frameRate > 0.0 ? frameRate : qMax(1.0, fileRate);
        value = std::round(value * 1000.0) / 1000.0;
        if (qFuzzyCompare(value, 23.976)) return 24000.0 / 1001.0;
        if (qFuzzyCompare(value, 29.97)) return 30000.0 / 1001.0;
        if (qFuzzyCompare(value, 59.94)) return 60000.0 / 1001.0;
        return value;
    }
    // The animation's frame at the shot's `frame`, as
    // Model3DAnimationAssetInstance::AssetFrame (Project.dll 18013a900)
    // works it out: with Custom Playback the Playback value; otherwise the
    // time into the layer in the animation's frames - at its own rate, so an
    // overridden rate plays it faster or slower - and with Loop wrapped
    // around its length (fmod by frame count - 1). `fileRate` is the rate
    // its frames are counted in (model3d::kModelAnimationFrameRate).
    double animationFrame(int frame, double shotFps, int frameCount, double fileRate) const
    {
        if (customPlayback) return playbackAt(frame);
        double at = frame * rate(fileRate) / qMax(1e-9, shotFps);
        if (loop && frameCount > 1) at = std::fmod(at, double(frameCount - 1));
        return at;
    }
};

// A new transform for a model node that leaves it where it is: position and
// anchor point at its pivot (x, y, z in the model's units).
inline LayerTransform restingModelNodeTransform(double x, double y, double z)
{
    LayerTransform transform;
    transform.position = QPointF(x, y);
    transform.positionZ = z;
    transform.anchorPoint = transform.position;
    transform.anchorPointZ = z;
    return transform;
}

// A point tracker follows a small image patch through the source frames. Its
// coordinates are source-image pixels; keeping them separate from Transform
// lets the result be inspected, saved and later applied to any layer.
struct MotionTrack
{
    core::Identifier id = core::Identifier(
        QUuid::createUuid().toString(QUuid::WithoutBraces));
    QString name = QStringLiteral("Track");
    bool enabled = true;
    QPointF point;
    int sampleRadius = 8;
    int searchRadius = 24;
    KeyFrameList xCurve;
    KeyFrameList yCurve;

    QPointF pointAt(int frame) const
    {
        return QPointF(LayerTransform::curveValue(xCurve, frame, point.x()),
                       LayerTransform::curveValue(yCurve, frame, point.y()));
    }
};

// Layer kinds, recovered from the reference's own "New … Layer" command
// strings (1412d0d98..1412d0df0, in menu order): Point, Text, Grade, Light,
// Camera, Plane. They match its project::biff layer classes - PointLayer,
// TextLayer, GradeLayer, LightLayer, CameraLayer - with Media standing for the
// AssetLayer a dropped file produces.
enum class LayerKind
{
    Media,   // AssetLayer: carries clips referencing media
    Text,    // TextLayer
    Point,   // PointLayer: a null helper, nothing is drawn for it
    Grade,   // GradeLayer: its effects apply to everything beneath
    Light,   // LightLayer: 3D scene object
    Camera,  // CameraLayer: 3D scene object
    Plane,   // a solid-coloured plane
    Model3D, // Model3DLayer: hosts an imported model, always in the 3D scene
};

inline const char* layerKindName(LayerKind kind)
{
    switch (kind) {
    case LayerKind::Media:   return QT_TRANSLATE_NOOP("LayerKind", "Media");
    case LayerKind::Text:    return QT_TRANSLATE_NOOP("LayerKind", "Text");
    case LayerKind::Point:   return QT_TRANSLATE_NOOP("LayerKind", "Point");
    case LayerKind::Grade:   return QT_TRANSLATE_NOOP("LayerKind", "Grade");
    case LayerKind::Light:   return QT_TRANSLATE_NOOP("LayerKind", "Light");
    case LayerKind::Camera:  return QT_TRANSLATE_NOOP("LayerKind", "Camera");
    case LayerKind::Plane:   return QT_TRANSLATE_NOOP("LayerKind", "Plane");
    // The reference labels the layer "[Model]" in the timeline (141307180) and
    // names the class Model3DLayer, "3D Model Layer".
    case LayerKind::Model3D: return QT_TRANSLATE_NOOP("LayerKind", "Model");
    }
    return "";
}

// Stable token for the project file, so a kind survives a round trip.
inline QString layerKindToken(LayerKind kind)
{
    return QString::fromLatin1(layerKindName(kind));
}

inline LayerKind layerKindFromToken(const QString& token, LayerKind fallback = LayerKind::Media)
{
    for (LayerKind kind : {LayerKind::Media, LayerKind::Text, LayerKind::Point, LayerKind::Grade,
                           LayerKind::Light, LayerKind::Camera, LayerKind::Plane,
                           LayerKind::Model3D}) {
        if (token.compare(QString::fromLatin1(layerKindName(kind)), Qt::CaseInsensitive) == 0) {
            return kind;
        }
    }
    return fallback;
}

// A layer's Alignment (the reference's AutoOrient, saved as <AutoOrient>
// beside <AutoOrientPathAxis> and <AutoOrientLayer>): how it turns by itself.
// The numbers are the reference's (the Transform group's lists,
// VegasEffects.exe 0x14050fec0).
enum class AutoOrient
{
    None = 0,
    AlongMotionPath = 1,
    TowardsActiveCamera = 2,     // a 3D media, plane or text layer
    TowardsTargetPosition = 4,   // a camera, a light or a point layer
    TowardsLayer = 5,
};

struct Layer
{
    // LayerBase/ID is the persistent identity used by the reference.  It must
    // not be reminted on every save: parenting, selection and external links
    // address a layer by this value rather than by its editable name.
    core::Identifier id = core::Identifier(
        QUuid::createUuid().toString(QUuid::WithoutBraces));
    // Empty means a root layer.  The reference calls the matching model key
    // parentLayerID; the UI exposes it on the Layer panel.
    core::Identifier parentLayerId;
    QString name;
    LayerKind kind = LayerKind::Media;
    // Whether the layer sits in the composition's plane or in its 3D scene.
    // A Model3D layer is only ever in the scene, and so are Camera and Light.
    LayerDimension dimension = LayerDimension::TwoD;
    // Fill for a Plane layer; ignored by every other kind.
    QColor planeColor = QColor(128, 128, 128);
    // The imported model this layer shows, for LayerKind::Model3D. The
    // reference calls the link a Model3DAssetInstance; one asset can be
    // instanced by several layers, so the layer holds only the id.
    core::Identifier modelAssetId;
    // A camera layer's lens and clipping.
    CameraSettings camera;
    // A Light layer's own properties.
    LightSettings light;
    // Lights and shadows on a layer in the 3D scene.
    SurfaceShading surface;
    // Alignment, the axis Along Motion Path turns (X 1, Y 2, Z 3) and the
    // layer Towards Layer turns to.
    AutoOrient autoOrient = AutoOrient::None;
    int autoOrientAxis = 1;
    core::Identifier autoOrientLayer;
    int zIndex = 0;
    // Reference default for a newly created layer (see SAMPLES/screenshots).
    QString blendMode = QStringLiteral("None");
    double opacity = 1.0;
    bool visible = true;
    bool muted = false;
    bool locked = false;
    // LayerBase/MotionBlurOn: the layer takes part in the composite shot's
    // motion blur (toggled from its timeline row).
    bool motionBlur = false;
    QColor labelColor = QColor(48, 53, 62);
    LayerTransform transform;
    // Audio > Level (transform.audioLevel and its curve) in dB, between
    // frames too - see audioLevelAtSeconds below.
    double audioLevelAtSeconds(double seconds, double fps) const;
    QVector<Clip> clips;
    // Where a layer without clips - Point, Light, Camera, 3D model - lies on
    // the shot's timeline: the reference's StartFrame and EndFrame, which
    // every one of its layers has. Outside it the layer takes no part: a
    // camera is not the active one (CompositionAsset::ActiveCameraLayer), a
    // light lights nothing and a model is not drawn (Flux's layer loop). An
    // end below zero runs to the shot's end, as a new layer does (StartFrame
    // 0, EndFrame the shot's FrameCount).
    double spanStartSeconds = 0.0;
    double spanEndSeconds = -1.0;
    QVector<LayerMask> masks;
    QVector<MotionTrack> motionTracks;
    // Model3D: the nodes of its model given a transform of their own.
    QVector<ModelNodeTransform> modelNodes;
    const ModelNodeTransform* modelNode(const QString& path) const
    {
        for (const ModelNodeTransform& node : modelNodes)
            if (node.path == path) return &node;
        return nullptr;
    }
    ModelNodeTransform* modelNode(const QString& path)
    {
        for (ModelNodeTransform& node : modelNodes)
            if (node.path == path) return &node;
        return nullptr;
    }
    // Model3D: the animations playing on it, their motion added together.
    QVector<ModelAnimationInstance> modelAnimations;
    // Model3D: its materials with maps from elsewhere, by material name.
    QVector<ModelMaterialOverride> materialOverrides;
    const ModelMaterialOverride* materialOverride(const QString& material) const
    {
        for (const ModelMaterialOverride& entry : materialOverrides)
            if (entry.material == material) return &entry;
        return nullptr;
    }
    ModelMaterialOverride* materialOverride(const QString& material)
    {
        for (ModelMaterialOverride& entry : materialOverrides)
            if (entry.material == material) return &entry;
        return nullptr;
    }
};

// Whether a layer of this kind lies on the timeline by its span
// (Layer::spanStartSeconds) rather than by clips.
inline bool layerHasSpan(LayerKind kind)
{
    return kind == LayerKind::Point || kind == LayerKind::Light || kind == LayerKind::Camera
           || kind == LayerKind::Model3D;
}

inline bool layerHasSpan(const Layer& layer)
{
    return layer.clips.isEmpty() && layerHasSpan(layer.kind);
}

// Where a layer's span ends, in a shot `shotSeconds` long.
inline double layerSpanEnd(const Layer& layer, double shotSeconds)
{
    return layer.spanEndSeconds < 0.0 ? qMax(layer.spanStartSeconds, shotSeconds) : layer.spanEndSeconds;
}

// Whether a layer takes part at `frame` of a shot running at `fps`: one
// with a span by it, from its first frame up to the one its end falls on
// (StartFrame <= frame < EndFrame, as the reference tests it); one with
// clips always, its clips deciding what it shows.
inline bool layerSpanCovers(const Layer& layer, int frame, double fps)
{
    if (!layerHasSpan(layer)) return true;
    const double rate = qMax(1.0, fps);
    if (frame < qRound64(layer.spanStartSeconds * rate)) return false;
    return layer.spanEndSeconds < 0.0 || frame < qRound64(layer.spanEndSeconds * rate);
}

// A keyable property of a light, a camera's lens or a layer's Material, by
// the name the reference saves it under ("color", "spotlightAngle",
// "focusDistance", "matEmissiveColor"...): its keys, and the value it has
// without them - a number or a colour.
struct KeyedLayerProperty
{
    KeyFrameList* curve = nullptr;
    double* number = nullptr;
    QColor* color = nullptr;
    bool isValid() const { return curve != nullptr; }
};

inline KeyedLayerProperty keyedLayerProperty(Layer& layer, const QString& name)
{
    const auto number = [](KeyFrameList& curve, double& value) { return KeyedLayerProperty{&curve, &value, nullptr}; };
    const auto colour = [](KeyFrameList& curve, QColor& value) { return KeyedLayerProperty{&curve, nullptr, &value}; };
    if (layer.kind == LayerKind::Light) {
        LightSettings& light = layer.light;
        if (name == QLatin1String("color")) return colour(light.colorCurve, light.color);
        if (name == QLatin1String("intensity")) return number(light.intensityCurve, light.intensity);
        if (name == QLatin1String("reach")) return number(light.reachCurve, light.reach);
        if (name == QLatin1String("spotlightAngle")) return number(light.coneAngleCurve, light.coneAngle);
        if (name == QLatin1String("spotlightFeather")) return number(light.featherCurve, light.feather);
        if (name == QLatin1String("shadowOpacity")) return number(light.shadowOpacityCurve, light.shadowOpacity);
        if (name == QLatin1String("shadowDiffusion")) return number(light.shadowDiffusionCurve, light.shadowDiffusion);
        if (name == QLatin1String("shadowColor")) return colour(light.shadowColorCurve, light.shadowColor);
        return {};
    }
    if (layer.kind == LayerKind::Camera) {
        CameraSettings& camera = layer.camera;
        if (name == QLatin1String("zoom")) return number(camera.zoomCurve, camera.zoom);
        if (name == QLatin1String("focusDistance")) return number(camera.focusDistanceCurve, camera.focusDistance);
        if (name == QLatin1String("aperture")) return number(camera.apertureCurve, camera.aperture);
        if (name == QLatin1String("blurDoF")) return number(camera.blurCurve, camera.blur);
        return {};
    }
    SurfaceShading& surface = layer.surface;
    if (name == QLatin1String("matAmbient")) return number(surface.ambientCurve, surface.ambient);
    if (name == QLatin1String("matDiffuse")) return number(surface.diffuseCurve, surface.diffuse);
    if (name == QLatin1String("matSpecular")) return number(surface.specularCurve, surface.specular);
    if (name == QLatin1String("matShininess")) return number(surface.shininessCurve, surface.shininess);
    if (name == QLatin1String("matEmissiveColor")) return colour(surface.emissiveCurve, surface.emissive);
    if (name == QLatin1String("specularReflectivity"))
        return number(surface.specularReflectivityCurve, surface.specularReflectivity);
    if (name == QLatin1String("diffuseReflectivity"))
        return number(surface.diffuseReflectivityCurve, surface.diffuseReflectivity);
    return {};
}

// Its value at `frame`: a double or a QColor.
inline QVariant keyedLayerPropertyAt(const Layer& layer, const QString& name, int frame)
{
    const KeyedLayerProperty property = keyedLayerProperty(const_cast<Layer&>(layer), name);
    if (!property.isValid()) return QVariant();
    if (property.color) return QVariant(LayerTransform::curveColor(*property.curve, frame, *property.color));
    return QVariant(LayerTransform::curveValue(*property.curve, frame, *property.number));
}

// An edit at `frame`: the key there once the property has keys, its own
// value otherwise.
inline void setKeyedLayerProperty(Layer& layer, const QString& name, const QVariant& value, int frame)
{
    const KeyedLayerProperty property = keyedLayerProperty(layer, name);
    if (!property.isValid()) return;
    const QVariant stored = property.color ? QVariant(value.value<QColor>()) : QVariant(value.toDouble());
    if (!property.curve->isEmpty()) {
        property.curve->set(frame, stored);
    } else if (property.color) {
        *property.color = stored.value<QColor>();
    } else {
        *property.number = stored.toDouble();
    }
}

// A key at `frame` with the value there, or that key gone; the last one
// leaves its value as the property's own.
inline void toggleKeyedLayerProperty(Layer& layer, const QString& name, int frame)
{
    const KeyedLayerProperty property = keyedLayerProperty(layer, name);
    if (!property.isValid()) return;
    const QVariant now = keyedLayerPropertyAt(layer, name, frame);
    KeyFrameList& curve = *property.curve;
    if (curve.contains(frame)) {
        curve.removeAt(frame);
        if (curve.isEmpty()) {
            if (property.color) *property.color = now.value<QColor>();
            else *property.number = now.toDouble();
        }
    } else {
        if (curve.isEmpty()) curve.setDefaultValue(now);
        curve.set(frame, now);
    }
}

// An audio level curve between frames: a fade evaluated only per frame would
// step its gain 60 times a second instead of gliding.
inline double audioLevelAtSeconds(double staticDb, const KeyFrameList& curve,
                                  double seconds, double fps)
{
    if (curve.isEmpty()) return staticDb;
    if (fps <= 0.0) return curve.valueAt(0).toDouble();
    const double frame = seconds * fps;
    const double whole = std::floor(frame);
    const double a = curve.valueAt(int(whole)).toDouble();
    const double b = curve.valueAt(int(whole) + 1).toDouble();
    return a + (b - a) * (frame - whole);
}
inline double Layer::audioLevelAtSeconds(double seconds, double fps) const
{
    return composition::audioLevelAtSeconds(transform.audioLevel, transform.audioLevelCurve,
                                            seconds, fps);
}

} // namespace composition
} // namespace openvegas
