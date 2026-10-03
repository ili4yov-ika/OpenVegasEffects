#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>
#include <QSize>
#include <cstdint>

namespace openvegas::media {

// Copied out of libVLC's track lists before those lists are released.
// Audio indices are ordinals within the audio category, shared with FFmpeg's a:N.
struct AudioStreamInfo
{
    QString format;
    QString codec;
    QString language;
    QString name;
    unsigned sampleRate = 0;
    unsigned channels = 0;
};

struct MediaStreams
{
    bool hasVideo = false;
    QString videoFormat;
    QString videoCodec;
    QSize videoSize;
    QVector<AudioStreamInfo> audio;
};

inline QString codecFourcc(uint32_t code)
{
    QByteArray bytes;
    for (int shift = 0; shift < 32; shift += 8) {
        const unsigned ch = (code >> shift) & 0xff;
        if (ch < 32 || ch > 126)
            return code ? QStringLiteral("0x%1").arg(code, 8, 16, QLatin1Char('0')) : QString();
        bytes.append(char(ch));
    }
    return QString::fromLatin1(bytes).trimmed();
}

} // namespace openvegas::media
