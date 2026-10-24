#include "render/ExportPreset.h"

#include <QCoreApplication>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSaveFile>
#include <QVariant>

#include <cmath>

#include "render/VideoEncoder.h"

// Export presets: what a preset holds, its .hfxp file (the reference's XML -
// <Preset><MP4|AVI|MOV|WAV|ImageSequence> with an <ExportPreset> header and
// a PropertyManager of named values), the built-in presets, the checks the
// encoders' limits impose, and the FFmpeg arguments and filters a preset
// turns into.

namespace openvegas {
namespace render {

namespace {

// The element under <Preset> for each kind, and the Version it is written
// with: MP4 converts sample/bit rate indices to values up to version 3, so
// its values are written as version 4.
QString formatTag(ExportFormat format)
{
    switch (format) {
    case ExportFormat::MP4: return QStringLiteral("MP4");
    case ExportFormat::AVI: return QStringLiteral("AVI");
    case ExportFormat::MOV: return QStringLiteral("MOV");
    case ExportFormat::WAV: return QStringLiteral("WAV");
    case ExportFormat::ImageSequence: return QStringLiteral("ImageSequence");
    }
    return QString();
}

// The format a file's element names; false for an unknown one.
bool formatFromTag(const QString& tag, ExportFormat* format)
{
    for (ExportFormat f : {ExportFormat::MP4, ExportFormat::AVI, ExportFormat::MOV,
                           ExportFormat::WAV, ExportFormat::ImageSequence}) {
        if (formatTag(f) == tag) {
            *format = f;
            return true;
        }
    }
    return false;
}

// A PropertyManager <Prop> holding a static value, as the reference writes
// it (the value's type in the tag: b, i, db, s).
void appendProperty(QDomDocument& doc, QDomElement& properties, const QString& name,
                    const QVariant& value)
{
    QDomElement prop = doc.createElement(QStringLiteral("Prop"));
    prop.setAttribute(QStringLiteral("Type"), 0);
    prop.setAttribute(QStringLiteral("Spatial"), 0);
    prop.setAttribute(QStringLiteral("CanInterpT"), 0);
    QDomElement nameElement = doc.createElement(QStringLiteral("Name"));
    nameElement.appendChild(doc.createTextNode(name));
    prop.appendChild(nameElement);
    QDomElement holder = doc.createElement(QStringLiteral("Default"));
    holder.setAttribute(QStringLiteral("V"), 4);
    QString tag;
    QString text;
    switch (value.typeId()) {
    case QMetaType::Bool:
        tag = QStringLiteral("b");
        text = value.toBool() ? QStringLiteral("1") : QStringLiteral("0");
        break;
    case QMetaType::Int:
        tag = QStringLiteral("i");
        text = QString::number(value.toInt());
        break;
    case QMetaType::Double:
        tag = QStringLiteral("db");
        text = QString::number(value.toDouble(), 'g', 12);
        break;
    default:
        tag = QStringLiteral("s");
        text = value.toString();
        break;
    }
    QDomElement valueElement = doc.createElement(tag);
    valueElement.appendChild(doc.createTextNode(text));
    holder.appendChild(valueElement);
    prop.appendChild(holder);
    properties.appendChild(prop);
}

// name -> the text of its value (Static over Default).
QHash<QString, QString> readProperties(const QDomElement& properties)
{
    QHash<QString, QString> values;
    for (QDomElement prop = properties.firstChildElement(QStringLiteral("Prop")); !prop.isNull();
         prop = prop.nextSiblingElement(QStringLiteral("Prop"))) {
        const QString name = prop.firstChildElement(QStringLiteral("Name")).text().trimmed();
        if (name.isEmpty()) continue;
        QDomElement holder = prop.firstChildElement(QStringLiteral("Static"));
        if (holder.isNull()) holder = prop.firstChildElement(QStringLiteral("Default"));
        const QDomElement value = holder.firstChildElement();
        if (!value.isNull()) values.insert(name, value.text().trimmed());
    }
    return values;
}

// The translation context of each format's messages, in ExportFormat order.
const char* const kFormatContexts[] = {"MP4ExportPreset", "AVIExportPreset", "MOVExportPreset",
                                       "WAVExportPreset", "ImageSequenceExportPreset"};

const char* formatContext(ExportFormat format)
{
    return kFormatContexts[int(format)];
}

} // namespace

// The output file's extension (an image sequence's, by its image format).
QString ExportPreset::extension() const
{
    switch (format) {
    case ExportFormat::MP4: return QStringLiteral("mp4");
    case ExportFormat::AVI: return QStringLiteral("avi");
    case ExportFormat::MOV: return QStringLiteral("mov");
    case ExportFormat::WAV: return QStringLiteral("wav");
    case ExportFormat::ImageSequence:
        switch (imageFormat) {
        case ImageFormat::Png: return QStringLiteral("png");
        case ImageFormat::Jpg: return QStringLiteral("jpg");
        case ImageFormat::Bmp: return QStringLiteral("bmp");
        case ImageFormat::OpenExr: return QStringLiteral("exr");
        }
    }
    return QStringLiteral("mp4");
}

// The size exported for a source of the given size ("Use Source" per axis).
QSize ExportPreset::resolution(const QSize& source) const
{
    return QSize(widthUseSource ? source.width() : qMax(1, width),
                 heightUseSource ? source.height() : qMax(1, height));
}

double ExportPreset::frameRateFor(double source) const
{
    return frameRateUseSource || frameRate <= 0.0 ? source : frameRate;
}

// The names the interface shows the formats by.
QString exportFormatName(ExportFormat format)
{
    switch (format) {
    case ExportFormat::MP4:
        return QCoreApplication::translate("MP4ExportPreset", "MPEG-4 (.mp4)");
    case ExportFormat::AVI:
        return QCoreApplication::translate("AVIExportPreset", "Audio Video Interleave (.avi)");
    case ExportFormat::MOV:
        return QCoreApplication::translate("MOVExportPreset", "QuickTime (.mov)");
    case ExportFormat::WAV:
        return QCoreApplication::translate("WAVExportPreset", "Waveform Audio File Format (.WAV)");
    case ExportFormat::ImageSequence:
        return QCoreApplication::translate("ImageSequenceExportPreset",
                                           "Image Sequence (.png, .jpg, .bmp, .exr)");
    }
    return QString();
}

// A built-in preset's name is translated; a user's is shown as written.
QString exportPresetDisplayName(const ExportPreset& preset)
{
    return preset.userCreated ? preset.name
                              : QCoreApplication::translate("ExportPreset", preset.name.toUtf8().constData());
}

// The Resolution and Framerate columns of the preset list.
QString exportPresetResolutionText(const ExportPreset& preset)
{
    if (!preset.hasVideo()) return QStringLiteral("-");
    if (preset.widthUseSource && preset.heightUseSource)
        return QCoreApplication::translate("ExportPreset", "Match Source");
    const QString width = preset.widthUseSource
        ? QCoreApplication::translate("ExportPresetPropertiesItemModel", "From Source")
        : QString::number(preset.width);
    const QString height = preset.heightUseSource
        ? QCoreApplication::translate("ExportPresetPropertiesItemModel", "From Source")
        : QString::number(preset.height);
    return QStringLiteral("%1 x %2").arg(width, height);
}

QString exportPresetFrameRateText(const ExportPreset& preset)
{
    if (!preset.hasVideo()) return QStringLiteral("-");
    if (preset.frameRateUseSource)
        return QCoreApplication::translate("ExportPresetPropertiesItemModel", "From Source");
    return QString::number(preset.frameRate, 'g', 6);
}

QString exportPresetFileSuffix()
{
    // Tannen's ExportPresetManager reads "*.hfxp" from the ExportPresets
    // folder (VegasEffects.exe FUN_140697e30, DAT_14154aa68).
    return QStringLiteral("hfxp");
}

// Writes a preset as the reference does, with only the properties its
// format has; the file is replaced atomically.
bool writeExportPreset(const QString& path, const ExportPreset& p, QString* error)
{
    QDomDocument doc;
    QDomElement root = doc.createElement(QStringLiteral("Preset"));
    doc.appendChild(root);
    QDomElement kind = doc.createElement(formatTag(p.format));
    kind.setAttribute(QStringLiteral("Version"), p.format == ExportFormat::MP4 ? 4 : 1);
    root.appendChild(kind);
    QDomElement base = doc.createElement(QStringLiteral("ExportPreset"));
    base.setAttribute(QStringLiteral("Version"), 0);
    base.setAttribute(QStringLiteral("IsUserCreated"), p.userCreated ? 1 : 0);
    QDomElement comment = doc.createElement(QStringLiteral("Comment"));
    comment.appendChild(doc.createTextNode(p.comment));
    base.appendChild(comment);
    kind.appendChild(base);

    QDomElement properties = doc.createElement(QStringLiteral("PropertyManager"));
    properties.setAttribute(QStringLiteral("Version"), 7);
    const auto put = [&](const char* name, const QVariant& value) {
        appendProperty(doc, properties, QString::fromLatin1(name), value);
    };
    if (p.format != ExportFormat::WAV) {
        put("exportVideo", p.exportVideo);
        put("width", p.width);
        put("height", p.height);
        put("width_useSource", p.widthUseSource);
        put("height_useSource", p.heightUseSource);
        put("scaleMode", int(p.scaleMode));
    }
    if (p.format != ExportFormat::WAV && p.format != ExportFormat::ImageSequence) {
        put("frameRate", p.frameRate);
        put("frameRate_useSource", p.frameRateUseSource);
    }
    if (p.format != ExportFormat::ImageSequence) {
        put("exportAudio", p.exportAudio);
        put("audioChannels", 2);
    }
    switch (p.format) {
    case ExportFormat::MP4:
        put("videoCodec", 0);
        put("h264_profile", int(p.h264Profile));
        put("h264_bitRateMode", int(p.bitRateMode));
        put("h264_bitRate", p.bitRate);
        put("h264_targetBitRate", p.bitRate);
        put("h264_maxBitRate", p.maxBitRate);
        put("h264_aspectRatio", 0);
        put("audioCodec", 0);
        put("aac_sampleRate", p.audioSampleRate);
        put("aac_bitRate", p.audioBitRate);
        break;
    case ExportFormat::MOV:
        put("codec", int(p.movCodec));
        put("jpeg_quality", p.quality);
        put("cfhd_format", p.cineFormRgb ? 1 : 0);
        put("imageChannels", p.alpha ? 1 : 0);
        put("h264_bitrateBps", p.bitRate);
        put("h264_keyframeInterval", p.keyFrameInterval);
        put("audio_codec", int(p.audioCodec));
        put("audio_samplerateHz", p.audioSampleRate);
        put("audio_bitrateBps", p.audioBitRate);
        break;
    case ExportFormat::AVI:
        put("codec", int(p.aviCodec));
        put("cfhd_format", p.cineFormRgb ? 1 : 0);
        put("imageChannels", p.alpha ? 1 : 0);
        put("pcm_sampleRate", p.audioSampleRate);
        break;
    case ExportFormat::WAV:
        put("pcm_sampleRate", p.audioSampleRate);
        break;
    case ExportFormat::ImageSequence:
        put("format", int(p.imageFormat));
        put("jpg_quality", p.quality);
        put("imageChannels", p.alpha ? 1 : 0);
        put("prefix", p.prefix);
        break;
    }
    kind.appendChild(properties);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    file.write(doc.toByteArray(2));
    if (!file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

// Reads a preset file; its name is the file's. Missing or unreadable values
// keep the defaults, enumerations are clamped to their range.
bool readExportPreset(const QString& path, ExportPreset* preset, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    QDomDocument doc;
    const auto parsed = doc.setContent(&file);
    if (!parsed) {
        if (error) *error = parsed.errorMessage;
        return false;
    }
    const QDomElement root = doc.documentElement();
    const QDomElement kind = root.firstChildElement();
    ExportPreset p;
    if (root.tagName() != QLatin1String("Preset") || !formatFromTag(kind.tagName(), &p.format)) {
        if (error) *error = QStringLiteral("not an export preset");
        return false;
    }
    p.name = QFileInfo(path).completeBaseName();
    const QDomElement base = kind.firstChildElement(QStringLiteral("ExportPreset"));
    p.userCreated = base.attribute(QStringLiteral("IsUserCreated"), QStringLiteral("1")) != QLatin1String("0");
    p.comment = base.firstChildElement(QStringLiteral("Comment")).text();

    const QHash<QString, QString> v = readProperties(kind.firstChildElement(QStringLiteral("PropertyManager")));
    const auto flag = [&](const char* name, bool fallback) {
        const QString text = v.value(QString::fromLatin1(name));
        return text.isEmpty() ? fallback : text != QLatin1String("0");
    };
    const auto integer = [&](const char* name, int fallback) {
        bool ok = false;
        const int value = v.value(QString::fromLatin1(name)).toInt(&ok);
        return ok ? value : fallback;
    };
    const auto number = [&](const char* name, double fallback) {
        bool ok = false;
        const double value = v.value(QString::fromLatin1(name)).toDouble(&ok);
        return ok ? value : fallback;
    };
    p.exportVideo = flag("exportVideo", p.format != ExportFormat::WAV);
    p.exportAudio = flag("exportAudio", p.format != ExportFormat::ImageSequence);
    p.width = integer("width", p.width);
    p.height = integer("height", p.height);
    p.widthUseSource = flag("width_useSource", p.widthUseSource);
    p.heightUseSource = flag("height_useSource", p.heightUseSource);
    p.scaleMode = ExportPreset::ScaleMode(qBound(0, integer("scaleMode", int(p.scaleMode)), 2));
    p.frameRate = number("frameRate", p.frameRate);
    p.frameRateUseSource = flag("frameRate_useSource", p.frameRateUseSource);
    switch (p.format) {
    case ExportFormat::MP4:
        p.h264Profile = ExportPreset::H264Profile(qBound(0, integer("h264_profile", 2), 2));
        p.bitRateMode = ExportPreset::BitRateMode(qBound(0, integer("h264_bitRateMode", 1), 1));
        p.bitRate = integer(p.bitRateMode == ExportPreset::BitRateMode::Variable ? "h264_targetBitRate"
                                                                                 : "h264_bitRate",
                            p.bitRate);
        p.maxBitRate = integer("h264_maxBitRate", p.maxBitRate);
        p.audioCodec = ExportPreset::AudioCodec::Aac;
        p.audioSampleRate = integer("aac_sampleRate", p.audioSampleRate);
        p.audioBitRate = integer("aac_bitRate", p.audioBitRate);
        break;
    case ExportFormat::MOV:
        p.movCodec = ExportPreset::MovCodec(qBound(0, integer("codec", int(p.movCodec)), 8));
        p.quality = integer("jpeg_quality", p.quality);
        p.cineFormRgb = integer("cfhd_format", 0) == 1;
        p.alpha = integer("imageChannels", 0) == 1;
        p.bitRate = integer("h264_bitrateBps", p.bitRate);
        p.keyFrameInterval = integer("h264_keyframeInterval", 0);
        p.audioCodec = ExportPreset::AudioCodec(qBound(0, integer("audio_codec", 0), 1));
        p.audioSampleRate = integer("audio_samplerateHz", p.audioSampleRate);
        p.audioBitRate = integer("audio_bitrateBps", p.audioBitRate);
        break;
    case ExportFormat::AVI:
        p.aviCodec = ExportPreset::AviCodec(qBound(0, integer("codec", 0), 3));
        p.cineFormRgb = integer("cfhd_format", 0) == 1;
        p.alpha = integer("imageChannels", 0) == 1;
        p.audioCodec = ExportPreset::AudioCodec::Pcm;
        p.audioSampleRate = integer("pcm_sampleRate", p.audioSampleRate);
        break;
    case ExportFormat::WAV:
        p.audioCodec = ExportPreset::AudioCodec::Pcm;
        p.audioSampleRate = integer("pcm_sampleRate", p.audioSampleRate);
        break;
    case ExportFormat::ImageSequence:
        p.imageFormat = ExportPreset::ImageFormat(qBound(0, integer("format", 0), 3));
        p.quality = integer("jpg_quality", p.quality);
        p.alpha = integer("imageChannels", 0) == 1;
        p.prefix = v.value(QStringLiteral("prefix"));
        break;
    }
    *preset = p;
    return true;
}

QString defaultExportPresetName()
{
    // ExportPresetManager's constructor (VegasEffects.exe 140697640) writes
    // it to Options/DefaultExportPreset when nothing is set.
    return QStringLiteral("YouTube 1080p HD");
}

QVector<ExportPreset> builtInExportPresets()
{
    // The names and comments are the reference's built-ins (context
    // "ExportPreset" of its catalogues); their files are not shipped with
    // the application, so the settings are this port's, after the targets'
    // published recommendations.
    QVector<ExportPreset> presets;
    const auto h264 = [&](const char* name, const char* comment, int width, int height, int bitRate,
                          int maxBitRate, ExportPreset::ScaleMode mode, ExportPreset::H264Profile profile) {
        ExportPreset p;
        p.name = QString::fromLatin1(name);
        p.comment = QString::fromLatin1(comment);
        p.userCreated = false;
        p.format = ExportFormat::MP4;
        p.widthUseSource = p.heightUseSource = width <= 0;
        p.width = width > 0 ? width : 1920;
        p.height = height > 0 ? height : 1080;
        p.bitRate = bitRate;
        p.maxBitRate = maxBitRate;
        p.scaleMode = mode;
        p.h264Profile = profile;
        p.audioBitRate = 320000;
        presets.append(p);
    };
    using Mode = ExportPreset::ScaleMode;
    using Profile = ExportPreset::H264Profile;
    h264(QT_TRANSLATE_NOOP("ExportPreset", "YouTube 1080p HD"),
         QT_TRANSLATE_NOOP("ExportPreset", "H.264 compressed for 24/25/30fps upload to YouTube"),
         1920, 1080, 12000000, 16000000, Mode::KeepAspectRatio, Profile::High);
    h264(QT_TRANSLATE_NOOP("ExportPreset", "YouTube 2160p UHD"),
         QT_TRANSLATE_NOOP("ExportPreset", "H.264 compressed for 24/25/30fps upload to YouTube"),
         3840, 2160, 45000000, 60000000, Mode::KeepAspectRatio, Profile::High);
    h264(QT_TRANSLATE_NOOP("ExportPreset", "Match Source"),
         QT_TRANSLATE_NOOP("ExportPreset", "H.264 matching timeline dimensions compressed for upload to YouTube"),
         0, 0, 16000000, 24000000, Mode::KeepAspectRatio, Profile::High);
    h264(QT_TRANSLATE_NOOP("ExportPreset", "Vimeo 1080p HD"),
         QT_TRANSLATE_NOOP("ExportPreset", "H.264 compressed for 24/25/30fps upload to Vimeo"),
         1920, 1080, 15000000, 20000000, Mode::KeepAspectRatio, Profile::High);
    h264(QT_TRANSLATE_NOOP("ExportPreset", "Facebook 720p HD"),
         QT_TRANSLATE_NOOP("ExportPreset", "H.264 compressed for 24/25/30fps upload to Facebook"),
         1280, 720, 6000000, 8000000, Mode::KeepAspectRatio, Profile::Main);
    h264(QT_TRANSLATE_NOOP("ExportPreset", "Instagram Square"),
         QT_TRANSLATE_NOOP("ExportPreset", "H.264 compressed for upload to Instagram"),
         1080, 1080, 6000000, 8000000, Mode::KeepAspectRatioByExpanding, Profile::Main);
    h264(QT_TRANSLATE_NOOP("ExportPreset", "Vertical Video (TikTok, Reels, etc)"),
         QT_TRANSLATE_NOOP("ExportPreset", "H.264 compressed for 24/25/30fps upload to TikTok, Instagram, YouTube, etc."),
         1080, 1920, 8000000, 12000000, Mode::KeepAspectRatioByExpanding, Profile::High);
    h264(QT_TRANSLATE_NOOP("ExportPreset", "Apple Legacy iPads & iPhones"),
         QT_TRANSLATE_NOOP("ExportPreset", "H.264 compressed for Apple iPad 2, iPhone 4S, Apple TV 3 (and above)"),
         1280, 720, 5000000, 6000000, Mode::KeepAspectRatio, Profile::Main);

    const auto mov = [&](const char* name, const char* comment, ExportPreset::MovCodec codec,
                         bool alpha, bool rgb) {
        ExportPreset p;
        p.name = QString::fromLatin1(name);
        p.comment = QString::fromLatin1(comment);
        p.userCreated = false;
        p.format = ExportFormat::MOV;
        p.movCodec = codec;
        p.alpha = alpha;
        p.cineFormRgb = rgb;
        p.audioCodec = ExportPreset::AudioCodec::Pcm;
        presets.append(p);
    };
    using Mov = ExportPreset::MovCodec;
    mov(QT_TRANSLATE_NOOP("ExportPreset", "Apple ProRes 422"),
        QT_TRANSLATE_NOOP("ExportPreset", "High-quality compressed 4:2:2 source 10-bit pixel depth"),
        Mov::ProRes422, false, false);
    mov(QT_TRANSLATE_NOOP("ExportPreset", "Apple ProRes 422 (HQ)"),
        QT_TRANSLATE_NOOP("ExportPreset", "High-quality visually lossless 4:2:2 source 10-bit pixel depth"),
        Mov::ProResHQ, false, false);
    mov(QT_TRANSLATE_NOOP("ExportPreset", "Apple ProRes 4444 (Alpha)"),
        QT_TRANSLATE_NOOP("ExportPreset", "Highest-quality visually lossless 4:4:4 source 10-bit pixel depth + Alpha"),
        Mov::ProRes4444, true, false);
    mov(QT_TRANSLATE_NOOP("ExportPreset", "Apple Photo-JPEG"),
        QT_TRANSLATE_NOOP("ExportPreset", "Photo-JPEG compressed movie"), Mov::PhotoJpeg, false, false);
    mov(QT_TRANSLATE_NOOP("ExportPreset", "GoPro CineForm YUV 10-bit (MOV)"),
        QT_TRANSLATE_NOOP("ExportPreset", "High-quality compressed codec"), Mov::CineForm, false, false);
    mov(QT_TRANSLATE_NOOP("ExportPreset", "GoPro CineForm RGB 12-bit with Alpha (MOV)"),
        QT_TRANSLATE_NOOP("ExportPreset", "Highest-quality visually lossless 4:4:4 source 12-bit pixel depth + Alpha"),
        Mov::CineForm, true, true);

    {
        ExportPreset p;
        p.name = QString::fromLatin1(QT_TRANSLATE_NOOP("ExportPreset", "Uncompressed AVI with Alpha"));
        p.comment = QString::fromLatin1(QT_TRANSLATE_NOOP("ExportPreset", "Lossless uncompressed 4:4:4 8-bit pixel depth + Alpha"));
        p.userCreated = false;
        p.format = ExportFormat::AVI;
        p.aviCodec = ExportPreset::AviCodec::Uncompressed;
        p.alpha = true;
        p.audioCodec = ExportPreset::AudioCodec::Pcm;
        presets.append(p);
    }
    const auto sequence = [&](const char* name, const char* comment, ExportPreset::ImageFormat format,
                              bool alpha) {
        ExportPreset p;
        p.name = QString::fromLatin1(name);
        p.comment = QString::fromLatin1(comment);
        p.userCreated = false;
        p.format = ExportFormat::ImageSequence;
        p.imageFormat = format;
        p.alpha = alpha;
        p.exportAudio = false;
        presets.append(p);
    };
    sequence(QT_TRANSLATE_NOOP("ExportPreset", "PNG Sequence"),
             QT_TRANSLATE_NOOP("ExportPreset", "Lossless PNG 4:4:4 source 8-bit pixel depth"),
             ExportPreset::ImageFormat::Png, false);
    sequence(QT_TRANSLATE_NOOP("ExportPreset", "PNG Sequence with Alpha"),
             QT_TRANSLATE_NOOP("ExportPreset", "Lossless PNG 4:4:4 source 8-bit pixel depth + Alpha"),
             ExportPreset::ImageFormat::Png, true);
    sequence(QT_TRANSLATE_NOOP("ExportPreset", "OpenEXR Sequence 32-bit with Alpha"),
             QT_TRANSLATE_NOOP("ExportPreset", "Lossless compressed 4:4:4 32-bit pixel depth + Alpha"),
             ExportPreset::ImageFormat::OpenExr, true);

    const auto audio = [&](const char* name, const char* comment, ExportFormat format,
                           ExportPreset::AudioCodec codec) {
        ExportPreset p;
        p.name = QString::fromLatin1(name);
        p.comment = QString::fromLatin1(comment);
        p.userCreated = false;
        p.format = format;
        p.exportVideo = false;
        p.audioCodec = codec;
        presets.append(p);
    };
    audio(QT_TRANSLATE_NOOP("ExportPreset", "PCM Audio Only (WAV)"),
          QT_TRANSLATE_NOOP("ExportPreset", "High-quality PCM Stereo Audio"), ExportFormat::WAV,
          ExportPreset::AudioCodec::Pcm);
    audio(QT_TRANSLATE_NOOP("ExportPreset", "AAC Audio Only (MP4)"),
          QT_TRANSLATE_NOOP("ExportPreset", "High-quality AAC Stereo Audio"), ExportFormat::MP4,
          ExportPreset::AudioCodec::Aac);
    audio(QT_TRANSLATE_NOOP("ExportPreset", "Uncompressed Audio Only (MOV)"),
          QT_TRANSLATE_NOOP("ExportPreset", "Uncompressed Audio"), ExportFormat::MOV,
          ExportPreset::AudioCodec::Pcm);
    return presets;
}

// The reasons a preset cannot export a source of the given size and rate -
// the reference's messages: a stream must be on, H.264 needs even sizes,
// CineForm multiples of 16 x 8 and its RGB format for alpha, DV its exact
// PAL or NTSC format. Empty when it can.
QStringList validateExportPreset(const ExportPreset& p, const QSize& source, double sourceFps)
{
    QStringList problems;
    const char* context = formatContext(p.format);
    const QSize size = p.resolution(source);
    switch (p.format) {
    case ExportFormat::WAV:
        if (!p.exportAudio)
            problems << QCoreApplication::translate("WAVExportPreset", "Audio export must be enabled.");
        return problems;
    case ExportFormat::ImageSequence:
        if (!p.exportVideo)
            problems << QCoreApplication::translate("ImageSequenceExportPreset", "Video export must be enabled.");
        return problems;
    default:
        break;
    }
    if (!p.exportVideo && !p.exportAudio)
        problems << QCoreApplication::translate(context, "Video and/or Audio export must be enabled.");
    if (!p.exportVideo) return problems;
    const bool h264 = p.format == ExportFormat::MP4
                      || (p.format == ExportFormat::MOV && p.movCodec == ExportPreset::MovCodec::H264);
    if (h264 && (size.width() % 2 || size.height() % 2)) {
        problems << QCoreApplication::translate("MP4ExportPreset",
                                                "(H264) The width and height (%1 x %2) must be multiples of 2.")
                        .arg(size.width()).arg(size.height());
    }
    const bool cineForm = (p.format == ExportFormat::MOV && p.movCodec == ExportPreset::MovCodec::CineForm)
                          || (p.format == ExportFormat::AVI && p.aviCodec == ExportPreset::AviCodec::CineForm);
    if (cineForm) {
        if (size.width() % 16 || size.height() % 8) {
            problems << QCoreApplication::translate(
                            context, "(GoPro Cineform) The width and height (%1 x %2) must be multiples of 16 and 8 respectively.")
                            .arg(size.width()).arg(size.height());
        }
        if (p.alpha && !p.cineFormRgb) {
            problems << QCoreApplication::translate(
                context, "(GoPro Cineform) The format must be set to 12-bit RGB/RGBA to export RGBA.");
        }
    }
    if (p.format == ExportFormat::AVI) {
        const double fps = p.frameRateFor(sourceFps);
        if (p.aviCodec == ExportPreset::AviCodec::DvPal && (size != QSize(720, 576) || std::abs(fps - 25.0) > 0.01)) {
            problems << QCoreApplication::translate(
                "AVIExportPreset", "(DV PAL) The dimensions and frame rate must be 720 x 576 @ 25 fps.");
        }
        if (p.aviCodec == ExportPreset::AviCodec::DvNtsc
            && (size != QSize(720, 480) || std::abs(fps - 30000.0 / 1001.0) > 0.01)) {
            problems << QCoreApplication::translate(
                "AVIExportPreset", "(DV NTSC) The dimensions and frame rate must be 720 x 480 @ 29.97 fps.");
        }
    }
    return problems;
}

// FFmpeg's video codec arguments for a preset: H.264 through the chosen
// encoder (software or hardware) with the preset's profile and rate control,
// ProRes by profile, CineForm, Photo-JPEG, uncompressed or DV.
QStringList exportVideoArguments(const ExportPreset& p, const QString& h264Encoder)
{
    QStringList args;
    if (!p.hasVideo()) return {QStringLiteral("-vn")};
    const auto prores = [&](int profile, bool alpha) {
        args << QStringLiteral("-c:v") << QStringLiteral("prores_ks") << QStringLiteral("-profile:v")
             << QString::number(profile) << QStringLiteral("-pix_fmt")
             << (alpha ? QStringLiteral("yuva444p10le")
                       : profile >= 4 ? QStringLiteral("yuv444p10le") : QStringLiteral("yuv422p10le"));
        if (alpha) args << QStringLiteral("-alpha_bits") << QStringLiteral("16");
    };
    const auto cineForm = [&] {
        args << QStringLiteral("-c:v") << QStringLiteral("cfhd") << QStringLiteral("-pix_fmt")
             << (p.cineFormRgb ? (p.alpha ? QStringLiteral("gbrap12le") : QStringLiteral("gbrp12le"))
                               : QStringLiteral("yuv422p10le"));
    };
    const auto rate = [&] {
        const H264Rate h264Rate {int(p.h264Profile), p.bitRateMode == ExportPreset::BitRateMode::Constant,
                                 p.bitRate, qMax(p.bitRate, p.maxBitRate), p.keyFrameInterval};
        args << h264EncoderArguments(h264Encoder, h264Rate);
    };
    switch (p.format) {
    case ExportFormat::MP4:
        rate();
        args << QStringLiteral("-movflags") << QStringLiteral("+faststart");
        break;
    case ExportFormat::MOV:
        switch (p.movCodec) {
        case ExportPreset::MovCodec::ProResProxy: prores(0, false); break;
        case ExportPreset::MovCodec::ProResLT: prores(1, false); break;
        case ExportPreset::MovCodec::ProRes422: prores(2, false); break;
        case ExportPreset::MovCodec::ProResHQ: prores(3, false); break;
        case ExportPreset::MovCodec::ProRes4444: prores(4, p.alpha); break;
        case ExportPreset::MovCodec::ProRes4444XQ: prores(5, p.alpha); break;
        case ExportPreset::MovCodec::H264: rate(); break;
        case ExportPreset::MovCodec::PhotoJpeg:
            // Quality 100 % is FFmpeg's q 2, 0 % its 31.
            args << QStringLiteral("-c:v") << QStringLiteral("mjpeg") << QStringLiteral("-q:v")
                 << QString::number(qBound(2, int(std::lround(31.0 - qBound(0, p.quality, 100) * 0.29)), 31))
                 << QStringLiteral("-pix_fmt") << QStringLiteral("yuvj422p");
            break;
        case ExportPreset::MovCodec::CineForm: cineForm(); break;
        }
        break;
    case ExportFormat::AVI:
        switch (p.aviCodec) {
        case ExportPreset::AviCodec::Uncompressed:
            args << QStringLiteral("-c:v") << QStringLiteral("rawvideo") << QStringLiteral("-pix_fmt")
                 << (p.alpha ? QStringLiteral("bgra") : QStringLiteral("bgr24"));
            break;
        case ExportPreset::AviCodec::CineForm: cineForm(); break;
        case ExportPreset::AviCodec::DvNtsc:
            args << QStringLiteral("-c:v") << QStringLiteral("dvvideo") << QStringLiteral("-pix_fmt")
                 << QStringLiteral("yuv411p");
            break;
        case ExportPreset::AviCodec::DvPal:
            args << QStringLiteral("-c:v") << QStringLiteral("dvvideo") << QStringLiteral("-pix_fmt")
                 << QStringLiteral("yuv420p");
            break;
        }
        break;
    case ExportFormat::WAV:
    case ExportFormat::ImageSequence:
        break;
    }
    return args;
}

// The scaling filter from the shot's frames to the preset's size, by its
// scale mode: stretched, fitted with bars, or filled and cropped.
QString exportVideoFilter(const ExportPreset& p, const QSize& source, double pixelAspect)
{
    const bool square = std::abs(pixelAspect - 1.0) <= 1e-9;
    const QSize target = p.resolution(source);
    if (target == source) {
        // The shot's own pixels: only their shape is told.
        return square ? QString() : QStringLiteral("setsar=%1").arg(sampleAspectRatio(pixelAspect));
    }
    QStringList chain;
    // The frames are the shot's pixels; a non-square shot is first brought to
    // its square shape, which is what the preset's size is measured in.
    if (!square) {
        chain << QStringLiteral("scale=%1:%2").arg(qMax(2, int(std::lround(source.width() * pixelAspect / 2.0)) * 2))
                                               .arg(source.height())
              << QStringLiteral("setsar=1");
    }
    const QString w = QString::number(target.width());
    const QString h = QString::number(target.height());
    switch (p.scaleMode) {
    case ExportPreset::ScaleMode::IgnoreAspectRatio:
        chain << QStringLiteral("scale=%1:%2").arg(w, h);
        break;
    case ExportPreset::ScaleMode::KeepAspectRatio:
        chain << QStringLiteral("scale=%1:%2:force_original_aspect_ratio=decrease").arg(w, h)
              << QStringLiteral("pad=%1:%2:(ow-iw)/2:(oh-ih)/2").arg(w, h);
        break;
    case ExportPreset::ScaleMode::KeepAspectRatioByExpanding:
        chain << QStringLiteral("scale=%1:%2:force_original_aspect_ratio=increase").arg(w, h)
              << QStringLiteral("crop=%1:%2").arg(w, h);
        break;
    }
    chain << QStringLiteral("setsar=1");
    return chain.join(QLatin1Char(','));
}

// For YUV output: converts with the BT.709 matrix in limited range and tags
// the stream so, so players do not guess (and shift the colors). RGB output
// is left as it is.
QStringList withVideoColorTags(QStringList arguments)
{
    const int pixelFormat = arguments.lastIndexOf(QStringLiteral("-pix_fmt"));
    if (pixelFormat < 0 || pixelFormat + 1 >= arguments.size()) return arguments;
    const QString format = arguments.at(pixelFormat + 1);
    if (!format.startsWith(QLatin1String("yuv")) && !format.startsWith(QLatin1String("nv"))) return arguments;
    // The scale filter converts to the encoder's format itself (format
    // negotiation), so its matrix is the one used; setparams marks the frames.
    const QString convert = QStringLiteral(
        "scale=out_color_matrix=bt709:out_range=tv,"
        "setparams=colorspace=bt709:color_primaries=bt709:color_trc=bt709:range=tv");
    const int filter = arguments.lastIndexOf(QStringLiteral("-vf"));
    if (filter >= 0 && filter + 1 < arguments.size()) arguments[filter + 1] += QLatin1Char(',') + convert;
    else arguments << QStringLiteral("-vf") << convert;
    arguments << QStringLiteral("-colorspace") << QStringLiteral("bt709")
              << QStringLiteral("-color_primaries") << QStringLiteral("bt709")
              << QStringLiteral("-color_trc") << QStringLiteral("bt709")
              << QStringLiteral("-color_range") << QStringLiteral("tv");
    return arguments;
}

// FFmpeg's audio arguments: AAC at the preset's bit rate or 16-bit PCM,
// always stereo.
QStringList exportAudioArguments(const ExportPreset& p)
{
    if (!p.hasAudio()) return {QStringLiteral("-an")};
    QStringList args;
    if (p.audioCodec == ExportPreset::AudioCodec::Aac) {
        args << QStringLiteral("-c:a") << QStringLiteral("aac") << QStringLiteral("-b:a")
             << QString::number(qMax(32000, p.audioBitRate));
    } else {
        args << QStringLiteral("-c:a") << QStringLiteral("pcm_s16le");
    }
    args << QStringLiteral("-ar") << QString::number(qMax(8000, p.audioSampleRate))
         << QStringLiteral("-ac") << QStringLiteral("2");
    return args;
}

} // namespace render
} // namespace openvegas
