#include "model3d/ModelImportSettings.h"

#include <QCoreApplication>

namespace openvegas {
namespace model3d {

double ImportSettings::unitScale() const
{
    // Metres per unit. The reference offers these as the interpretation of the
    // numbers in the file, so a model authored in millimetres and one in
    // inches end up at the same size once both are read.
    switch (unit) {
    case Unit::Millimeter: return 0.001;
    case Unit::Centimeter: return 0.01;
    case Unit::Decimeter:  return 0.1;
    case Unit::Meter:      return 1.0;
    case Unit::Kilometer:  return 1000.0;
    case Unit::Inch:       return 0.0254;
    case Unit::Pixel:
    case Unit::FromFile:
        break;
    }
    return 1.0;
}

QStringList unitNames()
{
    // Reference comboBoxSingleUnitType order.
    return QStringList{
        QCoreApplication::translate("Model3D", "From File"),
        QCoreApplication::translate("Model3D", "Pixel"),
        QCoreApplication::translate("Model3D", "Millimeter"),
        QCoreApplication::translate("Model3D", "Centimeter"),
        QCoreApplication::translate("Model3D", "Decimeter"),
        QCoreApplication::translate("Model3D", "Meter"),
        QCoreApplication::translate("Model3D", "Kilometer"),
        QCoreApplication::translate("Model3D", "Inch"),
    };
}

QStringList normalMethodNames()
{
    // Reference comboBoxGenerationMethod order.
    return QStringList{
        QCoreApplication::translate("Model3D", "From File"),
        QCoreApplication::translate("Model3D", "From File + Auto Smoothing"),
        QCoreApplication::translate("Model3D", "Generate Faceted"),
        QCoreApplication::translate("Model3D", "Generate Faceted + Auto Smoothing"),
    };
}

} // namespace model3d
} // namespace openvegas
