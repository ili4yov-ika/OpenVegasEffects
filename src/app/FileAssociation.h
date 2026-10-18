#pragma once

#include <QString>

class QSettings;

namespace openvegas {
namespace app {

// The .vegfx project file type as Windows knows it: the ProgID the installer
// registers (tools/nsis_installer.nsi) and the icon its files show - the
// executable's second icon (resources/icons/iconfile.ico, index 1).
namespace FileAssociation {

inline constexpr char kProjectExtension[] = ".vegfx";
inline constexpr char kProjectProgId[] = "OpenVegasEffects.Project";
inline constexpr int kProjectIconIndex = 1;

// Writes, under `classes` (a Software\Classes root), the project ProgID with
// its description, DefaultIcon and open command for `executable`, offers it
// in .vegfx's OpenWithProgids and gives Applications\<exe name> the same
// icon and command. It never makes the program the default for .vegfx - that
// stays the user's choice, and once they make it (Open with > Always) the
// files show the project icon whichever of the two entries they picked.
// Returns whether anything changed.
bool registerProjectFileType(QSettings& classes, const QString& executable);

// The above for the current user (HKCU\Software\Classes) and the running
// executable, telling Explorer to refresh its icons when something changed.
// Does nothing outside Windows.
void registerForCurrentUser();

} // namespace FileAssociation
} // namespace app
} // namespace openvegas
