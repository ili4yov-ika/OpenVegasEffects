#include "media/AudioWaveform.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>
#include <cstdlib>

// Audio waveforms for the timeline's clips. FFmpeg decodes a file's audio to
// mono 16-bit PCM at 8 kHz in the background; WaveformBuilder reduces it to a
// peak and an RMS level per 10 ms bucket, and WaveformCache keeps the result
// in memory and on disk (the media cache's waveform folder), keyed by the
// file's path, size, modification time and audio stream.
namespace openvegas {
namespace media {

namespace {

constexpr int kDecodeRate = 8000;       // plenty for 10 ms envelope buckets
constexpr quint32 kCacheMagic = 0x4f565746; // "OVWF"
constexpr quint32 kCacheVersion = 1;

} // namespace

WaveformBuilder::WaveformBuilder(int sampleRate)
    : m_samplesPerBucket(qMax(1, sampleRate / WaveformPeaks::kBucketsPerSecond))
{
}

// Adds PCM samples; every full bucket appends its peak and RMS, both 0..1.
void WaveformBuilder::feed(const qint16* samples, qsizetype count)
{
    for (qsizetype i = 0; i < count; ++i) {
        const int value = samples[i];
        m_peak = qMax(m_peak, std::abs(value));
        m_squares += double(value) * value;
        if (++m_inBucket == m_samplesPerBucket) {
            m_result.peak.append(float(qMin(1.0, m_peak / 32768.0)));
            m_result.rms.append(float(qMin(1.0, std::sqrt(m_squares / m_inBucket) / 32768.0)));
            m_inBucket = 0;
            m_peak = 0;
            m_squares = 0.0;
        }
    }
}

WaveformPeaks WaveformBuilder::finish()
{
    if (m_inBucket > 0) {
        m_result.peak.append(float(qMin(1.0, m_peak / 32768.0)));
        m_result.rms.append(float(qMin(1.0, std::sqrt(m_squares / m_inBucket) / 32768.0)));
        m_inBucket = 0;
    }
    return std::move(m_result);
}

// The heights a clip's waveform is drawn with: the source span
// [sourceStart, sourceEnd] in seconds split into `columns`, each the highest
// peak or RMS level of its buckets (Options > Audio Waveforms) - on a 60 dB
// scale when logarithmic scaling is on.
QVector<float> waveformColumns(const WaveformPeaks& peaks, double sourceStart, double sourceEnd,
                               int columns, WaveformStyle style, bool logarithmic)
{
    QVector<float> heights(qMax(0, columns), 0.0f);
    if (peaks.isEmpty() || columns <= 0 || sourceEnd <= sourceStart) return heights;
    const QVector<float>& levels = style == WaveformStyle::Peak ? peaks.peak : peaks.rms;
    const double bucketsPerColumn = (sourceEnd - sourceStart)
                                    * WaveformPeaks::kBucketsPerSecond / columns;
    for (int column = 0; column < columns; ++column) {
        const double from = sourceStart * WaveformPeaks::kBucketsPerSecond
                            + column * bucketsPerColumn;
        qsizetype first = qsizetype(std::floor(from));
        qsizetype last = qsizetype(std::ceil(from + bucketsPerColumn));
        first = qBound<qsizetype>(0, first, levels.size());
        last = qBound<qsizetype>(first, qMax(last, first + 1), levels.size());
        float level = 0.0f;
        for (qsizetype bucket = first; bucket < last; ++bucket) {
            level = qMax(level, levels.at(bucket));
        }
        if (logarithmic) {
            level = level <= 0.001f
                ? 0.0f
                : float(qBound(0.0, (20.0 * std::log10(double(level)) + 60.0) / 60.0, 1.0));
        }
        heights[column] = level;
    }
    return heights;
}

namespace {

WaveformStore& defaultStore()
{
    static WaveformStore store {QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
                                    .filePath(QStringLiteral("waveforms")), {}, {}};
    return store;
}

} // namespace

void WaveformCache::setDefaultStore(const WaveformStore& store)
{
    defaultStore() = store;
}

WaveformCache::WaveformCache(QObject* parent)
    : QObject(parent)
    , m_ffmpeg(QStandardPaths::findExecutable(QStringLiteral("ffmpeg")))
    , m_store(defaultStore())
{
}

WaveformCache::~WaveformCache()
{
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(1000);
    }
    delete m_builder;
}

QString WaveformCache::keyFor(const QString& filePath, int audioStreamIndex)
{
    const QFileInfo info(filePath);
    if (!info.exists()) return {};
    const QByteArray identity = QStringLiteral("%1|%2|%3|audio:%4")
                                    .arg(info.absoluteFilePath())
                                    .arg(info.size())
                                    .arg(info.lastModified().toMSecsSinceEpoch())
                                    .arg(audioStreamIndex)
                                    .toUtf8();
    return QString::fromLatin1(
        QCryptographicHash::hash(identity, QCryptographicHash::Sha1).toHex());
}

// The file's levels when they are known (in memory or on disk); otherwise
// null, and the file is queued for decoding - peaksReady tells when it is done.
// A file that failed once is not tried again in this session.
const WaveformPeaks* WaveformCache::peaks(const QString& filePath, int audioStreamIndex)
{
    const QString key = keyFor(filePath, audioStreamIndex);
    if (key.isEmpty() || m_failed.contains(key)) return nullptr;
    const auto ready = m_ready.constFind(key);
    if (ready != m_ready.constEnd()) return &ready.value();
    if (loadFromDisk(key)) return &m_ready[key];
    if (!m_queued.contains(key) && !m_ffmpeg.isEmpty()) {
        m_queued.insert(key);
        m_queue.append({filePath, audioStreamIndex});
        startNext();
    }
    return nullptr;
}

// Decodes the next queued file, one FFmpeg process at a time, feeding its
// standard output to the builder as it arrives.
void WaveformCache::startNext()
{
    if (m_process || m_queue.isEmpty()) return;
    const Pending next = m_queue.takeFirst();
    m_processPath = next.path;
    m_processStream = next.stream;
    m_processKey = keyFor(m_processPath, m_processStream);
    if (m_processKey.isEmpty()) {
        startNext();
        return;
    }
    delete m_builder;
    m_builder = new WaveformBuilder(kDecodeRate);
    auto* process = new QProcess(this);
    m_process = process;
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process] {
        QByteArray bytes = process->readAllStandardOutput();
        if (m_builder && bytes.size() >= 2) {
            m_builder->feed(reinterpret_cast<const qint16*>(bytes.constData()), bytes.size() / 2);
        }
    });
    connect(process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus status) {
        process->deleteLater();
        const bool ok = status == QProcess::NormalExit && code == 0;
        finishDecode(m_processKey, m_processPath, ok);
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        process->deleteLater();
        finishDecode(m_processKey, m_processPath, false);
    });
    QStringList arguments {QStringLiteral("-v"), QStringLiteral("error"),
                              QStringLiteral("-nostdin"), QStringLiteral("-i"), m_processPath,
                              QStringLiteral("-vn"), QStringLiteral("-ac"), QStringLiteral("1"),
                              QStringLiteral("-ar"), QString::number(kDecodeRate),
                              QStringLiteral("-f"), QStringLiteral("s16le"),
                              QStringLiteral("pipe:1")};
    if (m_processStream >= 0) {
        arguments.insert(arguments.size() - 1, QStringLiteral("-map"));
        arguments.insert(arguments.size() - 1, QStringLiteral("0:a:%1").arg(m_processStream));
    }
    process->start(m_ffmpeg, arguments);
}

void WaveformCache::finishDecode(const QString& key, const QString& filePath, bool ok)
{
    m_process.clear();
    WaveformPeaks peaks = m_builder ? m_builder->finish() : WaveformPeaks();
    delete m_builder;
    m_builder = nullptr;
    m_queued.remove(key);
    if (ok && !peaks.isEmpty()) {
        saveToDisk(key, peaks);
        m_ready.insert(key, std::move(peaks));
    } else {
        m_failed.insert(key);
    }
    emit peaksReady(filePath);
    startNext();
}

// The ".peaks" file: magic "OVWF", version, then the peak and RMS lists as
// little-endian floats. A stored file read back counts as used for the media
// cache's DaysToKeep.
bool WaveformCache::loadFromDisk(const QString& key)
{
    if (m_store.directory.isEmpty()) return false;
    QFile file(QDir(m_store.directory).filePath(key + QStringLiteral(".peaks")));
    if (!file.open(QIODevice::ReadOnly)) return false;
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    quint32 magic = 0, version = 0;
    WaveformPeaks peaks;
    stream >> magic >> version >> peaks.peak >> peaks.rms;
    if (stream.status() != QDataStream::Ok || magic != kCacheMagic || version != kCacheVersion
        || peaks.isEmpty() || peaks.peak.size() != peaks.rms.size()) {
        return false;
    }
    m_ready.insert(key, std::move(peaks));
    if (m_store.used) m_store.used(key);
    return true;
}

void WaveformCache::saveToDisk(const QString& key, const WaveformPeaks& peaks) const
{
    if (m_store.directory.isEmpty() || !QDir().mkpath(m_store.directory)) return;
    const QString path = QDir(m_store.directory).filePath(key + QStringLiteral(".peaks"));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return;
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    stream << kCacheMagic << kCacheVersion << peaks.peak << peaks.rms;
    if (stream.status() == QDataStream::Ok && file.commit() && m_store.stored) m_store.stored(key, path);
}

} // namespace media
} // namespace openvegas
