#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QVector>

#include <functional>

class QProcess;

namespace openvegas {
namespace media {

// Where conformed audio is kept: a folder of the media cache, and hooks that
// record each file in the media cache database (MediaCacheDB) - `stored`
// when one has been written, `used` when one is read again - so it ages out
// with DaysToKeepMediaCacheFiles and the cache's size cap.
struct ConformedAudioStore
{
    QString directory;
    std::function<void(const QString& key, const QString& file)> stored;
    std::function<void(const QString& key)> used;
};

// The reference's conformed audio (Marvin's ConformedAudioFileHelper::
// CreateConformedAudioFile, MediaDB::AddConformedFile / GetConformedFilename
// by path, stream and rate): a media file's audio stream decoded once to PCM
// at the project's rate - here 16-bit stereo WAV - in the media cache, which
// playback reads (ConformedAudioFile::GetSamples) instead of decoding the
// compressed media again. Made in the background, one file at a time; keyed
// by the file's path, size, modification time, the stream and the rate, so a
// replaced file is conformed again.
class ConformedAudioCache : public QObject
{
    Q_OBJECT

public:
    explicit ConformedAudioCache(QObject* parent = nullptr);
    ~ConformedAudioCache() override;

    void setStore(const ConformedAudioStore& store) { m_store = store; }
    void setDecoder(const QString& ffmpegPath) { m_ffmpeg = ffmpegPath; }
    // The store caches made from now on start with.
    static void setDefaultStore(const ConformedAudioStore& store);

    static QString keyFor(const QString& filePath, int audioStreamIndex, int sampleRate);
    // The conformed file of the stream at `sampleRate`; empty while it is
    // made (the first call starts that), or when the file has no audio.
    QString conformed(const QString& filePath, int audioStreamIndex, int sampleRate);
    bool isBusy() const { return !m_queue.isEmpty() || m_process; }

signals:
    void conformedReady(const QString& filePath);

private:
    QString fileFor(const QString& key) const;
    void startNext();

    QString m_ffmpeg;
    ConformedAudioStore m_store;
    struct Pending { QString key, path; int stream = -1, rate = 0; };
    QVector<Pending> m_queue;
    QSet<QString> m_queued;
    QSet<QString> m_failed;
    QPointer<QProcess> m_process;
    Pending m_running;
};

} // namespace media
} // namespace openvegas
