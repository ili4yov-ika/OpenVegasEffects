#pragma once

#include <QHash>
#include <QImage>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QMatrix4x4>

#include <QPoint>
#include <QSize>

#include <array>
#include <functional>

#include "core/Identifier.h"
#include "plugin/EffectSpec.h"

class QImage;

namespace openvegas::composition { class Composition; }

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
void registerNativeGeometryModule(
    const core::Identifier& id, const QString& filePath,
    const QString& dependencyDirectory, bool renderingVerified,
    const QVector<EffectParameterSpec>& parameters = {});
bool nativeEffectFrameRenderingVerified(const core::Identifier& id);
bool nativeGeometryRenderingVerified(const core::Identifier& id);
bool nativeVideoTransitionRenderingVerified(const core::Identifier& id);
bool nativeAudioEffectRenderingVerified(const core::Identifier& id);
bool nativeAudioTransitionRenderingVerified(const core::Identifier& id);
// Registered as an AudioTransitions module, runnable or not. Transitions that
// are not audio (including modules missing on this machine) are video ones.
bool isAudioTransition(const core::Identifier& id);
bool nativeBehaviorRenderingVerified(const core::Identifier& id);
bool nativeBehaviorSimulationRenderingVerified(const core::Identifier& id);
bool nativeBehaviorSubObjectRenderingVerified(const core::Identifier& id);

// Result of the three Behavior callbacks used by Tannen. TransformationAtTime
// (Notify 102) writes a column-major 4x4 matrix; OpacityAtTime (Notify 104)
// writes the opacity multiplier. The matrix starts as identity and opacity as
// one, so plugins that implement only one callback compose predictably.
struct NativeBehaviorResult
{
    QMatrix4x4 transformation;
    float opacity = 1.0f;
};

// What a Behavior is told about its layer besides the ID, as
// PluginBehaviorEffect::TransformationAtTime (Tannen 0x18034b6d0) and
// PluginHostAPI::GetPreBehaviorEffectTransformation (+0x300) tell it.
struct NativeBehaviorLayer
{
    // AbstractLayer::WorldTransformationAtTime without the Behaviors:
    // column-major, composition units from the frame's centre, Y up. The
    // modules read the layer's position from it (Drop starts the layer above
    // the frame wherever it stands).
    std::array<float, 16> world {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    // tagBiffBehaviorMC +0x20..+0x2c: MinX, MinY, MaxX, MaxY of the layer in
    // its own units - the text box (point text: the frame's width and its
    // lines), a footage layer 0..width by 0..height, a grade layer the frame.
    std::array<float, 4> bounds {0, 0, 0, 0};
    // +0x30/+0x34: the composition's size.
    QSize composition;
};

// BIFF's SubObjectTransformationAtTime (Notify 105) edits one 0x5c-byte
// record per glyph. The trailing 0x18 bytes are the native ClipValue (a
// clipping flag, three padding bytes, four floats, and one trailing flag).
// Keep both flags until their exact combination rules are recovered.
struct NativeSubObjectClipValue
{
    bool enabled = false;
    float values[4] {0.0f, 0.0f, 0.0f, 0.0f};
    bool secondary = false;
};

struct NativeSubObjectResult
{
    QVector<QMatrix4x4> transformations;
    QVector<NativeSubObjectClipValue> clipValues;
    QVector<float> opacities;
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
                            const QStringList& parameterValues = {},
                            const composition::Composition* composition = nullptr,
                            const core::Identifier& sourceLayerId = {},
                            const NativeBehaviorLayer& layer = {});

// Frame-only 102/104 path used before the layer's simulation behaviors are
// evaluated together. Calling the simulation modules separately would reset
// velocity between Acceleration, Gravity and Drag.
bool evaluateNativeBehaviorFrame(NativeBehaviorResult& result, int timelineFrame,
                                 int localFrame, int layerDurationFrames,
                                 int canvasWidth, int canvasHeight, double frameRate,
                                 const core::Identifier& id,
                                 const QStringList& parameterValues = {},
                                 const composition::Composition* composition = nullptr,
                                 const core::Identifier& sourceLayerId = {},
                                 const NativeBehaviorLayer& layer = {});
bool evaluateNativeSubObjectBehavior(
    NativeSubObjectResult& result, int timelineFrame, int localFrame,
    int layerDurationFrames, int canvasWidth, int canvasHeight,
    double frameRate, const core::Identifier& id,
    const QStringList& parameterValues, const core::Identifier& sourceLayerId);
bool simulateNativeBehaviorStack(NativeBehaviorResult& result, int timelineFrame,
                                 int localFrame, int layerDurationFrames,
                                 int canvasWidth, int canvasHeight, double frameRate,
                                 const QVector<NativeBehaviorRequest>& behaviors,
                                 const composition::Composition* composition = nullptr,
                                 const core::Identifier& sourceLayerId = {});

// Host geometry of the Geometry ABI (PluginGeometryEffect::ProcessGeometry,
// Notify 101). Flux hands text to these modules as batches of 0x80-byte
// records: 32-byte vertices (position, normal, uv), 0x14-byte triangles and
// 0x18-byte polygons. A polygon is one outline loop of a face - glyph holes
// are separate loops of the same face - and stays a loop until the host fills
// it after the last module, so Extrude can build walls along its edges.
struct NativeGeometryVertex
{
    float position[3] {0.0f, 0.0f, 0.0f};
    float normal[3] {0.0f, 0.0f, 1.0f};
    float uv[2] {0.0f, 0.0f};
};

// `material` and `group` travel with every triangle and polygon; modules copy
// them unchanged onto the faces they derive (Extrude's walls keep the
// polygon's). Flux fills each material's polygons together.
struct NativeGeometryTriangle
{
    qint32 indices[3] {0, 0, 0};
    qint32 material = 0;
    qint32 group = 0;
};

enum NativeGeometryPolygonFlag : quint32
{
    NativeGeometryFrontFace = 0x1,
    NativeGeometryBackFace = 0x2,       // with Front: one double-sided face
    NativeGeometryInternalEdges = 0x4,  // Extrude builds walls only on request
};

struct NativeGeometryPolygon
{
    QVector<qint32> indices;
    qint32 material = 0;
    qint32 group = 0;
    quint32 flags = NativeGeometryFrontFace | NativeGeometryBackFace;
};

struct NativeGeometryBatch
{
    QVector<NativeGeometryVertex> vertices;
    QVector<NativeGeometryTriangle> triangles;
    QVector<NativeGeometryPolygon> polygons;
    // +0x2c: the object's box in its own plane. RotateGeometry pivots around
    // the middle of extents[2..3] vertically, so glyphs of one line share an
    // axis; x and z come from the vertices.
    float extents[4] {0.0f, 0.0f, 0.0f, 0.0f};
    // +0x3c: column-major placement of the batch.
    float matrix[16] {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
};

// Runs one Geometry module over `geometry`. On success the module's output
// replaces it; false (unregistered, rejected, fault, or no output) leaves it
// unchanged, as Flux keeps the previous geometry when a module sets none.
bool applyNativeGeometryEffect(QVector<NativeGeometryBatch>& geometry,
                               const core::Identifier& id,
                               const QStringList& parameterValues,
                               int timelineFrame, int localFrame,
                               int layerDurationFrames, double frameRate);

// Viewer custom UI of a native module - what the reference viewer hands the
// selected effect so it can draw over the frame and take the pointer and the
// keyboard: PluginFile::CustomUISetup/Shutdown/Render/MouseEvent/KeyEvent/
// GainFocus/LoseFocus/HasContextMenu/ContextMenu, Notify(1001..1013) with
// context type 4. MotionTrack's feature picker and BendGeometry's handles
// live there. The context (api+0x20, 0xa8 bytes) describes the view; mouse
// and key events go through api+0x18.
//
// The port hands the module a view in its layer's pixels with the matrix that
// places them on the canvas (NativeInstanceHost::describe): event positions
// are canvas pixels taken back through it, and what it draws is a canvas-
// sized overlay the viewer scales like the frame, so the module never needs
// to know the viewer's zoom or pan.
struct NativeCustomUiView
{
    core::Identifier layerId;     // +0x00, the layer the effect is on
    QSize area;                   // +0x10/+0x14: the layer's pixel space (MotionTrack:
                                  // its footage), where points and drawing live
    double zoomX = 1.0;           // +0x18/+0x20: screen pixels per area pixel
    double zoomY = 1.0;
    qint32 frame = 0;             // +0x40: composition frame (GetLayerPixelTransform)
    qint32 parameterFrame = 0;    // +0x44: frame the parameters are read at
    qint32 layerFrame = 0;        // +0x4c: frame within the layer's own media
    qint32 layerFrameEnd = 0;     // +0x50
    std::array<float, 16> matrix {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};  // +0x54
    double pixelRatio = 1.0;      // +0x98
    // Not in the block: which effect instance this is (the module keeps its
    // state per instance - MotionTrack its analysis), and the shot's rate for
    // the render context's millisecond fields.
    QString instanceKey;
    double frameRate = 30.0;
    // The overlay drawn and the pointer positions given are in `target`
    // pixels (the canvas; empty = the area itself), and `matrix` takes the
    // layer's pixels there - so it is identity when the layer fills the
    // canvas unmoved. Render gets it with the orthographic step to clip
    // space added, and pointer positions are taken back through its inverse.
    QSize target;
};

// AbstractPlugin::MouseEventType, which PluginFile::CustomUIMouseEvent turns
// into Notify(1004 + type): 1004 a move, 1005 a press (MotionTrack starts a
// new lasso there), 1006 a release.
enum class NativeCustomUiMouse { Move = 0, Press = 1, Release = 2 };
enum class NativeCustomUiKey { Press = 0, Release = 1, Text = 2 };

// The mouse event at api+0x18: +0x00/+0x04 the point, +0x08 1.0 while the
// button is held (MotionTrack compares it with 0.5), +0x20.. the button,
// the buttons held, modifiers (1 Shift, 2 Ctrl, 4 Alt) and the click count.
struct NativeCustomUiPointer
{
    QPoint position;
    bool pressed = false;
    int button = 0;               // 1 left, 2 right, 3 middle
    int buttons = 0;              // bit 0 left, 1 right, 2 middle
    int modifiers = 0;
    int clicks = 0;
};

struct NativeCustomUiResult
{
    bool handled = false;         // Notify returned 1
    bool redraw = false;          // RedrawCustomUI (+0x2c8) during the call
    int cursor = 0;               // SetCursor (+0x388) code, 0 = none
    bool backgroundRequested = false;  // RequestBackgroundProcessing (+0x398)
    int backgroundDelayMs = 0;    // ... and the delay it asked for
    // Values the module set (SetStringValue/SetIntValue/SetBoolValue...), by
    // parameter index, and the enabled state it gave controls
    // (SetPropertyState +0xa8, byte 0), by key.
    QHash<int, QString> values;
    QHash<QString, bool> enabled;
};

// Modules known to draw viewer custom UI (MotionTrack, BendGeometry).
bool nativeEffectHasCustomUi(const core::Identifier& id);
NativeCustomUiResult nativeCustomUiSetup(const core::Identifier& id, const QStringList& values,
                                         const NativeCustomUiView& view);
NativeCustomUiResult nativeCustomUiShutdown(const core::Identifier& id, const QStringList& values,
                                            const NativeCustomUiView& view);
NativeCustomUiResult nativeCustomUiMouse(const core::Identifier& id, const QStringList& values,
                                         const NativeCustomUiView& view, NativeCustomUiMouse type,
                                         const NativeCustomUiPointer& pointer);
// `keysym` is the X11 keysym the modules compare with (0xffe3 Control_L,
// 0xffe9 Alt_L, ...); see nativeKeysym().
NativeCustomUiResult nativeCustomUiKey(const core::Identifier& id, const QStringList& values,
                                       const NativeCustomUiView& view, NativeCustomUiKey type,
                                       quint32 keysym, const QString& text);
NativeCustomUiResult nativeCustomUiFocus(const core::Identifier& id, const QStringList& values,
                                         const NativeCustomUiView& view, bool gained);
NativeCustomUiResult nativeCustomUiHasContextMenu(const core::Identifier& id,
                                                  const QStringList& values,
                                                  const NativeCustomUiView& view);
NativeCustomUiResult nativeCustomUiContextMenu(const core::Identifier& id,
                                               const QStringList& values,
                                               const NativeCustomUiView& view);
// Notify(1003) into a transparent area-sized target; null when the module
// draws nothing or the GL path is unavailable.
QImage nativeCustomUiRender(const core::Identifier& id, const QStringList& values,
                            const NativeCustomUiView& view, NativeCustomUiResult* result = nullptr);
// Called (on the GUI thread) whenever a module asks RedrawCustomUI from
// outside an event the viewer is delivering - e.g. background processing.
void setNativeCustomUiRedrawHandler(std::function<void()> handler);

// --- The rest of a module's life on the same instance -----------------------
// Tannen's PluginFile calls, for the modules with custom UI (MotionTrack):
//  - NotifyPropertyChanged, Notify(7): api+0x18 -> {char* key, +0x14 frame,
//    +0x18 1}, the behaviour render context (type 0) at api+0x20;
//  - NotifyBackgroundProcess, Notify(18): the same context, after the module
//    asked RequestBackgroundProcessing(ms);
//  - SerializeInstanceData, Notify(5): the module hands its bytes to the
//    SerializeInstanceBytes callback at api+0x110; DeserializeInstanceData,
//    Notify(6): bytes at api+0x30, size at api+0x38;
//  - TransformationAtTime, Notify(102), context type 6, as a Behavior.
// The render context (0xc8 bytes): +0x08 layer ID, +0x24 PAR, +0x60/+0x64
// frame size, +0x6c/+0x70 time and layer time in ms, +0x98/+0x9c frame and
// layer frame, +0xa0 the layer's length in frames.
NativeCustomUiResult nativePropertyChanged(const core::Identifier& id, const QStringList& values,
                                           const NativeCustomUiView& view, const QString& key);
NativeCustomUiResult nativeBackgroundProcess(const core::Identifier& id, const QStringList& values,
                                             const NativeCustomUiView& view);
QByteArray nativeInstanceData(const core::Identifier& id, const QStringList& values,
                              const NativeCustomUiView& view);
NativeCustomUiResult nativeRestoreInstanceData(const core::Identifier& id,
                                               const QStringList& values,
                                               const NativeCustomUiView& view,
                                               const QByteArray& data);
// The layer transform the instance gives at `frame` (column-major 4x4,
// composition pixels, Y up - the module's own Y-down result flipped); false
// when it gives none.
bool nativeInstanceTransformation(const core::Identifier& id, const QStringList& values,
                                  const NativeCustomUiView& view, int frame, int layerFrame,
                                  std::array<float, 16>* matrix);

// What the host knows about layers and their footage, asked by those calls:
// GetLayerInfoV2 (+0x128: +0x28 type - 0 footage -, +0x2c start frame, +0x34
// length), GetAssetInfo (+0x3c8, tagBiffAssetInfo: frame count, rate, size,
// type 0 video) and GetAssetTexture (+0x3d0: the footage frame as a texture;
// MotionTrack reads it back with glGetTexImage).
struct NativeLayerInfo
{
    bool valid = false;
    int type = 0;                 // +0x28
    int startFrame = 0;           // +0x2c
    int durationFrames = 1;       // +0x34
    QSize size;                   // +0x6c/+0x70: the layer's source size
    // +0x74: AbstractLayer::WorldTransformationAtTime, column-major.
    std::array<float, 16> world {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
};
struct NativeAssetInfo
{
    bool valid = false;
    int frameCount = -1;          // +0x00
    double frameRate = 0.0;       // +0x08
    // +0x10: the footage frame the layer starts on (MotionTrack subtracts it
    // from the layer offset); +0x14 is left zero.
    int startFrame = 0;
    int type = 5;                 // +0x18: 0 video, 3 composite shot, 5 none
    QString key;                  // what the tracked features are stored under
};
struct NativeSourceHost
{
    std::function<NativeLayerInfo(const QString& layerId)> layerInfo;
    std::function<NativeAssetInfo(const QString& layerId)> assetInfo;
    std::function<QImage(const QString& layerId, int assetFrame)> assetFrame;
};
void setNativeSourceHost(NativeSourceHost host);

// Modules whose layer transform comes from their own instance (MotionTrack):
// the renderer cannot run them, so the instance's matrices are computed on
// the GUI thread for every frame of the layer and kept here by instance key.
bool nativeBehaviorUsesInstance(const core::Identifier& id);
// The registered module's parameters, in the order of its values.
QVector<EffectParameterSpec> nativeParameters(const core::Identifier& id);
void setNativeInstanceTransforms(const QString& instanceKey, int firstFrame,
                                 const QVector<std::array<float, 16>>& matrices);
bool nativeInstanceTransformAt(const QString& instanceKey, int frame,
                               std::array<float, 16>* matrix);
void clearNativeInstanceTransforms(const QString& instanceKey = QString());

// SetPropertyState (+0xa8, tagPropertyState byte 0) as an instance last set
// it, by instance key and control key. Tannen hands the state to the
// instance and to the controls panel; the port hides the control's row while
// it is off - MotionTrack shows its status during the analysis and switches
// it off with the transform calculated. A control never set is shown.
void setNativeControlStates(const QString& instanceKey, const QHash<QString, bool>& states);
bool nativeControlShown(const QString& instanceKey, const QString& key);
void clearNativeControlStates(const QString& instanceKey = QString());

// SaveTrackingData/GetNumberOfFeatures/GetTrackingData (+0x3b0..+0x3c0): the
// features a module tracked between two frames of a footage asset - each
// point on the previous frame and where it went, and the affine fit (2x3,
// row-major) - kept per asset frame like AbstractAsset::SetTrackedFeatures.
struct NativeTrackedFeatures
{
    QVector<QPointF> from;
    QVector<QPointF> to;
    std::array<float, 6> affine {1, 0, 0, 0, 1, 0};
};
QHash<int, NativeTrackedFeatures> nativeTrackedFeatures(const QString& assetKey);
void clearNativeTrackedFeatures(const QString& assetKey = QString());
// Qt key -> the X11 keysym Tannen passes through (Qt's own key codes for the
// named keys differ).
quint32 nativeKeysym(int qtKey, const QString& text);
// Tannen's mapping of SetCursor codes to cursor shapes (CustomUIMouseEvent):
// 1 arrow, 2 cross, 3 size-all, 4 pointing hand, 6 vertical, 7 B-diagonal,
// 8 horizontal, 9 F-diagonal, 20 open hand. -1 for codes it leaves alone.
int nativeCursorShape(int code);

// When a frame is: Plugin2DEffect::Render (Tannen 0x180333890) puts the time
// and the layer's time in milliseconds at RenderContext +0x6c/+0x70, the
// frame and the layer's frame at +0x98/+0x9c and the layer's length in frames
// at +0xa0 (a layer frame past the end is held on the last one). Effects that
// move by themselves - noise, grain, flicker, shake - read them.
struct NativeFrameTime
{
    int frame = 0;
    int layerFrame = 0;
    int layerFrames = 1;
    double frameRate = 30.0;
};

// Executes the 2D BIFF GPU path (Notify 8 once per render thread, Notify 10 per
// frame). Returns false without changing image when the id is unregistered or
// the module/context rejects the request.
bool applyNativeEffectToImage(QImage& image, const core::Identifier& id,
                              const QStringList& parameterValues = {},
                              const NativeFrameTime& time = {});

// Executes a native video-transition frame with both source textures and the
// normalized transition position supplied through the distinct transition
// RenderContext recovered from PluginVideoTransition::Render.
bool applyNativeVideoTransition(QImage& output, const QImage& from, const QImage& to,
                                float progress, const core::Identifier& id,
                                const QStringList& parameterValues = {});

// Timing of the layer that owns an audio effect, reported through
// GetLayerInfoV2/GetTimelineInfo. Modules such as AudioReverse derive their
// source ranges from the layer duration.
struct NativeAudioLayer
{
    qint64 durationSamples = 0; // 0 = unknown; reported as one frame
    double frameRate = 30.0;
};

// Executes the native 16-bit interleaved PCM paths recovered from
// PluginAudioEffect::Render and PluginAudioTransition::Render. Audio effects
// edit samples in place. Transitions write the overlapping input blocks to
// output and use sampleOffset/totalTransitionSamples for their fade position.
//
// startSample is layer-local (0 = first sample of the layer). Before Render
// the host calls GetSampleRanges (Notify 12); modules such as Equaliser,
// Echo and the reverbs ask for history before startSample there. Those frames
// come from sourceSamples, whose first frame is sourceStartSample; frames it
// does not cover are silence. Without sourceSamples only the block is known.
bool applyNativeAudioEffect(QVector<qint16>& interleavedSamples, int channels,
                            int sampleRate, qint64 startSample,
                            const core::Identifier& id,
                            const QStringList& parameterValues = {},
                            const QString& instanceKey = {},
                            const QVector<qint16>* sourceSamples = nullptr,
                            qint64 sourceStartSample = 0,
                            const NativeAudioLayer& layer = {});
// Offline variant: splits a long buffer into blockFrames-sized renders on one
// persistent instance, asks parameterValuesAt for the values at each block's
// first frame offset and keeps the whole unprocessed buffer visible as the
// layer's dry source, so both history and look-ahead ranges are exact.
bool applyNativeAudioEffectBlocks(
    QVector<qint16>& interleavedSamples, int channels, int sampleRate,
    qint64 startSample, const core::Identifier& id,
    const std::function<QStringList(qint64 frameOffset)>& parameterValuesAt,
    const QString& instanceKey = {}, int blockFrames = 480,
    const NativeAudioLayer& layer = {});
// sampleOffset is the first output frame's position inside the transition of
// totalTransitionSamples frames; cutSample is the edit point there (-1 = the
// middle). Fade dips `from` to silence before it and raises `to` after it.
bool applyNativeAudioTransition(QVector<qint16>& output,
                                const QVector<qint16>& from,
                                const QVector<qint16>& to, int channels,
                                qint32 sampleOffset, qint32 totalTransitionSamples,
                                const core::Identifier& id,
                                const QStringList& parameterValues = {},
                                qint32 cutSample = -1);

// Releases the context, live modules and GL objects owned by the calling
// render thread. Call while QGuiApplication and that thread's event loop still
// exist; RenderManager does this before stopping its worker.
void releaseNativeEffectThreadRenderer();

} // namespace plugin
} // namespace openvegas
