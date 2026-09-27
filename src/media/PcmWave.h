#pragma once
#include <QDataStream>
#include <QIODevice>
#include <limits>

namespace openvegas::media {
struct PcmFormat {
    enum SampleFormat { Unknown, Int16, Float };
    int channels = 0, rate = 0;
    SampleFormat sample = Unknown;
    void setChannelCount(int value) { channels = value; }
    void setSampleRate(int value) { rate = value; }
    void setSampleFormat(SampleFormat value) { sample = value; }
    int channelCount() const { return channels; }
    int sampleRate() const { return rate; }
    SampleFormat sampleFormat() const { return sample; }
    int bytesPerFrame() const { return channels * (sample == Int16 ? 2 : 4); }
    bool isValid() const { return channels >= 1 && channels <= 2 && rate >= 8000 && rate <= 192000 && sample != Unknown; }
};
// Write a seekable, signed 16-bit PCM stream as a standard RIFF/WAVE file.
// Ignore an incomplete trailing sample frame; never publish a partial header.
inline bool writePcmWave(QIODevice& pcm, QIODevice& output, const PcmFormat& format)
{
    if (!format.isValid() || format.sampleFormat() != PcmFormat::Int16
        || pcm.isSequential() || !pcm.seek(0)) return false;
    const qint64 size = pcm.size() / format.bytesPerFrame() * format.bytesPerFrame();
    if (size <= 0 || size > std::numeric_limits<quint32>::max() - 36) return false;
    QDataStream stream(&output);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData("RIFF", 4); stream << quint32(size + 36);
    stream.writeRawData("WAVEfmt ", 8); stream << quint32(16) << quint16(1)
        << quint16(format.channelCount()) << quint32(format.sampleRate())
        << quint32(format.sampleRate() * format.bytesPerFrame())
        << quint16(format.bytesPerFrame()) << quint16(16);
    stream.writeRawData("data", 4); stream << quint32(size);
    if (stream.status() != QDataStream::Ok) return false;
    qint64 remaining = size;
    while (remaining > 0) {
        const QByteArray chunk = pcm.read(qMin<qint64>(remaining, 65536));
        if (chunk.isEmpty() || output.write(chunk) != chunk.size()) return false;
        remaining -= chunk.size();
    }
    return true;
}
}
