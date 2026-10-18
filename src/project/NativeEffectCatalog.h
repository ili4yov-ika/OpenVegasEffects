#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>
#include <optional>

namespace openvegas {
namespace project {

// What a project file needs to know about a native plugin to write and read
// its effects as the reference's EffectInstance does (Project.dll
// EffectInstance::Serialize 0x1803f7e00): the reference's plugin ID (the
// module's FXID), its name and version, the layer's container it goes in and
// its properties - the keys the module registered, by type.
struct NativeEffectEntry
{
    enum class Container
    {
        None,             // transitions: not an effect list entry
        Effects,          // 2D and audio effects (EffectsHelper::SerializeEffects)
        GeometryEffects,  // SerializeGeometryEffects
        BehaviorEffects,  // SerializeBehaviorEffects, inside LayerBase
    };
    struct Parameter
    {
        QString name;          // the property's key
        QString type;          // EffectParameterSpec::type
        QString defaultValue;
        QStringList choices;
    };

    QString pluginId;          // this port's
    QString guid;              // the reference's PluginID
    QString name;              // PluginName
    int versionMajor = 0;
    int versionMinor = 0;
    Container container = Container::None;
    QVector<Parameter> parameters;
};

// Finds a native plugin by this port's ID or by the reference's PluginID.
using NativeEffectLookup = std::function<std::optional<NativeEffectEntry>(const QString& idOrGuid)>;

inline NativeEffectLookup& nativeEffectLookupSlot()
{
    static NativeEffectLookup lookup;
    return lookup;
}

// Set by the plugin manager once it knows the plugins; without one, effects
// travel only in the port's own extension.
inline void setNativeEffectLookup(NativeEffectLookup lookup)
{
    nativeEffectLookupSlot() = std::move(lookup);
}

inline std::optional<NativeEffectEntry> findNativeEffect(const QString& idOrGuid)
{
    const NativeEffectLookup& lookup = nativeEffectLookupSlot();
    if (!lookup || idOrGuid.isEmpty()) return std::nullopt;
    return lookup(idOrGuid);
}

} // namespace project
} // namespace openvegas
