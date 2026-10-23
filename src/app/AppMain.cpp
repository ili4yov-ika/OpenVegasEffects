#include "app/AppMain.h"
#include "app/FileAssociation.h"
#include "app/GpuDriver.h"
#include "app/PluginFolders.h"

#include "media/AudioWaveform.h"
#include "media/DecodedFrameCache.h"
#include "media/ConformedAudio.h"
#include "app/ProjectDefaults.h"

#include <QFileInfo>
#include <QCoreApplication>
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QMessageBox>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QSurfaceFormat>
#include <QTimer>

#include "core/Log.h"
#include "ui/AutoSave.h"
#include "ui/PromptMessage.h"
#include "ui/SplashScreen.h"
#include "ui/Theme.h"
#include "plugin/PluginManager.h"

namespace openvegas {
namespace app {

namespace {

// Whether an OpenGL 4.1 core context can be created and made current - the
// version every native .hfpl GPU module compiles its GLSL 4.10 for.
// With its renderer and version strings, which the driver check reads.
bool openGl41Available(QString* renderer = nullptr, QString* version = nullptr)
{
    if (qEnvironmentVariableIsSet("OPENVEGAS_FORCE_GPU_WARNING")) return false;
    QSurfaceFormat format;
    format.setVersion(4, 1);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QOffscreenSurface surface;
    surface.setFormat(format);
    surface.create();
    QOpenGLContext context;
    context.setFormat(format);
    if (!surface.isValid() || !context.create() || !context.makeCurrent(&surface)) {
        return false;
    }
    const QSurfaceFormat actual = context.format();
    if (QOpenGLFunctions* gl = context.functions()) {
        const auto read = [gl](GLenum name) {
            const auto* value = reinterpret_cast<const char*>(gl->glGetString(name));
            return value ? QString::fromLatin1(value) : QString();
        };
        if (renderer) *renderer = read(GL_RENDERER);
        if (version) *version = read(GL_VERSION);
    }
    context.doneCurrent();
    return actual.majorVersion() > 4
        || (actual.majorVersion() == 4 && actual.minorVersion() >= 1);
}

// The media cache's size on disk is kept under this: the reference ages its
// files by days only, but decoded frames can fill a disk well within them.
constexpr qint64 kMediaCacheMaxBytes = qint64(4) << 30;
// How much may be written between checks of the cap while running.
constexpr qint64 kMediaCacheCheckBytes = qint64(256) << 20;

void recordMediaCacheFile(cache::CacheDB* database, const QString& key, const QString& file, qint64 bytes)
{
    const qint64 now = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
    cache::CacheEntry entry;
    entry.key = key;
    entry.cacheFile = file;
    entry.createdUtcMs = entry.lastUsedUtcMs = now;
    entry.bytes = bytes;
    database->put(entry);
}

} // namespace

AppMain::AppMain(QObject* parent)
    : QObject(parent)
{
}

AppMain::~AppMain() = default;


int AppMain::run(QApplication* application, const QString& projectToOpen)
{
    const QSettings appearance(QSettings::IniFormat, QSettings::UserScope,
                               Settings::organizationName(), Settings::applicationName());
    if (appearance.value(QStringLiteral("Options/Theme"),
                         QStringLiteral("Dark")).toString() == QLatin1String("System")) {
        application->setStyleSheet(QString());
    } else {
        ui::applyTheme(application);
    }

    // Reference bootstrap (FUN_1401c2ce0) shows its SplashScreen shortly after
    // the theme is set up and completes it just before the event loop starts,
    // so the window never appears under a stale picture.
    ui::SplashScreen splash;
    splash.showSplash();

    splash.setStatus(tr("Preparing folders..."));
    initializeFolders();
    splash.setStatus(tr("Loading settings..."));
    initializeSettings();
    // Project files show their own icon once the program opens them.
    FileAssociation::registerForCurrentUser();
    splash.setStatus(tr("Opening media cache..."));
    initializeCache();
    splash.setStatus(tr("Checking license..."));
    initializeLicense();
    splash.setStatus(tr("Loading plug-ins..."));
    initializePlugins();
    splash.setStatus(tr("Creating project..."));
    initializeProject();

    const bool pluginsMissing = m_pluginManager->allPluginIds().isEmpty();

    splash.setStatus(tr("Building workspace..."));
    initializeUi();
    // Keep the splash up while the (comparatively expensive) panel tree and
    // plugin controls are constructed. QSplashScreen::finish waits until the
    // real window is visible, preventing a blank desktop between both windows.
    splash.complete(m_mainWindow);

    logPluginStatus();

    // The reference checks the GPU once its window exists (FUN_1401c2ce0,
    // "%1 : Unsupported GPU" gated by ShowGPUWarning, Continue/Exit). Here the
    // requirement is what the native GPU effects need: an OpenGL 4.1 core
    // context. Then its driver check: Intel integrated graphics with an old
    // driver are warned (app::warnAboutGpuDriver).
    QString renderer, glVersion;
    if (!openGl41Available(&renderer, &glVersion)) {
        OV_LOG_WARN(QStringLiteral("OpenGL 4.1 core profile is not available"));
        const QString name = QCoreApplication::applicationName();
        const auto answer = ui::showPrompt(
            m_mainWindow, QStringLiteral("GPUWarning"), QMessageBox::Warning,
            tr("%1 : Unsupported GPU").arg(name),
            tr("Your computer does not appear to support OpenGL 4.1, which %1 needs to run "
               "its native GPU effects and transitions.<br><br>If you choose to continue, "
               "those effects will not render.<br><br>If you think this is incorrect, please "
               "ensure that you have the latest GPU drivers installed and try again.<br>")
                .arg(name),
            QMessageBox::Ignore | QMessageBox::Abort, QMessageBox::Ignore,
            [](QMessageBox& box) {
                box.setTextFormat(Qt::RichText);
                box.button(QMessageBox::Ignore)->setText(tr("Continue"));
                box.button(QMessageBox::Abort)->setText(tr("Exit"));
            },
            QMessageBox::Ignore);
        if (answer == QMessageBox::Abort) {
            return 0;
        }
    } else if (!app::warnAboutGpuDriver(m_mainWindow, renderer, glVersion)) {
        // "%1 : Outdated GPU Driver", Exit.
        return 0;
    }

    // The reference guards against a few silent startup failures with
    // message boxes. "No plugins loaded" is the one most likely to be hit in a
    // clean layout (the plugin tree simply isn't beside the executable yet),
    // so it is surfaced as a warning rather than dying.
    if (pluginsMissing) {
        QMessageBox::warning(nullptr, QStringLiteral("OpenVegas Effects"),
                             QStringLiteral("No plugins were loaded because they were not found "
                                            "at the expected location.\n\nPlease run Setup to "
                                            "repair the installation."));
    }

    // A session lock in the auto-save folder; one left by a process that is
    // gone means the last session ended abnormally, and its auto-saves are
    // offered for recovery, as the reference does.
    const bool previousSessionCrashed = ui::autosave::beginSession();

    // A path on the command line (shell file association, "Open with") is
    // loaded once the window exists.
    if (!projectToOpen.isEmpty() && m_mainWindow) {
        m_mainWindow->openProject(projectToOpen);
    }
    if (previousSessionCrashed && m_mainWindow) {
        QTimer::singleShot(0, m_mainWindow, [window = m_mainWindow] { window->offerAutoSaveRecovery(); });
    }

    const int code = application->exec();
    ui::autosave::endSession();
    return code;
}

void AppMain::initializeFolders()
{
    QString root =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (root.isEmpty()) {
        OV_LOG_ERROR(QStringLiteral("No usable app data location; using fallback"));
        root = QDir::temp().filePath(QStringLiteral("OpenVegasEffects"));
    }
    m_paths.setRoot(QDir::cleanPath(root));

    const core::Result r = m_paths.ensureCreated();
    if (r.isFailure()) {
        QMessageBox::critical(nullptr, QStringLiteral("OpenVegas Effects"),
                              QStringLiteral("Failed to create user data folders:\n%1").arg(r.message()));
    }
}

void AppMain::initializeSettings()
{
    m_settings = std::make_unique<Settings>();
    m_settings->setOpenedOnceVersion(core::Version(0, 1, 0));

    core::Log::instance().setFile(m_paths.fileIn(UserDataFolder::Presets, QStringLiteral("app.log")));
}

void AppMain::initializeCache()
{
    m_cacheDB = std::make_unique<cache::CacheDB>();

    // The reference keeps the media cache in its own location driven by
    // Options/MediaCacheDB and Options/MediaCacheFiles, not inside the presets
    // folder, and prunes it by Options/DaysToKeepMediaCacheFiles.
    const QString cachePath = m_settings->mediaCacheDbPath();
    const QString cacheFilesDir = m_settings->mediaCacheFilesPath();
    QDir().mkpath(cacheFilesDir);

    // Waveform peaks - the decoded audio's summary the timeline draws - are
    // the media cache's files: kept under MediaCacheFiles and recorded in
    // MediaCacheDB, so a reopened project reads them instead of decoding
    // again and they go after DaysToKeepMediaCacheFiles unused.
    media::WaveformStore peaks;
    peaks.directory = QDir(cacheFilesDir).filePath(QStringLiteral("Peaks"));
    media::WaveformCache::setDefaultStore(peaks);
    // Decoded video frames that were slow to get are kept there too.
    media::DecodedFrameStore frames;
    frames.directory = QDir(cacheFilesDir).filePath(QStringLiteral("Frames"));
    media::DecodedFrameCache::setDefaultStore(frames);
    // Conformed audio - the media's audio decoded once at the project's rate,
    // which playback reads (the reference's ConformedAudioFile).
    media::ConformedAudioStore conformed;
    conformed.directory = QDir(cacheFilesDir).filePath(QStringLiteral("Conformed"));
    media::ConformedAudioCache::setDefaultStore(conformed);

    const core::Result r = m_cacheDB->open(cachePath);
    if (r.isFailure()) {
        OV_LOG_WARN(QStringLiteral("Cache unavailable: %1").arg(r.message()));
        return;
    }
    cache::CacheDB* database = m_cacheDB.get();
    peaks.stored = [database](const QString& key, const QString& file) {
        recordMediaCacheFile(database, QStringLiteral("peaks:") + key, file, QFileInfo(file).size());
    };
    peaks.used = [database](const QString& key) {
        database->touch(QStringLiteral("peaks:") + key,
                        QDateTime::currentDateTimeUtc().toMSecsSinceEpoch());
    };
    media::WaveformCache::setDefaultStore(peaks);
    frames.stored = [database, written = qint64(0)](const QString& key, const QString& file,
                                                    qint64 bytes) mutable {
        recordMediaCacheFile(database, QStringLiteral("frame:") + key, file, bytes);
        written += bytes;
        if (written >= kMediaCacheCheckBytes) {
            written = 0;
            database->pruneToBytes(kMediaCacheMaxBytes);
        }
    };
    frames.used = [database](const QString& key) {
        database->touch(QStringLiteral("frame:") + key,
                        QDateTime::currentDateTimeUtc().toMSecsSinceEpoch());
    };
    media::DecodedFrameCache::setDefaultStore(frames);
    conformed.stored = [database](const QString& key, const QString& file) {
        recordMediaCacheFile(database, QStringLiteral("conformed:") + key, file, QFileInfo(file).size());
        database->pruneToBytes(kMediaCacheMaxBytes);
    };
    conformed.used = [database](const QString& key) {
        database->touch(QStringLiteral("conformed:") + key,
                        QDateTime::currentDateTimeUtc().toMSecsSinceEpoch());
    };
    media::ConformedAudioCache::setDefaultStore(conformed);
    OV_LOG_INFO(QStringLiteral("Cache DB opened at %1 (%2 entries), files in %3")
                    .arg(cachePath)
                    .arg(m_cacheDB->entryCount())
                    .arg(cacheFilesDir));

    const int days = m_settings->daysToKeepMediaCacheFiles();
    if (days > 0) {
        const qint64 cutoff =
            QDateTime::currentDateTimeUtc().addDays(-days).toMSecsSinceEpoch();
        const qint64 pruned = m_cacheDB->pruneOlderThan(cutoff);
        if (pruned > 0) {
            OV_LOG_INFO(QStringLiteral("Cache: pruned %1 entries older than %2 day(s)")
                            .arg(pruned)
                            .arg(days));
        }
    }
    const qint64 overSize = m_cacheDB->pruneToBytes(kMediaCacheMaxBytes);
    if (overSize > 0) {
        OV_LOG_INFO(QStringLiteral("Cache: pruned %1 least recently used entries over the size cap")
                        .arg(overSize));
    }
}

void AppMain::initializeLicense()
{
    m_licenseManager = std::make_unique<license::OpenLicenseManager>();

    const license::LicenseInfo info = m_licenseManager->info();
    OV_LOG_INFO(QStringLiteral("License: status=%1 edition=%2 (product %3, host %4)")
                    .arg(license::toString(info.status),
                         license::toString(info.edition))
                    .arg(static_cast<int>(Settings::productEdition()))
                    .arg(static_cast<int>(license::toHostEdition(Settings::productEdition()))));
}

QVector<QString> AppMain::pluginSearchFolders() const
{
    QVector<QString> pluginDirs;
    const auto add = [&pluginDirs](const QString& folder) {
        const QString path = QDir::cleanPath(folder);
        for (const QString& existing : std::as_const(pluginDirs))
            if (QDir::cleanPath(existing).compare(path, Qt::CaseInsensitive) == 0) return;
        pluginDirs.push_back(folder);
    };
    add(m_paths.pathFor(UserDataFolder::Plugins));
    add(m_paths.pathFor(UserDataFolder::PluginPresets));
    // The VEGAS Effects installation's plugin tree and the folders chosen in
    // Options > Plugins (another FXhome program's among them).
    for (const QString& folder : PluginFolders::scannedFolders()) add(folder);
    // The reference ships its plugin tree beside the executable, so an
    // installed build finds them without anything being copied to user data.
    const QString bundled =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("Plugins"));
    if (QFileInfo::exists(bundled)) add(bundled);
    return pluginDirs;
}

void AppMain::rescanPlugins()
{
    if (!m_pluginManager) return;
    m_pluginManager->setPluginDirs(pluginSearchFolders());
    const core::Result r = m_pluginManager->scan();
    if (r.isFailure()) OV_LOG_WARN(QStringLiteral("Plugin scan failed: %1").arg(r.message()));
    if (m_mainWindow) m_mainWindow->regenerateEffects(m_pluginManager.get());
}

void AppMain::initializePlugins()
{
    m_pluginManager = plugin::createPluginManager();
    const QString pluginsFolder = m_paths.pathFor(UserDataFolder::Plugins);
    m_pluginManager->setPluginDirs(pluginSearchFolders());

    plugin::Callbacks callbacks;
    callbacks.resolveUserFolder = [this](int folderIndex) -> QString {
        return m_paths.pathFor(m_paths.allFolders().value(static_cast<int>(folderIndex)));
    };
    callbacks.logMessage = [](int level, const QString& message) {
        core::Log::instance().write(static_cast<core::LogLevel>(level), QLatin1String("[plugin] ") + message);
    };
    m_pluginManager->setCallbacks(callbacks);

    const core::Result r = m_pluginManager->scan();
    if (r.isFailure()) {
        OV_LOG_WARN(QStringLiteral("Plugin scan failed: %1").arg(r.message()));
    }
    OV_LOG_INFO(QStringLiteral("Plugins folder: %1").arg(pluginsFolder));
}

void AppMain::initializeProject()
{
    m_composition = std::make_shared<composition::Composition>();
    // A new project's composite shot, named in the interface's language.
    m_composition->setName(QCoreApplication::translate("Composition", "Untitled"));
    applyNewProjectDefaults(*m_composition);

    m_mediaManager = std::make_shared<media::MediaManager>();

    m_renderManager = std::make_unique<render::RenderManager>(this);
    m_renderManager->setComposition(m_composition);
    m_renderManager->setMediaManager(m_mediaManager);
}

void AppMain::initializeUi()
{
    m_mainWindow = new ui::MainWindow(this);
    m_mainWindow->bindModel(m_composition, m_mediaManager, m_renderManager.get());
    m_mainWindow->regenerateEffects(m_pluginManager.get());
    m_mainWindow->setPluginRescan([this] { rescanPlugins(); });
    m_mainWindow->show();
    // No plugin anywhere it looks - VEGAS Effects not installed where the
    // reference puts itself: Options opens on its Plugins page, once.
    int found = 0;
    const QString presets = m_paths.pathFor(UserDataFolder::PluginPresets);
    for (const QString& folder : pluginSearchFolders())
        if (QDir::cleanPath(folder).compare(QDir::cleanPath(presets), Qt::CaseInsensitive) != 0)
            found += PluginFolders::pluginCount(folder);
    if (found == 0 && !PluginFolders::promptShown()) {
        PluginFolders::setPromptShown(true);
        QPointer<ui::MainWindow> window = m_mainWindow;
        QTimer::singleShot(0, m_mainWindow, [window] {
            if (window) window->showPluginOptions();
        });
    }
}

void AppMain::logPluginStatus() const
{
    const int count = static_cast<int>(m_pluginManager->allPluginIds().size());
    OV_LOG_INFO(QStringLiteral("Plugin log: %1 effect(s) registered").arg(count));
}

} // namespace app
} // namespace openvegas
