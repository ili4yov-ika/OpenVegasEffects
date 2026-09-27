#pragma once

#include <QWidget>

class QLabel;
class QDockWidget;
class QMainWindow;
class QMouseEvent;
class QToolButton;

namespace openvegas::ui {

// Title bar shared by every dock panel.  VEGAS Effects exposes a compact
// three-line menu next to the panel name instead of relying on platform title
// buttons.  Besides making the affordance consistent, this keeps docking
// usable with a frameless dark title bar.
class DockTitleBar final : public QWidget
{
    Q_OBJECT

public:
    explicit DockTitleBar(QDockWidget* dock);

protected:
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void showPanelMenu();

    QDockWidget* m_dock = nullptr;
    QLabel* m_title = nullptr;
    QToolButton* m_menuButton = nullptr;
};

void installDockTitleBar(QDockWidget* dock);
void installDockTabMenus(QMainWindow* window);

} // namespace openvegas::ui
