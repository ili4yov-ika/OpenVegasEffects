#include "media/VideoProbe.h"

#include <QDeadlineTimer>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QSettings>
#include "app/Settings.h"
#include <QThread>

#include <cstring>

#include "core/Log.h"

namespace openvegas {
namespace media {

namespace {

// A picture within this far of the one asked for is the one asked for. Video is
// not sample-accurate on a seek, and demanding better would mean re-seeking
// forever on a long-GOP file.
constexpr qint64 kAcceptableDriftMs = 250;

// A request this far ahead of where the player already sits is served by letting
// the decoder run on instead of seeking - a seek on a long-GOP file costs a key
// frame and everything after it, and playing a clip forward asks for exactly
// this pattern.
//
// Kept to a few frames rather than the half second it started at. Running on
// means decoding every picture in between and waiting for each, and libVLC
// paces itself to the presentation clock, so a 0.5 s gap cost 1.7 s per frame -
// slower than throwing the decoder away and opening a new one. At 120 ms only a
// genuine step-forward takes this path: 24 ms a frame, against 33 ms for a seek.
//
// Raising the playback rate to decode faster than real time was tried and made
// it worse (681 ms a frame at 16x), so the rate is left alone.
constexpr qint64 kSequentialStepMs = 120;

// libVLC decodes into whatever chroma it is told to. RV32 is the one every
// build has, and its memory order is B, G, R, A - QImage::Format_ARGB32 on a
// little-endian machine.
constexpr const char* kChroma = "RV32";

// A player opened only to be asked what size its video is needs somewhere to
// put the pictures it decodes on the way; this swallows them.
struct SinkContext
{
    QByteArray buffer;
};

void* discardLock(void* opaque, void** planes)
{
    auto* sink = static_cast<SinkContext*>(opaque);
    *planes = sink->buffer.data();
    return nullptr;
}

void discardUnlock(void*, void*, void* const*) {}
void discardDisplay(void*, void*) {}

// Opens a player on `filePath` purely to learn the stream's size, which libVLC
// only knows once the decoder has started.
QSize readFrameSize(const QString& filePath, int timeoutMs)
{
    const VlcApi& api = vlc();
    libvlc_instance_t* instance = vlcInstance();
    if (!instance) {
        return QSize();
    }
    const QByteArray path = QDir::toNativeSeparators(filePath).toUtf8();
    libvlc_media_t* media = api.libvlc_media_new_path(instance, path.constData());
    if (!media) {
        return QSize();
    }
    libvlc_media_player_t* player = api.libvlc_media_player_new_from_media(media);
    api.libvlc_media_release(media);
    if (!player) {
        return QSize();
    }

    // Small fixed surface: the size question is answered by
    // libvlc_video_get_size, which reports the stream's own dimensions, not the
    // surface's, so there is no reason to allocate a full frame here.
    SinkContext sink;
    sink.buffer.resize(64 * 64 * 4);
    api.libvlc_video_set_format(player, kChroma, 64, 64, 64 * 4);
    api.libvlc_video_set_callbacks(player, discardLock, discardUnlock, discardDisplay, &sink);
    api.libvlc_audio_set_mute(player, 1);

    QSize size;
    if (api.libvlc_media_player_play(player) == 0) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < timeoutMs) {
            unsigned width = 0;
            unsigned height = 0;
            if (api.libvlc_video_get_size(player, 0, &width, &height) == 0 && width > 0
                && height > 0) {
                size = QSize(int(width), int(height));
                break;
            }
            const int state = api.libvlc_media_player_get_state(player);
            if (state == VlcError || state == VlcEnded) {
                break;
            }
            QThread::msleep(10);
        }
    }
    api.libvlc_media_player_stop(player);
    api.libvlc_media_player_release(player);
    return size;
}

} // namespace

VideoInfo probeVideo(const QString& filePath, int timeoutMs)
{
    VideoInfo info;
    const VlcApi& api = vlc();
    if (!api.available) {
        return info;
    }
    libvlc_instance_t* instance = vlcInstance();
    if (!instance || !QFileInfo::exists(filePath)) {
        return info;
    }

    const QByteArray path = QDir::toNativeSeparators(filePath).toUtf8();
    libvlc_media_t* media = api.libvlc_media_new_path(instance, path.constData());
    if (!media) {
        return info;
    }

    // Parsing reads the container; it is bounded, so an unreadable file fails
    // rather than blocking.
    api.libvlc_media_parse_with_options(media, VlcParseLocal, timeoutMs);
    QElapsedTimer timer;
    timer.start();
    libvlc_time_t duration = 0;
    while (timer.elapsed() < timeoutMs) {
        duration = api.libvlc_media_get_duration(media);
        if (duration > 0) {
            break;
        }
        QThread::msleep(10);
    }
    api.libvlc_media_release(media);

    if (duration <= 0) {
        return info;
    }
    info.durationSeconds = double(duration) / 1000.0;
    info.frameSize = readFrameSize(filePath, timeoutMs);
    info.valid = true;
    return info;
}

// ---------------------------------------------------------------------------
// VideoDecoder
// ---------------------------------------------------------------------------

VideoDecoder::VideoDecoder(const QString& filePath, int timeoutMs)
    : m_filePath(filePath)
    , m_timeoutMs(timeoutMs)
{
    m_ready = open();
}

VideoDecoder::~VideoDecoder()
{
    close();
}

void* VideoDecoder::lockCallback(void* opaque, void** planes)
{
    auto* self = static_cast<VideoDecoder*>(opaque);
    // Held across the decode so a frameAt() reading the buffer cannot see a
    // half-written picture. Released in unlockCallback.
    self->m_mutex.lock();
    *planes = self->m_buffer.data();
    return nullptr;
}

void VideoDecoder::unlockCallback(void* opaque, void*, void* const*)
{
    auto* self = static_cast<VideoDecoder*>(opaque);
    self->m_mutex.unlock();
}

void VideoDecoder::displayCallback(void* opaque, void*)
{
    auto* self = static_cast<VideoDecoder*>(opaque);
    QMutexLocker lock(&self->m_mutex);
    ++self->m_pending;
    self->m_delivered.wakeAll();
}

bool VideoDecoder::open()
{
    const VlcApi& api = vlc();
    if (!api.available) {
        return false;
    }
    libvlc_instance_t* instance = vlcInstance();
    if (!instance || !QFileInfo::exists(m_filePath)) {
        return false;
    }

    const VideoInfo info = probeVideo(m_filePath, m_timeoutMs);
    if (!info.valid || info.frameSize.isEmpty()) {
        return false;
    }
    m_frameSize = info.frameSize;
    m_durationMs = qint64(info.durationSeconds * 1000.0);

    const QByteArray path = QDir::toNativeSeparators(m_filePath).toUtf8();
    libvlc_media_t* media = api.libvlc_media_new_path(instance, path.constData());
    if (!media) {
        return false;
    }
    const QSettings settings = app::Settings::optionSettings();
    const bool hardware = settings.value(QStringLiteral("Options/UseHardwareDecoding"), true).toBool();
    const QByteArray acceleration = api.version.startsWith(QLatin1String("4."))
        ? (hardware ? QByteArray(":hw-dec") : QByteArray(":no-hw-dec"))
        : (hardware ? QByteArray(":avcodec-hw=any") : QByteArray(":avcodec-hw=none"));
    api.libvlc_media_add_option(media, acceleration.constData());
    const int threads = qBound(0, settings.value(QStringLiteral("Options/RenderThreads"), 0).toInt(), 64);
    const QByteArray threadOption = QByteArray(":avcodec-threads=") + QByteArray::number(threads);
    api.libvlc_media_add_option(media, threadOption.constData());
    m_player = api.libvlc_media_player_new_from_media(media);
    api.libvlc_media_release(media);
    if (!m_player) {
        return false;
    }

    const int width = m_frameSize.width();
    const int height = m_frameSize.height();
    m_buffer.resize(width * height * 4);
    api.libvlc_video_set_format(m_player, kChroma, unsigned(width), unsigned(height),
                                unsigned(width * 4));
    api.libvlc_video_set_callbacks(m_player, &VideoDecoder::lockCallback,
                                   &VideoDecoder::unlockCallback, &VideoDecoder::displayCallback,
                                   this);
    // A decoder, not a player: the sound belongs to AudioPlayer.
    api.libvlc_audio_set_mute(m_player, 1);
    api.libvlc_audio_set_volume(m_player, 0);
    return true;
}

void VideoDecoder::close()
{
    if (!m_player) {
        return;
    }
    const VlcApi& api = vlc();
    api.libvlc_media_player_stop(m_player);
    api.libvlc_media_player_release(m_player);
    m_player = nullptr;
}

bool VideoDecoder::waitForPicture(int timeoutMs)
{
    QMutexLocker lock(&m_mutex);
    QDeadlineTimer deadline(timeoutMs);
    while (m_pending == 0) {
        if (!m_delivered.wait(&m_mutex, deadline)) {
            return false;
        }
    }
    return true;
}

QImage VideoDecoder::frameAt(double seconds)
{
    if (!m_ready || !m_player) {
        return QImage();
    }
    const VlcApi& api = vlc();

    qint64 wantedMs = qint64(seconds * 1000.0 + 0.5);
    if (wantedMs < 0) {
        wantedMs = 0;
    }
    if (m_durationMs > 0 && wantedMs >= m_durationMs) {
        wantedMs = m_durationMs - 1;
    }

    {
        QMutexLocker lock(&m_mutex);
        m_pending = 0;
    }

    // Playing forward: let the decoder run to the wanted time instead of
    // seeking to it. A seek costs the key frame and everything up to the
    // target, which during playback is paid once per frame.
    const qint64 delta = wantedMs - m_playerPositionMs;
    const bool sequential =
        m_playerPositionMs >= 0 && delta > 0 && delta <= kSequentialStepMs;
    if (!sequential) {
        api.libvlc_media_player_set_time(m_player, wantedMs);
    }

    // The player has to be running for the decoder to produce anything; it is
    // paused again as soon as a picture lands, so a still frame does not leave
    // a file playing in the background.
    const int state = api.libvlc_media_player_get_state(m_player);
    if (state != VlcPlaying) {
        if (state == VlcNothingSpecial || state == VlcStopped || state == VlcEnded
            || state == VlcError) {
            if (api.libvlc_media_player_play(m_player) != 0) {
                return m_lastFrame;
            }
            // A fresh play starts at zero whatever was asked for before it, so
            // the seek is repeated once the pipeline is up.
            api.libvlc_media_player_set_time(m_player, wantedMs);
        } else {
            api.libvlc_media_player_set_pause(m_player, 0);
        }
    }

    QImage picture;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < m_timeoutMs) {
        if (!waitForPicture(int(m_timeoutMs - timer.elapsed()))) {
            break;
        }
        const qint64 at = api.libvlc_media_player_get_time(m_player);
        // Copy under the lock: the decoder writes into the same buffer.
        {
            QMutexLocker lock(&m_mutex);
            m_pending = 0;
            const QImage view(reinterpret_cast<const uchar*>(m_buffer.constData()),
                              m_frameSize.width(), m_frameSize.height(),
                              m_frameSize.width() * 4, QImage::Format_ARGB32);
            picture = view.convertToFormat(QImage::Format_RGBA8888);
        }
        m_playerPositionMs = at;
        // Close enough, or past it - a seek lands on a key frame and the
        // decoder walks forward, so the first picture at or after the target is
        // the answer.
        if (at + kAcceptableDriftMs >= wantedMs) {
            break;
        }
    }

    api.libvlc_media_player_set_pause(m_player, 1);

    if (picture.isNull()) {
        // Nothing arrived in time; the previous picture is a better answer than
        // a hole, and is what the viewer used to fall back to.
        return m_lastFrame;
    }
    m_lastFrame = picture;
    return picture;
}

QImage decodeVideoFrame(const QString& filePath, double seconds, int timeoutMs)
{
    VideoDecoder decoder(filePath, timeoutMs);
    if (!decoder.isValid()) {
        return QImage();
    }
    return decoder.frameAt(seconds);
}

} // namespace media
} // namespace openvegas
