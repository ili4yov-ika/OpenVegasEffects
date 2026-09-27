#pragma once

#include <QDockWidget>
#include <QStringList>

class QListWidget;
class QListWidgetItem;

namespace openvegas {
namespace ui {

// Mirrors the reference "Start" panel (type 2056). Class names from reference
// RTTI: biff::ui::StartPanel with the inner biff::ui::common::StartPanelWidget
// ("StartPanel" 0x1412bccb0, "StartPanelWidget" 0x1412d1af8).
//
// In the reference this is the home surface docked along the bottom of the
// Effects screen (see the recovered Screen XML: bottom = Start). It carries
// the project entry points and the recent-projects list:
//
//   toolButtonNewComp            "New Composite Shot"
//   toolButtonNewCompsFromMedia  "New Composite Shots\nFrom Footage"
//   toolButtonEditScreen         "Edit Screen" / "Go to the Edit Screen"
//   listViewRecentProjects       recent projects, with "Clear Recents"
//
// The reference renders the learning strip through QtWebEngine; this port
// keeps it as a plain link label so the panel stays a native widget.
class StartPanel : public QDockWidget
{
    Q_OBJECT

public:
    explicit StartPanel(QWidget* parent = nullptr);

    // Transfers the Designer-created Start surface to Timeline's shared page
    // stack while this object keeps owning its signals and recent-list state.
    QWidget* takeContentWidget();

    void setRecentProjects(const QStringList& paths);
    QStringList recentProjects() const { return m_recentProjects; }

    // Pushes `path` to the front, de-duplicating, capped at maxRecentProjects.
    void addRecentProject(const QString& path);

    static constexpr int maxRecentProjects = 10;

signals:
    void newCompositeShotRequested();
    void newCompositeShotsFromMediaRequested();
    void editScreenRequested();
    void recentProjectActivated(const QString& path);
    void clearRecentsRequested();
    void tutorialsRequested();
    void importFileRequested();
    void newProjectRequested();
    void openProjectRequested();

private:
    void rebuildRecentList();

    QListWidget* m_recentList = nullptr;
    QStringList m_recentProjects;
};

} // namespace ui
} // namespace openvegas
