#include "ui/StartPanel.h"
#include "ui_Start.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

// The Start page (ui/panels/Start.ui): new and open project, new composite
// shot (empty or one per media file), import, the recent projects and the
// tutorials link. The panel only reports what was clicked; MainWindow acts
// on it. Its content is shown as a page of the Timeline dock rather than as
// a dock of its own (takeContentWidget).

namespace openvegas {
namespace ui {

StartPanel::StartPanel(QWidget* parent)
    : QDockWidget(parent)
{
    Ui::StartPanel form;
    form.setupUi(this);
    QToolButton* newComp = form.toolButtonNewComp;
    connect(newComp, &QToolButton::clicked,
            this, &StartPanel::newCompositeShotRequested);
    QToolButton* fromMedia = form.toolButtonNewCompsFromMedia;
    connect(fromMedia, &QToolButton::clicked,
            this, &StartPanel::newCompositeShotsFromMediaRequested);
    QToolButton* importFile = form.AddMedia;
    connect(importFile, &QToolButton::clicked, this, &StartPanel::importFileRequested);
    QToolButton* editScreen = form.toolButtonEditScreen;
    connect(editScreen, &QToolButton::clicked,
            this, &StartPanel::editScreenRequested);
    QToolButton* newProject = form.toolButtonNew;
    // The project file's icon (resources/icons/iconfile.svg) beside the label.
    newProject->setIcon(QIcon(QStringLiteral(":/icons/project-file.svg")));
    newProject->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(newProject, &QToolButton::clicked, this, &StartPanel::newProjectRequested);
    QToolButton* openProject = form.toolButtonOpen;
    connect(openProject, &QToolButton::clicked, this, &StartPanel::openProjectRequested);
    // Clearing empties the list here at once; MainWindow clears the setting.
    QToolButton* clearRecents = form.toolButtonClearRecents;
    connect(clearRecents, &QToolButton::clicked, this, [this] {
        setRecentProjects(QStringList());
        emit clearRecentsRequested();
    });
    // Each item carries its full path; double-click or Enter opens it.
    m_recentList = form.listViewRecentProjects;
    connect(m_recentList, &QListWidget::itemActivated,
            this, [this](QListWidgetItem* item) {
        if (!item) {
            return;
        }
        emit recentProjectActivated(item->data(Qt::UserRole).toString());
    });
    QLabel* tutorials = form.labelStart;
    connect(tutorials, &QLabel::linkActivated,
            this, [this](const QString&) { emit tutorialsRequested(); });
}

// Detaches the form's content so the Timeline dock can show it as its start
// page; the panel keeps its connections to the widgets inside.
QWidget* StartPanel::takeContentWidget()
{
    QWidget* content = widget();
    if (content) {
        content->setParent(nullptr);
    }
    return content;
}

// Replaces the list (the settings' recent projects at start-up), keeping the
// first maxRecentProjects.
void StartPanel::setRecentProjects(const QStringList& paths)
{
    m_recentProjects = paths.mid(0, maxRecentProjects);
    rebuildRecentList();
}

void StartPanel::addRecentProject(const QString& path)
{
    if (path.isEmpty()) {
        return;
    }
    m_recentProjects.removeAll(path);
    m_recentProjects.prepend(path);
    while (m_recentProjects.size() > maxRecentProjects) {
        m_recentProjects.removeLast();
    }
    rebuildRecentList();
}

// One item per project: its file name, with the full path as tooltip.
void StartPanel::rebuildRecentList()
{
    m_recentList->clear();
    for (const QString& path : m_recentProjects) {
        const QFileInfo info(path);
        QListWidgetItem* item = new QListWidgetItem(info.fileName(), m_recentList);
        item->setToolTip(path);
        item->setData(Qt::UserRole, path);
    }
}

} // namespace ui
} // namespace openvegas
