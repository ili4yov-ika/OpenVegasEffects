#include "media/MediaManager.h"

#include <QDir>
#include <QFileInfo>
#include <QMutexLocker>
#include <QStringList>

#include "core/Log.h"
#include "media/VideoProbe.h"
#include "media/VlcBackend.h"
#include "model3d/ModelLoader.h"

namespace openvegas {
namespace media {

MediaAsset* MediaManager::assetByIdForEdit(const core::Identifier& id)
{
    for (auto& asset : m_assets) if (asset.id() == id) return &asset;
    return nullptr;
}

namespace {
const QStringList kVideoExtensions = {QStringLiteral("mp4"), QStringLiteral("mov"), QStringLiteral("avi"),
                                      QStringLiteral("mkv"), QStringLiteral("mxf"), QStringLiteral("webm")};
const QStringList kImageExtensions = {QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
                                      QStringLiteral("bmp"), QStringLiteral("tga"), QStringLiteral("exr"),
                                      QStringLiteral("dpx")};
const QStringList kAudioExtensions = {QStringLiteral("wav"), QStringLiteral("mp3"), QStringLiteral("aac"),
                                      QStringLiteral("wma")};
} // namespace

MediaManager::MediaManager()
{
    m_supportedExtensions =
        kVideoExtensions + kImageExtensions + kAudioExtensions + model3d::modelExtensions();
}

MediaAsset MediaManager::assetById(const core::Identifier& id) const
{
    for (const MediaAsset& asset : m_assets) {
        if (asset.id() == id) {
            return asset;
        }
    }
    return MediaAsset{};
}

MediaAsset MediaManager::assetByFilePath(const QString& filePath) const
{
    const QString norm = QDir::cleanPath(filePath);
    for (const MediaAsset& asset : m_assets) {
        if (QDir::cleanPath(asset.filePath()) == norm) {
            return asset;
        }
    }
    return MediaAsset{};
}

MediaAsset* MediaManager::assetByFilePathForEdit(const QString& filePath)
{
    const QString norm = QDir::cleanPath(filePath);
    for (MediaAsset& asset : m_assets) {
        if (QDir::cleanPath(asset.filePath()) == norm) {
            return &asset;
        }
    }
    return nullptr;
}

MediaKind MediaManager::kindForPath(const QString& filePath)
{
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    if (kVideoExtensions.contains(suffix)) {
        return MediaKind::Video;
    }
    if (kImageExtensions.contains(suffix)) {
        return MediaKind::Image;
    }
    if (kAudioExtensions.contains(suffix)) {
        return MediaKind::Audio;
    }
    return MediaKind::Video;
}

void MediaManager::registerMissingFile(const QString& filePath)
{
    if (!filePath.isEmpty() && !assetByFilePath(filePath).isValid()) {
        MediaAsset asset;
        asset.setFilePath(filePath);
        m_assets.push_back(asset);
    }
}

core::Result MediaManager::importFile(const QString& filePath)
{
    const QFileInfo info(filePath);
    if (!info.exists()) {
        return core::Result::fail(core::ResultStatus::MissingResource,
                                  QStringLiteral("File not found: %1").arg(filePath));
    }

    MediaAsset asset;
    asset.setFilePath(info.absoluteFilePath());

    // Video carries its length and resolution inside the file. Without reading
    // them the timeline fell back to a fixed three seconds for every clip, so
    // an imported video was cut short or padded regardless of what it is.
    if (asset.kind() == MediaKind::Video) {
        const VideoInfo probed = probeVideo(asset.filePath());
        if (probed.valid) {
            asset.setDurationSeconds(probed.durationSeconds);
            if (probed.frameSize.isValid() && !probed.frameSize.isEmpty()) {
                asset.setFrameSize(probed.frameSize);
            }
        } else {
            OV_LOG_WARN(QStringLiteral("Could not read video properties: %1").arg(asset.filePath()));
        }
    }

    if (asset.kind() == MediaKind::Audio) {
        const double duration = vlcMediaDurationSeconds(asset.filePath());
        if (duration > 0) asset.setDurationSeconds(duration);
    }

    for (const MediaAsset& existing : m_assets) {
        if (existing.filePath() == asset.filePath()) {
            return core::Result::ok();
        }
    }

    m_assets.push_back(asset);
    return core::Result::ok();
}


core::Result MediaManager::importModel(const QString& filePath, const model3d::ImportSettings& settings)
{
    const QFileInfo info(filePath);
    if (!info.exists()) {
        return core::Result::fail(core::ResultStatus::MissingResource,
                                  QStringLiteral("File not found: %1").arg(filePath));
    }

    const model3d::LoadResult loaded = model3d::loadModel(info.absoluteFilePath(), settings);
    if (!loaded.ok) {
        // The reference reports a failed model import as "The 3D model could
        // not be imported." and says no more; the loader's own reason is more
        // use than that, so it is passed through.
        return core::Result::fail(core::ResultStatus::InvalidArgument, loaded.message);
    }
    if (!loaded.message.isEmpty()) {
        OV_LOG_WARN(QStringLiteral("%1: %2").arg(info.fileName(), loaded.message));
    }

    MediaAsset asset;
    asset.setFilePath(info.absoluteFilePath());

    {
        QMutexLocker lock(&m_videoMutex);
        m_models.insert(asset.id().value(), loaded.mesh);
        m_modelSettings.insert(asset.id().value(), settings);
    }

    // A re-import of the same file replaces the geometry above and keeps the
    // single asset, which is what makes the settings dialog's Update Preview
    // land on the layers already using it.
    for (const MediaAsset& existing : m_assets) {
        if (existing.filePath() == asset.filePath()) {
            return core::Result::ok();
        }
    }
    m_assets.push_back(asset);
    return core::Result::ok();
}

model3d::Mesh MediaManager::modelMesh(const core::Identifier& id) const
{
    QMutexLocker lock(&m_videoMutex);
    return m_models.value(id.value());
}

bool MediaManager::hasModel(const core::Identifier& id) const
{
    QMutexLocker lock(&m_videoMutex);
    return m_models.contains(id.value());
}

model3d::ImportSettings MediaManager::modelSettings(const core::Identifier& id) const
{
    QMutexLocker lock(&m_videoMutex);
    return m_modelSettings.value(id.value());
}

core::Result MediaManager::importFiles(const QStringList& paths)
{
    for (const QString& path : paths) {
        const core::Result r = importFile(path);
        if (r.isFailure()) {
            return r;
        }
    }
    return core::Result::ok();
}

void MediaManager::removeAsset(const core::Identifier& id)
{
    for (int i = m_assets.size() - 1; i >= 0; --i) {
        if (m_assets[i].id() == id) {
            m_assets.removeAt(i);
            return;
        }
    }
}

void MediaManager::clear()
{
    m_assets.clear();
    {
        QMutexLocker lock(&m_videoMutex);
        m_models.clear();
        m_modelSettings.clear();
    }
    // The frame cache and the pending requests are keyed by asset id, so they
    // would otherwise outlive the assets they belong to.
    clearVideoFrames();
}

void MediaManager::replaceProjectAssets(MediaManager&& staged)
{
    if (&staged == this) return;
    QMutexLocker lock(&m_videoMutex);
    m_assets = std::move(staged.m_assets);
    m_models = std::move(staged.m_models);
    m_modelSettings = std::move(staged.m_modelSettings);
    m_videoFrames.clear(); m_videoFrameOrder.clear(); m_videoFrameBytes = 0;
    m_lastVideoFrames.clear(); m_videoRequests.clear();
}

// Frames are keyed by asset and source frame, so a scrub back and forth
// reuses what has already been decoded.
QString MediaManager::frameKey(const core::Identifier& id, int sourceFrame)
{
    return id.value() + QLatin1Char(0x23) + QString::number(sourceFrame);
}

QImage MediaManager::videoFrame(const core::Identifier& id, int sourceFrame) const
{
    const QString key = frameKey(id, sourceFrame);
    QMutexLocker lock(&m_videoMutex);
    const auto it = m_videoFrames.constFind(key);
    if (it != m_videoFrames.constEnd()) {
        return it.value();
    }
    // Miss: leave a request for the GUI thread rather than decoding here.
    const QPair<core::Identifier, int> request(id, sourceFrame);
    if (!m_videoRequests.contains(request)) {
        m_videoRequests.append(request);
    }
    // Dragging the playhead asks for frames far faster than they can be
    // decoded, so the queue is capped and the oldest entries - the positions
    // the playhead has already left - are the ones dropped.
    const int kMaxPendingRequests = 24;
    while (m_videoRequests.size() > kMaxPendingRequests) {
        m_videoRequests.removeFirst();
    }
    return QImage();
}

void MediaManager::putVideoFrame(const core::Identifier& id, int sourceFrame,
                                 const QImage& frame)
{
    // A null frame is still recorded: it marks the decode as attempted so
    // an unreadable file is not requested again on every render.
    const QString key = frameKey(id, sourceFrame);
    QMutexLocker lock(&m_videoMutex);
    const auto existing = m_videoFrames.constFind(key);
    if (existing == m_videoFrames.constEnd()) {
        m_videoFrameOrder.append(key);
    } else {
        m_videoFrameBytes -= existing.value().sizeInBytes();
    }
    m_videoFrames.insert(key, frame);
    m_videoFrameBytes += frame.sizeInBytes();
    if (!frame.isNull()) {
        m_lastVideoFrames.insert(id.value(), frame);
    }

    // Bounded by bytes, not by a frame count: at four bytes a pixel a 4K frame
    // is 34 MB against a 1080p frame's 8, so a fixed count that is comfortable
    // for one is several gigabytes for the other. The oldest go first.
    const qint64 kMaxCacheBytes = 256LL * 1024 * 1024;
    while (m_videoFrameBytes > kMaxCacheBytes && m_videoFrameOrder.size() > 1) {
        const QString oldest = m_videoFrameOrder.takeFirst();
        m_videoFrameBytes -= m_videoFrames.take(oldest).sizeInBytes();
    }
}

QImage MediaManager::lastVideoFrame(const core::Identifier& id) const
{
    QMutexLocker lock(&m_videoMutex);
    return m_lastVideoFrames.value(id.value());
}

bool MediaManager::takeVideoRequest(core::Identifier* id, int* sourceFrame)
{
    QMutexLocker lock(&m_videoMutex);
    if (m_videoRequests.isEmpty()) {
        return false;
    }
    // Newest first: the last request is the one the current playhead needs,
    // and anything older is a position that has already been scrubbed past.
    const QPair<core::Identifier, int> request = m_videoRequests.takeLast();
    if (id) {
        *id = request.first;
    }
    if (sourceFrame) {
        *sourceFrame = request.second;
    }
    return true;
}

void MediaManager::clearVideoFrames()
{
    QMutexLocker lock(&m_videoMutex);
    m_videoFrames.clear();
    m_videoFrameOrder.clear();
    m_videoFrameBytes = 0;
    m_lastVideoFrames.clear();
    m_videoRequests.clear();
}

} // namespace media
} // namespace openvegas
