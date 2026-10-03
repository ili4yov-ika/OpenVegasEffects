#include "render/FrameDiskCache.h"

#include <QDataStream>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace openvegas {
namespace render {

namespace {

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
    QFile file(pathFor(key));
    if (!file.open(QIODevice::ReadWrite)) return false;
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    quint32 magic = 0, version = 0;
    qint32 width = 0, height = 0;
    QByteArray compressed;
    stream >> magic >> version >> width >> height >> compressed;
    if (stream.status() != QDataStream::Ok || magic != kMagic || version != kVersion
        || QSize(width, height) != size) {
        return false;
    }
    QByteArray pixels = qUncompress(compressed);
    if (pixels.size() != qsizetype(width) * height * 4) return false;
    file.setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime);
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
    stream << kMagic << kVersion << qint32(size.width()) << qint32(size.height())
           << qCompress(rgba, 1);
    return stream.status() == QDataStream::Ok && file.commit();
}

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
