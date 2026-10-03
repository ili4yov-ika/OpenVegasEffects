#pragma once

#include "composition/KeyFrame.h"
#include "core/Identifier.h"

#include <QByteArray>
#include <QMap>
#include <QString>
#include <QStringList>

namespace openvegas {
namespace composition {

// Where a transition plugin sits on its clip. The reference keeps a separate
// project::Transition (Length, RelativeStartFrame, From/ToObject) as a clip's
// Incoming/OutgoingTransition; here it is an Effect entry with an edge, so the
// timeline rows, inspector, keyframes and undo treat it like any other effect.
enum class TransitionEdge
{
    None, // an ordinary effect, applied to every frame
    In,   // from the adjacent previous clip on the layer, or from nothing
    Out,  // to nothing at the end of the clip
};

struct Effect
{
    core::Identifier pluginId;
    QString name;
    QStringList parameterValues;
    bool enabled = true;

    TransitionEdge transitionEdge = TransitionEdge::None;
    // Transition::Length, in seconds of composition time.
    double transitionSeconds = 1.0;
    bool isTransition() const { return transitionEdge != TransitionEdge::None; }

    // Animation per parameter index. A parameter with no entry is static and
    // reads from parameterValues; one with an entry is driven by its curve.
    // The reference keeps the same split: a property holds a default value and
    // an optional KeyFrameList (see MARKDOWN/RE_VegasEffects.md).
    QMap<int, KeyFrameList> animation;

    // What a native module keeps of its own (PluginFile::SerializeInstanceData,
    // Notify 5): MotionTrack's footage analysis, selected features and
    // transforms. Written as <InstanceBytes>, as EffectInstance does, and
    // handed back with Notify(6) when the instance is made again.
    QByteArray instanceData;

    bool isAnimated(int parameterIndex) const
    {
        const auto it = animation.constFind(parameterIndex);
        return it != animation.constEnd() && !it->isEmpty();
    }

    // Static value, or the animated one when the parameter has keyframes.
    QVariant parameterAt(int parameterIndex, int frame) const
    {
        const auto it = animation.constFind(parameterIndex);
        if (it != animation.constEnd() && !it->isEmpty()) {
            return it->valueAt(frame);
        }
        return (parameterIndex >= 0 && parameterIndex < parameterValues.size())
                   ? QVariant(parameterValues.at(parameterIndex))
                   : QVariant();
    }
};

} // namespace composition
} // namespace openvegas