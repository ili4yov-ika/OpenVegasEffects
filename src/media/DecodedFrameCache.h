#pragma once

#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>
#include <QThreadPool>

#include <functional>

namespace openvegas {
namespace media {

// Where decoded frames are kept: a folder of the media cache, and hooks that
// record each file in the media cache database (MediaCacheDB) - `stored` when
// a file has been written, with its size, `used` when one is read again - so
// frames age out with DaysToKeepMediaCacheFiles and the cache's size cap.
struct DecodedFrameStore
{
    QString directory;
    std::function<void(const QString& key, const QString& file, qint64 bytes)> stored;
    std::function<void(const QString& key)> used;
};

// Decoded video frames kept on disk between sessions, in the media cache
// (Options/MediaCacheFiles/Frames). Only frames that were slow to decode go
// there - a seek far into a long GOP, a heavy codec - since reading one back
// costs a few milliseconds while keeping every frame would take tens of
// gigabytes an hour of footage. Frames are written compressed on a thread of
// their own; `stored` is called on the thread that owns the cache.
class DecodedFrameCache : public QObject
{
    Q_OBJECT

public:
    explicit DecodedFrameCache(QObject* parent = nullptr);
    ~DecodedFrameCache() override;

    void setStore(const DecodedFrameStore& store) { m_store = store; }
    const DecodedFrameStore& frameStore() const { return m_store; }
    // The store caches made from now on start with.
    static void setDefaultStore(const DecodedFrameStore& store);

    // The frame of `decodedFile` at `sourceMilliseconds`, delivered at `size`:
    // the file's path, size and modification time are in it, so a replaced
    // file (or its proxy) gets frames of its own. Empty for a missing file.
    static QString keyFor(const QString& decodedFile, int sourceMilliseconds, const QSize& size);
    // Whether a frame of `size` that took `decodeMilliseconds` is worth
    // keeping: reading it back must be clearly quicker than decoding it again.
    static bool worthKeeping(qint64 decodeMilliseconds, const QSize& size);

    // The frame kept under `key`; otherwise what `decode` gives, kept when it
    // was slow to get (worthKeeping).
    QImage fetch(const QString& key, const std::function<QImage()>& decode);
    // The frame kept under `key`, or a null image.
    QImage load(const QString& key);
    // Keeps the frame; the file is written in the background.
    void store(const QString& key, const QImage& frame);
    // Waits for the frames still being written.
    void waitForWrites();

    QString pathFor(const QString& key) const;

private:
    DecodedFrameStore m_store;
    QThreadPool m_writers;
};

} // namespace media
} // namespace openvegas
