#pragma once

#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>

namespace openvegas {
namespace render {

// The kind of an export preset - the element under <Preset> that names its
// class in the reference (biff::ui::exporter::MP4ExportPreset "MP4",
// AVIExportPreset "AVI", MOVExportPreset "MOV", WAVExportPreset "WAV",
// ImageSequenceExportPreset).
enum class ExportFormat { MP4, AVI, MOV, WAV, ImageSequence };

// One export preset: how the Export panel turns a shot into a file. The
// fields are the reference's preset properties, under their own names in
// the preset file (exportVideo, width/width_useSource, frameRate, scaleMode,
// h264_profile, h264_bitRateMode, h264_targetBitRate, aac_sampleRate,
// codec, format, jpg_quality, prefix, ...); rates are in bits per second
// and hertz, as the reference's MP4 presets store them from version 4.
struct ExportPreset
{
    enum class ScaleMode { IgnoreAspectRatio, KeepAspectRatio, KeepAspectRatioByExpanding };
    enum class H264Profile { Baseline, Main, High };
    enum class BitRateMode { Constant, Variable };
    // MOV codecs, in the reference's list order.
    enum class MovCodec { ProResProxy, ProResLT, ProRes422, ProResHQ, ProRes4444, ProRes4444XQ,
                          H264, PhotoJpeg, CineForm };
    enum class AviCodec { Uncompressed, CineForm, DvNtsc, DvPal };
    enum class AudioCodec { Pcm, Aac };
    enum class ImageFormat { Png, Jpg, Bmp, OpenExr };

    QString name;             // the file's name for a user preset
    QString comment;
    bool userCreated = true;  // ExportPreset IsUserCreated
    ExportFormat format = ExportFormat::MP4;

    bool exportVideo = true;
    bool exportAudio = true;
    bool widthUseSource = true;
    bool heightUseSource = true;
    int width = 1920;
    int height = 1080;
    bool frameRateUseSource = true;
    double frameRate = 30.0;
    ScaleMode scaleMode = ScaleMode::KeepAspectRatio;

    // MP4 (H.264 and AAC); MOV's H.264 uses the bit rate and key frame
    // interval below.
    H264Profile h264Profile = H264Profile::High;
    BitRateMode bitRateMode = BitRateMode::Variable;
    int bitRate = 16000000;      // constant, or the target of a variable one
    int maxBitRate = 24000000;
    int keyFrameInterval = 0;    // frames; 0 leaves it to the encoder
    AudioCodec audioCodec = AudioCodec::Aac;
    int audioSampleRate = 48000;
    int audioBitRate = 320000;

    MovCodec movCodec = MovCodec::ProResHQ;
    AviCodec aviCodec = AviCodec::Uncompressed;
    int quality = 90;            // Photo-JPEG and JPG sequences, percent
    bool cineFormRgb = false;    // 12-bit RGB/RGBA rather than 10-bit YUV 4:2:2
    bool alpha = false;          // RGBA output where the codec carries it

    ImageFormat imageFormat = ImageFormat::Png;
    QString prefix;

    bool isValid() const { return !name.isEmpty(); }
    // The output file's suffix: mp4, avi, mov, wav, or the image format's.
    QString extension() const;
    // The frame size for a shot of `source` pixels.
    QSize resolution(const QSize& source) const;
    double frameRateFor(double source) const;
    bool hasVideo() const { return format != ExportFormat::WAV && exportVideo; }
    bool hasAudio() const { return format != ExportFormat::ImageSequence && exportAudio; }
};

// "MPEG-4 (.mp4)", "QuickTime (.mov)", ... as the reference names the kinds.
QString exportFormatName(ExportFormat format);
// A preset's name as shown: a built-in one in the user's language.
QString exportPresetDisplayName(const ExportPreset& preset);
// Its resolution and frame rate as the Presets list shows them.
QString exportPresetResolutionText(const ExportPreset& preset);
QString exportPresetFrameRateText(const ExportPreset& preset);

// Preset files: "<name>.hfxp", the reference's XML -
//   <Preset><MP4 Version="4">
//     <ExportPreset Version="0" IsUserCreated="1"><Comment>...</Comment></ExportPreset>
//     <PropertyManager Version="7"><Prop ...><Name>width</Name><Default V="4"><i>1920</i>...
//   </MP4></Preset>
// The name is the file's.
QString exportPresetFileSuffix();
bool readExportPreset(const QString& path, ExportPreset* preset, QString* error = nullptr);
bool writeExportPreset(const QString& path, const ExportPreset& preset, QString* error = nullptr);

// The presets the reference ships (names and comments its own, settings
// this port's choice where the files are not recovered), and the default
// for new export tasks (Options/DefaultExportPreset, "YouTube 1080p HD").
QVector<ExportPreset> builtInExportPresets();
QString defaultExportPresetName();

// What the preset cannot export, in the reference's words ("(H264) The width
// and height (%1 x %2) must be multiples of 2.", "Audio export must be
// enabled.", ...); empty when it can.
QStringList validateExportPreset(const ExportPreset& preset, const QSize& source, double sourceFps);

// FFmpeg output options for a preset of a movie or audio kind: the video
// codec and its rate (`h264Encoder` for H.264), the filter that brings the
// shot's frames (`source` pixels of `pixelAspect`) to the preset's size, the
// frame rate, and the audio codec.
QStringList exportVideoArguments(const ExportPreset& preset, const QString& h264Encoder);
QString exportVideoFilter(const ExportPreset& preset, const QSize& source, double pixelAspect);
// The rendered frames are RGB pictures (sRGB, as the Viewer shows them). A
// YUV output - H.264 in any encoder, ProRes - gets them through the BT.709
// matrix in limited range and says so in the stream; left to FFmpeg's
// default the conversion was BT.601 and untagged, which players take as
// BT.709 for HD video, showing reds and yellows brighter and greens darker
// than the shot. Arguments without a YUV -pix_fmt come back unchanged.
QStringList withVideoColorTags(QStringList arguments);
QStringList exportAudioArguments(const ExportPreset& preset);

} // namespace render
} // namespace openvegas
