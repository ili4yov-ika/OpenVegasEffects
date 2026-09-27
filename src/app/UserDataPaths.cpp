#include "app/UserDataPaths.h"

#include <QDir>
#include <QStandardPaths>

namespace openvegas {
namespace app {

const QVector<UserDataFolder>& UserDataPaths::allFolders()
{
    static const QVector<UserDataFolder> folders = {
        UserDataFolder::Translations,
        UserDataFolder::TemplatesAV,
        UserDataFolder::EnvironmentMaps,
        UserDataFolder::ExportPresets,
        UserDataFolder::PluginPresets,
        UserDataFolder::Plugins,
        UserDataFolder::Presets,
        UserDataFolder::Textures,
        UserDataFolder::Workspaces,
        UserDataFolder::Objects,
        UserDataFolder::Tutorials,
    };
    return folders;
}

// Relative paths exactly as the reference resolver hands them out. Recovered
// from FUN_14025b000, whose switch maps the folder index to a name and then
// appends a sub-directory for two of them: index 1 and index 8 share the base
// "Templates" and are completed with "\AV" and "\Workspaces" respectively.
// So there is no index that yields a bare "Templates" - the port used to place
// both of these at the top level, which put them in the wrong directory.
QString UserDataPaths::folderName(UserDataFolder folder)
{
    switch (folder) {
    case UserDataFolder::Translations:    return QStringLiteral("Translations");
    case UserDataFolder::TemplatesAV:     return QStringLiteral("Templates/AV");
    case UserDataFolder::EnvironmentMaps: return QStringLiteral("EnvironmentMaps");
    case UserDataFolder::ExportPresets:   return QStringLiteral("ExportPresets");
    case UserDataFolder::PluginPresets:   return QStringLiteral("PluginPresets");
    case UserDataFolder::Plugins:         return QStringLiteral("Plugins");
    case UserDataFolder::Presets:         return QStringLiteral("Presets");
    case UserDataFolder::Textures:        return QStringLiteral("Textures");
    case UserDataFolder::Workspaces:      return QStringLiteral("Templates/Workspaces");
    case UserDataFolder::Objects:         return QStringLiteral("Objects");
    case UserDataFolder::Tutorials:       return QStringLiteral("Tutorials");
    }
    return QString();
}

void UserDataPaths::setRoot(const QString& rootPath)
{
    m_root = QDir(rootPath);
}

QString UserDataPaths::pathFor(UserDataFolder folder) const
{
    return m_root.absoluteFilePath(folderName(folder));
}

core::Result UserDataPaths::ensureCreated(UserDataFolder folder) const
{
    const QString name = folderName(folder);
    if (name.isEmpty()) {
        return core::Result::fail(core::ResultStatus::InvalidArgument,
                                  QStringLiteral("Unknown user data folder"));
    }
    const QDir dir = m_root;
    if (!dir.mkpath(name)) {
        return core::Result::fail(core::ResultStatus::OperationFailed,
                                  QStringLiteral("Could not create folder %1").arg(name));
    }
    return core::Result::ok();
}

// Deliberate divergence from the reference: its resolver only *validates* -
// with the third argument set it builds a QDir, and returns an empty string
// when the directory does not exist, never creating one (hence its "please run
// Setup to repair the installation" message). The folders are laid down by the
// installer there. This port ships no installer, so it creates them on first
// run instead; mkpath handles the nested Templates/... entries.
core::Result UserDataPaths::ensureCreated() const
{
    if (m_root.path().isEmpty()) {
        return core::Result::fail(core::ResultStatus::InvalidArgument,
                                  QStringLiteral("User data root is not set"));
    }
    if (!m_root.mkpath(QStringLiteral("."))) {
        return core::Result::fail(core::ResultStatus::OperationFailed,
                                  QStringLiteral("Could not create %1").arg(m_root.absolutePath()));
    }
    for (UserDataFolder folder : allFolders()) {
        const core::Result r = ensureCreated(folder);
        if (r.isFailure()) {
            return r;
        }
    }
    return core::Result::ok();
}

QString UserDataPaths::fileIn(UserDataFolder folder, const QString& fileName) const
{
    return m_root.absoluteFilePath(folderName(folder) + QLatin1Char('/') + fileName);
}

} // namespace app
} // namespace openvegas