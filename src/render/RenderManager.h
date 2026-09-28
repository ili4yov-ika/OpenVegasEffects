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
#include "core/Identifier.h"
#include "media/MediaAsset.h"
#include "media/MediaManager.h"
#include "model3d/Renderer3D.h"

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
    std::shared_ptr<composition::Composition> compositionSnapshot;
};

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
    // Takes the layer, not just its transform: a Plane needs its fill colour
    // and the kind decides what is drawn at all.
    QImage renderClip(const composition::Layer& layer, const composition::Clip& clip,
                      double localTimeSeconds, const QSize& canvasSize, int frame) const;
    // Rasterises a 3D model layer over the whole canvas, transparent where the
    // model is not. Null when the layer has no geometry to draw.
    QImage renderModelLayer(const composition::Layer& layer, const QSize& canvasSize,
                            int frame) const;
    // Camera the 3D layers are seen through: the composition's first visible
    // camera layer, or the default one the reference gives a composite shot
    // that has none.
    model3d::Camera sceneCamera(const QSize& canvasSize, int frame) const;
    // Light the models are shaded by, taken from the first visible light layer
    // and falling back to the reference's "defaultLight".
    model3d::Light sceneLight(int frame) const;
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
                      bool renderEffects = true);
    void cancelAll();
    void startPlaybackCache(double fromSeconds, const QSize& size, bool renderEffects = true);
    void cancelPlaybackCache();
    bool isCaching() const { return m_caching; }
    int cachedFrameCount() const { return m_frameCache.size(); }

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
    QCache<QString, QByteArray> m_frameCache{256 * 1024}; // cost in KiB
    bool m_caching = false;
    int m_cacheFrame = 0, m_cacheFirst = 0, m_cacheLast = 0;
    int m_cacheRequest = -1;
    QSize m_cacheSize;
    bool m_cacheEffects = true;
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
