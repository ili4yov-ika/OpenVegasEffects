#include "app/FileAssociation.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <shlobj.h>
#ifdef _MSC_VER
#pragma comment(lib, "shell32.lib")
#endif
#endif

namespace openvegas {
namespace app {
namespace FileAssociation {

namespace {

// QSettings names a registry key's own (default) value "Default".
bool put(QSettings& classes, const QString& key, const QString& value)
{
    if (classes.contains(key) && classes.value(key).toString() == value) return false;
    classes.setValue(key, value);
    return true;
}

} // namespace

bool registerProjectFileType(QSettings& classes, const QString& executable)
{
    if (executable.isEmpty()) return false;
    const QString path = QDir::toNativeSeparators(QFileInfo(executable).absoluteFilePath());
    const QString icon = QStringLiteral("\"%1\",%2").arg(path).arg(kProjectIconIndex);
    const QString command = QStringLiteral("\"%1\" \"%2\"").arg(path, QStringLiteral("%1"));
    const QString progId = QString::fromLatin1(kProjectProgId);
    const QString extension = QString::fromLatin1(kProjectExtension);
    const QString application = QStringLiteral("Applications/") + QFileInfo(path).fileName();
    bool changed = false;
    changed |= put(classes, progId + QStringLiteral("/Default"), QStringLiteral("OpenVegas Effects Project"));
    changed |= put(classes, progId + QStringLiteral("/DefaultIcon/Default"), icon);
    changed |= put(classes, progId + QStringLiteral("/shell/open/command/Default"), command);
    changed |= put(classes, extension + QStringLiteral("/OpenWithProgids/") + progId, QString());
    changed |= put(classes, application + QStringLiteral("/DefaultIcon/Default"), icon);
    changed |= put(classes, application + QStringLiteral("/shell/open/command/Default"), command);
    changed |= put(classes, application + QStringLiteral("/SupportedTypes/") + extension, QString());
    return changed;
}

void registerForCurrentUser()
{
#ifdef Q_OS_WIN
    QSettings classes(QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes"), QSettings::NativeFormat);
    if (registerProjectFileType(classes, QCoreApplication::applicationFilePath())) {
        classes.sync();
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    }
#endif
}

} // namespace FileAssociation
} // namespace app
} // namespace openvegas
