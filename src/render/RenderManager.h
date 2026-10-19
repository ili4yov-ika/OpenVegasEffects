#pragma once

#include <QByteArray>
#include <QCache>
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QSize>
#include <QThread>
#include <QVector>

#include <memory>

#include "composition/Composition.h"
#include "composition/TextStyle.h"
#include "core/Identifier.h"
#include "media/MediaAsset.h"
#include "media/MediaManager.h"
#include <optional>

#include "model3d/Renderer3D.h"
#include "plugin/NativeEffectRender.h"
#include "render/FrameDiskCache.h"

namespace openvegas {
namespace render {

// GetAssetInfo (tagBiffAssetInfo) of a clip's footage, as the native modules
// that analyse footage ask for it: a video's frames at the shot's rate, by
// time in the source, or a sequence's own frames and rate; the source frame
// the clip starts on; type 0. A nested shot is type 3: its frames at the
// shot's rate. Invalid for anything else.
plugin::NativeAssetInfo nativeFootageInfo(const composition::Clip& clip,
                                          const media::MediaManager* media, double shotRate);
// AbstractLayer::WorldTransformationAtTime of a layer without its Behaviors,
// as the native modules get it (tagBiffBehaviorMC, GetLayerInfoV2 +0x74):
// column-major, composition units from the frame's centre, Y up - its
// parent's, then its position, its turn about the anchor point and its scale.
std::array<float, 16> nativeLayerWorld(const composition::Composition* composition,
                                       const composition::Layer& layer, int frame);
// The instance a native effect lives on: "<layer ID>/<clip>/<effect>".
QString nativeInstanceKey(const core::Identifier& layerId, int clip, int effect);

enum class RenderState
{
    Idle = 0,
    Rendering,
    Paused,
    Done,
    Failed,
};

struct FrameRequest
{
    int frameIndex = 0;
    double timeSeconds = 0.0;
    QSize size;
    core::Identifier cacheKey;
    // Set from the viewer's quality button. The reference's profiles are lists
    // of switches (profile-2d-effects-rendering, profile-shadows, ...); of them
    // this renderer has only the first, so a profile reaches it as this one
    // flag. False means the layer effects are skipped, which is what the
    // reference reports as "Effects disabled by quality profile."
    bool renderEffects = true;
    // Nested shots are rendered over transparency; so are their pre-renders.
    bool transparentBackground = false;
    // Options/PreRenderDirectoryPath/<project>: nested shots with a current
    // pre-render are read from <this>/<composition id> instead of rendered.
    QString preRenderDirectory;
    std::shared_ptr<composition::Composition> compositionSnapshot;
    // A Viewer view with a camera of its own (Perspective, Front, ...): the
    // shot's 3D scene alone seen through it, with no fog, and the floor grid
    // under it when asked for (Options ShowFloorPlane).
    std::optional<model3d::Camera> viewCamera;
    bool floorPlane = false;
};

// Cache key of a rendered frame: time, size and options plus everything the
// picture depends on - the layers of the shot and of every nested shot, and
// the size/mtime of the media files. Shared by the timeline cache and the
// pre-renders, so a changed shot simply stops matching its old frames.
QString renderStateKey(const composition::Composition& composition,
                       const media::MediaManager* media, double timeSeconds, const QSize& size,
                       bool renderEffects, bool transparentBackground,
                       const QString& variant = QString());

// Pre-render folder of one composite shot below the project's pre-render root.
QString preRenderFolder(const QString& projectPreRenderDirectory,
                        const composition::Composition& composition);
// The audio of a shot's pre-render (the reference's pre-render has video and
// audio: "Could not delete asset pre-render audio", "The pre-render has no
// audio"): the shot's mix as a WAV in its folder, named by the state it was
// made for, so an edited shot stops matching it as its frames do.
// A shot's frame at `seconds`, at its display size over transparency, as a
// nested shot is drawn (its pre-render when it matches): what a module gets
// as footage when it tracks a composite shot (tagBiffAssetInfo type 3).
QImage renderCompositionFrame(const std::shared_ptr<composition::Composition>& shot,
                              const std::shared_ptr<media::MediaManager>& media, double seconds,
                              const QString& preRenderDirectory = QString());

QString preRenderAudioPath(const QString& projectPreRenderDirectory,
                           const composition::Composition& composition, const media::MediaManager* media);
// That file when it is there for the shot as it is now; empty otherwise.
QString preRenderedAudio(const QString& projectPreRenderDirectory,
                         const composition::Composition& composition, const media::MediaManager* media);

class RenderWorker : public QObject
{
    Q_OBJECT

public:
    explicit RenderWorker(QObject* parent = nullptr);
    ~RenderWorker() override;

    // Must be called before the worker is moved to its thread.
    void setComposition(std::shared_ptr<composition::Composition> comp);
    void setMediaManager(std::shared_ptr<media::MediaManager> media);
    // renderCompositionFrame, on the calling thread.
    static QImage renderComposition(const std::shared_ptr<composition::Composition>& shot,
                                    const std::shared_ptr<media::MediaManager>& media, double seconds,
                                    const QString& preRenderDirectory);

public slots:
    void renderFrame(const FrameRequest& request, QByteArray mediaBytes);

signals:
    void frameReady(int frameIndex, QByteArray rgba, QSize size, QString cacheKey);
    void failed(int frameIndex, QString message);

private:
    QImage renderFrameImage(double timeSeconds, const QSize& size, bool withEffects,
                            bool transparentBackground = false) const;
    // The shot's frame at `scale` of its square size (Half 0.5, Quarter
    // 0.25), drawn at that size: native 2D effects on the reduced canvas, the
    // 3D scene through the full shot's camera rasterised into the smaller
    // frame, motion blur with the samples its smear needs at that size.
    QImage renderShotImage(double timeSeconds, double scale, bool withEffects,
                           bool transparentBackground) const;
    // The shot's full square size while a reduced frame of `canvasSize` is
    // drawn, else `canvasSize`: what the camera is set up for and what text
    // geometry is laid out in.
    QSize sceneCanvas(const QSize& canvasSize) const;
    // Applies the clip's enabled effects to an already-rendered clip image.
    void applyClipEffects(QImage& image, const composition::Clip& clip, int frame) const;
    // Runs native Behavior callbacks and applies their 2D matrix to the
    // already-rendered clip. Returns the accumulated opacity multiplier.
    double applyClipBehaviors(QImage& image, const composition::Layer& layer,
                              const composition::Clip& clip,
                              int frame, int localFrame,
                              const QSize& canvasSize) const;
    // What applyClipBehaviors puts on a picture of `imageSize` at `frame`
    // (between frames too): the move about the picture's centre, and the
    // opacity multiplier. A 3D sheet's move is in its sheet matrix: none.
    QTransform clipBehaviorTransform(const composition::Layer& layer, const composition::Clip& clip,
                                     double frame, const QSize& imageSize, const QSize& canvasSize,
                                     double* opacity) const;
    void applyLayerMasks(QImage& image, const composition::Layer& layer) const;
    // A nested shot's frame from its pre-render, or null when there is none
    // for the shot's current state.
    QImage loadPreRenderedFrame(const composition::Composition& shot, double localSeconds,
                                const QSize& size) const;
    // A clip's finished picture at a composition time: source frame, effects,
    // behaviors (their opacity folded into alpha) and layer masks. Also used
    // outside the clip's own span, where a transition plays into its handles.
    // Null for clips without picture (audio).
    QImage renderLayerClipImage(const composition::Layer& layer, const composition::Clip& clip,
                                double timeSeconds, const QSize& size, bool withEffects) const;
    // Takes the layer, not just its transform: a Plane needs its fill colour
    // and the kind decides what is drawn at all.
    // In a reduced preview the content is laid out in `space` (the shot's
    // full square size when not given) and drawn scaled into `canvasSize`.
    QImage renderClip(const composition::Layer& layer, const composition::Clip& clip,
                      double localTimeSeconds, const QSize& canvasSize, int frame,
                      const QSize& space = QSize()) const;
    // Rasterises a 3D model layer over the whole canvas, transparent where the
    // model is not. Null when the layer has no geometry to draw.
    // A layer's own picture at `frame` - its content and effects without its
    // transform, at its own size - as a material's map takes it (Use Layer).
    // A media asset's picture at the shot's `frame`, at its own size
    // (environment type 1003).
    QImage assetTexture(const core::Identifier& assetId, int frame) const;
    // The environment a surface reflects (environmentMapType): the shot
    // beneath (`composite`), a layer's picture or an asset's.
    QImage environmentOf(const composition::SurfaceShading& surface, const QImage& composite,
                         const core::Identifier& forLayer, int frame) const;
    QImage layerTexture(const core::Identifier& layerId, const core::Identifier& forLayer,
                        int frame) const;
    QImage renderModelLayer(const composition::Layer& layer, const QSize& canvasSize,
                            int frame) const;
    // Text with Geometry modules (Extrude, Bevel, Bend, Rotate Geometry):
    // glyph outlines go through the modules in order, are filled and drawn
    // as a lit mesh over the whole canvas.
    QImage renderTextGeometry(const composition::Layer& layer, const composition::Clip& clip,
                              const composition::TextStyle& style,
                              const QVector<const composition::Effect*>& effects,
                              const QSize& canvasSize, int frame) const;
    // That mesh, in the text's own space, and where a 3D text layer puts it
    // in the scene (its transform with its Alignment).
    model3d::Mesh textGeometryMesh(const composition::Layer& layer, const composition::Clip& clip,
                                   const composition::TextStyle& style,
                                   const QVector<const composition::Effect*>& effects,
                                   const QSize& canvasSize, int frame) const;
    QMatrix4x4 textGeometryPlacement(const composition::Layer& layer, int frame) const;
    // The style a text clip draws at `frame` and its enabled Geometry
    // modules; false for a clip that is not text or runs none.
    // Whether `layer` is drawn at `frame` as text with Geometry effects.
    bool layerHasTextGeometry(const composition::Layer& layer, int frame) const;
    static bool textGeometryOf(const composition::Clip& clip, int frame, composition::TextStyle* style,
                               QVector<const composition::Effect*>* effects);
    // Camera the 3D layers are seen through: the composition's first visible
    // camera layer, or the default one the reference gives a composite shot
    // that has none.
    model3d::Camera sceneCamera(const QSize& canvasSize, int frame) const;
    // The lights a 3D layer is drawn with (render::sceneLightsOf) and its
    // Material. A shot without a light layer draws its 3D layers unlit, as
    // the reference does.
    model3d::Lighting sceneLighting(const composition::Layer& layer, int frame) const;
    // The shot's fog (RenderSettings), for 3D layers only.
    model3d::Fog sceneFog() const;
    // The depth of field of the shot's camera (its HasDoF, Focus Distance,
    // Aperture and Blur), for its 3D scene only.
    model3d::DepthOfField sceneDepthOfField(int frame) const;
    // The project's 3D antialiasing mode (1..9), which nested shots take from
    // the shot they are in, and the samples per pixel the 3D scene is drawn
    // with for it (model3d::antialiasingSamples).
    int antialiasingMode() const;
    int sceneSamples() const;
    // Whether the layer is part of the 3D scene: a 3D media, plane or text
    // layer, or a model.
    static bool inScene(const composition::Layer& layer);
    // Distance from the camera of each drawn pixel of `image` (infinity where
    // it is clear): where the pixel's ray meets the layer's sheet, or for a
    // model or Geometry text, where the layer stands.
    QVector<float> sceneDistances(const composition::Layer& layer, const QImage& image, int frame) const;
    // What `image` of a scene layer shows at each pixel (the G-buffer of
    // its ambient occlusion): for a sheet, where the pixel's ray meets it,
    // its normal turned to the eye; for a model or Geometry text, what its
    // mesh was drawn with (m_meshSurfaces). Only where the picture covers
    // half the pixel or more: the reference's G-buffer has no smoothed
    // edges, and what an edge pixel mostly shows is what lies behind it.
    model3d::SurfaceBuffer sceneSurfaces(const composition::Layer& layer, const QImage& image, int frame,
                                         int leastAlpha = 128) const;
    // The scene reflected in a sheet's plane (through `origin`, `normal`
    // to the camera's side), as Flux draws a receiving sheet's planar
    // reflection map (DynamicReflectionsPlanarMap; its render 0x18043e030):
    // the `images` of the layers casting reflections, drawn through the
    // camera mirrored in the plane (m_reflectionCamera), without fog, what lies
    // behind the plane cut away (PlanarClippingFragColor), near over far;
    // then turned left to right - Flux reads the map at (1 - x / width,
    // y / height) - so it lines up with the shot. Straight RGBA.
    QImage mirroredScene(const QVector<const composition::Layer*>& casters, const QVector<QImage>& images,
                         const QVector3D& origin, const QVector3D& normal, int frame) const;
    // The `images` of scene layers drawn through the reflection camera,
    // near over far by where each pixel meets them, those on the far side
    // of the plane through `origin` facing `normal` cut away when `clipped`.
    // Straight RGBA, as seen.
    QImage reflectionComposite(const QVector<const composition::Layer*>& layers, const QVector<QImage>& images,
                               int frame, bool clipped, const QVector3D& origin, const QVector3D& normal) const;
    // A model's surroundings as a lat-long picture `height` texels high and
    // twice as wide (model3d::latLongOf), from the six `faces` of a cube
    // round `centre`, each seen through its camera (`cameras`): each
    // direction read off the face it looks through most squarely.
    static QImage latLongFromCube(const QVector<QImage>& faces, const QVector<model3d::Camera>& cameras,
                                  const QVector3D& centre, int height);
    // The layer's model posed at `frame` (its animations, then its nodes'
    // transforms), its materials as the asset has them.
    model3d::Mesh posedModel(const composition::Layer& layer, int frame) const;
    // PlanarReflectionFragColor on a sheet's picture: the mirror's colour
    // times its alpha and `amount` (Specular Reflectivity / 100) - twice, as
    // the shader scales the whole texel and then its colour by its alpha -
    // added where the sheet is.
    static void addPlanarReflection(QImage& image, const QImage& mirror, double amount);
    // EnvironmentMapFragColor on a 3D text sheet's picture: the lat-long
    // `environment` along each pixel's view reflected in the sheet, times
    // its alpha and Specular Reflectivity, and the environment's irradiance
    // on the sheet's normal (model3d::irradianceOf) times its colour, alpha
    // and Diffuse Reflectivity, added.
    void addEnvironment(QImage& image, const composition::Layer& layer, const QImage& environment, int frame) const;
    // The scene's ambient occlusion, as Flux draws it (Flux.dll
    // 0x1804668f0): each layer that receives it darkened by the occlusion
    // (model3d::ambientOcclusionMap, with its own settings) of what the
    // pixels show of it and of the layers that cast it, the nearest at each
    // pixel - a sheet wholly, a model where its surface is opaque.
    void occludeScene(const QVector<const composition::Layer*>& layers, QVector<QImage>& images, int frame) const;
    // A 3D layer that is a picture on a plane (media, plane, nested shot,
    // text): its content rendered flat and unmoved, then carried into the
    // scene by the layer transform and seen through the shot's camera, with
    // fog by each pixel's distance from it.
    QImage renderClipIn3D(const composition::Layer& layer, const composition::Clip& clip,
                          double localTimeSeconds, const QSize& canvasSize, int frame) const;
    // That sheet: its content drawn flat (`content`), its size in the
    // scene's units and where the layer transform puts it.
    struct SheetPicture
    {
        QImage content;
        QSizeF sheet;
        QMatrix4x4 model;
    };
    SheetPicture sheetPicture(const composition::Layer& layer, const composition::Clip& clip,
                              double localTimeSeconds, const QSize& canvasSize, int frame) const;
    // The shadow maps of the frame's lights (render::sceneLightsOf order):
    // the visible 3D layers whose Material casts shadows - a sheet by its
    // picture's opaque part, a model by its triangles - seen from each light
    // that casts them, at the project's Shadow Map Size.
    QVector<std::shared_ptr<const model3d::ShadowMap>> sceneShadowMaps(double timeSeconds,
                                                                       const QSize& canvasSize) const;
    // Layer transform as a 4x4: anchor point, scale, orientation, the three
    // rotations and the position, in the order the reference applies them.
    static QMatrix4x4 layerModelMatrix(const composition::LayerTransform& transform, int frame);

    QColor clipColor(const core::Identifier& mediaId) const;
    QImage loadImageMedia(const core::Identifier& mediaId) const;
    // Cached video frame for a time inside the clip; null when not decoded yet.
    QImage loadVideoFrame(const core::Identifier& mediaId, double localSeconds) const;
    // False for audio, which has no picture to composite.
    bool contributesVideo(const core::Identifier& mediaId) const;
    static const composition::Effect* textEffectOf(const composition::Clip& clip);
    int frameForTime(double timeSeconds) const;
    QString clipLabel(const core::Identifier& mediaId) const;

    std::shared_ptr<composition::Composition> m_composition;
    std::shared_ptr<media::MediaManager> m_mediaManager;
    mutable QHash<QString, QImage> m_imageCache;
    QString m_preRenderDirectory;
    // Image-sequence stills by file; bounded, unlike single images (cost KiB).
    mutable QCache<QString, QImage> m_sequenceFrames{128 * 1024};
    // Model textures reduced to the project's Model Texture Max Size, by
    // picture and size (cost in KiB).
    mutable QCache<QString, QImage> m_limitedTextures{64 * 1024};
    mutable bool m_frameComplete = true;
    // The shadow maps of the frame being drawn (sceneShadowMaps).
    mutable QVector<std::shared_ptr<const model3d::ShadowMap>> m_shadowMaps;
    // While a scene layer is drawn for a frame with ambient occlusion: what
    // its meshes show at each pixel, by layer.
    mutable bool m_collectSurfaces = false;
    mutable QHash<QString, model3d::SurfaceBuffer> m_meshSurfaces;
    // While a reflection map is drawn: the camera mirrored in the
    // receiving sheet's plane, or a face of a model's cube, which the scene
    // is seen through, fogless.
    mutable std::optional<model3d::Camera> m_reflectionCamera;
    // What the models drawn this frame reflect (renderModelLayer), by layer.
    mutable QHash<QString, std::shared_ptr<const model3d::Reflections>> m_modelReflections;
    // The scale the frame being drawn is at (1 at full size): layers are
    // placed in the shot's full square units and drawn through it.
    mutable double m_renderScale = 1.0;
    // Set on the worker of a nested shot: the antialiasing of the project it
    // is drawn in. 0 takes the composition's own.
    int m_antialiasingMode = 0;
    // The frame's view camera and floor (FrameRequest).
    std::optional<model3d::Camera> m_viewCamera;
    bool m_floorPlane = false;
    // The floor grid of a view, on the y = 0 plane.
    void paintFloorGrid(QImage& image) const;
};

class RenderManager : public QObject
{
    Q_OBJECT

public:
    explicit RenderManager(QObject* parent = nullptr);
    ~RenderManager() override;

    RenderState state() const { return m_state; }

    void setComposition(std::shared_ptr<composition::Composition> comp) { m_composition = std::move(comp); }
    void setMediaManager(std::shared_ptr<media::MediaManager> media) { m_mediaManager = std::move(media); }

    // `renderEffects` comes from the viewer's quality profile; the size is the
    // preview resolution, which is the project's only at Full.
    void requestFrame(int frameIndex, double timeSeconds, const QSize& size,
                      bool renderEffects = true, bool transparentBackground = false);
    // Where pre-rendered nested shots are looked up (empty: never).
    void setPreRenderDirectory(const QString& directory) { m_preRenderDirectory = directory; }
    // Frames for a Viewer view with its own camera (FrameRequest::viewCamera);
    // none renders the shot as it is.
    void setViewCamera(const std::optional<model3d::Camera>& camera, bool floorPlane = false)
    {
        m_viewCamera = camera;
        m_floorPlane = floorPlane;
    }
    void cancelAll();
    void startPlaybackCache(double fromSeconds, const QSize& size, bool renderEffects = true,
                            bool transparentBackground = false);
    void cancelPlaybackCache();
    bool isCaching() const { return m_caching; }
    int cachedFrameCount() const { return m_frameCache.size(); }
    // Options/TimelineCache: frames rendered by the playback cache are also
    // written here and read back before rendering. Empty disables it.
    void setDiskCacheDirectory(const QString& directory) { m_diskCache.setDirectory(directory); }
    // Which decoded media the frames are built from ("proxy" while previewing
    // through proxies); part of the cache key so export never reuses them.
    void setMediaVariant(const QString& variant) { m_mediaVariant = variant; }
    const FrameDiskCache& diskCache() const { return m_diskCache; }
    int diskCacheHits() const { return m_diskCacheHits; }
    // Whether the frame frameReady is delivering had every picture it needed:
    // false when a video frame was not decoded yet or a nested shot missed one.
    bool lastFrameComplete() const { return m_lastFrameComplete; }

signals:
    void stateChanged(render::RenderState state);
    // The size travels with the pixels. It used to be assumed to be the
    // composition's, which stopped being true the moment the preview could be
    // rendered at half or quarter resolution - and reading an RGBA buffer at
    // the wrong width does not fail, it just shears the picture.
    void frameReady(int frameIndex, QByteArray rgba, QSize size);
    void playbackCacheProgress(int completed, int total);
    void playbackCacheFinished();
    void playbackCacheFailed(QString reason);

private:
    void spawnWorker();
    void onFrameReady(int frameIndex, QByteArray rgba, QSize size, QString cacheKey);
    void requestNextCacheFrame();
    // Cache key of a frame: everything the picture depends on, including the
    // contents of nested shots and the size/mtime of the media files.
    QString frameCacheKey(double timeSeconds, const QSize& size, bool renderEffects,
                          bool transparentBackground = false) const;
    QCache<QString, QByteArray> m_frameCache{256 * 1024}; // cost in KiB
    FrameDiskCache m_diskCache;
    int m_diskCacheHits = 0;
    QString m_mediaVariant;
    bool m_caching = false;
    int m_cacheFrame = 0, m_cacheFirst = 0, m_cacheLast = 0;
    int m_cacheRequest = -1;
    QSize m_cacheSize;
    bool m_cacheEffects = true;
    bool m_cacheTransparent = false;
    QString m_preRenderDirectory;
    std::optional<model3d::Camera> m_viewCamera;
    bool m_floorPlane = false;
    QElapsedTimer m_cacheWait;
    bool m_lastFrameComplete = true;

    std::shared_ptr<composition::Composition> m_composition;
    std::shared_ptr<media::MediaManager> m_mediaManager;
    RenderState m_state = RenderState::Idle;
    QThread* m_workerThread = nullptr;
    RenderWorker* m_worker = nullptr;
};

} // namespace render
} // namespace openvegas

Q_DECLARE_METATYPE(openvegas::render::FrameRequest)
