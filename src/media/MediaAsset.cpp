#include "media/MediaAsset.h"

#include <QDir>
#include <QFileInfo>
#include <QImageReader>

#include "model3d/ModelLoader.h"

namespace openvegas {
namespace media {

void MediaAsset::setFilePath(const QString& path)
{
    m_filePath = path;
    m_fileName = QFileInfo(path).fileName();

    const QString suffix = QFileInfo(path).suffix().toLower();
    const QStringList videoExts = {QStringLiteral("mp4"), QStringLiteral("mov"), QStringLiteral("avi"),
                                   QStringLiteral("mkv"), QStringLiteral("mxf"), QStringLiteral("webm")};
    const QStringList imageExts = {QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
                                   QStringLiteral("bmp"), QStringLiteral("tga"), QStringLiteral("exr"),
                                   QStringLiteral("dpx")};
    const QStringList audioExts = {QStringLiteral("wav"), QStringLiteral("mp3"), QStringLiteral("aac"),
                                   QStringLiteral("wma")};

    if (videoExts.contains(suffix)) {
        m_kind = MediaKind::Video;
    } else if (imageExts.contains(suffix)) {
        m_kind = MediaKind::Image;
        const QSize size = QImageReader(m_filePath).size();
        if (size.isValid() && !size.isEmpty()) {
            m_frameSize = size;
        }
    } else if (audioExts.contains(suffix)) {
        m_kind = MediaKind::Audio;
    } else if (model3d::modelExtensions().contains(suffix)) {
        m_kind = MediaKind::Model3D;
    } else {
        m_kind = MediaKind::Video;
    }

    m_id = core::Identifier(QStringLiteral("media:") + m_filePath);
}

} // namespace media
} // namespace openvegas