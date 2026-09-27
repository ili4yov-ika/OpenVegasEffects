#pragma once

#include <QByteArray>
#include <QImage>
#include <QMutex>
#include <QSize>
#include <QString>
#include <QWaitCondition>

#include <memory>

#include "media/VlcBackend.h"

namespace openvegas {
namespace media {

// What a video file reports about itself. Filled by probeVideo(); `valid` says
// whether the file could be opened at all.
struct VideoInfo
{
    bool valid = false;
    double durationSeconds = 0.0;
    QSize frameSize;
};

// Reads duration and resolution from a video file.
//
// libVLC parses a file without opening a player: libvlc_media_parse_with_options
// reads the container and its track list, and the call is bounded by a timeout,
// so a file the demuxer cannot handle fails instead of hanging the caller. The
// resolution needs a player, which is why this opens one briefly with video
// rendered into nothing.
//
// Unlike the Qt Multimedia version this replaced, it does not need an event
// loop and does not have to run on the GUI thread.
VideoInfo probeVideo(const QString& filePath, int timeoutMs = 4000);

// A video file held open for repeated frame reads.
//
// The reference keeps a decoder object alive per source - fxh::media::
// FXVideoFrameDecoder is constructed, Initialize()d once for the codec, and
// then fed frame after frame - rather than standing one up per picture. This
// does the same: libVLC opens the file once, decodes into a buffer this class
// owns, and every request is a seek rather than a new pipeline.
//
// libVLC hands pictures over on its own decoder thread, so the buffer and the
// "a frame arrived" flag are guarded; frameAt() blocks on a condition variable
// rather than spinning a nested event loop the way the Qt version had to.
class VideoDecoder
{
public:
    explicit VideoDecoder(const QString& filePath, int timeoutMs = 4000);
    ~VideoDecoder();

    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    bool isValid() const { return m_ready; }
    QString filePath() const { return m_filePath; }
    QSize frameSize() const { return m_frameSize; }
    double durationSeconds() const { return m_durationMs / 1000.0; }

    // Picture shown at `seconds`. Null when the file could not be opened or the
    // seek produced nothing within the timeout.
    QImage frameAt(double seconds);

private:
    // libVLC's three video callbacks. They run on a decoder thread.
    static void* lockCallback(void* opaque, void** planes);
    static void unlockCallback(void* opaque, void* picture, void* const* planes);
    static void displayCallback(void* opaque, void* picture);

    bool open();
    void close();
    // Runs the player until a picture lands or the timeout expires.
    bool waitForPicture(int timeoutMs);

    QString m_filePath;
    int m_timeoutMs;
    bool m_ready = false;

    libvlc_media_player_t* m_player = nullptr;
    QSize m_frameSize;
    qint64 m_durationMs = 0;

    // The buffer libVLC decodes into, and the guard around it. `m_pending`
    // counts pictures delivered since the last request, so a stale frame left
    // over from a previous seek is not mistaken for the answer.
    QMutex m_mutex;
    QWaitCondition m_delivered;
    QByteArray m_buffer;
    int m_pending = 0;

    // Where the player was left after the last read. A request that lands just
    // ahead of it can let the decoder run on rather than seek, which is what
    // makes playing a clip forward cheap.
    qint64 m_playerPositionMs = -1;
    QImage m_lastFrame;
};

// One-shot convenience over VideoDecoder, kept for callers that read a single
// frame and do not want to own a decoder.
QImage decodeVideoFrame(const QString& filePath, double seconds, int timeoutMs = 4000);

} // namespace media
} // namespace openvegas
