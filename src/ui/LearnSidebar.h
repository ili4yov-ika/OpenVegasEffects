#pragma once

#include <QDockWidget>

namespace openvegas {
namespace ui {

// Mirrors the reference "Learn Sidebar": a dock on the right, toggled by
// "Toggle Learn Sidebar" (0x1412d0908) and remembered across runs in the
// setting learnSidebarIsOpen (0x1412c10d0); its content area keeps the
// reference object name learnWebViewSpace (0x1412c0e68).
//
// The reference fills it with VEGAS's online onboarding pages through Qt
// WebEngine. Those lessons are not part of this port, so the sidebar is a
// native widget with the same entry points - online tutorials in the
// system browser, a new composite shot, importing media - and the
// application does not carry a Chromium runtime for one static page.
class LearnSidebar : public QDockWidget
{
    Q_OBJECT

public:
    explicit LearnSidebar(QWidget* parent = nullptr);

signals:
    void tutorialsRequested();
    void commandRequested(const QString& name);
};

} // namespace ui
} // namespace openvegas
