#include "ui/StartPanel.h"
#include "ui_Start.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

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
    connect(newProject, &QToolButton::clicked, this, &StartPanel::newProjectRequested);
    QToolButton* openProject = form.toolButtonOpen;
    connect(openProject, &QToolButton::clicked, this, &StartPanel::openProjectRequested);
    QToolButton* clearRecents = form.toolButtonClearRecents;
    connect(clearRecents, &QToolButton::clicked, this, [this] {
        setRecentProjects(QStringList());
        emit clearRecentsRequested();
    });
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

QWidget* StartPanel::takeContentWidget()
{
    QWidget* content = widget();
    if (content) {
        content->setParent(nullptr);
    }
    return content;
}

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
