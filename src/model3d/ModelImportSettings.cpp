#include "model3d/ModelImportSettings.h"

#include <QCoreApplication>

namespace openvegas {
namespace model3d {

double ImportSettings::unitScale() const
{
    // The reference's millimetres a unit; its pixel is 2.8352 of them.
    constexpr double kMillimetresPerPixel = 2.835200071334839;   // float 2.8352
    switch (unit) {
    case Unit::Millimeter: return 1.0 / kMillimetresPerPixel;
    case Unit::Centimeter: return 10.0 / kMillimetresPerPixel;
    case Unit::Decimeter:  return 100.0 / kMillimetresPerPixel;
    case Unit::Meter:      return 1000.0 / kMillimetresPerPixel;
    case Unit::Kilometer:  return 1000000.0 / kMillimetresPerPixel;
    case Unit::Inch:       return 25.4 / kMillimetresPerPixel;
    case Unit::Foot:       return 304.8 / kMillimetresPerPixel;
    case Unit::Yard:       return 914.4 / kMillimetresPerPixel;
    case Unit::Mile:       return 1609344.0 / kMillimetresPerPixel;
    case Unit::Pixel:      break;
    }
    return 1.0;
}

int unitCode(ImportSettings::Unit unit)
{
    return 3001 + int(unit);
}

bool unitFromCode(int code, ImportSettings::Unit* unit)
{
    if (code < 3001 || code > 3010) return false;
    *unit = ImportSettings::Unit(code - 3001);
    return true;
}

QStringList unitNames()
{
    // The reference's Unit combo (Model3DAdvancedPane), in LinearType order.
    return QStringList{
        QCoreApplication::translate("Model3D", "Millimeter"),
        QCoreApplication::translate("Model3D", "Centimeter"),
        QCoreApplication::translate("Model3D", "Decimeter"),
        QCoreApplication::translate("Model3D", "Meter"),
        QCoreApplication::translate("Model3D", "Kilometer"),
        QCoreApplication::translate("Model3D", "Inch"),
        QCoreApplication::translate("Model3D", "Foot"),
        QCoreApplication::translate("Model3D", "Yard"),
        QCoreApplication::translate("Model3D", "Mile"),
        QCoreApplication::translate("Model3D", "Pixel"),
    };
}

QStringList normalMethodNames()
{
    // The reference's Method combo, by NormalGenerationType.
    return QStringList{
        QCoreApplication::translate("Model3D", "From File"),
        QCoreApplication::translate("Model3D", "Generate Faceted + Auto Smoothing"),
        QCoreApplication::translate("Model3D", "Generate Faceted"),
        QCoreApplication::translate("Model3D", "From File + Auto Smoothing"),
    };
}

} // namespace model3d
} // namespace openvegas
