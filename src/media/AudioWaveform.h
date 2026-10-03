#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

class QProcess;

namespace openvegas {
namespace media {

// Mono peak data of one media file: per 10 ms bucket the absolute peak and the
// RMS, both normalised to 0..1. The reference keeps the same summary in a
// peak file beside its media cache (MediaAudioStream::WaveformPreviewAvailable,
// WaveformPreviewIsReady); a timeline column reads it instead of PCM.
struct WaveformPeaks
{
    static constexpr int kBucketsPerSecond = 100;
    QVector<float> peak;
    QVector<float> rms;
    bool isEmpty() const { return peak.isEmpty(); }
    double durationSeconds() const { return double(peak.size()) / kBucketsPerSecond; }
};

// Accumulates mono PCM16 into buckets; feed() may be called with any chunk
// size, as FFmpeg output arrives.
class WaveformBuilder
{
public:
    explicit WaveformBuilder(int sampleRate);
    void feed(const qint16* samples, qsizetype count);
    WaveformPeaks finish();

private:
    int m_samplesPerBucket = 1;
    qsizetype m_inBucket = 0;
    int m_peak = 0;
    double m_squares = 0.0;
    WaveformPeaks m_result;
};

enum class WaveformStyle { Rms, Peak };

// Heights (0..1) of `columns` columns covering the source interval
// [sourceStart, sourceEnd) seconds, the part of the media a clip shows after
// slip and rate stretch. Each column takes the loudest bucket it spans; the
// log scale maps -60..0 dBFS linearly, like the level meters.
QVector<float> waveformColumns(const WaveformPeaks& peaks, double sourceStart, double sourceEnd,
                               int columns, WaveformStyle style, bool logarithmic);

// Builds peaks in the background, one FFmpeg decode per file at a time, and
// keeps them in memory and in a disk cache keyed by path, size and mtime - a
// replaced file gets a fresh waveform and an unreadable one none at all.
// The selected audio-stream ordinal is part of the key.
class WaveformCache : public QObject
{
    Q_OBJECT

public:
    explicit WaveformCache(QObject* parent = nullptr);
    ~WaveformCache() override;

    // Peaks for a file, or nullptr while they are built (the first call
    // starts that) or when the file has no decodable audio.
    const WaveformPeaks* peaks(const QString& filePath, int audioStreamIndex = -1);
    void setDecoder(const QString& ffmpegPath) { m_ffmpeg = ffmpegPath; }
    void setCacheDirectory(const QString& directory) { m_cacheDirectory = directory; }

signals:
    void peaksReady(const QString& filePath);

private:
    static QString keyFor(const QString& filePath, int audioStreamIndex);
    void startNext();
    void finishDecode(const QString& key, const QString& filePath, bool ok);
    bool loadFromDisk(const QString& key);
    void saveToDisk(const QString& key, const WaveformPeaks& peaks) const;

    QString m_ffmpeg;
    QString m_cacheDirectory;
    QHash<QString, WaveformPeaks> m_ready;   // by key
    QSet<QString> m_failed;                  // keys without audio
    struct Pending { QString path; int stream; };
    QVector<Pending> m_queue;
    QSet<QString> m_queued;                  // keys queued or decoding
    QPointer<QProcess> m_process;
    QString m_processKey;
    QString m_processPath;
    int m_processStream = -1;
    WaveformBuilder* m_builder = nullptr;
};

} // namespace media
} // namespace openvegas
