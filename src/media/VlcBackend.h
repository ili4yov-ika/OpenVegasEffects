#pragma once

#include <QString>
#include <QStringList>
#include "media/MediaStreamInfo.h"

#include <cstdint>
#include <cstddef>
#include <functional>

namespace openvegas {
namespace media {

// Binding to libVLC, loaded at run time.
//
// Both playback and video decoding share this backend. VLC 3 is supported
// through its stable C ABI; VLC 4 adapters use the vendored headers in
// thirdparty/vlc/include. No VLC import library is required.
// Opaque handles; libVLC never exposes their contents.
struct libvlc_instance_t;
struct libvlc_media_t;
struct libvlc_media_player_t;

using libvlc_time_t = int64_t;

// Values of libvlc_state_t that this port tests for.
enum VlcState
{
    VlcNothingSpecial = 0,
    VlcOpening = 1,
    VlcBuffering = 2,
    VlcPlaying = 3,
    VlcPaused = 4,
    VlcStopped = 5,
    VlcEnded = 6,
    VlcError = 7,
};

// libvlc_media_parse_flag_t
enum VlcParseFlag
{
    VlcParseLocal = 0x00,
    VlcParseNetwork = 0x01,
};

// Video callbacks, as libVLC declares them.
using VlcLockCallback = void*(*)(void* opaque, void** planes);
using VlcUnlockCallback = void (*)(void* opaque, void* picture, void* const* planes);
using VlcDisplayCallback = void (*)(void* opaque, void* picture);

// The resolved entry points. available is false if loading or ABI binding failed.
struct VlcApi
{
    bool available = false;
    QString version;      // what libvlc_get_version() reported
    QString libraryPath;  // where libvlc.dll was found
    QString error;        // why it is not available, when it is not

    libvlc_instance_t* (*libvlc_new)(int argc, const char* const* argv) = nullptr;
    void (*libvlc_release)(libvlc_instance_t*) = nullptr;
    const char* (*libvlc_get_version)() = nullptr;
    const char* (*libvlc_errmsg)() = nullptr;

    libvlc_media_t* (*libvlc_media_new_path)(libvlc_instance_t*, const char* path) = nullptr;
    void (*libvlc_media_release)(libvlc_media_t*) = nullptr;
    int (*libvlc_media_parse_with_options)(libvlc_media_t*, int flags, int timeoutMs) = nullptr;
    libvlc_time_t (*libvlc_media_get_duration)(libvlc_media_t*) = nullptr;

    libvlc_media_player_t* (*libvlc_media_player_new_from_media)(libvlc_media_t*) = nullptr;
    void (*libvlc_media_player_release)(libvlc_media_player_t*) = nullptr;
    int (*libvlc_media_player_play)(libvlc_media_player_t*) = nullptr;
    void (*libvlc_media_player_pause)(libvlc_media_player_t*) = nullptr;
    void (*libvlc_media_player_set_pause)(libvlc_media_player_t*, int doPause) = nullptr;
    void (*libvlc_media_player_stop)(libvlc_media_player_t*) = nullptr;
    int (*libvlc_media_player_is_playing)(libvlc_media_player_t*) = nullptr;
    int (*libvlc_media_player_get_state)(libvlc_media_player_t*) = nullptr;
    libvlc_time_t (*libvlc_media_player_get_time)(libvlc_media_player_t*) = nullptr;
    void (*libvlc_media_player_set_time)(libvlc_media_player_t*, libvlc_time_t) = nullptr;
    float (*libvlc_media_player_get_position)(libvlc_media_player_t*) = nullptr;
    void (*libvlc_media_player_set_position)(libvlc_media_player_t*, float) = nullptr;
    libvlc_time_t (*libvlc_media_player_get_length)(libvlc_media_player_t*) = nullptr;
    int (*libvlc_media_player_will_play)(libvlc_media_player_t*) = nullptr;
    unsigned (*libvlc_media_player_get_role)(libvlc_media_player_t*) = nullptr;

    // Frame size, once the player has opened the stream - avoids declaring
    // libvlc_media_track_t, whose layout is a tagged union.
    int (*libvlc_video_get_size)(libvlc_media_player_t*, unsigned num, unsigned* px,
                                unsigned* py) = nullptr;
    void (*libvlc_video_set_format)(libvlc_media_player_t*, const char* chroma,
                                        unsigned width, unsigned height, unsigned pitch) = nullptr;
    void (*libvlc_video_set_callbacks)(libvlc_media_player_t*, VlcLockCallback, VlcUnlockCallback,
                                       VlcDisplayCallback, void* opaque) = nullptr;

    libvlc_media_t* (*libvlc_media_new_location)(libvlc_instance_t*, const char*) = nullptr;
    void (*libvlc_media_add_option)(libvlc_media_t*, const char*) = nullptr;
    libvlc_media_t* (*libvlc_media_new_callbacks)(
        libvlc_instance_t*, int (*)(void*, void**, uint64_t*), ptrdiff_t (*)(void*, unsigned char*, size_t),
        int (*)(void*, uint64_t), void (*)(void*), void*) = nullptr;
    void (*libvlc_audio_set_callbacks)(libvlc_media_player_t*,
        void (*)(void*, const void*, unsigned, int64_t),
        void (*)(void*, int64_t), void (*)(void*, int64_t),
        void (*)(void*, int64_t), void (*)(void*), void*) = nullptr;
    void (*libvlc_audio_set_format)(libvlc_media_player_t*, const char*, unsigned, unsigned) = nullptr;

    int (*libvlc_audio_set_volume)(libvlc_media_player_t*, int volume) = nullptr;
    void (*libvlc_audio_set_mute)(libvlc_media_player_t*, int mute) = nullptr;

    // A parsed media's video codec and all audio stream descriptions.
    MediaStreams (*mediaStreams)(libvlc_media_t*) = nullptr;
    // VLC 3 parsing is asynchronous; 4 means done, 1/2/3 terminal failures.
    int (*mediaParsedStatus)(libvlc_media_t*) = nullptr;

    // The first video track of a parsed media: its sample aspect ratio and
    // frame rate (libvlc_video_track_t). VLC 3 reads them through
    // libvlc_media_tracks_get, VLC 4 through libvlc_media_get_tracklist; null
    // when neither is there. False without a video track.
    bool (*videoTrackFormat)(libvlc_media_t*, unsigned* sarNum, unsigned* sarDen,
                             unsigned* rateNum, unsigned* rateDen) = nullptr;
};

bool bindVlc4(VlcApi& api, const std::function<void*(const char*)>& resolve);

// Resolves libVLC once and returns the same table on every call. Searching, in
// order: OPENVEGAS_VLC_DIR, the folder holding the executable, the registry
// entry VLC's installer writes, the usual Program Files locations, and finally
// a bare LoadLibrary so an entry on PATH works.
const VlcApi& vlc();

// Shared libVLC instance, created on first use with the options this port
// wants: no interface, no video title, quiet logging. Null when libVLC is
// missing.
libvlc_instance_t* vlcInstance();

// Human-readable line for the log and the About box.
QString vlcDescription();
double vlcMediaDurationSeconds(const QString& path, int timeoutMs = 2000);

} // namespace media
} // namespace openvegas
