#pragma once

#include <QString>
#include <QStringList>

namespace openvegas {
namespace app {

// Where native plugins (.hfpl) are looked for besides the user's own
// Plugins folder: the VEGAS Effects installation, whose plugin tree the
// reference ships beside its executable, and folders chosen in Options >
// Plugins - another FXhome program's plugins among them (HitFilm, Imerge).
//
// Settings (Options ini): "Plugins/UseInstallation" (bool, default true),
// "Plugins/Folders" (string list), "Plugins/PromptShown" (bool) - whether
// Options was opened on the Plugins page because none were found.
namespace PluginFolders {

// The "Program Files" folders of this machine (64- and 32-bit), each once.
QStringList programFilesRoots();
// The VEGAS Effects installation: <Program Files>/FXhome/VEGAS Effects.
QString installationFolder(const QStringList& roots = programFilesRoots());
// Its plugin tree: <installation>/Plugins (empty when it is not there).
QString installationPluginFolder(const QStringList& roots = programFilesRoots());
// The plugin trees of the FXhome programs installed: <root>/FXhome/<program>
// /Plugins, the VEGAS Effects one first.
QStringList fxhomePluginFolders(const QStringList& roots = programFilesRoots());
// The .hfpl modules under `folder`, its category sub-folders included.
int pluginCount(const QString& folder);

bool useInstallation();
void setUseInstallation(bool use);
QStringList chosenFolders();
void setChosenFolders(const QStringList& folders);
bool promptShown();
void setPromptShown(bool shown);

// The folders to scan besides the user's own: the installation's (when used
// and present), then the chosen ones that exist, each once.
QStringList scannedFolders(const QStringList& roots = programFilesRoots());
// Whether Options should open on its Plugins page by itself: no plugin in
// any scanned folder, and it has not done so before.
bool shouldPrompt(const QStringList& roots = programFilesRoots());

} // namespace PluginFolders

} // namespace app
} // namespace openvegas
