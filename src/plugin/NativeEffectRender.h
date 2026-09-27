#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <QMatrix4x4>

#include "core/Identifier.h"
#include "plugin/EffectSpec.h"

class QImage;

namespace openvegas {
namespace plugin {

// Scan-time registry used by the renderer. Native ids are stored in project
// files, while the module path is installation-specific and must be resolved
// again by PluginManager on each launch.
void clearNativeEffectModules();
void registerNativeEffectModule(const core::Identifier& id, const QString& filePath,
                                const QString& dependencyDirectory,
                                bool frameRenderingVerified,
                                const QVector<EffectParameterSpec>& parameters = {});
void registerNativeVideoTransitionModule(
    const core::Identifier& id, const QString& filePath,
    const QString& dependencyDirectory, bool renderingVerified,
    const QVector<EffectParameterSpec>& parameters = {});
void registerNativeAudioEffectModule(
    const core::Identifier& id, const QString& filePath,
    const QString& dependencyDirectory, bool renderingVerified,
    const QVector<EffectParameterSpec>& parameters = {});
void registerNativeAudioTransitionModule(
    const core::Identifier& id, const QString& filePath,
    const QString& dependencyDirectory, bool renderingVerified,
    const QVector<EffectParameterSpec>& parameters = {});
void registerNativeBehaviorModule(
    const core::Identifier& id, const QString& filePath,
    const QString& dependencyDirectory, bool renderingVerified,
    const QVector<EffectParameterSpec>& parameters = {});
bool nativeEffectFrameRenderingVerified(const core::Identifier& id);
bool nativeVideoTransitionRenderingVerified(const core::Identifier& id);
bool nativeAudioEffectRenderingVerified(const core::Identifier& id);
bool nativeAudioTransitionRenderingVerified(const core::Identifier& id);
bool nativeBehaviorRenderingVerified(const core::Identifier& id);
bool nativeBehaviorSimulationRenderingVerified(const core::Identifier& id);

// Result of the three Behavior callbacks used by Tannen. TransformationAtTime
// (Notify 102) writes a column-major 4x4 matrix; OpacityAtTime (Notify 104)
// writes the opacity multiplier. The matrix starts as identity and opacity as
// one, so plugins that implement only one callback compose predictably.
struct NativeBehaviorResult
{
    QMatrix4x4 transformation;
    float opacity = 1.0f;
};

struct NativeBehaviorRequest
{
    core::Identifier id;
    QStringList parameterValues;
};

bool evaluateNativeBehavior(NativeBehaviorResult& result, int timelineFrame,
                            int localFrame, int layerDurationFrames,
                            int canvasWidth, int canvasHeight, double frameRate,
                            const core::Identifier& id,
                            const QStringList& parameterValues = {});

// Frame-only 102/104 path used before the layer's simulation behaviors are
// evaluated together. Calling the simulation modules separately would reset
// velocity between Acceleration, Gravity and Drag.
bool evaluateNativeBehaviorFrame(NativeBehaviorResult& result, int timelineFrame,
                                 int localFrame, int layerDurationFrames,
                                 int canvasWidth, int canvasHeight, double frameRate,
                                 const core::Identifier& id,
                                 const QStringList& parameterValues = {});
bool simulateNativeBehaviorStack(NativeBehaviorResult& result, int timelineFrame,
                                 int localFrame, int layerDurationFrames,
                                 int canvasWidth, int canvasHeight, double frameRate,
                                 const QVector<NativeBehaviorRequest>& behaviors);

// Executes the 2D BIFF GPU path (Notify 8 once per render thread, Notify 10 per
// frame). Returns false without changing image when the id is unregistered or
// the module/context rejects the request.
bool applyNativeEffectToImage(QImage& image, const core::Identifier& id,
                              const QStringList& parameterValues = {});

// Executes a native video-transition frame with both source textures and the
// normalized transition position supplied through the distinct transition
// RenderContext recovered from PluginVideoTransition::Render.
bool applyNativeVideoTransition(QImage& output, const QImage& from, const QImage& to,
                                float progress, const core::Identifier& id,
                                const QStringList& parameterValues = {});

// Executes the native 16-bit interleaved PCM paths recovered from
// PluginAudioEffect::Render and PluginAudioTransition::Render. Audio effects
// edit samples in place. Transitions write the overlapping input blocks to
// output and use sampleOffset/totalTransitionSamples for their fade position.
bool applyNativeAudioEffect(QVector<qint16>& interleavedSamples, int channels,
                            int sampleRate, qint64 startSample,
                            const core::Identifier& id,
                            const QStringList& parameterValues = {});
bool applyNativeAudioTransition(QVector<qint16>& output,
                                const QVector<qint16>& from,
                                const QVector<qint16>& to, int channels,
                                qint32 sampleOffset, qint32 totalTransitionSamples,
                                const core::Identifier& id,
                                const QStringList& parameterValues = {});

// Releases the context, live modules and GL objects owned by the calling
// render thread. Call while QGuiApplication and that thread's event loop still
// exist; RenderManager does this before stopping its worker.
void releaseNativeEffectThreadRenderer();

} // namespace plugin
} // namespace openvegas
