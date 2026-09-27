#pragma once

#include <QSettings>
#include <QString>
#include <QStringList>
#include <memory>

#include "core/Version.h"
#include "license/LicenseTypes.h"

namespace openvegas {
namespace app {

class Settings
{
public:
    // Organization / application names QSettings keys off. They are constants
    // rather than reads of QCoreApplication so that a setting needed *before*
    // QApplication exists still reaches the same ini file - see
    // enableHighDpiScaling(). main() sets the same pair on the application.
    static QString organizationName() { return QStringLiteral("OpenVegas"); }
    static QString applicationName() { return QStringLiteral("OpenVegasEffects"); }
    static QSettings optionSettings()
    {
        return QSettings(QSettings::IniFormat, QSettings::UserScope,
                         organizationName(), applicationName());
    }

    // An empty path selects the standard per-user ini for the current
    // organization/application names.
    explicit Settings(const QString& configFilePath = {});

    // Reference key "Options/EnableHighDpiScaling" (bool, default true), read
    // by FUN_140233270 and applied to the high-DPI application attributes at
    // 1401c31d9 / 1401c31ec - that is, before the QApplication constructor.
    // Static for exactly that reason: at the point it is needed there is no
    // application object yet, so it opens the ini by the names above.
    static bool enableHighDpiScaling();
    // Viewer preferences from the reference's Preferences Viewer page
    // (checkBoxShowMouseCoordinates / checkBoxEnablePlaybackUpdate). Static,
    // like the scaling flag above, because the widgets that honour them are
    // built before any Settings instance exists.
    static bool showMouseCoordinates();
    static void setShowMouseCoordinates(bool enabled);
    static bool enablePlaybackUpdate();
    static void setEnablePlaybackUpdate(bool enabled);
    static void setEnableHighDpiScaling(bool enabled);

    // Viewer view options. The reference keeps these in its "Options" group and
    // drives them from two places at once - the viewer's own Options menu
    // (toolButtonOption, built by FUN_1408d93d0) and the Preferences Viewer
    // page (FUN_1403a6bb0) - so they are read back, not only written:
    //   ShowCheckerboard2D  1412c7a40   checkerboard behind the frame
    //   BackgroundColor     1412c7a20   what the checkerboard is tinted from
    //   ShowMotionPath      1412c79d8   draw the selected layer's position path
    //   MotionPathKeyFrames 1412c7590   how many keys of it to show
    static bool showCheckerboard2D();
    static void setShowCheckerboard2D(bool enabled);
    // Kept as a colour name rather than a QColor so this header stays clear of
    // QtGui; every caller is a widget and can build the QColor itself.
    static QString viewerBackgroundColor();
    static void setViewerBackgroundColor(const QString& colorName);
    static QString defaultViewerBackgroundColor() { return QStringLiteral("#18181c"); }
    static bool showMotionPath();
    static void setShowMotionPath(bool enabled);
    static int motionPathKeyFrames();
    static void setMotionPathKeyFrames(int frames);

    // Durations edited on Preferences > General. Values are stored as the
    // reference's hh:mm:ss.zzz strings and exposed as seconds to the model.
    static double compositeShotDefaultDurationSeconds();
    static double editorDefaultDurationSeconds();
    static double planeDefaultDurationSeconds();

    // Viewer quality button (toolButtonPlaybackQuality, 141320cf8). The
    // reference stores the four values in its "Options/Profiles" group:
    // PlaybackQualityProfile / PausedQualityProfile name one of the built-in
    // profiles (Final, Draft, Quick, Fastest) and PlaybackDownsampleMode /
    // PausedDownsampleMode one of the resolutions (Antialiased, Full, 1/2, 1/4).
    // Stored under the reference's own labels: an enumerator's numbering is not
    // recoverable from the binary, the labels are.
    static QString playbackQualityProfile();
    static void setPlaybackQualityProfile(const QString& name);
    static QString pausedQualityProfile();
    static void setPausedQualityProfile(const QString& name);
    static QString playbackDownsampleMode();
    static void setPlaybackDownsampleMode(const QString& name);
    static QString pausedDownsampleMode();
    static void setPausedDownsampleMode(const QString& name);

    QString filePath() const { return m_settings->fileName(); }

    core::Version openedOnceVersion() const;
    void setOpenedOnceVersion(const core::Version& version);

    QString lastProjectPath() const;
    void setLastProjectPath(const QString& path);

    // Recent project list shown by the Start panel. Key name mirrors the
    // reference setting "RecentProjects".
    QStringList recentProjects() const;
    void setRecentProjects(const QStringList& paths);

    // Learn sidebar visibility. Key name mirrors the reference setting
    // "learnSidebarIsOpen".
    bool learnSidebarIsOpen() const;
    void setLearnSidebarIsOpen(bool open);

    // Media cache configuration, mirroring the reference. Keys live in the
    // "Options" group under the names the reference uses:
    //   Options/MediaCacheDB              path to the cache database file
    //   Options/MediaCacheFiles           directory holding cached frames
    //   Options/DaysToKeepMediaCacheFiles retention in days
    // Both path getters follow the reference behaviour: they seed the setting
    // on first use and, if the containing directory has since disappeared,
    // fall back to the default and rewrite the stored value.
    QString mediaCacheDbPath();
    void setMediaCacheDbPath(const QString& path);

    QString mediaCacheFilesPath();
    void setMediaCacheFilesPath(const QString& path);

    // Default 30. The reference rejects anything from 366 upwards and resets
    // the stored value back to 30.
    int daysToKeepMediaCacheFiles();
    void setDaysToKeepMediaCacheFiles(int days);

    static int defaultDaysToKeepMediaCacheFiles() { return 30; }
    static int maxDaysToKeepMediaCacheFiles() { return 365; }

    // UI language tag ("", "en", "ru", "ja", "zh_CN"). Empty follows the
    // system locale. Key name mirrors the reference "Options" group.
    // Where the viewer's Export Frame button writes. Reference keys:
    // "Options/SnapshotDirectory", shown in its options as "Default Snapshot
    // Directory:"; its own default is an "ExportSnapshots" folder in the user's
    // home directory.
    QString snapshotDirectory();
    void setSnapshotDirectory(const QString& path);

    QString language() const;
    void setLanguage(const QString& locale);

    int thumbnailCacheSizeMb() const;
    void setThumbnailCacheSizeMb(int mb);

    int autosaveIntervalSeconds() const;
    void setAutosaveIntervalSeconds(int seconds);

    bool turboRenderingEnabled() const;
    void setTurboRenderingEnabled(bool enabled);

    // Product edition for this build. Recovered from the reference: the
    // product edition (5000 = Vegas, 6000 = HitFilm) is stored/used for
    // version strings and activation labels, and is mapped onto the
    // BiffHostEdition (0x9c4 / 0x898) handed to PluginManager::Create via
    // license::toHostEdition. A standalone build is the "Vegas" product.
    static license::ProductEdition productEdition() { return license::ProductEdition::Vegas; }

private:
    std::unique_ptr<QSettings> m_settings;
};

} // namespace app
} // namespace openvegas
