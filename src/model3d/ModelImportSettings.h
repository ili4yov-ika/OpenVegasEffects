#pragma once

#include <QString>
#include <QStringList>

namespace openvegas {
namespace model3d {

// How a model file is interpreted on the way in.
//
// These are the reference's Model3DAdvancedPane controls, field for field:
// a coordinate-system group (Flip YZ Axis, Center Anchor Point), a unit/scale
// group (Unit, Single Unit Scale, Auto Normalize), a normals group (Generation
// Method, Angle, Unify Normals, Flip Normals) and a UV group (Flip UV
// Coordinates). Defaults match what the dialog opens with.
struct ImportSettings
{
    // "From File", or one of the named units. Reference combo entries, in its
    // own order: From File, Pixel, Millimeter, Centimeter, Decimeter, Meter,
    // Kilometer, Inch.
    enum class Unit
    {
        FromFile,
        Pixel,
        Millimeter,
        Centimeter,
        Decimeter,
        Meter,
        Kilometer,
        Inch,
    };

    // Reference comboBoxGenerationMethod: From File, From File + Auto
    // Smoothing, Generate Faceted, Generate Faceted + Auto Smoothing.
    enum class NormalMethod
    {
        FromFile,
        FromFileAutoSmoothing,
        GenerateFaceted,
        GenerateFacetedAutoSmoothing,
    };

    // Coordinate system
    bool flipYZAxis = false;
    bool centerAnchorPoint = true;

    // Unit / scale
    Unit unit = Unit::FromFile;
    double singleUnitScale = 1.0;
    // Scales the model so it fits the composition rather than arriving at
    // whatever size its own units imply.
    bool autoNormalize = true;

    // Normals
    NormalMethod normalMethod = NormalMethod::FromFileAutoSmoothing;
    double autoSmoothingAngle = 60.0;   // reference " Degrees" suffix
    bool unifyNormals = false;
    bool flipNormals = false;

    // UV mapping
    bool flipUVCoordinates = false;

    // Metres per unit for the named units, so a model in millimetres and one
    // in inches end up the same size. FromFile and Pixel scale by 1.
    double unitScale() const;
};

// Display names in the reference's own order, for the Unit combo.
QStringList unitNames();
QStringList normalMethodNames();

} // namespace model3d
} // namespace openvegas
