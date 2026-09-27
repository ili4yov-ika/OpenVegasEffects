// Signatures and callback layouts are taken from the vendored VLC 4 headers.
#include <vlc/vlc.h>
#include "media/VlcBackend.h"
#include <functional>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <unordered_map>

namespace openvegas::media {
namespace {
struct Functions {
    decltype(&::libvlc_abi_version) abi = nullptr;
    decltype(&::libvlc_media_new_path) path = nullptr;
    decltype(&::libvlc_media_new_location) location = nullptr;
    decltype(&::libvlc_media_new_callbacks) callbacks = nullptr;
    decltype(&::libvlc_media_release) releaseMedia = nullptr;
    decltype(&::libvlc_media_player_new_from_media) player = nullptr;
    decltype(&::libvlc_media_player_release) releasePlayer = nullptr;
    decltype(&::libvlc_media_player_stop_async) stop = nullptr;
    decltype(&::libvlc_media_player_get_state) state = nullptr;
    decltype(&::libvlc_media_player_is_playing) playing = nullptr;
    decltype(&::libvlc_media_player_get_time) time = nullptr;
    decltype(&::libvlc_media_player_get_length) length = nullptr;
    decltype(&::libvlc_media_get_duration) duration = nullptr;
    decltype(&::libvlc_media_player_set_time) setTime = nullptr;
    decltype(&::libvlc_media_player_set_position) setPosition = nullptr;
    decltype(&::libvlc_parser_new) parserNew = nullptr;
    decltype(&::libvlc_parser_destroy) parserDestroy = nullptr;
    decltype(&::libvlc_parser_queue) parserQueue = nullptr;
    decltype(&::libvlc_parser_task_release) taskRelease = nullptr;
} f;
::libvlc_media_player_t* native(libvlc_media_player_t* p) { return reinterpret_cast<::libvlc_media_player_t*>(p); }
::libvlc_media_t* native(libvlc_media_t* m) { return reinterpret_cast<::libvlc_media_t*>(m); }
struct Input {
    int (*open)(void*, void**, uint64_t*);
    ptrdiff_t (*read)(void*, unsigned char*, size_t);
    int (*seek)(void*, uint64_t);
    void (*close)(void*);
    void* opaque;
};
struct OpenInput { std::shared_ptr<Input> input; void* opaque; };
std::mutex contextsMutex;
std::unordered_map<void*, std::shared_ptr<Input>> mediaContexts, playerContexts;
const ::libvlc_media_open_cbs inputCallbacks = {
    0,
    [](void* opaque, void** data, uint64_t* size) -> int {
        std::shared_ptr<Input> input;
        { std::lock_guard<std::mutex> lock(contextsMutex);
          for (const auto& entry : playerContexts) if (entry.second.get() == opaque) { input = entry.second; break; }
          if (!input) for (const auto& entry : mediaContexts) if (entry.second.get() == opaque) { input = entry.second; break; }
        }
        if (!input) return -1;
        void* stream = input->opaque; *size = UINT64_MAX;
        if (input->open && input->open(input->opaque, &stream, size) != 0) return -1;
        *data = new OpenInput{std::move(input), stream}; return 0;
    },
    [](void* opaque, unsigned char* bytes, size_t size) -> ptrdiff_t {
        auto& stream = *static_cast<OpenInput*>(opaque); return stream.input->read(stream.opaque, bytes, size);
    },
    nullptr, // Current application inputs are non-seekable live PCM.
    [](void* opaque) {
        std::unique_ptr<OpenInput> stream(static_cast<OpenInput*>(opaque));
        if (stream->input->close) stream->input->close(stream->opaque);
    }
};
template<typename T> bool symbol(const std::function<void*(const char*)>& resolve, const char* name, T& target) {
    target = reinterpret_cast<T>(resolve(name)); return target != nullptr;
}
}

bool bindVlc4(VlcApi& api, const std::function<void*(const char*)>& resolve) {
    if (!symbol(resolve, "libvlc_abi_version", f.abi)
        || (f.abi() & 0xffff0000) != (LIBVLC_ABI_VERSION_INT & 0xffff0000)) {
        api.error = QStringLiteral("VLC 4 ABI does not match thirdparty/vlc/include; rebuild VLC and application together");
        return false;
    }
#define BIND(member, name) if (!symbol(resolve, #name, f.member)) return false
    BIND(path, libvlc_media_new_path); BIND(location, libvlc_media_new_location);
    BIND(callbacks, libvlc_media_new_callbacks); BIND(releaseMedia, libvlc_media_release);
    BIND(player, libvlc_media_player_new_from_media); BIND(releasePlayer, libvlc_media_player_release);
    BIND(stop, libvlc_media_player_stop_async); BIND(state, libvlc_media_player_get_state);
    BIND(playing, libvlc_media_player_is_playing); BIND(time, libvlc_media_player_get_time);
    BIND(length, libvlc_media_player_get_length); BIND(duration, libvlc_media_get_duration);
    BIND(setTime, libvlc_media_player_set_time); BIND(setPosition, libvlc_media_player_set_position);
    BIND(parserNew, libvlc_parser_new); BIND(parserDestroy, libvlc_parser_destroy);
    BIND(parserQueue, libvlc_parser_queue); BIND(taskRelease, libvlc_parser_task_release);
#undef BIND
    api.libvlc_media_new_path = [](libvlc_instance_t*, const char* path) {
        return reinterpret_cast<libvlc_media_t*>(f.path(path));
    };
    api.libvlc_media_new_location = [](libvlc_instance_t*, const char* url) {
        return reinterpret_cast<libvlc_media_t*>(f.location(url));
    };
    api.libvlc_media_new_callbacks = [](libvlc_instance_t*, auto open, auto read, auto seek, auto close, void* opaque) -> libvlc_media_t* {
        if (seek) return nullptr; // Unsupported inputs are rejected explicitly.
        auto context = std::make_shared<Input>(Input{open, read, seek, close, opaque});
        auto* media = f.callbacks(&inputCallbacks, context.get());
        if (media) { std::lock_guard<std::mutex> lock(contextsMutex); mediaContexts[media] = std::move(context); }
        return reinterpret_cast<libvlc_media_t*>(media);
    };
    api.libvlc_media_player_new_from_media = [](libvlc_media_t* media) {
        auto* player = f.player(reinterpret_cast<::libvlc_instance_t*>(vlcInstance()), native(media), nullptr, nullptr);
        if (player) { std::lock_guard<std::mutex> lock(contextsMutex);
            const auto found = mediaContexts.find(media);
            if (found != mediaContexts.end()) playerContexts[player] = found->second;
        }
        return reinterpret_cast<libvlc_media_player_t*>(player);
    };
    api.libvlc_media_release = [](libvlc_media_t* media) {
        f.releaseMedia(native(media));
        std::lock_guard<std::mutex> lock(contextsMutex); mediaContexts.erase(media);
    };
    api.libvlc_media_player_release = [](libvlc_media_player_t* player) {
        f.releasePlayer(native(player));
        std::lock_guard<std::mutex> lock(contextsMutex); playerContexts.erase(player);
    };
    api.libvlc_media_player_stop = [](libvlc_media_player_t* player) { f.stop(native(player)); };
    api.libvlc_media_player_get_state = [](libvlc_media_player_t* player) -> int {
        switch (f.state(native(player))) {
        case libvlc_NothingSpecial: return VlcNothingSpecial;
        case libvlc_Opening: return VlcOpening;
        case libvlc_Playing: return VlcPlaying;
        case libvlc_Paused: return VlcPaused;
        case libvlc_Error: return VlcError;
        default: return VlcStopped;
        }
    };
    api.libvlc_media_player_is_playing = [](libvlc_media_player_t* player) -> int { return f.playing(native(player)); };
    api.libvlc_media_player_will_play = [](libvlc_media_player_t*) { return 1; };
    api.libvlc_media_player_get_time = [](libvlc_media_player_t* player) -> libvlc_time_t { return f.time(native(player)) / 1000; };
    api.libvlc_media_player_get_length = [](libvlc_media_player_t* player) -> libvlc_time_t { return f.length(native(player)) / 1000; };
    api.libvlc_media_get_duration = [](libvlc_media_t* media) -> libvlc_time_t { return f.duration(native(media)) / 1000; };
    api.libvlc_media_player_set_time = [](libvlc_media_player_t* player, libvlc_time_t time) { f.setTime(native(player), time * 1000, false); };
    api.libvlc_media_player_set_position = [](libvlc_media_player_t* player, float position) { f.setPosition(native(player), position, false); };
    api.libvlc_media_parse_with_options = [](libvlc_media_t* media, int, int timeoutMs) -> int {
        ::libvlc_parser_cfg config{}; config.timeout = int64_t(timeoutMs) * 1000;
        auto* parser = f.parserNew(reinterpret_cast<::libvlc_instance_t*>(vlcInstance()), &config);
        if (!parser) return -1;
        struct Wait { std::mutex mutex; std::condition_variable ready; bool done = false; } wait;
        ::libvlc_parser_cbs callbacks{};
        callbacks.on_parsed = [](void* opaque, ::libvlc_parser_task*, ::libvlc_parser_status_t) {
            auto& state = *static_cast<Wait*>(opaque);
            std::lock_guard<std::mutex> lock(state.mutex); state.done = true; state.ready.notify_all();
        };
        ::libvlc_parser_request_t request{}; request.media = native(media);
        auto* task = f.parserQueue(parser, &request, &callbacks, &wait);
        if (task) { std::unique_lock<std::mutex> lock(wait.mutex);
            wait.ready.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] { return wait.done; });
        }
        f.parserDestroy(parser); // Joins even after timeout; callback stack stays alive.
        if (task) f.taskRelease(task);
        return task ? 0 : -1;
    };
    return true;
}
}
