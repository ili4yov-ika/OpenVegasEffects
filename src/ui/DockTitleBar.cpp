#include "ui/DockTitleBar.h"

#include <QAction>
#include <QDockWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QStyle>
#include <QTabBar>
#include <QToolButton>

namespace openvegas::ui {
namespace {

QIcon panelMenuIcon(const QColor& color)
{
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap));
    // The recovered title-bar control is the small three-stroke panel menu.
    // Draw it instead of using a font glyph so it remains crisp on every DPI.
    for (int x : {5, 8, 11}) {
        painter.drawLine(x, 4, x, 12);
    }
    return QIcon(pixmap);
}

QMainWindow* ownerWindow(QDockWidget* dock)
{
    if (!dock) return nullptr;
    for (QObject* parent = dock->parent(); parent; parent = parent->parent()) {
        if (auto* window = qobject_cast<QMainWindow*>(parent)) return window;
    }
    return qobject_cast<QMainWindow*>(dock->window());
}

void dockPanelTo(QDockWidget* dock, Qt::DockWidgetArea area)
{
    if (QMainWindow* owner = ownerWindow(dock)) {
        owner->addDockWidget(area, dock);
        dock->setFloating(false);
        dock->show();
        dock->raise();
    }
}

void showDockMenu(QDockWidget* dock, QWidget* anchor)
{
    if (!dock || !anchor) return;
    QMenu menu(anchor);
    QAction* floating = menu.addAction(DockTitleBar::tr("Float Panel"));
    floating->setCheckable(true);
    floating->setChecked(dock->isFloating());
    QObject::connect(floating, &QAction::toggled, dock, &QDockWidget::setFloating);

    QMenu* dockMenu = menu.addMenu(DockTitleBar::tr("Dock Panel"));
    QAction* left = dockMenu->addAction(DockTitleBar::tr("Left"));
    QAction* right = dockMenu->addAction(DockTitleBar::tr("Right"));
    QAction* top = dockMenu->addAction(DockTitleBar::tr("Top"));
    QAction* bottom = dockMenu->addAction(DockTitleBar::tr("Bottom"));
    QObject::connect(left, &QAction::triggered, anchor,
                     [dock] { dockPanelTo(dock, Qt::LeftDockWidgetArea); });
    QObject::connect(right, &QAction::triggered, anchor,
                     [dock] { dockPanelTo(dock, Qt::RightDockWidgetArea); });
    QObject::connect(top, &QAction::triggered, anchor,
                     [dock] { dockPanelTo(dock, Qt::TopDockWidgetArea); });
    QObject::connect(bottom, &QAction::triggered, anchor,
                     [dock] { dockPanelTo(dock, Qt::BottomDockWidgetArea); });

    menu.addSeparator();
    QAction* close = menu.addAction(DockTitleBar::tr("Close Panel"));
    close->setEnabled(dock->features().testFlag(QDockWidget::DockWidgetClosable));
    QObject::connect(close, &QAction::triggered, dock, &QDockWidget::close);
    menu.exec(anchor->mapToGlobal(QPoint(0, anchor->height())));
}

} // namespace

DockTitleBar::DockTitleBar(QDockWidget* dock)
    : QWidget(dock)
    , m_dock(dock)
{
    setObjectName(QStringLiteral("dockPanelTitleBar"));
    setFixedHeight(20);
    setCursor(Qt::SizeAllCursor);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(5, 0, 2, 0);
    layout->setSpacing(2);

    m_title = new QLabel(dock->windowTitle(), this);
    m_title->setObjectName(QStringLiteral("dockPanelTitle"));
    m_title->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
    m_title->setAttribute(Qt::WA_TransparentForMouseEvents);
    layout->addWidget(m_title);

    m_menuButton = new QToolButton(this);
    m_menuButton->setObjectName(QStringLiteral("dockPanelMenu"));
    m_menuButton->setAutoRaise(true);
    m_menuButton->setCursor(Qt::ArrowCursor);
    m_menuButton->setFixedSize(18, 18);
    m_menuButton->setIconSize(QSize(16, 16));
    m_menuButton->setIcon(panelMenuIcon(palette().color(QPalette::WindowText)));
    m_menuButton->setToolTip(tr("Panel menu"));
    layout->addWidget(m_menuButton);
    layout->addStretch(1);

    connect(m_menuButton, &QToolButton::clicked, this, &DockTitleBar::showPanelMenu);
    connect(dock, &QDockWidget::windowTitleChanged, m_title, &QLabel::setText);
}

void DockTitleBar::showPanelMenu()
{
    showDockMenu(m_dock, m_menuButton);
}

void DockTitleBar::mousePressEvent(QMouseEvent* event)
{
    // QDockWidget owns the native drag state.  Ignoring title-bar mouse events
    // deliberately propagates them to the dock, restoring Qt's translucent
    // docking preview and every split/tab target (including below another
    // dock).  Handling the drag here would reduce docking to hand-written edge
    // bands and make inner split targets unreachable.
    event->ignore();
}

void DockTitleBar::mouseMoveEvent(QMouseEvent* event)
{
    event->ignore();
}

void DockTitleBar::mouseReleaseEvent(QMouseEvent* event)
{
    event->ignore();
}

void DockTitleBar::mouseDoubleClickEvent(QMouseEvent* event)
{
    // Propagation also gives the dock its standard double-click float/dock
    // toggle, keeping that state in the same native controller as dragging.
    event->ignore();
}

void installDockTitleBar(QDockWidget* dock)
{
    if (!dock || dock->titleBarWidget()) return;
    dock->setTitleBarWidget(new DockTitleBar(dock));
}

void installDockTabMenus(QMainWindow* window)
{
    if (!window) return;
    const auto docks = window->findChildren<QDockWidget*>(QString(), Qt::FindDirectChildrenOnly);
    const auto tabBars = window->findChildren<QTabBar*>();
    for (QTabBar* tabBar : tabBars) {
        for (int index = 0; index < tabBar->count(); ++index) {
            if (QWidget* current = tabBar->tabButton(index, QTabBar::RightSide);
                current && current->objectName() == QLatin1String("dockPanelTabMenu")) {
                continue;
            }
            QString title = tabBar->tabText(index);
            title.remove(QLatin1Char('&'));
            QDockWidget* matchingDock = nullptr;
            for (QDockWidget* dock : docks) {
                QString dockTitle = dock->windowTitle();
                dockTitle.remove(QLatin1Char('&'));
                if (dockTitle == title) {
                    matchingDock = dock;
                    break;
                }
            }
            if (!matchingDock) continue; // a regular QTabWidget, not dock tabs

            auto* button = new QToolButton(tabBar);
            button->setObjectName(QStringLiteral("dockPanelTabMenu"));
            button->setAutoRaise(true);
            button->setFixedSize(18, 18);
            button->setIconSize(QSize(14, 14));
            button->setIcon(panelMenuIcon(tabBar->palette().color(QPalette::WindowText)));
            button->setToolTip(DockTitleBar::tr("Panel menu"));
            QObject::connect(button, &QToolButton::clicked, tabBar,
                             [matchingDock, button] { showDockMenu(matchingDock, button); });
            tabBar->setTabButton(index, QTabBar::RightSide, button);
        }
    }
}

} // namespace openvegas::ui
