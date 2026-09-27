#pragma once

#include "core/Identifier.h"

namespace openvegas {
namespace plugin {

enum class PluginKind
{
    Unknown = 0,
    Effects,
    ColorEffects,
    Transitions,
    Generators,
    Filters,
    Emission,
    AssetImporter,
    Exporter,

    // Native plugin categories as shipped by the reference. They correspond to
    // its plugin wrapper classes and to the sub-folders of "Plugins":
    //   2D, Audio, AudioTransitions, Behavior, Geometry, VideoTransitions.
    Effect2D,
    // Tannen.dll wraps After Effects plugins separately (PluginAE2DEffect);
    // this port has no AE host, so nothing produces it yet.
    EffectAE2D,
    AudioEffect,
    AudioTransition,
    BehaviorEffect,
    GeometryEffect,
    VideoTransition,
};

using PluginId = core::Identifier;

} // namespace plugin
} // namespace openvegas