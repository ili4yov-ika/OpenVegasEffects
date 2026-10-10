#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QSize>
#include <QString>
#include <QThreadPool>

#include <memory>
#include <vector>

#include "render/ExportPreset.h"

class QImage;
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
// export queue both run. Frames come from RenderManagers of its own - so a
// shot other than the one on screen can be exported while work goes on -
// several frames at once (Options/RenderThreads), each rendered again until
// on-demand video decoding has caught up with it, kept as PNG by a writer
// pool, and then turned into the target: an image sequence for .png, .jpg or
// .exr, or a movie through FFmpeg with the shot's audio mixed in for .mp4
// (H.264) and .mov (ProRes).
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
        // Options/RenderThreads: frames rendered at once; 0 picks by the
        // machine's cores.
        int renderThreads = 0;
        // Options/TurboRendering: the timeline cache directory, whose frames
        // of the same shot state, size and media are kept rather than
        // rendered again. Empty renders every frame.
        QString cacheDirectory;
        // The Export panel's preset: format, size, frame rate, codecs and
        // rates. Without one the output path's suffix picks the format, as
        // tasks queued before presets did.
        ExportPreset preset;
    };

    explicit ExportJob(Request request, QObject* parent = nullptr);
    ~ExportJob() override;

    void start();
    // Stops rendering or encoding; finished() follows with ok = false.
    void cancel();
    bool isRunning() const { return m_running; }
    int frameCount() const { return m_request.lastFrame - m_request.firstFrame + 1; }
    int framesDone() const { return m_done; }
    // Frames taken from the timeline cache (Request::cacheDirectory).
    int framesFromCache() const;
    // How many frames are rendered at once for a renderThreads setting.
    static int rendererCount(int renderThreads, int frames);
    qint64 elapsedMilliseconds() const { return m_clock.isValid() ? m_clock.elapsed() : 0; }
    const Request& request() const { return m_request; }

signals:
    void progress(int done, int total);
    // Every frame is rendered; FFmpeg (or the sequence writer) takes over.
    void encoding();
    void finished(bool ok, const QString& message);

private:
    // A renderer and the frame it is working on.
    struct Renderer
    {
        std::unique_ptr<RenderManager> manager;
        int frame = -1;
        QElapsedTimer waiting;   // since the frame first came back incomplete
    };
    void requestNext(Renderer& renderer);
    void render(Renderer& renderer);
    void consume(Renderer& renderer, int frameIndex, const QByteArray& rgba, const QSize& size);
    void written(int frame, bool ok);
    void encode();
    void writeSequence();
    // An image-sequence frame as the preset wants it: its size by its scale
    // mode, on black unless it keeps alpha.
    static QImage sequenceFrame(const QImage& frame, const ExportPreset& preset, const QSize& source,
                                double pixelAspect);
    void finish(bool ok, const QString& message);

    Request m_request;
    std::vector<std::unique_ptr<Renderer>> m_renderers;
    std::unique_ptr<QTemporaryDir> m_temporary;
    QPointer<QProcess> m_process;
    QThreadPool m_writers;       // PNG frames are written off the GUI thread
    QElapsedTimer m_clock;
    int m_next = 0;              // the next frame no renderer has taken
    int m_writing = 0;           // frames handed to the writers, not yet written
    int m_done = 0;
    // Cleared once decoding has failed to deliver a frame in time, so later
    // incomplete frames are not waited on as long.
    bool m_patient = true;
    bool m_running = false;
};

} // namespace render
} // namespace openvegas
