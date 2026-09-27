#include "media/VlcBackend.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibrary>
#include <QSettings>
#include <QElapsedTimer>
#include <QThread>
#include <QtGlobal>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include "core/Log.h"

namespace openvegas {
namespace media {

namespace {

// Where libvlc.dll might be. The order is deliberate: an explicit override
// first, then a copy shipped beside the application, then whatever VLC's own
// installer recorded, then the standard install locations.
QStringList candidateDirectories()
{
    QStringList dirs;

    const QByteArray override = qgetenv("OPENVEGAS_VLC_DIR");
    if (!override.isEmpty()) {
        dirs << QString::fromUtf8(override)
             << QString::fromUtf8(override) + QStringLiteral("/lib");
    }

    dirs << QCoreApplication::applicationDirPath()
         << QCoreApplication::applicationDirPath() + QStringLiteral("/vlc")
         << QCoreApplication::applicationDirPath() + QStringLiteral("/vlc/lib");
#ifdef OPENVEGAS_VLC_RUNTIME_DIR
    dirs << QString::fromUtf8(OPENVEGAS_VLC_RUNTIME_DIR)
         << QString::fromUtf8(OPENVEGAS_VLC_RUNTIME_DIR) + QStringLiteral("/lib");
#endif

#ifdef Q_OS_WIN
    // The installer writes InstallDir under both hives, and a 32-bit VLC on a
    // 64-bit machine lands in the WOW6432Node view - which is exactly the copy
    // this build must not load, so only the native view is consulted.
    for (const char* key : {"HKEY_LOCAL_MACHINE\\SOFTWARE\\VideoLAN\\VLC",
                            "HKEY_CURRENT_USER\\SOFTWARE\\VideoLAN\\VLC"}) {
        QSettings settings(QLatin1String(key), QSettings::NativeFormat);
        const QString installDir = settings.value(QStringLiteral("InstallDir")).toString();
        if (!installDir.isEmpty()) {
            dirs << installDir;
        }
    }

    for (const char* variable : {"ProgramFiles", "ProgramW6432"}) {
        const QByteArray root = qgetenv(variable);
        if (!root.isEmpty()) {
            dirs << QString::fromLocal8Bit(root) + QStringLiteral("/VideoLAN/VLC");
        }
    }
#elif defined(Q_OS_MACOS)
    dirs << QCoreApplication::applicationDirPath() + QStringLiteral("/../Frameworks/vlc/lib")
         << QStringLiteral("/Applications/VLC.app/Contents/MacOS/lib")
         << QStringLiteral("/opt/homebrew/lib") << QStringLiteral("/usr/local/lib");
#else
    dirs << QStringLiteral("/usr/lib") << QStringLiteral("/usr/local/lib");
#endif

    dirs.removeDuplicates();
    return dirs;
}

QStringList libraryFileNames()
{
#ifdef Q_OS_WIN
    return {QStringLiteral("libvlc.dll")};
#elif defined(Q_OS_MAC)
    return {QStringLiteral("libvlc.dylib")};
#else
    return {QStringLiteral("libvlc.so.12"), QStringLiteral("libvlc.so"), QStringLiteral("libvlc.so.5")};
#endif
}

// libVLC finds its plugins relative to the library when it is installed
// normally, but not when it is loaded from an arbitrary directory. Pointing
// VLC_PLUGIN_PATH at the folder next to the library covers both.
void announcePluginPath(const QString& libraryDir)
{
    if (qEnvironmentVariableIsSet("VLC_PLUGIN_PATH")) {
        return;
    }
    for (const QString& suffix : {QStringLiteral("/plugins"), QStringLiteral("/vlc/plugins"), QStringLiteral("/lib/vlc/plugins"), QStringLiteral("/../plugins")}) {
        const QString plugins = QDir::cleanPath(libraryDir + suffix);
        if (QFileInfo::exists(plugins)) {
            qputenv("VLC_PLUGIN_PATH", QDir::toNativeSeparators(plugins).toUtf8()); break;
        }
    }
}

#ifdef Q_OS_WIN

// LoadLibraryEx rather than QLibrary: libvlc.dll pulls in libvlccore.dll from
// its own directory, and a plain LoadLibrary does not look there. Without
// LOAD_WITH_ALTERED_SEARCH_PATH the load fails on a perfectly good install -
// which is exactly what happened the first time this was tried.
struct NativeLibrary
{
    HMODULE handle = nullptr;
    unsigned long lastError = 0;

    bool load(const QString& path)
    {
        const QString native = QDir::toNativeSeparators(path);
        handle = LoadLibraryExW(reinterpret_cast<const wchar_t*>(native.utf16()), nullptr,
                                QFileInfo(path).isAbsolute() ? LOAD_WITH_ALTERED_SEARCH_PATH : 0);
        if (!handle) {
            lastError = GetLastError();
        }
        return handle != nullptr;
    }

    bool isLoaded() const { return handle != nullptr; }

    void* resolve(const char* name) const
    {
        return handle ? reinterpret_cast<void*>(GetProcAddress(handle, name)) : nullptr;
    }
};

#else

struct NativeLibrary
{
    QLibrary library;
    unsigned long lastError = 0;

    bool load(const QString& path)
    {
        library.setFileName(path);
        return library.load();
    }

    bool isLoaded() const { return library.isLoaded(); }
    void* resolve(const char* name) const
    {
        return reinterpret_cast<void*>(const_cast<QLibrary&>(library).resolve(name));
    }
};

#endif

template <typename Fn>
bool resolve(NativeLibrary& library, const char* name, Fn& target)
{
    target = reinterpret_cast<Fn>(library.resolve(name));
    return target != nullptr;
}

VlcApi loadApi()
{
    VlcApi api;

    static NativeLibrary library;
    const QStringList fileNames = libraryFileNames();

    QStringList tried;
    const QStringList dirs = candidateDirectories();
    for (const QString& dir : dirs) {
        for (const QString& fileName : fileNames) {
            const QString path = QDir(dir).filePath(fileName);
            if (!QFileInfo::exists(path)) continue;
            if (library.load(path)) {
                announcePluginPath(dir);
                api.libraryPath = path;
                break;
            }
            tried << QStringLiteral("%1 (error %2)").arg(path).arg(library.lastError);
        }
        if (library.isLoaded()) break;
    }
    if (!library.isLoaded()) {
        // Last resort: whatever the loader finds on its own search path.
        for (const QString& fileName : fileNames) {
            if (library.load(fileName)) {
                api.libraryPath = fileName;
                break;
            }
        }
    }
    if (!library.isLoaded()) {
        api.error = tried.isEmpty()
                        ? QStringLiteral("libVLC was not found (looked in: %1)")
                              .arg(dirs.join(QStringLiteral("; ")))
                        : QStringLiteral("libVLC could not be loaded: %1")
                              .arg(tried.join(QStringLiteral("; ")));
        return api;
    }

    if (!resolve(library, "libvlc_get_version", api.libvlc_get_version)) {
        api.error = QStringLiteral("%1 is not libVLC").arg(api.libraryPath);
        return api;
    }
    api.version = QString::fromLatin1(api.libvlc_get_version());
    const bool v4 = api.version.startsWith(QLatin1String("4."));
    if (!v4 && !api.version.startsWith(QLatin1String("3."))) {
        api.error = QStringLiteral("Unsupported libVLC version: %1").arg(api.version);
        return api;
    }

    const bool common =
        resolve(library, "libvlc_new", api.libvlc_new)
        && resolve(library, "libvlc_release", api.libvlc_release)
        && resolve(library, "libvlc_errmsg", api.libvlc_errmsg)
        && resolve(library, "libvlc_media_add_option", api.libvlc_media_add_option)
        && resolve(library, "libvlc_media_player_play", api.libvlc_media_player_play)
        && resolve(library, "libvlc_media_player_pause", api.libvlc_media_player_pause)
        && resolve(library, "libvlc_media_player_set_pause", api.libvlc_media_player_set_pause)
        && resolve(library, "libvlc_video_get_size", api.libvlc_video_get_size)
        && resolve(library, "libvlc_video_set_format", api.libvlc_video_set_format)
        && resolve(library, "libvlc_video_set_callbacks", api.libvlc_video_set_callbacks)
        && resolve(library, "libvlc_audio_set_callbacks", api.libvlc_audio_set_callbacks)
        && resolve(library, "libvlc_audio_set_format", api.libvlc_audio_set_format)
        && resolve(library, "libvlc_audio_set_volume", api.libvlc_audio_set_volume)
        && resolve(library, "libvlc_audio_set_mute", api.libvlc_audio_set_mute);
    const bool compatible = v4
        ? bindVlc4(api, [&](const char* name) { return library.resolve(name); })
        : resolve(library, "libvlc_media_new_path", api.libvlc_media_new_path)
        && resolve(library, "libvlc_media_new_location", api.libvlc_media_new_location)
        && resolve(library, "libvlc_media_new_callbacks", api.libvlc_media_new_callbacks)
        && resolve(library, "libvlc_media_release", api.libvlc_media_release)
        && resolve(library, "libvlc_media_parse_with_options", api.libvlc_media_parse_with_options)
        && resolve(library, "libvlc_media_get_duration", api.libvlc_media_get_duration)
        && resolve(library, "libvlc_media_player_new_from_media", api.libvlc_media_player_new_from_media)
        && resolve(library, "libvlc_media_player_release", api.libvlc_media_player_release)
        && resolve(library, "libvlc_media_player_stop", api.libvlc_media_player_stop)
        && resolve(library, "libvlc_media_player_is_playing", api.libvlc_media_player_is_playing)
        && resolve(library, "libvlc_media_player_get_state", api.libvlc_media_player_get_state)
        && resolve(library, "libvlc_media_player_set_time", api.libvlc_media_player_set_time)
        && resolve(library, "libvlc_media_player_get_time", api.libvlc_media_player_get_time)
        && resolve(library, "libvlc_media_player_set_position", api.libvlc_media_player_set_position)
        && resolve(library, "libvlc_media_player_get_length", api.libvlc_media_player_get_length)
        && resolve(library, "libvlc_media_player_will_play", api.libvlc_media_player_will_play);
    const bool ok = common && compatible;
    if (!ok) {
        if (api.error.isEmpty()) api.error = QStringLiteral("libVLC %1 is missing an entry point this build needs")
                        .arg(api.version);
        return api;
    }

    api.available = true;
    return api;
}

} // namespace

const VlcApi& vlc()
{
    static const VlcApi api = [] {
        VlcApi loaded = loadApi();
        if (loaded.available) {
            OV_LOG_INFO(QStringLiteral("libVLC %1 loaded from %2")
                            .arg(loaded.version, loaded.libraryPath));
        } else {
            OV_LOG_WARN(QStringLiteral("Media playback is unavailable: %1").arg(loaded.error));
        }
        return loaded;
    }();
    return api;
}

libvlc_instance_t* vlcInstance()
{
    struct Instance {
        libvlc_instance_t* value;
        ~Instance() { if (value) vlc().libvlc_release(value); }
    };
    static const Instance instance{[] () -> libvlc_instance_t* {
        const VlcApi& api = vlc();
        if (!api.available) {
            return nullptr;
        }
        // No interface, no on-screen title, no plugin cache rebuild noise, and
        // errors only: this is a decoding back end, not a media player.
        const char* options[] = {
            "--no-video-title-show",
            "--no-osd",
            "--no-snapshot-preview",
            "--no-stats",
            "--no-sub-autodetect-file",
            "--quiet",
            "--intf=dummy",
        };
        libvlc_instance_t* created =
            api.libvlc_new(int(sizeof(options) / sizeof(options[0])), options);
        if (!created) {
            OV_LOG_WARN(QStringLiteral("libvlc_new failed: %1")
                            .arg(QString::fromUtf8(api.libvlc_errmsg ? api.libvlc_errmsg() : "")));
        }
        return created;
    }()};
    return instance.value;
}

QString vlcDescription()
{
    const VlcApi& api = vlc();
    if (!api.available) {
        return api.error;
    }
    return QStringLiteral("libVLC %1 (%2)").arg(api.version, api.libraryPath);
}

double vlcMediaDurationSeconds(const QString& path, int timeoutMs)
{
    if (path.isEmpty() || !QFileInfo::exists(path)) return 0;
    auto* instance = vlcInstance();
    if (!instance) return 0;
    const auto& api = vlc();
    const QByteArray native = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath()).toUtf8();
    auto* media = api.libvlc_media_new_path(instance, native.constData());
    if (!media) return 0;
    api.libvlc_media_parse_with_options(media, VlcParseLocal, timeoutMs);
    QElapsedTimer timer; timer.start();
    libvlc_time_t duration;
    do {
        duration = api.libvlc_media_get_duration(media);
        if (duration > 0) break;
        QThread::msleep(10);
    } while (timer.elapsed() < timeoutMs);
    api.libvlc_media_release(media);
    return duration > 0 ? double(duration) / 1000 : 0;
}

} // namespace media
} // namespace openvegas
