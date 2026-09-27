#pragma once

#include <QString>
#include <QStringList>

class QCoreApplication;

namespace openvegas {
namespace app {

// UI localisation. The reference ships its translations as
// Translations/<locale>/VegasEffects_<locale>.qm next to a matching Qt
// catalogue, and only bundles ja and zh_CN - English lives in the source.
// This port keeps the same folder convention for drop-in catalogues and also
// embeds its own, so a plain build is already localised.
//
// Lookup order for a locale:
//   1. <user data>/Translations/<locale>/openvegaseffects_<locale>.qm
//   2. :/i18n/openvegaseffects_<locale>.qm            (embedded)
// The matching Qt catalogue (qt_<locale>.qm) is loaded alongside when present.
class Translations
{
public:
    // Locales this build carries.
    static QStringList availableLocales();

    // Human-readable name for a locale tag, in that language.
    static QString displayName(const QString& locale);

    // Installs translators for `locale`. An empty locale means "follow the
    // system"; "en" means no translation. `translationsRoot` is the user data
    // Translations folder and may be empty.
    // Returns the locale actually applied.
    static QString install(QCoreApplication* app,
                           const QString& locale,
                           const QString& translationsRoot = QString());
};

} // namespace app
} // namespace openvegas
