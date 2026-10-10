#pragma once

#include <QString>

class QWidget;

namespace openvegas::app {

// The reference's startup check of the GPU driver (VegasEffects.exe
// FUN_1401c2ce0, after its GPU check): Intel integrated graphics whose driver
// is older than the build each generation needs get "%1 : Outdated GPU
// Driver" with the adapter, its driver's version and date and Intel's
// download centre, gated by Options > Prompts "GPUDriverWarning".

// The GPU's class by its OpenGL renderer string (FUN_1401bffe0): Intel
// integrated graphics by generation - 7 for UHD 605/620/630 (and the Radeon
// RX Vega M on Intel's packages), 6 for Kaby Lake, 5 Skylake, 4 Broadwell,
// 3 Haswell, 2 Ivy Bridge, 1 Sandy Bridge, 0 older - matched by its vendor
// at the start of the string and its model at the end, case aside; 8 for
// anything else.
int gpuClassOf(const QString& renderer);

// The display adapter's driver as the reference reads it (FUN_1401bf8f0):
// the Display class adapter whose description and the renderer contain one
// another, its DriverVersion, DriverDate and DriverDesc (Windows registry).
// Without one the device is the renderer and the version the last word of
// GL_VERSION (Intel's "4.6.0 - Build 31.0.101.5186").
struct GpuDriverInfo
{
    QString version;
    QString date;
    QString device;
};
GpuDriverInfo gpuDriverInfo(const QString& renderer, const QString& glVersion);

// What a class needs and what the version gives: classes 3 and 4 the last
// part of the version at least 5057, class 2 at least 5059, classes 5 to 7
// the last two parts as part x 10000 + part at least 1006326 (Intel's
// 100.6326); other classes are not checked.
struct GpuDriverCheck
{
    int gpuClass = 8;
    qint64 required = 0;
    qint64 installed = -1;
    bool outdated() const { return required > 0 && installed >= 0 && installed < required; }
};
GpuDriverCheck checkGpuDriver(const QString& renderer, const QString& driverVersion);

// The warning's text, as the reference builds it.
QString gpuDriverWarningText(const GpuDriverInfo& info);

// The check and, for an outdated driver, the warning (Continue / Exit).
// False when the user chose Exit.
bool warnAboutGpuDriver(QWidget* parent, const QString& renderer, const QString& glVersion);

} // namespace openvegas::app
