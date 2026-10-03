#pragma once

#include <QSize>
#include <QString>
#include <QStringList>
#include <QColor>
#include <QImage>

#include "core/Identifier.h"
#include "media/MediaStreamInfo.h"

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

// Per-asset proxy, as in the reference's ProxyMediaMenu (None, Performance,
// Quality): a smaller or all-intra copy decoded for preview instead of the
// original. Export always reads the original.
enum class ProxyMode
{
    None,
    Performance,
    Quality,
};

// Numbered stills that play as one video clip, like the reference's
// "Import Sequence Videos" (MediaAsset IsImageSequence/FrameRate). The asset's
// path is the first file plus this marker, which keeps it apart from that
// file imported as a single image and lets the path-keyed project code
// carry it unchanged.
inline constexpr char kImageSequenceMarker[] = "#sequence";

// The other files of the contiguous numbered run that `file` belongs to,
// sorted, including `file` itself; empty when it has no numbered neighbours.
QStringList findImageSequence(const QString& file);

class MediaAsset
{
public:
    MediaAsset() = default;

    static MediaAsset fromFile(const QString& filePath);
    static MediaAsset fromImageSequence(const QStringList& files, double frameRate);

    ProxyMode proxyMode() const { return m_proxyMode; }
    void setProxyMode(ProxyMode mode) { m_proxyMode = mode; }

    bool isImageSequence() const { return !m_sequenceFiles.isEmpty(); }
    const QStringList& sequenceFiles() const { return m_sequenceFiles; }
    double sequenceFrameRate() const { return m_sequenceFrameRate; }
    // File shown at a time inside the media.
    QString sequenceFileAt(double seconds) const;
    // A file that exists on disk for this asset: the first still of a
    // sequence, the path itself otherwise.
    QString sourcePath() const;

    bool isValid() const { return m_id.isValid(); }

    core::Identifier id() const { return m_id; }
    QString filePath() const { return m_filePath; }
    QString fileName() const { return m_fileName; }
    MediaKind kind() const { return m_kind; }
    // Length on the timeline: the file's own, stretched when the frame rate
    // is overridden (its frames play at the new rate).
    double durationSeconds() const;
    QSize frameSize() const { return m_frameSize; }
    const MediaStreams& fileStreams() const { return m_streams; }
    void setFileStreams(const MediaStreams& streams) { m_streams = streams; }
    // -1 uses the source's default audio stream, otherwise an audio ordinal.
    int audioStreamIndex() const { return m_audioStreamIndex; }
    void setAudioStreamIndex(int index) { m_audioStreamIndex = qMax(-1, index); }
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

    // Width of a pixel over its height. The file's own - the stream's sample
    // aspect ratio, as MediaAssetRef::PixelAspectRatio reports it - unless
    // the user overrides it (MediaOverrideOptions::PixelAspectRatio, saved as
    // <OverridePAR>/<PAR> with the composition's PAR values; an image's
    // ImageAsset::PixelAspectRatio is <PAR> alone). Media are placed width x
    // this wide, as Flux takes AbstractAsset::PixelAspectRatioValue.
    double filePixelAspect() const { return m_filePixelAspect; }
    void setFilePixelAspect(double value) { m_filePixelAspect = value > 0.0 ? value : 1.0; }
    bool overridesPixelAspect() const { return m_overridePixelAspect; }
    // composition::Composition::PixelAspect (no custom value).
    int pixelAspectOverride() const { return m_pixelAspectOverride; }
    void setPixelAspectOverride(bool enabled, int pixelAspect);
    double pixelAspectValue() const;
    // The PAR value closest to the file's own (Square when none is close).
    int filePixelAspectKind() const;
    // The stream's frame rate; 0 when the file does not say. 23.976, 29.97
    // and 59.94 are taken as their NTSC fractions, as
    // MediaVideoStream::FrameRate normalises them.
    double fileFrameRate() const { return m_fileFrameRate; }
    void setFileFrameRate(double rate);

    // The rest of MediaOverrideOptions (<OverrideFrameRate>/<FrameRate>,
    // <OverrideAlpha>/<AlphaMode>, <OverrideColorLevels>/<ColorLevels>,
    // <OverrideColorSpace>/<ColorSpace>), with the values Project.dll's
    // MediaVideoStream getters return: alpha 0 straight, 1 premultiplied;
    // levels 0 automatic, 1 video (studio), 2 studio with super whites and
    // blacks, 3 computer (full); space 0 automatic, 1 Rec. 601, 2 Rec. 709.
    // A still has its alpha override alone (ImageAsset::OverriddenAlpha).
    enum AlphaMode { StraightAlpha = 0, PremultipliedAlpha = 1 };
    enum ColorLevels { AutomaticLevels = 0, StudioLevels, StudioSuperLevels, FullLevels };
    enum ColorSpace { AutomaticSpace = 0, Rec601, Rec709 };
    bool overridesFrameRate() const { return m_overrideFrameRate; }
    double frameRateOverride() const { return m_frameRateOverride; }
    void setFrameRateOverride(bool enabled, double rate);
    // The rate the media plays at: the override, the file's, or (for an
    // image sequence) the sequence's own.
    double frameRate() const;
    // Where in the file a time inside the media (at the rate it plays at)
    // falls: the frame shown is the one the override's count reaches.
    double sourceSecondsAt(double seconds) const;
    // An image sequence's rate is set as it is (the reference shows no
    // "From File" for one).
    void setSequenceFrameRate(double rate);

    bool fileHasAlpha() const { return m_fileHasAlpha; }
    bool overridesAlpha() const { return m_overrideAlpha; }
    int alphaOverride() const { return m_alphaOverride; }
    void setAlphaOverride(bool enabled, int mode);
    int alphaMode() const { return m_overrideAlpha ? m_alphaOverride : StraightAlpha; }
    int colorLevels() const { return m_colorLevels; }
    void setColorLevels(int levels);
    int colorSpace() const { return m_colorSpace; }
    void setColorSpace(int space);
    // MediaAsset::SetHardwareAccelerationPermitted (<HWAccelerate>): whether
    // this file may be decoded on the GPU when Options allows it at all.
    bool hardwareDecoding() const { return m_hardwareDecoding; }
    void setHardwareDecoding(bool permitted) { m_hardwareDecoding = permitted; }
    // Whether frames need interpretFrame at all.
    bool reinterpretsPixels() const;
    // A decoded frame as the overrides read it: premultiplied alpha divided
    // out; full-range footage the decoder took as studio range squeezed
    // back; a forced Rec. 601/709 matrix in place of the decoder's default
    // (601 up to 576 lines, 709 above). libVLC tells neither the range nor
    // the matrix it used, so its defaults are what is undone.
    QImage interpretFrame(const QImage& frame) const;

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
    QStringList m_sequenceFiles;
    double m_sequenceFrameRate = 30.0;
    double m_filePixelAspect = 1.0;
    bool m_overridePixelAspect = false;
    int m_pixelAspectOverride = 0;
    double m_fileFrameRate = 0.0;
    bool m_overrideFrameRate = false;
    double m_frameRateOverride = 0.0;
    bool m_fileHasAlpha = false;
    bool m_overrideAlpha = false;
    int m_alphaOverride = StraightAlpha;
    int m_colorLevels = AutomaticLevels;
    int m_colorSpace = AutomaticSpace;
    bool m_hardwareDecoding = true;
    ProxyMode m_proxyMode = ProxyMode::None;
    MediaStreams m_streams;
    int m_audioStreamIndex = -1;
};

} // namespace media
} // namespace openvegas
