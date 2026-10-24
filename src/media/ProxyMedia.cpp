#include "media/ProxyMedia.h"
#include "app/OptionValues.h"
#include <QSaveFile>

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

// Proxy media: smaller H.264 copies of video sources that the viewer can
// decode instead of the originals (Media > Proxy, Options' proxy folder,
// quality and encoder). FFmpeg makes them one at a time in the background;
// each is written to "<proxy>.part" and moved into place only when complete,
// so a cancelled or failed transcode never leaves a proxy that looks valid.

namespace openvegas {
namespace media {

// The modes as the project file and the settings spell them.
QString proxyModeToken(ProxyMode mode)
{
    switch (mode) {
    case ProxyMode::Performance: return QStringLiteral("Performance");
    case ProxyMode::Quality:     return QStringLiteral("Quality");
    case ProxyMode::None:        break;
    }
    return QStringLiteral("None");
}

ProxyMode proxyModeFromToken(const QString& token)
{
    if (token.compare(QLatin1String("Performance"), Qt::CaseInsensitive) == 0) {
        return ProxyMode::Performance;
    }
    if (token.compare(QLatin1String("Quality"), Qt::CaseInsensitive) == 0) {
        return ProxyMode::Quality;
    }
    return ProxyMode::None;
}

// Where a source's proxy lives: <root>/<project>/<name>-<hash>-<mode>.mp4.
// The hash covers the source's path, size and modification time, so editing
// or replacing the source gives a new proxy instead of reusing a stale one.
QString proxyFilePath(const QString& proxyRoot, const QString& projectName,
                      const QString& sourcePath, ProxyMode mode)
{
    if (proxyRoot.isEmpty() || mode == ProxyMode::None) return {};
    const QFileInfo source(sourcePath);
    const QByteArray identity = QStringLiteral("%1|%2|%3")
                                    .arg(source.absoluteFilePath())
                                    .arg(source.size())
                                    .arg(source.lastModified().toMSecsSinceEpoch())
                                    .toUtf8();
    const QString hash = QString::fromLatin1(
        QCryptographicHash::hash(identity, QCryptographicHash::Sha1).toHex().left(16));
    const QString project = projectName.isEmpty() ? QStringLiteral("Untitled") : projectName;
    return QDir(QDir(proxyRoot).filePath(project))
        .filePath(QStringLiteral("%1-%2-%3.mp4")
                      .arg(source.completeBaseName(), hash, proxyModeToken(mode).toLower()));
}

QStringList proxyEncodeArguments(const QString& source, const QString& output, ProxyMode mode,
                                 const QString& quality, const QString& encoder)
{
    // The CRF (or QSV global quality) of each proxy quality: lower is better.
    const QString token = app::proxyQualityToken(quality);
    const int level = token == QLatin1String("Low") ? 28
        : token == QLatin1String("High") ? 18 : 23;
    // Performance proxies are a quarter of the source's width and height,
    // Quality ones half. Frame sizes stay even, which H.264 4:2:0 needs (the
    // reference rejects odd sources outright). The first video stream only,
    // no audio: the sound always comes from the original. Quality proxies
    // are all keyframes (-g 1), so scrubbing never waits for a GOP.
    const QString scale = mode == ProxyMode::Performance
        ? QStringLiteral("scale=trunc(iw/4)*2:trunc(ih/4)*2")
        : QStringLiteral("scale=trunc(iw/2)*2:trunc(ih/2)*2");
    QStringList arguments {QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"),
                           QStringLiteral("-nostdin"), QStringLiteral("-i"), source,
                           QStringLiteral("-map"), QStringLiteral("0:v:0"),
                           QStringLiteral("-an"), QStringLiteral("-vf"), scale,
                           QStringLiteral("-g"),
                           mode == ProxyMode::Quality ? QStringLiteral("1") : QStringLiteral("15")};
    if (encoder == QLatin1String("h264_qsv")) {
        arguments << QStringLiteral("-c:v") << encoder << QStringLiteral("-global_quality")
                  << QString::number(level) << QStringLiteral("-pix_fmt") << QStringLiteral("nv12");
    } else {
        arguments << QStringLiteral("-c:v") << QStringLiteral("libx264")
                  << QStringLiteral("-preset") << QStringLiteral("veryfast")
                  << QStringLiteral("-crf") << QString::number(level)
                  << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p");
    }
    arguments << QStringLiteral("-f") << QStringLiteral("mp4") << output;
    return arguments;
}

// Moves a finished transcode into place: copied through QSaveFile, so an
// existing proxy is replaced atomically or not at all, then the temporary
// file is removed. False, with the reason, when anything fails.
bool publishProxyFile(const QString& completedPath, const QString& outputPath, QString* error)
{
    if (error) error->clear();
    const auto fail = [error](const QString& message) {
        if (error) *error = message;
        return false;
    };
    QFile input(completedPath);
    if (!input.open(QIODevice::ReadOnly)) return fail(input.errorString());
    if (input.size() <= 0) return fail(ProxyGenerator::tr("The generated proxy file is empty."));
    const QString source = QFileInfo(input).canonicalFilePath();
    const QString target = QFileInfo(outputPath).canonicalFilePath();
    if (!target.isEmpty() && source == target)
        return fail(ProxyGenerator::tr("The proxy destination must differ from the temporary file."));
    QSaveFile output(outputPath);
    output.setDirectWriteFallback(false); // Never truncate an existing proxy.
    if (!output.open(QIODevice::WriteOnly)) return fail(output.errorString());
    while (!input.atEnd()) {
        const QByteArray bytes = input.read(1024 * 1024);
        if (bytes.isEmpty()) return fail(input.errorString());
        if (output.write(bytes) != bytes.size()) return fail(output.errorString());
    }
    if (!output.commit()) return fail(output.errorString());
    input.close();
    QFile::remove(completedPath);
    return true;
}

ProxyGenerator::ProxyGenerator(QObject* parent)
    : QObject(parent)
    , m_ffmpeg(QStandardPaths::findExecutable(QStringLiteral("ffmpeg")))
{
}

ProxyGenerator::~ProxyGenerator()
{
    cancelAll();
}

// Queues a proxy unless the same output is already queued or being made.
void ProxyGenerator::enqueue(const QString& source, const QString& output, ProxyMode mode,
                             const QString& quality, const QString& encoder)
{
    if (output.isEmpty() || isPending(output)) return;
    m_queue.append({source, output,
                    proxyEncodeArguments(source, output + QStringLiteral(".part"), mode,
                                         quality, encoder)});
    startNext();
}

bool ProxyGenerator::isPending(const QString& output) const
{
    if (m_process && m_current.output == output) return true;
    for (const Job& job : m_queue) {
        if (job.output == output) return true;
    }
    return false;
}

int ProxyGenerator::pendingCount() const
{
    return int(m_queue.size()) + (m_process ? 1 : 0);
}

// Drops the queue and kills the running transcode, removing its partial file.
// The killed job reports nothing: its signals are disconnected first.
void ProxyGenerator::cancelAll()
{
    m_queue.clear();
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(2000);
        QFile::remove(m_current.output + QStringLiteral(".part"));
        m_process->deleteLater();
        m_process.clear();
    }
}

// Starts the next queued job when none is running. Jobs that cannot start
// (no ffmpeg on PATH, no proxy folder) are reported as failed at once.
void ProxyGenerator::startNext()
{
    if (m_process || m_queue.isEmpty()) return;
    m_current = m_queue.takeFirst();
    if (m_ffmpeg.isEmpty()) {
        emit proxyFinished(m_current.source, m_current.output, false,
                           tr("Could not launch the transcoder process."));
        startNext();
        return;
    }
    if (!QDir().mkpath(QFileInfo(m_current.output).absolutePath())) {
        emit proxyFinished(m_current.source, m_current.output, false,
                           tr("Could not create the proxy directory."));
        startNext();
        return;
    }
    auto* process = new QProcess(this);
    m_process = process;
    connect(process, &QProcess::finished, this,
            [this, process](int code, QProcess::ExitStatus status) {
        process->deleteLater();
        m_process.clear();
        const QString part = m_current.output + QStringLiteral(".part");
        bool ok = status == QProcess::NormalExit && code == 0;
        QString error;
        if (ok) {
            ok = publishProxyFile(part, m_current.output, &error);
            if (!ok) QFile::remove(part);
        } else {
            error = tr("Media transcoding failed. (Error code: %1)").arg(code);
            QFile::remove(part);
        }
        emit proxyFinished(m_current.source, m_current.output, ok, error);
        startNext();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart) return;
        QFile::remove(m_current.output + QStringLiteral(".part"));
        process->deleteLater();
        m_process.clear();
        emit proxyFinished(m_current.source, m_current.output, false,
                           tr("Could not launch the transcoder process."));
        startNext();
    });
    process->start(m_ffmpeg, m_current.arguments);
}

} // namespace media
} // namespace openvegas
