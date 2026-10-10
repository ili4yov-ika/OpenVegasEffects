#pragma once

#include <QString>
#include <QStringList>

namespace openvegas {
namespace render {

// H.264 encoder for MP4 export. With Options/UseHardwareEncoding the first
// FFmpeg hardware encoder that really encodes on this machine is used -
// NVIDIA NVENC, Intel Quick Sync, AMD AMF, then Windows Media Foundation -
// and libx264 otherwise. Each candidate is tried once per process with a
// tiny test encode, since FFmpeg lists encoders whose device or driver is
// missing.
QString chooseH264Encoder(const QString& ffmpeg, bool hardware);

// FFmpeg output options for that encoder: codec, quality and pixel format.
QStringList h264EncoderArguments(const QString& encoder);

// An export preset's H.264 rate: profile (0 Baseline, 1 Main, 2 High), a
// constant or a variable bit rate with its ceiling, in bits per second, and
// the key frame interval in frames (0 leaves it to the encoder).
struct H264Rate
{
    int profile = 2;
    bool constant = false;
    int bitRate = 16000000;
    int maxBitRate = 24000000;
    int keyFrameInterval = 0;
};
// The same encoder at that rate, in each encoder's own options.
QStringList h264EncoderArguments(const QString& encoder, const H264Rate& rate);

// Whether one encoder really encodes with this FFmpeg (cached per process).
bool h264EncoderWorks(const QString& ffmpeg, const QString& encoder);

// A pixel aspect ratio as FFmpeg's setsar wants it ("10/11"): the smallest
// fraction within 1e-6 of it, denominators up to 10000.
QString sampleAspectRatio(double pixelAspect);

} // namespace render
} // namespace openvegas
