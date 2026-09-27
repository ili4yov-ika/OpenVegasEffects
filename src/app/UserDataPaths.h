#pragma once

#include <QDir>
#include <QString>
#include <QVector>

#include "core/Result.h"

namespace openvegas {
namespace app {

// Indexes are the reference's own (FUN_14025b000), so a folder index that
// crosses the plugin Callbacks boundary keeps its meaning. Note index 1 is the
// AV template folder and index 8 the workspace folder - both nested under
// "Templates", neither a top-level directory.
enum class UserDataFolder
{
    Translations   = 0,
    TemplatesAV    = 1,
    EnvironmentMaps = 2,
    ExportPresets  = 3,
    PluginPresets  = 4,
    Plugins        = 5,
    Presets        = 6,
    Textures       = 7,
    Workspaces     = 8,
    Objects        = 9,
    Tutorials      = 10,
};

class UserDataPaths
{
public:
    static const QVector<UserDataFolder>& allFolders();

    static QString folderName(UserDataFolder folder);

    void setRoot(const QString& rootPath);

    QString root() const { return m_root.absolutePath(); }

    QString pathFor(UserDataFolder folder) const;

    core::Result ensureCreated() const;
    core::Result ensureCreated(UserDataFolder folder) const;

    QString fileIn(UserDataFolder folder, const QString& fileName) const;

private:
    QDir m_root;
};

} // namespace app
} // namespace openvegas