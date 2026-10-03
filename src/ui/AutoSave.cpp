#include "ui/AutoSave.h"

#include "app/Settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QXmlStreamReader>

#include <algorithm>
#include <memory>

namespace openvegas {
namespace ui {
namespace autosave {
namespace {

const char kSuffix[] = ".vegfx.autosave";
const char kUntitled[] = "Untitled Project";

std::unique_ptr<QLockFile>& sessionLock()
{
    static std::unique_ptr<QLockFile> lock;
    return lock;
}

QString& folderOverride()
{
    static QString path;
    return path;
}

} // namespace

void setFolderOverride(const QString& path)
{
    folderOverride() = path;
}

bool enabled()
{
    const QSettings settings = app::Settings::optionSettings();
    // Older builds of this port kept the switch as AutoSaveEnabled.
    return settings.value(QStringLiteral("Options/AutoSave"),
                          settings.value(QStringLiteral("Options/AutoSaveEnabled"), true))
        .toBool();
}

int frequencyMinutes()
{
    const QSettings settings = app::Settings::optionSettings();
    if (settings.contains(QStringLiteral("Options/AutoSaveFrequency")))
        return qBound(1, settings.value(QStringLiteral("Options/AutoSaveFrequency")).toInt(), 120);
    if (settings.contains(QStringLiteral("Options/AutoSaveIntervalSeconds"))) {
        const int seconds = settings.value(QStringLiteral("Options/AutoSaveIntervalSeconds")).toInt();
        return qBound(1, (seconds + 30) / 60, 120);
    }
    return 10;
}

QString defaultFolder()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
        .filePath(app::Settings::organizationName() + QLatin1Char('/')
                  + app::Settings::applicationName() + QStringLiteral("/AutoSave"));
}

QString folder()
{
    if (!folderOverride().isEmpty()) {
        QDir().mkpath(folderOverride());
        return QDir::cleanPath(folderOverride());
    }
    const QString configured = QDir::fromNativeSeparators(
        app::Settings::optionSettings().value(QStringLiteral("Options/AutoSavePath")).toString().trimmed());
    if (!configured.isEmpty()) {
        const QFileInfo info(configured);
        if ((info.exists() || QDir().mkpath(configured)) && QFileInfo(configured).isDir()
            && QFileInfo(configured).isWritable()) {
            return QDir::cleanPath(configured);
        }
    }
    const QString fallback = defaultFolder();
    QDir().mkpath(fallback);
    return QDir::cleanPath(fallback);
}

QString baseNameFor(const QString& projectPath)
{
    return projectPath.isEmpty() ? QString::fromLatin1(kUntitled)
                                 : QFileInfo(projectPath).completeBaseName();
}

QStringList filesFor(const QString& projectPath)
{
    const QString base = baseNameFor(projectPath);
    const QRegularExpression pattern(QStringLiteral("^%1%2(\\d+)$")
                                         .arg(QRegularExpression::escape(base),
                                              QRegularExpression::escape(QString::fromLatin1(kSuffix))));
    QStringList files;
    const QDir dir(folder());
    for (const QFileInfo& info : dir.entryInfoList({QStringLiteral("*") + QString::fromLatin1(kSuffix)
                                                    + QStringLiteral("*")}, QDir::Files)) {
        if (!pattern.match(info.fileName()).hasMatch()) continue;
        // The same base name can come from two folders; the marker says which.
        const QString owner = projectOf(info.absoluteFilePath());
        if (!projectPath.isEmpty() && !owner.isEmpty()
            && QDir::cleanPath(owner) != QDir::cleanPath(projectPath)) continue;
        files.append(info.absoluteFilePath());
    }
    return files;
}

QString nextFile(const QString& projectPath)
{
    const QString base = baseNameFor(projectPath);
    int next = 1;
    const QDir dir(folder());
    const QRegularExpression pattern(QStringLiteral("^%1%2(\\d+)$")
                                         .arg(QRegularExpression::escape(base),
                                              QRegularExpression::escape(QString::fromLatin1(kSuffix))));
    for (const QString& name : dir.entryList({QStringLiteral("*") + QString::fromLatin1(kSuffix)
                                              + QStringLiteral("*")}, QDir::Files)) {
        const auto match = pattern.match(name);
        if (match.hasMatch()) next = qMax(next, match.captured(1).toInt() + 1);
    }
    return dir.filePath(base + QString::fromLatin1(kSuffix) + QString::number(next));
}

void clearFor(const QString& projectPath)
{
    for (const QString& file : filesFor(projectPath)) QFile::remove(file);
}

QString projectOf(const QString& autosaveFile)
{
    QFile file(autosaveFile);
    if (!file.open(QIODevice::ReadOnly)) return QString();
    QXmlStreamReader reader(&file);
    while (!reader.atEnd()) {
        if (reader.readNextStartElement()
            && reader.name() == QLatin1String("OpenVegasAutoSaveOf")) {
            return reader.readElementText();
        }
        // The marker sits right at the top of <Project>; once the asset list
        // starts there is none.
        if (reader.isStartElement() && reader.name() == QLatin1String("AssetList")) break;
    }
    return QString();
}

QVector<Entry> entries()
{
    QVector<Entry> list;
    const QDir dir(folder());
    for (const QFileInfo& info : dir.entryInfoList({QStringLiteral("*") + QString::fromLatin1(kSuffix)
                                                    + QStringLiteral("*")}, QDir::Files)) {
        list.append({info.absoluteFilePath(), projectOf(info.absoluteFilePath()),
                     info.lastModified()});
    }
    std::sort(list.begin(), list.end(),
              [](const Entry& a, const Entry& b) { return a.saved > b.saved; });
    return list;
}

bool beginSession()
{
    const QString path = QDir(folder()).filePath(QStringLiteral("session.lock"));
    const bool leftOver = QFile::exists(path);
    auto lock = std::make_unique<QLockFile>(path);
    lock->setStaleLockTime(0);   // stale only when its process is gone
    if (!lock->tryLock(0)) {
        // Another instance is running and owns the lock: nothing crashed.
        return false;
    }
    sessionLock() = std::move(lock);
    return leftOver;
}

void endSession()
{
    if (sessionLock()) {
        sessionLock()->unlock();
        sessionLock().reset();
    }
}

} // namespace autosave
} // namespace ui
} // namespace openvegas
