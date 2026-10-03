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
    QImage renderClip(const composition::Layer& layer, const composition::Clip& clip,
                      double localTimeSeconds, const QSize& canvasSize, int frame) const;
    // Rasterises a 3D model layer over the whole canvas, transparent where the
    // model is not. Null when the layer has no geometry to draw.
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
    // Light the models are shaded by, taken from the first visible light layer
    // and falling back to the reference's "defaultLight".
    model3d::Light sceneLight(int frame) const;
    // The shot's fog (RenderSettings), for 3D layers only.
    model3d::Fog sceneFog() const;
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
    mutable bool m_frameComplete = true;
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
    QElapsedTimer m_cacheWait;

    std::shared_ptr<composition::Composition> m_composition;
    std::shared_ptr<media::MediaManager> m_mediaManager;
    RenderState m_state = RenderState::Idle;
    QThread* m_workerThread = nullptr;
    RenderWorker* m_worker = nullptr;
};

} // namespace render
} // namespace openvegas

Q_DECLARE_METATYPE(openvegas::render::FrameRequest)
