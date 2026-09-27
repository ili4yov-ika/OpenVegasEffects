#pragma once

#include <memory>

#include "plugin/EffectSpec.h"

namespace openvegas {
namespace plugin {

class EffectInstance
{
public:
    virtual ~EffectInstance() = default;

    virtual EffectSpec spec() const = 0;
    virtual void setParameter(const QString& name, const QString& value) = 0;
    virtual QString parameter(const QString& name) const = 0;
    virtual bool render(int width, int height, unsigned char* rgba) = 0;
};

class Plugin
{
public:
    virtual ~Plugin() = default;

    virtual PluginId id() const = 0;
    virtual QString name() const = 0;
    virtual QString category() const = 0;
    virtual core::Version version() const = 0;
    virtual QString libraryPath() const = 0;
    virtual PluginKind kind() const = 0;
};

} // namespace plugin
} // namespace openvegas