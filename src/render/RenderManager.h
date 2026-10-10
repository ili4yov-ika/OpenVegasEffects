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
#include "render/FrameDiskCache.h"

namespace openvegas {
namespace render {

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

class RenderWorker : public QObject
{
    Q_OBJECT

public:
    explicit RenderWorker(QObject* parent = nullptr);
    ~RenderWorker() override;

    // Must be called before the worker is moved to its thread.
    void setComposition(std::shared_ptr<composition::Composition> comp);
    void setMediaManager(std::shared_ptr<media::MediaManager> media);

public slots:
    void renderFrame(const FrameRequest& request, QByteArray mediaBytes);

signals:
    void frameReady(int frameIndex, QByteArray rgba, QSize size, QString cacheKey);
    void failed(int frameIndex, QString message);

private:
    QImage renderFrameImage(double timeSeconds, const QSize& size, bool withEffects,
                            bool transparentBackground = false) const;
    // The shot's frame at `scale` of its square size (Half 0.5, Quarter
    // 0.25): drawn at that size when everything in it can be (frameScalesDown),
    // else at full size and reduced.
    QImage renderShotImage(double timeSeconds, double scale, bool withEffects,
                           bool transparentBackground) const;
    // Whether a reduced preview may draw the whole shot reduced: no native
    // module on a grade layer that cannot be. The 3D scene is drawn reduced
    // too (its camera is the full shot's, rasterised into the smaller frame),
    // and so is motion blur, with the samples its smear needs at that size.
    bool frameScalesDown() const;
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
    // A 3D picture layer's sheet in the scene (layer transform, Y up).
    static QMatrix4x4 layerPlaneMatrix(const composition::LayerTransform& transform, int frame);
    // Whether the layer is part of the 3D scene: a 3D media, plane or text
    // layer, or a model.
    static bool inScene(const composition::Layer& layer);
    // Distance from the camera of each drawn pixel of `image` (infinity where
    // it is clear): where the pixel's ray meets the layer's sheet, or for a
    // model or Geometry text, where the layer stands.
    QVector<float> sceneDistances(const composition::Layer& layer, const QImage& image, int frame) const;
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
