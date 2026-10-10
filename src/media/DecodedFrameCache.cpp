#include "media/DecodedFrameCache.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <cstring>

namespace openvegas {
namespace media {

namespace {

constexpr quint32 kMagic = 0x5246564f;   // "OVFR"
constexpr quint32 kVersion = 1;

DecodedFrameStore& defaultStore()
{
    static DecodedFrameStore store;
    return store;
}

} // namespace

void DecodedFrameCache::setDefaultStore(const DecodedFrameStore& store)
{
    defaultStore() = store;
}

DecodedFrameCache::DecodedFrameCache(QObject* parent)
    : QObject(parent)
    , m_store(defaultStore())
{
    // One writer: frames arrive one at a time and the disk is the limit.
    m_writers.setMaxThreadCount(1);
}

DecodedFrameCache::~DecodedFrameCache()
{
    m_writers.waitForDone();
}

QString DecodedFrameCache::keyFor(const QString& decodedFile, int sourceMilliseconds, const QSize& size)
{
    const QFileInfo info(decodedFile);
    if (!info.exists()) return {};
    const QByteArray identity = QStringLiteral("%1|%2|%3|%4ms|%5x%6")
                                    .arg(info.absoluteFilePath())
                                    .arg(info.size())
                                    .arg(info.lastModified().toMSecsSinceEpoch())
                                    .arg(sourceMilliseconds)
                                    .arg(size.width())
                                    .arg(size.height())
                                    .toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha1).toHex());
}

bool DecodedFrameCache::worthKeeping(qint64 decodeMilliseconds, const QSize& size)
{
    // Reading back is mostly inflating: about a millisecond a megabyte of
    // pixels (7 ms for 1080p), so a frame must have cost twice that, and at
    // least a frame's time at 40 fps, to be kept.
    const double inflateMs = double(size.width()) * size.height() * 4 / (1024.0 * 1024.0);
    return size.width() > 0 && size.height() > 0
        && double(decodeMilliseconds) >= qMax(25.0, 2.0 * inflateMs);
}

QString DecodedFrameCache::pathFor(const QString& key) const
{
    // Two levels of folders keep any one of them small.
    return QDir(m_store.directory).filePath(key.left(2) + QLatin1Char('/') + key + QStringLiteral(".ovframe"));
}

QImage DecodedFrameCache::fetch(const QString& key, const std::function<QImage()>& decode)
{
    QImage frame = load(key);
    if (!frame.isNull()) return frame;
    QElapsedTimer decodeTime;
    decodeTime.start();
    frame = decode();
    if (!frame.isNull() && worthKeeping(decodeTime.elapsed(), frame.size())) store(key, frame);
    return frame;
}

QImage DecodedFrameCache::load(const QString& key)
{
    if (m_store.directory.isEmpty() || key.isEmpty()) return QImage();
    QFile file(pathFor(key));
    if (!file.open(QIODevice::ReadOnly)) return QImage();
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    quint32 magic = 0, version = 0, width = 0, height = 0, format = 0;
    QByteArray packed;
    stream >> magic >> version >> width >> height >> format >> packed;
    if (stream.status() != QDataStream::Ok || magic != kMagic || version != kVersion || width == 0
        || height == 0 || width > 32768 || height > 32768
        || format != quint32(QImage::Format_RGBA8888)) {
        return QImage();
    }
    const QByteArray pixels = qUncompress(packed);
    if (pixels.size() != qsizetype(width) * qsizetype(height) * 4) return QImage();
    QImage frame(int(width), int(height), QImage::Format_RGBA8888);
    for (int y = 0; y < int(height); ++y)
        std::memcpy(frame.scanLine(y), pixels.constData() + qsizetype(y) * width * 4, size_t(width) * 4);
    if (m_store.used) m_store.used(key);
    return frame;
}

void DecodedFrameCache::store(const QString& key, const QImage& frame)
{
    if (m_store.directory.isEmpty() || key.isEmpty() || frame.isNull()) return;
    const QString path = pathFor(key);
    const QImage pixels = frame.convertToFormat(QImage::Format_RGBA8888);
    m_writers.start([this, key, path, pixels] {
        if (!QDir().mkpath(QFileInfo(path).absolutePath())) return;
        QByteArray raw;
        raw.reserve(qsizetype(pixels.width()) * pixels.height() * 4);
        for (int y = 0; y < pixels.height(); ++y)
            raw.append(reinterpret_cast<const char*>(pixels.constScanLine(y)), qsizetype(pixels.width()) * 4);
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return;
        QDataStream stream(&file);
        stream.setByteOrder(QDataStream::LittleEndian);
        // zlib's fastest level: the point is to beat the decoder, not to be small.
        stream << kMagic << kVersion << quint32(pixels.width()) << quint32(pixels.height())
               << quint32(QImage::Format_RGBA8888) << qCompress(raw, 1);
        if (stream.status() != QDataStream::Ok || !file.commit()) return;
        const qint64 bytes = QFileInfo(path).size();
        // Recorded on the owner's thread, where the database lives; the
        // destructor waits for this task, so `this` is still there.
        QMetaObject::invokeMethod(this, [this, key, path, bytes] {
            if (m_store.stored) m_store.stored(key, path, bytes);
        }, Qt::QueuedConnection);
    });
}

void DecodedFrameCache::waitForWrites()
{
    m_writers.waitForDone();
}

} // namespace media
} // namespace openvegas
