#pragma once

#include <QHash>
#include <QImage>
#include <QList>
#include <QMutex>
#include <QPair>
#include <QStringList>
#include <QVector>

#include "core/Result.h"
#include "media/MediaAsset.h"
#include "model3d/Mesh.h"
#include "model3d/ModelImportSettings.h"

namespace openvegas {
namespace media {

class MediaManager
{
public:
    MediaManager();

    QVector<MediaAsset> assets() const { return m_assets; }

    MediaAsset assetById(const core::Identifier& id) const;
    MediaAsset assetByFilePath(const QString& filePath) const;

    // Mutable lookup used to set per-asset state (e.g. trimmer points) after
    // import. Returns nullptr when no asset matches the path.
    MediaAsset* assetByFilePathForEdit(const QString& filePath);
    MediaAsset* assetByIdForEdit(const core::Identifier& id);

    core::Result importFile(const QString& filePath);
    // Keep an offline project's asset in Media so Relink Media can recover it.
    void registerMissingFile(const QString& filePath);
    // Imports a 3D model, parsing it with `settings` and keeping the geometry
    // beside the asset. Separate from importFile because a model is read
    // through a settings dialog the reference calls Model3DImport, and
    // re-importing with different settings has to replace what is stored.
    core::Result importModel(const QString& filePath, const model3d::ImportSettings& settings);
    core::Result importFiles(const QStringList& paths);

    void removeAsset(const core::Identifier& id);
    void clear();
    // Commit a successfully parsed project; the staged manager is exclusively owned by the loader.
    void replaceProjectAssets(MediaManager&& staged);

    QStringList supportedImportExtensions() const { return m_supportedExtensions; }

    // Video frames, decoded on demand.
    //
    // The worker only ever reads this cache, and a miss leaves a request behind
    // instead of decoding; the GUI thread drains the requests, decodes, stores
    // the result and asks for another render, so the frame lands one pass
    // later - not noticeable in a preview.
    //
    // The split began as a constraint: QMediaPlayer wanted the GUI thread. With
    // libVLC it is a choice rather than a rule - a decoder is not tied to any
    // thread - but the arrangement is kept, because it is what stops a slow
    // decode from stalling the compositor.
    //
    // Frames are keyed by source frame number so scrubbing reuses what has
    // already been decoded.
    QImage videoFrame(const core::Identifier& id, int sourceFrame) const;
    void putVideoFrame(const core::Identifier& id, int sourceFrame, const QImage& frame);
    // Most recently decoded frame for this asset, at whatever position, or a
    // null image when nothing has been decoded from it yet. The render worker
    // holds this while the frame it actually asked for is still being decoded:
    // playback outruns the decoder, and dropping to the placeholder on every
    // frame it misses is what put coloured bars over the footage.
    QImage lastVideoFrame(const core::Identifier& id) const;
    // Newest outstanding request, or false when there is nothing to decode.
    bool takeVideoRequest(core::Identifier* id, int* sourceFrame);
    void clearVideoFrames();

    // Geometry of an imported model, empty when the id is not a model asset.
    // Guarded like the frame cache: the render worker reads it from its own
    // thread while the GUI thread imports.
    model3d::Mesh modelMesh(const core::Identifier& id) const;
    bool hasModel(const core::Identifier& id) const;
    // Settings the model was last imported with, so the dialog reopens on them.
    model3d::ImportSettings modelSettings(const core::Identifier& id) const;

private:
    static MediaKind kindForPath(const QString& filePath);
    static QString frameKey(const core::Identifier& id, int sourceFrame);

    QVector<MediaAsset> m_assets;
    QStringList m_supportedExtensions;

    // Guards the frame cache and the request queue, which the render thread
    // and the GUI thread both touch.
    mutable QMutex m_videoMutex;
    mutable QHash<QString, QImage> m_videoFrames;
    mutable QList<QString> m_videoFrameOrder;   // insertion order, for eviction
    // Bytes held by m_videoFrames, tracked so the cache can be bounded by size
    // rather than by a frame count: one 4K frame is four times a 1080p one.
    mutable qint64 m_videoFrameBytes = 0;
    // One entry per asset: its last decoded picture, kept outside the eviction
    // budget so there is always something to hold on to.
    mutable QHash<QString, QImage> m_lastVideoFrames;
    mutable QList<QPair<core::Identifier, int>> m_videoRequests;

    // Loaded model geometry, keyed by asset id, with the settings it was read
    // with beside it. Shares m_videoMutex: both are cross-thread reads of
    // decoded media, and neither is contended enough to want its own lock.
    QHash<QString, model3d::Mesh> m_models;
    QHash<QString, model3d::ImportSettings> m_modelSettings;
};

} // namespace media
} // namespace openvegas
