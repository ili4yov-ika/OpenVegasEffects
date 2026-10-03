#include "media/ProxyMedia.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace openvegas {
namespace media {

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
    const int level = quality.compare(QLatin1String("Low"), Qt::CaseInsensitive) == 0 ? 28
        : quality.compare(QLatin1String("High"), Qt::CaseInsensitive) == 0 ? 18 : 23;
    // Frame sizes stay even, which H.264 4:2:0 needs (the reference rejects
    // odd sources outright).
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

ProxyGenerator::ProxyGenerator(QObject* parent)
    : QObject(parent)
    , m_ffmpeg(QStandardPaths::findExecutable(QStringLiteral("ffmpeg")))
{
}

ProxyGenerator::~ProxyGenerator()
{
    cancelAll();
}

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
            QFile::remove(m_current.output);
            ok = QFile::rename(part, m_current.output);
        } else {
            error = tr("Media transcoding failed. (Error code: %1)").arg(code);
            QFile::remove(part);
        }
        emit proxyFinished(m_current.source, m_current.output, ok, error);
        startNext();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart) return;
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
