#include "media/MediaAsset.h"

#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QMap>
#include <QRegularExpression>

#include <cmath>

#include "composition/Composition.h"
#include "model3d/ModelLoader.h"

namespace openvegas {
namespace media {

void MediaAsset::setPixelAspectOverride(bool enabled, int pixelAspect)
{
    using composition::Composition;
    m_overridePixelAspect = enabled;
    m_pixelAspectOverride = pixelAspect >= 0 && pixelAspect < Composition::CustomAspect
                                ? pixelAspect : Composition::SquarePixels;
}

double MediaAsset::pixelAspectValue() const
{
    return m_overridePixelAspect
               ? composition::Composition::pixelAspectValue(m_pixelAspectOverride, 1.0)
               : m_filePixelAspect;
}

int MediaAsset::filePixelAspectKind() const
{
    using composition::Composition;
    for (int kind = Composition::SquarePixels; kind < Composition::CustomAspect; ++kind) {
        if (std::abs(Composition::pixelAspectValue(kind, 1.0) - m_filePixelAspect) < 0.005)
            return kind;
    }
    return Composition::SquarePixels;
}

void MediaAsset::setFilePath(const QString& path)
{
    m_filePath = path;
    m_fileName = QFileInfo(path).fileName();
    m_streams = {};

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
        const QImageReader reader(m_filePath);
        m_streams.videoFormat = QString::fromLatin1(reader.format()).toUpper();
        m_streams.videoCodec = m_streams.videoFormat;
        const QSize size = reader.size();
        if (size.isValid() && !size.isEmpty()) {
            m_frameSize = size;
        }
        m_fileHasAlpha = QImage::toPixelFormat(reader.imageFormat()).alphaUsage()
                         == QPixelFormat::UsesAlpha;
    } else if (audioExts.contains(suffix)) {
        m_kind = MediaKind::Audio;
    } else if (model3d::modelExtensions().contains(suffix)) {
        m_kind = MediaKind::Model3D;
    } else {
        m_kind = MediaKind::Video;
    }

    m_id = core::Identifier(QStringLiteral("media:") + m_filePath);
}

QStringList findImageSequence(const QString& file)
{
    const QFileInfo info(file);
    static const QStringList stills = {QStringLiteral("png"), QStringLiteral("jpg"),
                                       QStringLiteral("jpeg"), QStringLiteral("bmp"),
                                       QStringLiteral("tga"), QStringLiteral("exr"),
                                       QStringLiteral("dpx"), QStringLiteral("tif"),
                                       QStringLiteral("tiff")};
    if (!stills.contains(info.suffix().toLower())) return {};
    // name = prefix + digits + tail; the digits are the frame number.
    static const QRegularExpression numbered(QStringLiteral("^(.*?)(\\d+)(\\D*)$"));
    const QRegularExpressionMatch own = numbered.match(info.completeBaseName());
    if (!own.hasMatch()) return {};
    const QString prefix = own.captured(1);
    const QString tail = own.captured(3);
    const QString suffix = info.suffix();
    QMap<qint64, QString> byNumber;
    const QDir dir = info.absoluteDir();
    for (const QFileInfo& candidate : dir.entryInfoList(
             {prefix + QStringLiteral("*") + tail + QLatin1Char('.') + suffix}, QDir::Files)) {
        const QRegularExpressionMatch match = numbered.match(candidate.completeBaseName());
        if (match.hasMatch() && match.captured(1) == prefix && match.captured(3) == tail
            && candidate.suffix() == suffix) {
            byNumber.insert(match.captured(2).toLongLong(), candidate.absoluteFilePath());
        }
    }
    // The contiguous run of numbers around the chosen file.
    const qint64 number = own.captured(2).toLongLong();
    qint64 first = number, last = number;
    while (byNumber.contains(first - 1)) --first;
    while (byNumber.contains(last + 1)) ++last;
    if (last == first) return {};
    QStringList files;
    for (qint64 n = first; n <= last; ++n) files.append(byNumber.value(n));
    return files;
}

MediaAsset MediaAsset::fromImageSequence(const QStringList& files, double frameRate)
{
    MediaAsset asset;
    if (files.isEmpty()) return asset;
    asset.setFilePath(QFileInfo(files.first()).absoluteFilePath()
                      + QLatin1String(kImageSequenceMarker));
    asset.m_kind = MediaKind::Video;
    asset.m_sequenceFiles = files;
    asset.m_sequenceFrameRate = frameRate > 0.0 ? frameRate : 30.0;
    asset.m_durationSeconds = files.size() / asset.m_sequenceFrameRate;
    const QFileInfo first(files.first());
    const QFileInfo lastFile(files.last());
    asset.m_fileName = QStringLiteral("%1 - %2").arg(first.fileName(), lastFile.fileName());
    const QImageReader reader(files.first());
    asset.m_streams.videoFormat = QString::fromLatin1(reader.format()).toUpper();
    asset.m_streams.videoCodec = asset.m_streams.videoFormat;
    const QSize size = reader.size();
    if (size.isValid() && !size.isEmpty()) asset.m_frameSize = size;
    asset.m_fileHasAlpha = QImage::toPixelFormat(reader.imageFormat()).alphaUsage()
                           == QPixelFormat::UsesAlpha;
    return asset;
}

QString MediaAsset::sequenceFileAt(double seconds) const
{
    if (m_sequenceFiles.isEmpty()) return {};
    const qsizetype index = qBound<qsizetype>(
        0, qsizetype(std::floor(seconds * m_sequenceFrameRate + 1e-6)),
        m_sequenceFiles.size() - 1);
    return m_sequenceFiles.at(index);
}

namespace {

// MediaVideoStream::FrameRate's normalisation: the NTSC rates as fractions.
double normalizedRate(double rate)
{
    for (double ntsc : {24000.0 / 1001.0, 30000.0 / 1001.0, 60000.0 / 1001.0}) {
        if (std::abs(rate - ntsc) < 0.005) return ntsc;
    }
    return rate;
}

} // namespace

void MediaAsset::setFileFrameRate(double rate)
{
    m_fileFrameRate = rate > 0.0 ? normalizedRate(rate) : 0.0;
}

void MediaAsset::setFrameRateOverride(bool enabled, double rate)
{
    m_overrideFrameRate = enabled && rate > 0.0;
    m_frameRateOverride = rate > 0.0 ? normalizedRate(rate) : 0.0;
}

double MediaAsset::frameRate() const
{
    if (isImageSequence()) return m_sequenceFrameRate;
    if (m_overrideFrameRate) return m_frameRateOverride;
    return m_fileFrameRate;
}

double MediaAsset::durationSeconds() const
{
    if (!isImageSequence() && m_overrideFrameRate && m_fileFrameRate > 0.0)
        return m_durationSeconds * m_fileFrameRate / m_frameRateOverride;
    return m_durationSeconds;
}

double MediaAsset::sourceSecondsAt(double seconds) const
{
    if (isImageSequence() || !m_overrideFrameRate || m_fileFrameRate <= 0.0) return seconds;
    const double frame = std::floor(qMax(0.0, seconds) * m_frameRateOverride + 1e-6);
    return frame / m_fileFrameRate;
}

void MediaAsset::setSequenceFrameRate(double rate)
{
    if (rate <= 0.0) return;
    m_sequenceFrameRate = normalizedRate(rate);
    if (isImageSequence()) m_durationSeconds = m_sequenceFiles.size() / m_sequenceFrameRate;
}

void MediaAsset::setAlphaOverride(bool enabled, int mode)
{
    m_overrideAlpha = enabled;
    m_alphaOverride = mode == PremultipliedAlpha ? PremultipliedAlpha : StraightAlpha;
}

void MediaAsset::setColorLevels(int levels)
{
    m_colorLevels = levels >= AutomaticLevels && levels <= FullLevels ? levels : AutomaticLevels;
}

void MediaAsset::setColorSpace(int space)
{
    m_colorSpace = space >= AutomaticSpace && space <= Rec709 ? space : AutomaticSpace;
}

bool MediaAsset::reinterpretsPixels() const
{
    if (alphaMode() == PremultipliedAlpha) return true;
    if (m_kind != MediaKind::Video || isImageSequence()) return false;
    if (m_colorLevels == FullLevels) return true;
    if (m_colorSpace == AutomaticSpace) return false;
    // A forced matrix only matters where it is not the decoder's default.
    const bool decoderRec709 = m_frameSize.height() > 576;
    return (m_colorSpace == Rec709) != decoderRec709;
}

QImage MediaAsset::interpretFrame(const QImage& frame) const
{
    if (frame.isNull() || !reinterpretsPixels()) return frame;
    QImage image = frame.convertToFormat(QImage::Format_RGBA8888);
    if (alphaMode() == PremultipliedAlpha) {
        // The same bytes read as premultiplied, divided out.
        image = QImage(image.constBits(), image.width(), image.height(), image.bytesPerLine(),
                       QImage::Format_RGBA8888_Premultiplied)
                    .convertToFormat(QImage::Format_RGBA8888);
    }
    if (m_kind != MediaKind::Video || isImageSequence()) return image;

    const bool squeeze = m_colorLevels == FullLevels;
    const bool decoderRec709 = m_frameSize.height() > 576;
    const bool rematrix = m_colorSpace != AutomaticSpace && (m_colorSpace == Rec709) != decoderRec709;
    if (!squeeze && !rematrix) return image;
    // Kr/Kb of the decoder's matrix (to get Y'CbCr back) and the forced one.
    const double fromKr = decoderRec709 ? 0.2126 : 0.299, fromKb = decoderRec709 ? 0.0722 : 0.114;
    const double toKr = m_colorSpace == Rec709 ? 0.2126 : 0.299, toKb = m_colorSpace == Rec709 ? 0.0722 : 0.114;
    for (int y = 0; y < image.height(); ++y) {
        uchar* row = image.scanLine(y);
        for (int x = 0; x < image.width(); ++x) {
            double r = row[4 * x], g = row[4 * x + 1], b = row[4 * x + 2];
            if (squeeze) {
                // The decoder stretched 16..235 to 0..255; the data was full.
                r = 16.0 + r * 219.0 / 255.0;
                g = 16.0 + g * 219.0 / 255.0;
                b = 16.0 + b * 219.0 / 255.0;
            }
            if (rematrix) {
                const double luma = fromKr * r + (1.0 - fromKr - fromKb) * g + fromKb * b;
                const double cb = (b - luma) / (2.0 * (1.0 - fromKb));
                const double cr = (r - luma) / (2.0 * (1.0 - fromKr));
                r = luma + 2.0 * (1.0 - toKr) * cr;
                b = luma + 2.0 * (1.0 - toKb) * cb;
                g = (luma - toKr * r - toKb * b) / (1.0 - toKr - toKb);
            }
            row[4 * x] = uchar(qBound(0, int(std::lround(r)), 255));
            row[4 * x + 1] = uchar(qBound(0, int(std::lround(g)), 255));
            row[4 * x + 2] = uchar(qBound(0, int(std::lround(b)), 255));
        }
    }
    return image;
}

QString MediaAsset::sourcePath() const
{
    return isImageSequence() ? m_sequenceFiles.first() : m_filePath;
}

} // namespace media
} // namespace openvegas