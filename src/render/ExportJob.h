#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QSize>
#include <QString>

#include <memory>

class QProcess;
class QTemporaryDir;

namespace openvegas {
namespace composition {
class Composition;
}
namespace media {
class MediaManager;
}
namespace render {

class RenderManager;

// One export of a shot's frame range to a file: what Export Contents and the
// export queue both run. Frames come from a RenderManager of its own - so a
// shot other than the one on screen can be exported while work goes on - each
// rendered twice (the first pass lets on-demand video decoding catch up),
// kept as PNG, and then turned into the target: an image sequence for .png,
// .jpg or .exr, or a movie through FFmpeg with the shot's audio mixed in for
// .mp4 (H.264) and .mov (ProRes).
class ExportJob : public QObject
{
    Q_OBJECT

public:
    struct Request
    {
        std::shared_ptr<composition::Composition> composition;
        std::shared_ptr<media::MediaManager> media;
        QString outputPath;          // its suffix picks the format
        int firstFrame = 0;
        int lastFrame = 0;           // inclusive
        QString preRenderDirectory;
        bool hardwareEncoding = true;
    };

    explicit ExportJob(Request request, QObject* parent = nullptr);
    ~ExportJob() override;

    void start();
    // Stops rendering or encoding; finished() follows with ok = false.
    void cancel();
    bool isRunning() const { return m_running; }
    int frameCount() const { return m_request.lastFrame - m_request.firstFrame + 1; }
    int framesDone() const { return m_done; }
    qint64 elapsedMilliseconds() const { return m_clock.isValid() ? m_clock.elapsed() : 0; }
    const Request& request() const { return m_request; }

signals:
    void progress(int done, int total);
    // Every frame is rendered; FFmpeg (or the sequence writer) takes over.
    void encoding();
    void finished(bool ok, const QString& message);

private:
    void requestNext();
    void consume(int frameIndex, const QByteArray& rgba, const QSize& size);
    void encode();
    void writeSequence();
    void finish(bool ok, const QString& message);

    Request m_request;
    std::unique_ptr<RenderManager> m_renderer;
    std::unique_ptr<QTemporaryDir> m_temporary;
    QPointer<QProcess> m_process;
    QElapsedTimer m_clock;
    int m_frame = -1;
    int m_done = 0;
    bool m_primed = false;
    bool m_running = false;
};

} // namespace render
} // namespace openvegas
