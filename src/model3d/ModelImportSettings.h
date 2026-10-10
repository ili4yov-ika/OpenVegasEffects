#pragma once

#include <QString>
#include <QStringList>

namespace openvegas {
namespace model3d {

// How a model file is interpreted on the way in.
//
// These are the reference's Model3DImportOptions (Project.dll), shown by its
// Model3DAdvancedPane: a coordinate-system group (Flip YZ Axis, Center Anchor
// Point), a unit/scale group (Unit, Single Unit Scale, Auto Normalize), a
// normals group (Generation Method, Angle, Unify Normals, Flip Normals) and a
// UV group (Flip UV Coordinates). Defaults are the options' own
// (Model3DImportOptions::Model3DImportOptions, 1802a4e80).
struct ImportSettings
{
    // The reference's LinearType, in the order its Unit combo lists them
    // (Model3DAdvancedPane, VegasEffects.exe 1404eaae0): codes 3001 (mm) to
    // 3010 (pixel). There is no "From File": the file's own unit only scales
    // animation assets that ask for it.
    enum class Unit
    {
        Millimeter,
        Centimeter,
        Decimeter,
        Meter,
        Kilometer,
        Inch,
        Foot,
        Yard,
        Mile,
        Pixel,
    };

    // The reference's NormalGenerationType, its values and combo order.
    enum class NormalMethod
    {
        FromFile,
        GenerateFacetedAutoSmoothing,
        GenerateFaceted,
        FromFileAutoSmoothing,
    };

    // Coordinate system
    bool flipYZAxis = false;
    bool centerAnchorPoint = true;

    // Unit / scale
    Unit unit = Unit::Pixel;
    double singleUnitScale = 1.0;
    // Sizes the model so half its bounding box's diagonal is 500 pixels,
    // whatever its unit and scale.
    bool autoNormalize = true;

    // Normals
    NormalMethod normalMethod = NormalMethod::FromFile;
    double autoSmoothingAngle = 80.0;   // reference " Degrees" suffix
    bool unifyNormals = false;
    bool flipNormals = false;

    // UV mapping
    bool flipUVCoordinates = false;

    // Pixels per unit: the unit's millimetres over the reference's 2.8352
    // millimetres a pixel (Model3DImportOptions::ConvertLinearToMM, 1802a4b50);
    // a pixel is one. Single Unit Scale multiplies it.
    double unitScale() const;

    bool operator==(const ImportSettings& o) const
    {
        return flipYZAxis == o.flipYZAxis && centerAnchorPoint == o.centerAnchorPoint && unit == o.unit
            && singleUnitScale == o.singleUnitScale && autoNormalize == o.autoNormalize
            && normalMethod == o.normalMethod && autoSmoothingAngle == o.autoSmoothingAngle
            && unifyNormals == o.unifyNormals && flipNormals == o.flipNormals
            && flipUVCoordinates == o.flipUVCoordinates;
    }
    bool operator!=(const ImportSettings& o) const { return !(*this == o); }
};

// The reference's LinearType code of a unit (3001..3010), and back.
int unitCode(ImportSettings::Unit unit);
bool unitFromCode(int code, ImportSettings::Unit* unit);

// Display names in the reference's own order, for the Unit combo.
QStringList unitNames();
QStringList normalMethodNames();

} // namespace model3d
} // namespace openvegas
