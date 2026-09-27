#pragma once

#include <QHash>
#include <QString>
#include <QVector>

#include <functional>

#include "core/Result.h"
#include "license/LicenseManager.h"
#include "plugin/EffectSpec.h"
#include "plugin/Plugin.h"
#include "plugin/PluginId.h"

namespace openvegas {
namespace plugin {

struct Callbacks
{
    std::function<QString(int folderIndex)> resolveUserFolder;
    std::function<void(int level, const QString& message)> logMessage;
};

class PluginManager
{
public:
    PluginManager() = default;
    virtual ~PluginManager() = default;

    virtual void setPluginDirs(const QVector<QString>& dirs) = 0;
    virtual void setCallbacks(const Callbacks& callbacks) = 0;

    virtual core::Result scan() = 0;

    virtual QVector<PluginId> allPluginIds() const = 0;
    virtual QVector<PluginId> pluginIdsByKind(PluginKind kind) const = 0;
    virtual QVector<PluginId> pluginIdsByCategory(const QString& category) const = 0;

    virtual std::shared_ptr<Plugin> plugin(const PluginId& id) const = 0;
    virtual EffectSpec spec(const PluginId& id) const = 0;

    virtual std::shared_ptr<EffectInstance> createEffect(const PluginId& id) = 0;
};

std::shared_ptr<PluginManager> createPluginManager();

} // namespace plugin
} // namespace openvegas