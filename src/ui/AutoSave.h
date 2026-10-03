#pragma once

#include <QDateTime>
#include <QString>
#include <QVector>

namespace openvegas {
namespace ui {
namespace autosave {

// Project auto-saving as the reference does it (Options > Auto Save,
// AutoSaveSettingsWidget; file naming FUN_1402cd290):
//  * Options/AutoSave turns it on (default on), Options/AutoSaveFrequency is
//    the interval in minutes (default 10), Options/AutoSavePath the folder -
//    by default Documents/<organization>/<application>/AutoSave, kept apart
//    from the project so an auto-save never touches the master file;
//  * each auto-save is a new file "<project base>.vegfx.autosave<N>"
//    ("Untitled Project" for a project never saved);
//  * one is only written while the project has changes since the last manual
//    save, and a manual save clears that project's auto-saves;
//  * after a session that did not end normally the next start offers to
//    recover them (AutoSaveRecoveryDialog, "Recovered Projects").

bool enabled();
int frequencyMinutes();
QString defaultFolder();
// The configured folder when it exists and is writable, otherwise the
// default one (created), as FUN_14022f0b0 falls back.
QString folder();
// Tests point the module at a scratch folder instead of the user's settings;
// an empty path removes the override.
void setFolderOverride(const QString& path);

QString baseNameFor(const QString& projectPath);
// The next free "<base>.vegfx.autosave<N>" in folder().
QString nextFile(const QString& projectPath);
QStringList filesFor(const QString& projectPath);
void clearFor(const QString& projectPath);

struct Entry
{
    QString file;          // the auto-save itself
    QString projectPath;   // the project it was taken from; empty if untitled
    QDateTime saved;
};
// Every auto-save in folder(), newest first.
QVector<Entry> entries();
// The project an auto-save belongs to, from its OpenVegasAutoSaveOf marker.
QString projectOf(const QString& autosaveFile);

// Unclean-exit detection: a lock file held for the session. A lock left by a
// process that no longer runs means the last session ended abnormally.
// Returns true when that was the case.
bool beginSession();
void endSession();

} // namespace autosave
} // namespace ui
} // namespace openvegas
