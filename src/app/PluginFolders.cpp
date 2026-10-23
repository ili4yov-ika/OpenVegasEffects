#include "app/PluginFolders.h"

#include "app/Settings.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QSettings>

// Where the native .hfpl plugins are looked for (Options > Plugins): the
// Plugins folder of an installed VEGAS Effects when it is used, and the
// folders the user chose - another FXhome program's plugins among them. The
// choices are kept in the options settings under Plugins/.
namespace openvegas {
namespace app {
namespace PluginFolders {

namespace {

const QString kUseInstallation = QStringLiteral("Plugins/UseInstallation");
const QString kFolders = QStringLiteral("Plugins/Folders");
const QString kPromptShown = QStringLiteral("Plugins/PromptShown");

QString cleaned(const QString& folder)
{
    return folder.trimmed().isEmpty() ? QString() : QDir::cleanPath(QDir::fromNativeSeparators(folder.trimmed()));
}

void appendOnce(QStringList& list, const QString& folder)
{
    const QString path = cleaned(folder);
    if (path.isEmpty()) return;
    for (const QString& existing : list)
        if (existing.compare(path, Qt::CaseInsensitive) == 0) return;
    list.append(path);
}

} // namespace

// The Program Files folders of this machine (64-bit, default and x86).
QStringList programFilesRoots()
{
    QStringList roots;
    for (const char* name : {"ProgramW6432", "ProgramFiles", "ProgramFiles(x86)"})
        appendOnce(roots, qEnvironmentVariable(name));
    if (roots.isEmpty()) appendOnce(roots, QStringLiteral("C:/Program Files"));
    return roots;
}

QString installationFolder(const QStringList& roots)
{
    for (const QString& root : roots) {
        const QString folder = QDir(root).filePath(QStringLiteral("FXhome/VEGAS Effects"));
        if (QFileInfo(folder).isDir()) return cleaned(folder);
    }
    return roots.isEmpty() ? QString() : cleaned(QDir(roots.first()).filePath(QStringLiteral("FXhome/VEGAS Effects")));
}

QString installationPluginFolder(const QStringList& roots)
{
    const QString folder = QDir(installationFolder(roots)).filePath(QStringLiteral("Plugins"));
    return QFileInfo(folder).isDir() ? cleaned(folder) : QString();
}

QStringList fxhomePluginFolders(const QStringList& roots)
{
    QStringList folders;
    appendOnce(folders, installationPluginFolder(roots));
    for (const QString& root : roots) {
        const QDir fxhome(QDir(root).filePath(QStringLiteral("FXhome")));
        if (!fxhome.exists()) continue;
        for (const QString& program : fxhome.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            const QString plugins = QDir(fxhome.filePath(program)).filePath(QStringLiteral("Plugins"));
            if (QFileInfo(plugins).isDir()) appendOnce(folders, plugins);
        }
    }
    return folders;
}

int pluginCount(const QString& folder)
{
    if (folder.isEmpty() || !QFileInfo(folder).isDir()) return 0;
    int count = 0;
    QDirIterator it(folder, {QStringLiteral("*.hfpl")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        ++count;
    }
    return count;
}

bool useInstallation()
{
    return Settings::optionSettings().value(kUseInstallation, true).toBool();
}

void setUseInstallation(bool use)
{
    QSettings settings = Settings::optionSettings();
    settings.setValue(kUseInstallation, use);
}

QStringList chosenFolders()
{
    QStringList folders;
    for (const QString& folder : Settings::optionSettings().value(kFolders).toStringList()) appendOnce(folders, folder);
    return folders;
}

void setChosenFolders(const QStringList& folders)
{
    QStringList kept;
    for (const QString& folder : folders) appendOnce(kept, folder);
    QSettings settings = Settings::optionSettings();
    settings.setValue(kFolders, kept);
}

bool promptShown()
{
    return Settings::optionSettings().value(kPromptShown, false).toBool();
}

void setPromptShown(bool shown)
{
    QSettings settings = Settings::optionSettings();
    settings.setValue(kPromptShown, shown);
}

// The folders the plugin scan reads, each once and only when it exists.
QStringList scannedFolders(const QStringList& roots)
{
    QStringList folders;
    if (useInstallation()) appendOnce(folders, installationPluginFolder(roots));
    for (const QString& folder : chosenFolders())
        if (QFileInfo(folder).isDir()) appendOnce(folders, folder);
    return folders;
}

// Options opens on its Plugins page by itself once, when no folder it
// would scan holds a plugin.
bool shouldPrompt(const QStringList& roots)
{
    if (promptShown()) return false;
    for (const QString& folder : scannedFolders(roots))
        if (pluginCount(folder) > 0) return false;
    return true;
}

} // namespace PluginFolders
} // namespace app
} // namespace openvegas
