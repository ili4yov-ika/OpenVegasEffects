#pragma once

#include <QColor>
#include <QColorDialog>
#include <QSettings>
#include <QString>

class QApplication;

namespace openvegas {
namespace ui {

// Exact constants recovered from the Common.qrc stylesheet embedded in
// VegasEffects.exe.  They are also recorded in the decompilation report at
// SAMPLES/VEGAS_Effects/decompile-src/VegasEffects.exe/90_reports/
// theme_qss_common.txt.
struct ThemeColors
{
    // Base surfaces
    QColor fillPanel0{34, 34, 34};     // #222222 app / panel background
    QColor fillPanel1{27, 27, 27};     // #1B1B1B panel chrome
    QColor fillPanel2{17, 17, 17};     // #111111 deep list/canvas background
    QColor fillPanelPlus1{51, 51, 51}; // #333333 raised surface
    QColor fillPanelPlus2{68, 68, 68};
    QColor fillPanelPlus3{85, 85, 85};
    QColor lineColor{27, 27, 27};      // #1B1B1B separators
    QColor lighten{255, 255, 255, 31};
    QColor darken{0, 0, 0, 31};

    // Accent / focus / selection
    QColor focus{18, 176, 255};        // #12B0FF
    QColor selection{86, 104, 115};    // #566873

    // Text (reference #CCCCCC / #FFFFFF)
    QColor textDefault{204, 204, 204};
    QColor textHover{223, 223, 223};
    QColor textDisabled{102, 102, 102};
    QColor textDarker{175, 175, 175};

    // Icons
    QColor iconDefault{221, 221, 221};
    QColor iconHover{255, 255, 255};
    QColor iconDisabled{221, 221, 221, 115};

    // Buttons
    QColor buttonDefault{44, 44, 44};
    QColor buttonHover{73, 73, 73};
    QColor buttonAddon{48, 140, 48};
    QColor buttonHoverAddon{56, 166, 56};
    QColor buttonDisabled{44, 44, 44, 115};
    QColor buttonSelection{86, 104, 115};

    // Timeline tracks
    QColor timelineVideo{43, 63, 97};    // #2B3F61
    QColor timelineAudio{0, 66, 52};     // #004234
    QColor timelineSelection{86, 104, 115};
    QColor progressComplete{48, 140, 48};
    QColor invalidInput{255, 0, 0};
    QColor playhead{255, 255, 255};      // #FFFFFF
};

// Font stacks from the reference theme.
QString themeFontFamily();      // "Segoe UI, Trebuchet MS, Droid Sans Fallback"
QString themeMenuFontFamily();  // "Lucida Grande, Segoe UI, Trebuchet MS, Droid Sans Fallback"

// Constituents of the reference theme shorthand.
const ThemeColors& themeColors();

QString themeStyleSheet();

// Applies the reference-derived dark theme to the whole application.
void applyTheme(QApplication* app);

// Every colour editor honours the same preference, as ColorBlockWidget does
// in the reference. Keeping this inline also serves header-only editors.
inline QColor interfaceColor(const QColor& initial, QWidget* parent,
                             const QString& title = QString())
{
    const auto flags = QSettings().value(QStringLiteral("Options/UseNativeColorPicker"), true).toBool()
        ? QColorDialog::ColorDialogOptions() : QColorDialog::ColorDialogOptions(QColorDialog::DontUseNativeDialog);
    return QColorDialog::getColor(initial, parent, title, flags);
}
void installInterfacePreferenceFilter(QApplication* app);

} // namespace ui
} // namespace openvegas
