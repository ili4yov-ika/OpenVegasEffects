#include <windows.h>

#include <string>
#include <vector>

#include <QApplication>
#include <QtGlobal>
#include <QDir>
#include <QIcon>
#include <QMessageBox>
#include <QSharedMemory>
#include <QStandardPaths>
#include <QString>
#include <QStringList>


#include "app/AppMain.h"
#include "app/Settings.h"
#include "app/Translations.h"

static QStringList commandLineArgumentsWide()
{
    int argc = 0;
    LPWSTR* argvw = CommandLineToArgvW(GetCommandLineW(), &argc);
    QStringList result;
    if (argvw) {
        for (int i = 0; i < argc; ++i) {
            result.append(QString::fromWCharArray(argvw[i]));
        }
        LocalFree(argvw);
    }
    return result;
}

// Reference behaviour (FUN_1401c2ce0): a "console" switch makes the *GUI*
// build attach to (or allocate) a console so that qDebug/Qt log output is
// visible while the window runs. The reference takes a bare "console"
// argument (no dash); "-console"/"--console" are accepted too for
// conventional command-line handling. It attaches to the parent console when
// one exists (e.g. launched from a terminal) and otherwise allocates its own,
// then re-points stdin/stdout/stderr at the CONIN$/CONOUT$ devices and raises
// the scrollback buffer to 500 lines (reference: dwSize.Y = 500).
static void enableConsoleIfRequested(const QStringList& args)
{
    const bool wantsConsole =
        args.contains(QStringLiteral("console")) ||
        args.contains(QStringLiteral("-console")) ||
        args.contains(QStringLiteral("--console"));
    if (!wantsConsole) {
        return;
    }

    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
        if (!AllocConsole()) {
            return;
        }
    }

    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out && out != INVALID_HANDLE_VALUE) {
        CONSOLE_SCREEN_BUFFER_INFO info{};
        if (GetConsoleScreenBufferInfo(out, &info)) {
            info.dwSize.Y = 500;
            SetConsoleScreenBufferSize(out, info.dwSize);
        }
    }

    FILE* unused = nullptr;
    freopen_s(&unused, "CONIN$", "r", stdin);
    freopen_s(&unused, "CONOUT$", "w", stdout);
    freopen_s(&unused, "CONOUT$", "w", stderr);
}

// Single-instance guard. Reference behaviour (FUN_1401c2ce0): a QSharedMemory
// object guards the whole application; creating it while another instance
// holds the key makes this one show a critical "Another instance …" box and
// exit. The key is a UUID minted for this product (NOT copied from the
// reference), so it stays unique to OpenVegasEffects. Returns false when
// another instance already owns the lock.
static bool acquireSingleInstanceLock(QSharedMemory* memory, const QString& appName)
{
    memory->setKey(QStringLiteral("OpenVegasEffects-29964FD1-7C52-4B1E-9A0A-8F2E3B9C0D8A"));
    if (memory->create(1)) {
        return true;
    }
    if (memory->error() == QSharedMemory::AlreadyExists) {
        QMessageBox::critical(nullptr, appName,
                              QStringLiteral("Another instance of %1 is already running.\n\n"
                                             "Only one instance is allowed at a time.")
                                  .arg(appName));
        return false;
    }
    // An unusual error (permission, stale handle). Fall back to running
    // anyway rather than blocking a legitimate launch.
    return true;
}


// Qt's default handler converts the message with QString::toLocal8Bit(), i.e.
// to the ANSI code page, while a Windows console decodes its own OEM page - so
// Cyrillic paths arrive as mojibake ("Мешапы" printed as "╠х°ря√"). Writing
// UTF-16 straight to the console handle sidesteps both code pages; when stderr
// is redirected to a file or a pipe there is no console, and UTF-8 is the
// sensible encoding to fall back on.
static void consoleMessageHandler(QtMsgType type, const QMessageLogContext& context,
                                  const QString& message)
{
    Q_UNUSED(type);
    Q_UNUSED(context);
    const QString line = message + QLatin1Char('\n');
    const HANDLE handle = GetStdHandle(STD_ERROR_HANDLE);
    DWORD mode = 0;
    if (handle && handle != INVALID_HANDLE_VALUE && GetConsoleMode(handle, &mode)) {
        DWORD written = 0;
        WriteConsoleW(handle, line.utf16(), static_cast<DWORD>(line.size()), &written, nullptr);
        return;
    }
    const QByteArray utf8 = line.toUtf8();
    fwrite(utf8.constData(), 1, static_cast<size_t>(utf8.size()), stderr);
    fflush(stderr);
}


int main(int argc, char** argv)
{
    Q_UNUSED(argc);
    Q_UNUSED(argv);

    // Shared OpenGL contexts let the viewer's QOpenGLWidget move between
    // docks and windows (the 360 Viewer hosts the same viewer) without losing
    // its GL resources; the attribute has to be set before QApplication.
    // Reference main (FUN_1401c2ce0) sets exactly this pair in this position,
    // ahead of its QApplication constructor: AA_ShareOpenGLContexts (0x12) at
    // 1401c3014 and AA_UseDesktopOpenGL (0xf) at 1401c3021, both true. Asking
    // for desktop GL keeps the viewer off the ANGLE / software paths.
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts, true);
    QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL, true);

    // The reference also fixes two menu attributes here: AA_DontUseNativeMenuBar
    // (6) true at 1401c31f4, so the menu bar stays inside the window on every
    // platform, and AA_DontShowIconsInMenus (2) *false* at 1401c3201 (XOR EDX
    // ahead of the call), i.e. menu icons are shown - which is the Windows
    // default but not the macOS one.
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeMenuBar, true);
    QCoreApplication::setAttribute(Qt::AA_DontShowIconsInMenus, false);

    // High-DPI is a user setting in the reference, not a constant: at 1401c31ce
    // it reads "Options/EnableHighDpiScaling" (bool, default true) and feeds the
    // result to AA_UseHighDpiPixmaps (0xd) and AA_EnableHighDpiScaling (0x14).
    // Both enumerators are deprecated no-ops in Qt 6, where scaling is always
    // on, so the setting is honoured through the environment switch Qt 6 still
    // reads. Like the attributes, it must be decided before QApplication - hence
    // the static reader, which opens the ini by name instead of going through an
    // instance that would need the application names to be set first.
    if (!openvegas::app::Settings::enableHighDpiScaling()) {
        qputenv("QT_ENABLE_HIGHDPI_SCALING", "0");
    }


    QStringList rawArgs = commandLineArgumentsWide();
    enableConsoleIfRequested(rawArgs);

    std::vector<char*> rawCStrings;
    std::vector<std::string> storage;
    storage.reserve(rawArgs.size());
    for (const QString& a : rawArgs) {
        const QByteArray utf8 = a.toUtf8();
        storage.push_back(std::string(utf8.constData(), utf8.size()));
    }
    for (std::string& s : storage) {
        rawCStrings.push_back(s.data());
    }

    int qtArgc = static_cast<int>(rawCStrings.size());
    qInstallMessageHandler(consoleMessageHandler);

    QApplication app(qtArgc, rawCStrings.data());

    const QString appName = QStringLiteral("OpenVegas Effects");
    // Same constants the static settings reader above used, so the ini the
    // application resolves and the one read before it existed are one file.
    app.setApplicationName(openvegas::app::Settings::applicationName());
    app.setOrganizationName(openvegas::app::Settings::organizationName());
    app.setApplicationVersion(QStringLiteral("0.1.0"));
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/logo.ico")));

    // A second instance is turned away unless it explicitly asked to be
    // allowed via the environment (the reference honours a similar bypass).
    // The guard has to outlive main's body. QSharedMemory releases its segment
    // from the destructor, so scoping it to the if-block dropped the key before
    // the event loop even started and every later instance sailed straight
    // through the check.
    const bool allowMultiple = qEnvironmentVariableIsSet("OPENVEGAS_ALLOW_MULTIPLE");
    QSharedMemory singleInstance;
    if (!allowMultiple && !acquireSingleInstanceLock(&singleInstance, appName)) {
        return 0;
    }

    // Translators must be installed before any widget is constructed, so this
    // happens ahead of AppMain. The organisation/application names above are
    // what QSettings keys off, hence the ordering.
    {
        const openvegas::app::Settings settings;
        const QString root =
            QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                .filePath(QStringLiteral("Translations"));
        openvegas::app::Translations::install(&app, settings.language(), root);
    }

    // First non-switch argument is a project to open - this is what the shell
    // hands us for a double-clicked .vegfx. rawArgs comes from the wide command
    // line, so non-ASCII paths survive.
    QString projectToOpen;
    for (int i = 1; i < rawArgs.size(); ++i) {
        const QString& candidate = rawArgs.at(i);
        if (!candidate.startsWith(QLatin1Char('-')) && !candidate.startsWith(QLatin1Char('/'))) {
            projectToOpen = candidate;
            break;
        }
    }

    openvegas::app::AppMain mainApp;
    const int exitCode = mainApp.run(&app, projectToOpen);
    return exitCode;
}
