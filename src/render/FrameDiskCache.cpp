#include "render/FrameDiskCache.h"

#include <QDataStream>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <atomic>

namespace openvegas {
namespace render {

namespace {

std::atomic<bool> g_debugSquares{false};

// A 200 px square (less on a smaller frame) of `colour` into RGBA rows, at
// the top left or the top right.
void drawDebugSquare(QByteArray& rgba, const QSize& size, bool right, uchar red, uchar blue)
{
    const int side = qMin(200, qMin(size.width(), size.height()));
    const int left = right ? size.width() - side : 0;
    uchar* pixels = reinterpret_cast<uchar*>(rgba.data());
    for (int y = 0; y < side; ++y) {
        uchar* row = pixels + qsizetype(y) * size.width() * 4;
        for (int x = left; x < left + side; ++x) {
            row[x * 4] = red; row[x * 4 + 1] = 0; row[x * 4 + 2] = blue; row[x * 4 + 3] = 255;
        }
    }
}

constexpr quint32 kMagic = 0x4f56464d; // "OVFM"
constexpr quint32 kVersion = 1;
const char kSuffix[] = ".ovframe";

} // namespace

QString FrameDiskCache::pathFor(const QString& key) const
{
    // Two-character fan-out keeps directories small on long timelines.
    return QDir(m_directory).filePath(key.left(2) + QLatin1Char('/') + key
                                      + QLatin1String(kSuffix));
}

bool FrameDiskCache::contains(const QString& key) const
{
    return isEnabled() && !key.isEmpty() && QFileInfo::exists(pathFor(key));
}

bool FrameDiskCache::load(const QString& key, const QSize& size, QByteArray* rgba) const
{
    if (!isEnabled() || key.isEmpty() || !rgba) return false;
    const QString path = pathFor(key);
    // Opening a missing file for writing creates it, empty - and an entry
    // that exists is never stored again, so the frame was lost for good.
    if (!QFileInfo::exists(path)) return false;
    QFile file(path);
    // Opened for writing only to mark it used; a read-only cache, or a file
    // another process holds for a moment, is still a hit, just not touched.
    const bool touch = file.open(QIODevice::ReadWrite);
    if (!touch && !file.open(QIODevice::ReadOnly)) return false;
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    quint32 magic = 0, version = 0;
    qint32 width = 0, height = 0;
    QByteArray compressed;
    stream >> magic >> version >> width >> height >> compressed;
    const auto discard = [&] {
        // A damaged entry (written whole by QSaveFile, so not one in
        // progress) goes, so the frame can be stored again.
        file.close();
        QFile::remove(path);
        return false;
    };
    if (stream.status() != QDataStream::Ok || magic != kMagic || version != kVersion) return discard();
    if (QSize(width, height) != size) return false;
    QByteArray pixels = qUncompress(compressed);
    if (pixels.size() != qsizetype(width) * height * 4) return discard();
    if (touch) file.setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime);
    if (debugSquares()) drawDebugSquare(pixels, size, true, 0, 128);
    *rgba = std::move(pixels);
    return true;
}

bool FrameDiskCache::store(const QString& key, const QSize& size, const QByteArray& rgba) const
{
    if (!isEnabled() || key.isEmpty() || rgba.size() != qsizetype(size.width()) * size.height() * 4) {
        return false;
    }
    const QString path = pathFor(key);
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    QByteArray stored = rgba;
    if (debugSquares()) drawDebugSquare(stored, size, false, 0, 255);
    stream << kMagic << kVersion << qint32(size.width()) << qint32(size.height())
           << qCompress(stored, 1);
    return stream.status() == QDataStream::Ok && file.commit();
}

void FrameDiskCache::setDebugSquares(bool on) { g_debugSquares = on; }
bool FrameDiskCache::debugSquares() { return g_debugSquares; }

int FrameDiskCache::prune(int days, const QDateTime& now) const
{
    if (!isEnabled() || days <= 0) return 0;
    const QDateTime limit = now.addDays(-days);
    int removed = 0;
    QDirIterator it(m_directory, {QStringLiteral("*") + QLatin1String(kSuffix)}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo info(it.next());
        if (info.lastModified() < limit && QFile::remove(info.absoluteFilePath())) ++removed;
    }
    return removed;
}

} // namespace render
} // namespace openvegas
