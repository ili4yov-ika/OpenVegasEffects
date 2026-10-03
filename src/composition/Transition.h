#pragma once

#include "composition/Layer.h"

#include <QVector>
#include <functional>

namespace openvegas {
namespace composition {

// One transition interval on a layer. fromClip/toClip index layer.clips; -1
// stands for nothing (transparent picture or silence), which is how a fade in
// at a layer's first clip or a fade out at a clip's end is expressed.
struct TransitionWindow
{
    int fromClip = -1;
    int toClip = -1;
    int ownerClip = -1;   // clip whose effect list holds the transition
    int effectIndex = -1;
    double start = 0.0;   // composition seconds
    double end = 0.0;
    // Edit point inside the window: the cut between adjacent clips, the middle
    // of an overlap, and the window's far side for a fade from/to nothing.
    // Audio Fade dips to silence exactly here.
    double cut = 0.0;

    bool contains(double seconds) const { return seconds >= start && seconds < end; }
    double progressAt(double seconds) const
    {
        return end > start ? qBound(0.0, (seconds - start) / (end - start), 1.0) : 1.0;
    }
};

// The clip an In transition on clipIndex starts from: the latest-ending clip on
// the same layer that begins earlier and reaches the cut (within tolerance) -
// the reference's Transition::FromObject for adjacent sequence objects.
inline int transitionPredecessor(const Layer& layer, int clipIndex, double tolerance)
{
    const Clip& to = layer.clips.at(clipIndex);
    int best = -1;
    for (int i = 0; i < layer.clips.size(); ++i) {
        const Clip& candidate = layer.clips.at(i);
        if (i == clipIndex || candidate.startSeconds >= to.startSeconds
            || candidate.endSeconds() < to.startSeconds - tolerance) {
            continue;
        }
        if (best < 0 || candidate.endSeconds() > layer.clips.at(best).endSeconds()) {
            best = i;
        }
    }
    return best;
}

// Resolves every enabled transition on the layer that `accept` admits (video
// or audio modules). A transition between adjacent clips is centred on the
// cut, so both clips play into their handles; overlapping clips transition
// over their overlap. Without a neighbour the window lies inside the clip.
inline QVector<TransitionWindow> transitionWindows(
    const Layer& layer, double tolerance, const std::function<bool(const Effect&)>& accept)
{
    QVector<TransitionWindow> windows;
    for (int clipIndex = 0; clipIndex < layer.clips.size(); ++clipIndex) {
        const Clip& clip = layer.clips.at(clipIndex);
        for (int effectIndex = 0; effectIndex < clip.effects.size(); ++effectIndex) {
            const Effect& effect = clip.effects.at(effectIndex);
            if (!effect.enabled || !effect.isTransition() || (accept && !accept(effect))) {
                continue;
            }
            const double length = qBound(0.0, effect.transitionSeconds,
                                         qMax(0.0, clip.durationSeconds));
            TransitionWindow window;
            window.ownerClip = clipIndex;
            window.effectIndex = effectIndex;
            if (effect.transitionEdge == TransitionEdge::In) {
                window.toClip = clipIndex;
                window.fromClip = transitionPredecessor(layer, clipIndex, tolerance);
                if (window.fromClip < 0) {
                    window.start = clip.startSeconds;
                    window.end = clip.startSeconds + length;
                    window.cut = window.start;
                } else {
                    const double previousEnd = layer.clips.at(window.fromClip).endSeconds();
                    if (previousEnd > clip.startSeconds + tolerance) {
                        window.start = clip.startSeconds;
                        window.end = qMin(previousEnd, clip.endSeconds());
                        window.cut = (window.start + window.end) * 0.5;
                    } else {
                        window.start = clip.startSeconds - effect.transitionSeconds * 0.5;
                        window.end = window.start + effect.transitionSeconds;
                        window.cut = clip.startSeconds;
                    }
                }
            } else {
                window.fromClip = clipIndex;
                window.start = clip.endSeconds() - length;
                window.end = clip.endSeconds();
                window.cut = window.end;
            }
            if (window.end > window.start) {
                windows.append(window);
            }
        }
    }
    return windows;
}

} // namespace composition
} // namespace openvegas
