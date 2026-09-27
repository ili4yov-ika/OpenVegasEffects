#include "ui/Theme.h"

#include <QApplication>
#include <QComboBox>
#include <QAbstractItemView>
#include <QEvent>

#include <QString>

namespace openvegas {
namespace ui {

namespace {
class InterfacePreferenceFilter : public QObject {
public:
    explicit InterfacePreferenceFilter(QObject* parent) : QObject(parent) {}
    bool eventFilter(QObject* target, QEvent* event) override {
        if (event->type() != QEvent::Wheel) return false;
        const auto* combo = qobject_cast<QComboBox*>(target);
        if (combo && !combo->view()->isVisible()
            && !app::Settings::optionSettings().value(QStringLiteral("Options/EnableWheelScrollMenus"), false).toBool()) {
            event->ignore(); return true;
        }
        return false;
    }
};
const ThemeColors& g_colors()
{
    static const ThemeColors colors;
    return colors;
}
} // namespace

const ThemeColors& themeColors()
{
    return g_colors();
}

QString themeFontFamily()
{
    return QStringLiteral("Segoe UI, Trebuchet MS, Droid Sans Fallback");
}

QString themeMenuFontFamily()
{
    return QStringLiteral("Lucida Grande, Segoe UI, Trebuchet MS, Droid Sans Fallback");
}

QString themeStyleSheet()
{
    const ThemeColors& c = g_colors();
    const QString fillPanel0 = c.fillPanel0.name();
    const QString fillPanel1 = c.fillPanel1.name();
    const QString fillPanel2 = c.fillPanel2.name();
    const QString focus = c.focus.name();
    const QString selection = c.selection.name();
    const QString textDisabled = c.textDisabled.name();
    const QString textHover = c.textHover.name();
    const QString buttonDefault = c.buttonDefault.name();
    const QString buttonHover = c.buttonHover.name();

    // QSS mirrors the reference dark theme (Segoe UI stack, 16px icons,
    // panel fills $fill-panel-*, focus $focus, selection $selection-0).
    // Placeholder order. It has to match the argument list exactly: QString::arg
    // maps by position, so a surplus value is silently dropped and Qt warns
    // "N argument(s) missing" with the whole sheet in the message.
    // %1 text, %2 fillPanel0, %3 fillPanel1, %4 lineColor, %5 selection,
    // %6 buttonDefault, %7 buttonHover, %8 focus, %9 fillPanel2,
    // %10 textHover, %11 lighten, %12 textDisabled
    return QStringLiteral(R"(
* {
    font-family: "Segoe UI", "Trebuchet MS", "Droid Sans Fallback";
    font-size: 12px;
    color: %1;
}
QMainWindow, QDialog, QDockWidget, QWidget {
    background-color: %2;
}
QDockWidget {
    color: %1;
    titlebar-close-icon: none;
    titlebar-normal-icon: none;
}
QDockWidget::title {
    background-color: %3;
    padding: 6px 10px;
    border-bottom: 1px solid %4;
    text-align: left;
}
QWidget#dockPanelTitleBar {
    background-color: %3;
    border-bottom: 1px solid %4;
}
QLabel#dockPanelTitle {
    background: transparent;
    padding: 0px;
}
QToolButton#dockPanelMenu,
QToolButton#dockPanelTabMenu {
    background: transparent;
    border: 0px;
    padding: 1px;
    min-width: 18px;
    min-height: 18px;
}
QToolButton#dockPanelMenu:hover,
QToolButton#dockPanelTabMenu:hover { background-color: %7; }
QMenuBar {
    background-color: %3;
    border-bottom: 1px solid %4;
    font-family: "Lucida Grande", "Segoe UI", "Trebuchet MS", "Droid Sans Fallback";
}
QMenuBar::item {
    background: transparent;
    padding: 3px 6px;
}
QMenuBar::item:selected { background-color: %5; }
QMenu {
    background-color: %2;
    border: 1px solid %4;
}
QMenu::item:selected { background-color: %5; }
QMenu::separator { height: 1px; background: %4; margin: 4px 8px; }
QStatusBar {
    background-color: %3;
    border-top: 1px solid %4;
    color: %1;
}
QToolBar {
    background-color: %2;
    border-bottom: 1px solid %4;
    spacing: 4px;
}
QToolButton {
    background-color: %6;
    border: 1px solid transparent;
    border-radius: 3px;
    padding: 3px;
    min-width: 16px;
    min-height: 16px;
    color: %1;
}
QToolButton:hover { background-color: %7; }
QToolButton:checked { background-color: %8; }
/* Text-only buttons that drop a menu. Qt parks the menu indicator in the
   bottom-right corner, where it sits under the label instead of beside it.
   These two get the arrow at the right edge, vertically centred, with room
   reserved so it never overlaps the text. Scoped by object name on purpose:
   the icon buttons that carry a flyout (the viewer's shape tool) want the
   default corner mark. */
QToolButton#toolButtonView, QToolButton#toolButtonOption,
QToolButton#toolButtonPlaybackQuality, QToolButton#viewScaleLabel {
    padding-right: 18px;
    text-align: left;
}
/* New Layer carries the green add disc on its left and no arrow at all - the
   reference shows the plus, not a menu triangle. */
QToolButton#timelineNewLayer::menu-indicator { image: none; width: 0px; }
QToolButton#toolButtonView::menu-indicator,
QToolButton#toolButtonOption::menu-indicator,
QToolButton#toolButtonPlaybackQuality::menu-indicator,
QToolButton#viewScaleLabel::menu-indicator {
    subcontrol-origin: padding;
    subcontrol-position: center right;
    right: 5px;
    width: 10px;
}
QTreeWidget, QListWidget, QTableWidget {
    background-color: %9;
    alternate-background-color: %3;
    border: none;
    outline: none;
}
QTreeWidget::item, QListWidget::item {
    padding: 3px 4px;
    color: %1;
}
QTreeWidget::item:hover, QListWidget::item:hover { background-color: %11; }
QTreeWidget::item:selected, QListWidget::item:selected {
    background-color: %5;
    color: %10;
}
QTreeWidget::item:selected:active, QListWidget::item:selected:active {
    background-color: %5;
}
QHeaderView::section {
    background-color: %3;
    padding: 4px 6px;
    border: none;
    border-bottom: 1px solid %4;
    color: %1;
}
QScrollBar:vertical { background: %9; width: 12px; margin: 0; }
QScrollBar::handle:vertical { background: %7; min-height: 24px; border-radius: 6px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar:horizontal { background: %9; height: 12px; margin: 0; }
QScrollBar::handle:horizontal { background: %7; min-width: 24px; border-radius: 6px; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
QTabWidget::pane { border: 1px solid %4; }
QTabBar::tab {
    background-color: %3;
    height: 20px;
    padding: 0 6px;
    border: 1px solid %4;
    color: %12;
}
QTabBar::tab:selected {
    background-color: %2;
    color: %1;
    border-bottom: 1px solid %8;
}
QTabBar::tab:hover:!selected { background-color: %7; }
QSplitter::handle { background-color: %3; }
QSplitter::handle:horizontal { width: 4px; }
QSplitter::handle:vertical { height: 4px; }
QSlider::groove:horizontal {
    height: 10px;
    border-radius: 5px;
    background: %9;
}
QSlider::handle:horizontal {
    width: 6px;
    margin: 2px 0;
    border-radius: 3px;
    background: %10;
}
QSlider::groove:vertical {
    width: 10px;
    border-radius: 5px;
    background: %9;
}
QSlider::handle:vertical {
    height: 6px;
    margin: 0 2px;
    border-radius: 3px;
    background: %10;
}
QPushButton {
    background-color: %6;
    border: 1px solid %4;
    border-radius: 3px;
    padding: 5px 12px;
    color: %1;
}
QPushButton:hover { background-color: %7; }
QPushButton:pressed { background-color: %5; }
QPushButton:disabled { background-color: rgba(44,44,44,115); color: %12; }
QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QTextEdit {
    background-color: %2;
    border: 1px solid %4;
    border-radius: 3px;
    padding: 2px 4px;
    color: %1;
    selection-background-color: %5;
}
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus, QTextEdit:focus {
    border: 1px solid %8;
}
QComboBox::drop-down { border: none; width: 20px; }
QComboBox QAbstractItemView {
    background-color: %9;
    border: 1px solid %4;
    selection-background-color: %5;
}
)")

        .arg(c.textDefault.name(), fillPanel0, fillPanel1, c.lineColor.name(), selection,
             buttonDefault, buttonHover, focus, fillPanel2, textHover, c.lighten.name(),
             textDisabled);
}

void applyTheme(QApplication* app)
{
    if (!app) {
        return;
    }
    app->setStyleSheet(themeStyleSheet());
}

void installInterfacePreferenceFilter(QApplication* app)
{
    if (!app || app->property("interfacePreferenceFilterInstalled").toBool()) return;
    app->installEventFilter(new InterfacePreferenceFilter(app));
    app->setProperty("interfacePreferenceFilterInstalled", true);
}

} // namespace ui
} // namespace openvegas
