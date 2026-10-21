#include "media/ConformedAudio.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace openvegas {
namespace media {

namespace {

ConformedAudioStore& defaultStore()
{
    static ConformedAudioStore store;
    return store;
}

} // namespace

void ConformedAudioCache::setDefaultStore(const ConformedAudioStore& store)
{
    defaultStore() = store;
}

ConformedAudioCache::ConformedAudioCache(QObject* parent)
    : QObject(parent)
    , m_ffmpeg(QStandardPaths::findExecutable(QStringLiteral("ffmpeg")))
    , m_store(defaultStore())
{
}

ConformedAudioCache::~ConformedAudioCache()
{
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(1000);
        QFile::remove(fileFor(m_running.key) + QStringLiteral(".part.wav"));
    }
}

QString ConformedAudioCache::keyFor(const QString& filePath, int audioStreamIndex, int sampleRate)
{
    const QFileInfo info(filePath);
    if (!info.exists()) return {};
    const QByteArray identity = QStringLiteral("%1|%2|%3|audio:%4|rate:%5")
                                    .arg(info.absoluteFilePath())
                                    .arg(info.size())
                                    .arg(info.lastModified().toMSecsSinceEpoch())
                                    .arg(audioStreamIndex)
                                    .arg(sampleRate)
                                    .toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha1).toHex());
}

QString ConformedAudioCache::fileFor(const QString& key) const
{
    return m_store.directory.isEmpty() || key.isEmpty()
        ? QString() : QDir(m_store.directory).filePath(key + QStringLiteral(".wav"));
}

QString ConformedAudioCache::conformed(const QString& filePath, int audioStreamIndex, int sampleRate)
{
    if (m_store.directory.isEmpty() || sampleRate <= 0) return {};
    const QString key = keyFor(filePath, audioStreamIndex, sampleRate);
    if (key.isEmpty() || m_failed.contains(key)) return {};
    const QString file = fileFor(key);
    if (QFileInfo(file).isFile() && QFileInfo(file).size() > 44) {
        if (m_store.used) m_store.used(key);
        return file;
    }
    if (!m_queued.contains(key) && !m_ffmpeg.isEmpty()) {
        m_queued.insert(key);
        m_queue.append({key, filePath, audioStreamIndex, sampleRate});
        startNext();
    }
    return {};
}

void ConformedAudioCache::startNext()
{
    if (m_process || m_queue.isEmpty()) return;
    m_running = m_queue.takeFirst();
    QDir().mkpath(m_store.directory);
    const QString partial = fileFor(m_running.key) + QStringLiteral(".part.wav");
    QStringList arguments {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-nostdin"),
                           QStringLiteral("-y"), QStringLiteral("-i"), m_running.path};
    if (m_running.stream >= 0)
        arguments << QStringLiteral("-map") << QStringLiteral("0:a:%1").arg(m_running.stream);
    arguments << QStringLiteral("-vn") << QStringLiteral("-ac") << QStringLiteral("2")
              << QStringLiteral("-ar") << QString::number(m_running.rate)
              << QStringLiteral("-c:a") << QStringLiteral("pcm_s16le") << partial;
    auto* process = new QProcess(this);
    m_process = process;
    connect(process, &QProcess::finished, this, [this, process, partial](int code, QProcess::ExitStatus status) {
        const Pending done = m_running;
        m_process = nullptr;
        process->deleteLater();
        m_queued.remove(done.key);
        const QString file = fileFor(done.key);
        if (status == QProcess::NormalExit && code == 0 && QFileInfo(partial).size() > 44) {
            QFile::remove(file);
            if (QFile::rename(partial, file)) {
                if (m_store.stored) m_store.stored(done.key, file);
                emit conformedReady(done.path);
            } else {
                m_failed.insert(done.key);
            }
        } else {
            QFile::remove(partial);
            m_failed.insert(done.key);   // no audio, or nothing FFmpeg can read
        }
        startNext();
    });
    process->start(m_ffmpeg, arguments);
    if (!process->waitForStarted(5000)) {
        // No FFmpeg to run: nothing is conformed, playback reads the media.
        process->disconnect(this);
        m_process = nullptr;
        process->deleteLater();
        m_queued.remove(m_running.key);
        m_failed.insert(m_running.key);
        startNext();
    }
}

} // namespace media
} // namespace openvegas
