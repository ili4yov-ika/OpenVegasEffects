#pragma once

#include "composition/Effect.h"

#include "core/Identifier.h"

#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <QVector>
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
};

// Blend modes offered for a layer. The list lives beside the model because a
// Layer stores its mode by name; RenderManager maps each of these onto a
// QPainter composition mode. Keep the two in step - a name with no mapping
// composites as plain source-over.
inline QStringList blendModeNames()
{
    return QStringList{
        QStringLiteral("None"),        QStringLiteral("Normal"),
        QStringLiteral("Add"),         QStringLiteral("Screen"),
        QStringLiteral("Multiply"),    QStringLiteral("Overlay"),
        QStringLiteral("Lighten"),     QStringLiteral("Darken"),
        QStringLiteral("Color Dodge"), QStringLiteral("Color Burn"),
        QStringLiteral("Hard Light"),  QStringLiteral("Soft Light"),
        QStringLiteral("Difference"),  QStringLiteral("Exclusion"),
    };
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
        return 3;
    case TransformProperty::Opacity:
    case TransformProperty::RotationX:
    case TransformProperty::RotationY:
    case TransformProperty::Rotation:
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
        }
        return 0.0;
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
    // Vertical FOV shared by the scene camera and 360 preview.
    double cameraFieldOfView = 39.6;
    int zIndex = 0;
    // Reference default for a newly created layer (see SAMPLES/screenshots).
    QString blendMode = QStringLiteral("None");
    double opacity = 1.0;
    bool visible = true;
    bool muted = false;
    bool locked = false;
    QColor labelColor = QColor(48, 53, 62);
    LayerTransform transform;
    QVector<Clip> clips;
    QVector<LayerMask> masks;
    QVector<MotionTrack> motionTracks;
};

} // namespace composition
} // namespace openvegas
