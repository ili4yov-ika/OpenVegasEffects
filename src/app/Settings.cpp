#include "app/Settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTime>

namespace openvegas {
namespace app {

namespace {
// QSettings(QString(), IniFormat) yields a settings object with no backing
// file, so nothing is ever persisted. Fall back to the standard per-user ini
// derived from the organization/application names when no path is supplied.
QSettings* makeSettings(const QString& configFilePath)
{
    if (!configFilePath.isEmpty()) {
        return new QSettings(configFilePath, QSettings::IniFormat);
    }
    return new QSettings(QSettings::IniFormat, QSettings::UserScope,
                         Settings::organizationName(), Settings::applicationName());
}
} // namespace

bool Settings::enableHighDpiScaling()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, organizationName(),
                       applicationName());
    return settings.value(QStringLiteral("Options/EnableHighDpiScaling"), true).toBool();
}

void Settings::setEnableHighDpiScaling(bool enabled)
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, organizationName(),
                       applicationName());
    settings.setValue(QStringLiteral("Options/EnableHighDpiScaling"), enabled);
}

bool Settings::showMouseCoordinates()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, organizationName(),
                       applicationName());
    return settings.value(QStringLiteral("Options/ShowMouseCoordinates"), false).toBool();
}

void Settings::setShowMouseCoordinates(bool enabled)
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, organizationName(),
                       applicationName());
    settings.setValue(QStringLiteral("Options/ShowMouseCoordinates"), enabled);
}

bool Settings::enablePlaybackUpdate()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, organizationName(),
                       applicationName());
    return settings.value(QStringLiteral("Options/EnablePlaybackUpdate"), true).toBool();
}

void Settings::setEnablePlaybackUpdate(bool enabled)
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, organizationName(),
                       applicationName());
    settings.setValue(QStringLiteral("Options/EnablePlaybackUpdate"), enabled);
}

namespace {
// The viewer options and the quality button are built before any Settings
// instance exists, exactly like the two flags above, so they read the same ini
// through this helper instead of an owned QSettings.
QVariant readOption(const QString& key, const QVariant& fallback)
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, Settings::organizationName(),
                       Settings::applicationName());
    return settings.value(key, fallback);
}

void writeOption(const QString& key, const QVariant& value)
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, Settings::organizationName(),
                       Settings::applicationName());
    settings.setValue(key, value);
}

double durationOption(const QString& key, const QTime& fallback)
{
    const QTime value = QTime::fromString(readOption(
        key, fallback.toString(QStringLiteral("hh:mm:ss.zzz"))).toString(),
        QStringLiteral("hh:mm:ss.zzz"));
    const QTime resolved = value.isValid() ? value : fallback;
    const double seconds = QTime(0, 0).msecsTo(resolved) / 1000.0;
    return seconds > 0.0 ? seconds : QTime(0, 0).msecsTo(fallback) / 1000.0;
}
} // namespace

bool Settings::showCheckerboard2D()
{
    return readOption(QStringLiteral("Options/ShowCheckerboard2D"), true).toBool();
}

void Settings::setShowCheckerboard2D(bool enabled)
{
    writeOption(QStringLiteral("Options/ShowCheckerboard2D"), enabled);
}

QString Settings::viewerBackgroundColor()
{
    const QString stored =
        readOption(QStringLiteral("Options/BackgroundColor"), defaultViewerBackgroundColor())
            .toString();
    return stored.isEmpty() ? defaultViewerBackgroundColor() : stored;
}

void Settings::setViewerBackgroundColor(const QString& colorName)
{
    writeOption(QStringLiteral("Options/BackgroundColor"), colorName);
}

bool Settings::showMotionPath()
{
    return readOption(QStringLiteral("Options/ShowMotionPath"), false).toBool();
}

void Settings::setShowMotionPath(bool enabled)
{
    writeOption(QStringLiteral("Options/ShowMotionPath"), enabled);
}

int Settings::motionPathKeyFrames()
{
    // The reference pairs ShowMotionPath with a key count (spinBoxMotionPathFrames)
    // so a long animation does not bury the frame under its own path.
    const int frames = readOption(QStringLiteral("Options/MotionPathKeyFrames"), 30).toInt();
    return qBound(1, frames, 1000);
}

void Settings::setMotionPathKeyFrames(int frames)
{
    writeOption(QStringLiteral("Options/MotionPathKeyFrames"), qBound(1, frames, 1000));
}

double Settings::compositeShotDefaultDurationSeconds()
{
    return durationOption(QStringLiteral("Options/CompositeShotDefaultDuration"),
                          QTime(0, 0, 30));
}

double Settings::planeDefaultDurationSeconds()
{
    return durationOption(QStringLiteral("Options/PlaneDefaultDuration"),
                          QTime(0, 0, 30));
}

double Settings::editorDefaultDurationSeconds()
{
    return durationOption(QStringLiteral("Options/EditorDefaultDuration"), QTime(0, 5, 0));
}

QString Settings::playbackQualityProfile()
{
    return readOption(QStringLiteral("Options/Profiles/PlaybackQualityProfile"),
                      QStringLiteral("Final"))
        .toString();
}

void Settings::setPlaybackQualityProfile(const QString& name)
{
    writeOption(QStringLiteral("Options/Profiles/PlaybackQualityProfile"), name);
}

QString Settings::pausedQualityProfile()
{
    return readOption(QStringLiteral("Options/Profiles/PausedQualityProfile"),
                      QStringLiteral("Final"))
        .toString();
}

void Settings::setPausedQualityProfile(const QString& name)
{
    writeOption(QStringLiteral("Options/Profiles/PausedQualityProfile"), name);
}

QString Settings::playbackDownsampleMode()
{
    return readOption(QStringLiteral("Options/Profiles/PlaybackDownsampleMode"),
                      QStringLiteral("Full"))
        .toString();
}

void Settings::setPlaybackDownsampleMode(const QString& name)
{
    writeOption(QStringLiteral("Options/Profiles/PlaybackDownsampleMode"), name);
}

QString Settings::pausedDownsampleMode()
{
    return readOption(QStringLiteral("Options/Profiles/PausedDownsampleMode"),
                      QStringLiteral("Full"))
        .toString();
}

void Settings::setPausedDownsampleMode(const QString& name)
{
    writeOption(QStringLiteral("Options/Profiles/PausedDownsampleMode"), name);
}

Settings::Settings(const QString& configFilePath)
    : m_settings(makeSettings(configFilePath))
{
}

core::Version Settings::openedOnceVersion() const
{
    return core::Version::fromString(m_settings->value(QStringLiteral("general/openedOnceVersion")).toString());
}

void Settings::setOpenedOnceVersion(const core::Version& version)
{
    m_settings->setValue(QStringLiteral("general/openedOnceVersion"), version.toString());
}

QString Settings::lastProjectPath() const
{
    return m_settings->value(QStringLiteral("general/lastProjectPath")).toString();
}

void Settings::setLastProjectPath(const QString& path)
{
    m_settings->setValue(QStringLiteral("general/lastProjectPath"), path);
}

QStringList Settings::recentProjects() const
{
    return m_settings->value(QStringLiteral("general/RecentProjects")).toStringList();
}

void Settings::setRecentProjects(const QStringList& paths)
{
    m_settings->setValue(QStringLiteral("general/RecentProjects"), paths);
}

bool Settings::learnSidebarIsOpen() const
{
    return m_settings->value(QStringLiteral("general/learnSidebarIsOpen"), false).toBool();
}

void Settings::setLearnSidebarIsOpen(bool open)
{
    m_settings->setValue(QStringLiteral("general/learnSidebarIsOpen"), open);
}

namespace {

// Reference defaults (recovered from the binary): the media cache lives under
// <app local data>/Media - the database as "cache.db" beside a "Files"
// directory for the cached frames.
QString defaultMediaRoot()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .absoluteFilePath(QStringLiteral("Media"));
}

QString defaultMediaCacheDbPath()
{
    QDir dir(defaultMediaRoot());
    dir.mkpath(QStringLiteral("."));
    return QDir::toNativeSeparators(dir.absoluteFilePath(QStringLiteral("cache.db")));
}

QString defaultMediaCacheFilesPath()
{
    QDir dir(defaultMediaRoot());
    dir.mkpath(QStringLiteral("Files"));
    return QDir::toNativeSeparators(dir.absoluteFilePath(QStringLiteral("Files")));
}

} // namespace

// Shared shape of the reference's two cache-path getters: seed the setting when
// it is missing, then validate that the containing directory still exists and
// silently fall back to the default when it does not.
QString Settings::mediaCacheDbPath()
{
    const QString key = QStringLiteral("Options/MediaCacheDB");
    if (!m_settings->contains(key)) {
        m_settings->setValue(key, defaultMediaCacheDbPath());
    }
    QString path = m_settings->value(key).toString();
    if (path.isEmpty() || !QFileInfo(path).dir().exists()) {
        path = defaultMediaCacheDbPath();
        m_settings->setValue(key, path);
    }
    return QDir::toNativeSeparators(path);
}

void Settings::setMediaCacheDbPath(const QString& path)
{
    m_settings->setValue(QStringLiteral("Options/MediaCacheDB"),
                         QDir::toNativeSeparators(path));
}

QString Settings::mediaCacheFilesPath()
{
    const QString key = QStringLiteral("Options/MediaCacheFiles");
    if (!m_settings->contains(key)) {
        m_settings->setValue(key, defaultMediaCacheFilesPath());
    }
    QString path = m_settings->value(key).toString();
    if (path.isEmpty() || !QFileInfo(path).dir().exists()) {
        path = defaultMediaCacheFilesPath();
        m_settings->setValue(key, path);
    }
    return QDir::toNativeSeparators(path);
}

void Settings::setMediaCacheFilesPath(const QString& path)
{
    m_settings->setValue(QStringLiteral("Options/MediaCacheFiles"),
                         QDir::toNativeSeparators(path));
}

int Settings::daysToKeepMediaCacheFiles()
{
    const QString key = QStringLiteral("Options/DaysToKeepMediaCacheFiles");
    if (!m_settings->contains(key)) {
        return defaultDaysToKeepMediaCacheFiles();
    }
    const int days = m_settings->value(key).toInt();
    if (days > maxDaysToKeepMediaCacheFiles() || days < 0) {
        // Reference clamps out-of-range values by rewriting the default back.
        m_settings->setValue(key, defaultDaysToKeepMediaCacheFiles());
        return defaultDaysToKeepMediaCacheFiles();
    }
    return days;
}

void Settings::setDaysToKeepMediaCacheFiles(int days)
{
    m_settings->setValue(QStringLiteral("Options/DaysToKeepMediaCacheFiles"),
                         qBound(0, days, maxDaysToKeepMediaCacheFiles()));
}

QString Settings::snapshotDirectory()
{
    const QString key = QStringLiteral("Options/SnapshotDirectory");
    QString path = m_settings->value(key).toString();
    if (path.isEmpty()) {
        path = QDir(QStandardPaths::writableLocation(QStandardPaths::HomeLocation))
                   .filePath(QStringLiteral("ExportSnapshots"));
        m_settings->setValue(key, path);
    }
    return path;
}

void Settings::setSnapshotDirectory(const QString& path)
{
    m_settings->setValue(QStringLiteral("Options/SnapshotDirectory"), path);
}

QString Settings::language() const
{
    return m_settings->value(QStringLiteral("Options/Language")).toString();
}

void Settings::setLanguage(const QString& locale)
{
    m_settings->setValue(QStringLiteral("Options/Language"), locale);
}

int Settings::thumbnailCacheSizeMb() const
{
    return qBound(1, m_settings->value(QStringLiteral("Options/ThumbnailCacheSizeMB"),
        m_settings->value(QStringLiteral("cache/thumbnailCacheSizeMb"), 1024)).toInt(), 2048);
}

void Settings::setThumbnailCacheSizeMb(int mb)
{
    m_settings->setValue(QStringLiteral("Options/ThumbnailCacheSizeMB"), qBound(1, mb, 2048));
}

int Settings::autosaveIntervalSeconds() const
{
    return qBound(30, m_settings->value(QStringLiteral("Options/AutoSaveIntervalSeconds"),
        m_settings->value(QStringLiteral("general/autosaveIntervalSeconds"), 300)).toInt(), 3600);
}

void Settings::setAutosaveIntervalSeconds(int seconds)
{
    m_settings->setValue(QStringLiteral("Options/AutoSaveIntervalSeconds"),
                         qBound(30, seconds, 3600));
}

bool Settings::turboRenderingEnabled() const
{
    return m_settings->value(QStringLiteral("Options/TurboRendering"),
        m_settings->value(QStringLiteral("render/turboRenderingEnabled"), false)).toBool();
}

void Settings::setTurboRenderingEnabled(bool enabled)
{
    m_settings->setValue(QStringLiteral("Options/TurboRendering"), enabled);
}

} // namespace app
} // namespace openvegas
