#pragma once

#include "media/MediaAsset.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVector>

class QProcess;

namespace openvegas {
namespace media {

QString proxyModeToken(ProxyMode mode);
ProxyMode proxyModeFromToken(const QString& token);

// Where the proxy of a video lives: Options/ProxyDirectoryPath/<project>/
// <hash of path, size and mtime>-<mode>.mp4. A replaced source gets a new
// file; "Delete Project Proxies" removes the project's folder.
QString proxyFilePath(const QString& proxyRoot, const QString& projectName,
                      const QString& sourcePath, ProxyMode mode);

// FFmpeg arguments producing a proxy. Performance halves the frame and keeps
// a short GOP; Quality keeps the size and makes every frame a key frame, so
// scrubbing never decodes a run of predicted frames. `quality` is the Options
// "Proxy quality" (Low/Medium/High), `encoder` libx264 or h264_qsv.
QStringList proxyEncodeArguments(const QString& source, const QString& output, ProxyMode mode,
                                 const QString& quality, const QString& encoder);

// Builds proxies one at a time in the background, as the reference's
// ProxyMediaManager queue does. The file only appears under its final name
// once FFmpeg has succeeded, so a half-written proxy is never read.
class ProxyGenerator : public QObject
{
    Q_OBJECT

public:
    explicit ProxyGenerator(QObject* parent = nullptr);
    ~ProxyGenerator() override;

    void setFfmpeg(const QString& ffmpeg) { m_ffmpeg = ffmpeg; }
    void enqueue(const QString& source, const QString& output, ProxyMode mode,
                 const QString& quality, const QString& encoder);
    bool isPending(const QString& output) const;
    int pendingCount() const;
    void cancelAll();

signals:
    void proxyFinished(const QString& source, const QString& output, bool ok,
                       const QString& error);

private:
    struct Job
    {
        QString source;
        QString output;
        QStringList arguments;
    };
    void startNext();

    QString m_ffmpeg;
    QVector<Job> m_queue;
    Job m_current;
    QPointer<QProcess> m_process;
};

} // namespace media
} // namespace openvegas
