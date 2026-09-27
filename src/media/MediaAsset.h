#pragma once

#include <QSize>
#include <QString>
#include <QColor>

#include "core/Identifier.h"

namespace openvegas {
namespace media {

enum class MediaKind
{
    Video = 0,
    Image,
    Audio,
    Composition,
    // An imported 3D model. The reference keeps these as Model3DAssets in the
    // same asset list as the footage, which is why they live here rather than
    // in a store of their own.
    Model3D,
};

class MediaAsset
{
public:
    MediaAsset() = default;

    static MediaAsset fromFile(const QString& filePath);

    bool isValid() const { return m_id.isValid(); }

    core::Identifier id() const { return m_id; }
    QString filePath() const { return m_filePath; }
    QString fileName() const { return m_fileName; }
    MediaKind kind() const { return m_kind; }
    double durationSeconds() const { return m_durationSeconds; }
    QSize frameSize() const { return m_frameSize; }
    QColor labelColor() const { return m_labelColor; }
    void setLabelColor(const QColor& color) { m_labelColor = color; }

    // Trimmer in/out points, in frames. Mirrors MediaAsset::SetTrimmerInPoint /
    // TrimmerInPoint (serialized as <InPoint>/<OutPoint> in .vegfx).
    int trimInPoint() const { return m_trimInPoint; }
    int trimOutPoint() const { return m_trimOutPoint; }
    void setTrimInPoint(int frames) { m_trimInPoint = qMax(0, frames); }
    void setTrimOutPoint(int frames) { m_trimOutPoint = qMax(0, frames); }

    void setFilePath(const QString& path);

    // Filled from the file itself for video, where the extension alone says
    // nothing about length or resolution. Images read theirs in setFilePath.
    void setDurationSeconds(double seconds) { m_durationSeconds = qMax(0.0, seconds); }
    void setFrameSize(const QSize& size) { m_frameSize = size; }

private:
    core::Identifier m_id;
    QString m_filePath;
    QString m_fileName;
    MediaKind m_kind = MediaKind::Video;
    double m_durationSeconds = 0.0;
    QSize m_frameSize;
    QColor m_labelColor; // Invalid color means no label.
    int m_trimInPoint = 0;
    int m_trimOutPoint = 0;
};

} // namespace media
} // namespace openvegas
