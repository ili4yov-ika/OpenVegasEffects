#pragma once

#include "composition/Layer.h"
#include "plugin/EffectSpec.h"
#include "plugin/NativeEffectRender.h"

#include <QtGlobal>

namespace openvegas {
namespace ui {

// Drag payload from the Effects panel: the plugin id as UTF-8.
inline constexpr char kEffectMimeType[] = "application/x-openvegas-effect";
// Drag payloads from the Media panel: a media asset's path, or a composite
// shot's id, as UTF-8. Dropped on the timeline they become a new layer.
inline constexpr char kMediaMimeType[] = "application/x-openvegas-media";
inline constexpr char kCompositeShotMimeType[] = "application/x-openvegas-composite-shot";

inline bool isTransitionSpec(const plugin::EffectSpec& spec)
{
    return spec.kind == plugin::PluginKind::VideoTransition
        || spec.kind == plugin::PluginKind::AudioTransition;
}

// Appends a new instance of `spec` to the clip with the documented defaults.
// A transition goes on the clip edge nearer `seconds` (composition time): the
// start blends from the previous clip on the layer or fades in, the end fades
// out. Each edge keeps one video and one audio transition, so a new one
// replaces the old of its kind. Returns the new effect's index.
inline int addEffectToClip(composition::Clip& clip, const plugin::EffectSpec& spec, double seconds)
{
    composition::Effect effect;
    effect.pluginId = spec.id;
    effect.name = spec.displayName.isEmpty() ? spec.name : spec.displayName;
    for (const plugin::EffectParameterSpec& parameter : spec.parameters) {
        effect.parameterValues.append(parameter.defaultValue);
    }
    if (isTransitionSpec(spec)) {
        const bool atEnd = seconds > clip.startSeconds + clip.durationSeconds * 0.5;
        effect.transitionEdge = atEnd ? composition::TransitionEdge::Out
                                      : composition::TransitionEdge::In;
        effect.transitionSeconds = qMin(1.0, qMax(0.0, clip.durationSeconds));
        const bool audio = spec.kind == plugin::PluginKind::AudioTransition;
        for (int i = int(clip.effects.size()) - 1; i >= 0; --i) {
            const composition::Effect& existing = clip.effects.at(i);
            if (existing.transitionEdge == effect.transitionEdge
                && plugin::isAudioTransition(existing.pluginId) == audio) {
                clip.effects.removeAt(i);
            }
        }
    }
    clip.effects.append(effect);
    return int(clip.effects.size()) - 1;
}

} // namespace ui
} // namespace openvegas
