#pragma once

#include <QHash>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QTransform>

#include <functional>
#include <map>
#include <memory>

#include "composition/Layer.h"
#include "core/Identifier.h"
#include "plugin/NativeEffectRender.h"

namespace openvegas {
namespace composition {
class Composition;
struct Effect;
struct Layer;
struct Clip;
}
namespace media {
class MediaManager;
class VideoDecoder;
}

namespace ui {

// The application side of the native modules that live on an instance of
// their own (MotionTrack, AutomaticStabilizer, MotionLock): what PluginFile
// and the project do for them in the reference.
//
//  - It answers the module's questions about layers and footage
//    (GetLayerInfoV2, GetAssetInfo, GetAssetTexture) from the shot and the
//    media - footage frames are decoded on the spot.
//  - It sends property changes the user makes (Notify 7) and runs background
//    processing (Notify 18) on a timer for as long as the module asks for it.
//  - Values the module sets (its status, feature IDs) go straight into the
//    effect, as plugin-made changes do in the reference: no undo step.
//  - When the module settles it keeps the instance's data in the effect
//    (Notify 5, saved as <InstanceBytes>) and, for MotionTrack, works out its
//    matrix for every frame of the clip (Notify 102), which the renderer then
//    reads; the 2D effects draw from that data themselves.
//  - AutomaticStabilizer and MotionLock analyse their own layer: each is told
//    it was put on its layer once (Notify 7, "Effect_Parent_Layer",
//    Plugin2DEffect::NotifySetParent), and what they ask of the project from
//    their buttons - a new point layer, keys on a layer
//    (plugin::NativeLayerRequest) - is made here as one undo step.
//
// An instance is named "<layer ID>/<clip>/<effect>", as the viewer's custom
// UI names it.
class NativeInstanceHost : public QObject
{
    Q_OBJECT

public:
    struct Ref
    {
        core::Identifier layerId;
        int clip = -1;
        int effect = -1;
        bool isValid() const { return layerId.isValid() && clip >= 0 && effect >= 0; }
    };

    using CompositionProvider = std::function<std::shared_ptr<composition::Composition>()>;
    using MediaProvider = std::function<std::shared_ptr<media::MediaManager>()>;

    NativeInstanceHost(CompositionProvider composition, MediaProvider media,
                       QObject* parent = nullptr);
    ~NativeInstanceHost() override;

    static QString instanceKey(const Ref& ref);
    // The layer motion an instance gives (nativeInstanceTransformation: the
    // footage's pixels, Y up) as the renderer applies it - on a `canvas` of
    // display pixels, from its centre, Y up - for footage placed there by
    // `placement` (its pixels, Y down, to the canvas, Y down).
    static std::array<float, 16> canvasMotion(const std::array<float, 16>& footageMotion,
                                              const QTransform& placement, const QSize& canvas);

    // The plugin, its values at `seconds` and the view its calls use. The
    // view's area is the tracked footage when the module names a source layer
    // ("motionFromLayer"), with the matrix that places it on the canvas.
    bool describe(const Ref& ref, double seconds, core::Identifier* pluginId,
                  QStringList* values, plugin::NativeCustomUiView* view) const;

    // Hands the instance its saved data first, once per instance.
    void prepare(const Ref& ref);
    // The user changed parameter `parameter` in the inspector (Notify 7). A
    // button only counts when it is pressed.
    void propertyChanged(const Ref& ref, int parameter, double seconds);
    // What a call on the instance returned - the viewer's events.
    void handleResult(const Ref& ref, const plugin::NativeCustomUiResult& result);
    // Every instance-backed effect of the shot gets its data back and its
    // matrices worked out (a project opened, another shot shown). Nothing in
    // the shot changes, so nothing is signalled: the next frame drawn shows it.
    void restoreAll();
    // restoreAll when the instance-backed effects moved, came or went, or
    // their data changed under them (undo): cheap otherwise.
    void sync();

    bool isBusy() const { return !m_pending.isEmpty(); }

    // The keys and static values a module's transforms give a layer
    // (CreatePointLayer / ApplyTransformToLayer): "position", "rotationZ"
    // and "scale" - keys at `firstFrame` plus each transform's time for more
    // than one, the values themselves for one (a key at that time where the
    // property is animated).
    static void applyLayerTransforms(composition::Layer& layer,
                                     const QVector<plugin::NativeLayerTransform>& transforms,
                                     int firstFrame, double frameRate);

signals:
    // Values, the module's data or the layer's motion changed.
    void effectChanged();
    // A module changed the shot's layers (`before` them) as one step named
    // `text`: the owner records it for undo.
    void layersEdited(const QVector<composition::Layer>& before, const QString& text);

private:
    composition::Effect* effectFor(const Ref& ref) const;
    // Values set, background work scheduled; `settle` bakes when nothing more
    // is asked for (a property change), otherwise only the end of background
    // work does.
    void apply(const Ref& ref, const plugin::NativeCustomUiResult& result, bool settle);
    void runBackground();
    void finish(const Ref& ref);
    void bake(const Ref& ref);
    // The edits a module asked for; true when the shot changed.
    bool applyLayerRequests(const Ref& ref, const QVector<plugin::NativeLayerRequest>& requests);
    // Where a layer starts on the shot, in frames: the clip that matters now
    // (layerById); a layer without clips starts with the shot.
    int layerStartFrame(const QString& layerId) const;

    // The source host's answers.
    plugin::NativeLayerInfo layerInfo(const QString& layerId) const;
public:
    // A MotionTrack result with the footage layer's world taken back off
    // (the module gives that world times the footage's motion).
    static std::array<float, 16> withoutWorld(const std::array<float, 16>& motion,
                                              const std::array<float, 16>& world);
private:
    plugin::NativeAssetInfo assetInfo(const QString& layerId) const;
    QImage assetFrame(const QString& layerId, int frame);
    // The layer with that ID and the clip of it that matters now: the one
    // being worked on, or the clip that overlaps it.
    const composition::Layer* layerById(const QString& layerId, int* clipIndex) const;
    // Where the footage the instance tracks sits at `frame`: its pixels (Y
    // down) to the render canvas (display pixels, Y down). False without one.
    bool footagePlacement(const Ref& ref, int frame, QSize* content, QTransform* placement) const;
    // The layer whose footage the instance reads at `frame` (its "Motion
    // from layer", or its own for the stabilizers); empty without one.
    QString footageLayerId(const Ref& ref, int frame) const;
    double frameRate() const;
    // The shot being worked on: the open one, or a shot nested in it while
    // its instances are restored (scopedTo).
    std::shared_ptr<composition::Composition> scope() const;
    // Every shot the open one plays - itself and those nested in it, at any
    // depth - with their instance-backed effects.
    QVector<std::pair<std::shared_ptr<composition::Composition>, QHash<QString, Ref>>> instancesInTree() const;

    CompositionProvider m_composition;
    mutable std::shared_ptr<composition::Composition> m_scope;
    // Frames of nested shots handed to modules as footage, by shot, state
    // and frame (a few, they are rendered on demand).
    // While an instance's motion is asked for at a frame, the layers'
    // worlds at it go with their GetLayerInfoV2 answers, and are kept to
    // take back off the module's result.
    mutable int m_worldFrame = -1;
    mutable QHash<QString, std::array<float, 16>> m_givenWorlds;
    QHash<QString, QImage> m_nestedFrames;
    QStringList m_nestedFrameOrder;
    QString m_preRenders;

public:
    // The project's pre-render root, for nested shots read as footage.
    void setPreRenderDirectory(const QString& directory) { m_preRenders = directory; }

private:
    MediaProvider m_media;
    QTimer m_background;
    QHash<QString, Ref> m_pending;
    // The instance a call is being made for, so the source host knows which
    // clip of its own layer is meant.
    mutable Ref m_context;
    QSet<QString> m_restored;
    // The 2D instances already told they were put on their layer.
    QSet<QString> m_parented;
    // The data each instance was last restored from or saved, by key.
    QHash<QString, size_t> m_baked;
    std::map<QString, std::unique_ptr<media::VideoDecoder>> m_decoders;
};

} // namespace ui
} // namespace openvegas
