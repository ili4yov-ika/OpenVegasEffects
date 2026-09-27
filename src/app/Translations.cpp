#include "app/Translations.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

namespace openvegas {
namespace app {

namespace {

const char* const kCatalogPrefix = "openvegaseffects_";

// Keeps the installed translators alive for the lifetime of the application.
QTranslator* g_appTranslator = nullptr;
QTranslator* g_qtTranslator = nullptr;

QString resolveCatalogue(const QString& locale, const QString& translationsRoot)
{
    const QString fileName = QString::fromLatin1(kCatalogPrefix) + locale + QStringLiteral(".qm");
    if (!translationsRoot.isEmpty()) {
        // Reference layout: Translations/<locale>/<catalogue>.qm
        const QString onDisk = QDir(translationsRoot).filePath(locale + QLatin1Char('/') + fileName);
        if (QFileInfo::exists(onDisk)) {
            return onDisk;
        }
    }
    const QString embedded = QStringLiteral(":/i18n/") + fileName;
    return QFileInfo::exists(embedded) ? embedded : QString();
}

} // namespace

QStringList Translations::availableLocales()
{
    return {QStringLiteral("en"), QStringLiteral("ru"),
            QStringLiteral("ja"), QStringLiteral("zh_CN")};
}

QString Translations::displayName(const QString& locale)
{
    if (locale == QLatin1String("en"))    return QStringLiteral("English");
    if (locale == QLatin1String("ru"))    return QString::fromUtf8("\xd0\xa0\xd1\x83\xd1\x81\xd1\x81\xd0\xba\xd0\xb8\xd0\xb9");
    if (locale == QLatin1String("ja"))    return QString::fromUtf8("\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e");
    if (locale == QLatin1String("zh_CN")) return QString::fromUtf8("\xe7\xae\x80\xe4\xbd\x93\xe4\xb8\xad\xe6\x96\x87");
    return locale;
}

QString Translations::install(QCoreApplication* app,
                              const QString& locale,
                              const QString& translationsRoot)
{
    if (!app) {
        return QString();
    }

    // install() is also useful after the Options dialog changes the locale.
    // Remove both catalogues first: otherwise switching to English leaves the
    // previously installed translator active, while switching between two
    // translated locales can install the same QTranslator twice.
    if (g_appTranslator) {
        app->removeTranslator(g_appTranslator);
    }
    if (g_qtTranslator) {
        app->removeTranslator(g_qtTranslator);
    }

    QString wanted = locale;
    if (wanted.isEmpty()) {
        // Follow the system, but only when this build carries that locale.
        const QLocale system;
        const QString name = system.name();                       // e.g. "ru_RU"
        const QString language = name.section(QLatin1Char('_'), 0, 0);
        if (availableLocales().contains(name)) {
            wanted = name;
        } else if (availableLocales().contains(language)) {
            wanted = language;
        } else {
            wanted = QStringLiteral("en");
        }
    }
    if (wanted == QLatin1String("en")) {
        return wanted;
    }

    const QString catalogue = resolveCatalogue(wanted, translationsRoot);
    if (catalogue.isEmpty()) {
        return QStringLiteral("en");
    }

    if (!g_appTranslator) {
        g_appTranslator = new QTranslator(app);
    }
    if (!g_appTranslator->load(catalogue)) {
        return QStringLiteral("en");
    }
    app->installTranslator(g_appTranslator);

    // Qt's own strings (standard dialogs, shortcuts) where available.
    if (!g_qtTranslator) {
        g_qtTranslator = new QTranslator(app);
    }
    const QString qtDir = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
    if (g_qtTranslator->load(QStringLiteral("qt_") + wanted, qtDir)) {
        app->installTranslator(g_qtTranslator);
    }
    return wanted;
}

} // namespace app
} // namespace openvegas
