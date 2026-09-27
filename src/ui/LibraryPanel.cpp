#include "ui/LibraryPanel.h"
#include "ui_Library.h"

#include <QDirIterator>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

namespace openvegas {
namespace ui {

LibraryPanel::LibraryPanel(QWidget* parent)
    : QDockWidget(parent)
{
    Ui::LibraryPanel form;
    form.setupUi(this);
    m_status = form.libraryStatus;
    m_list = form.libraryList;
    QToolButton* reloadButton = form.toolButtonReloadLibrary;
    connect(reloadButton, &QToolButton::clicked, this, &LibraryPanel::reload);

    connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        if (!item) {
            return;
        }
        const QString library = item->data(Qt::UserRole).toString();
        const QString preset = item->data(Qt::UserRole + 1).toString().isEmpty()
                                   ? item->text() : item->data(Qt::UserRole + 1).toString();
        emit presetActivated(library, preset);
    });
}

void LibraryPanel::setLibraries(const QStringList& names)
{
    m_list->clear();
    for (const QString& name : names) {
        auto* header = new QListWidgetItem(name, m_list);
        header->setFlags(header->flags() & ~Qt::ItemIsSelectable);
        header->setData(Qt::UserRole, name);
    }
}

void LibraryPanel::addPreset(const QString& library, const QString& preset)
{
    auto* item = new QListWidgetItem(preset, m_list);
    item->setData(Qt::UserRole, library);
    item->setData(Qt::UserRole + 1, preset);
}

void LibraryPanel::setLibraryPaths(const QString& mediaPath, const QString& templatePath)
{
    m_mediaPath = mediaPath;
    m_templatePath = templatePath;
    reload();
}

void LibraryPanel::reload()
{
    m_list->clear();
    m_status->setText(tr("Loading Library..."));
    QTimer::singleShot(0, this, [this] {
        int count = 0;
        QFileIconProvider icons;
        const auto addDirectory = [this, &count, &icons](const QString& title,
                                                         const QString& path) {
            auto* header = new QListWidgetItem(title, m_list);
            header->setFlags(header->flags() & ~Qt::ItemIsSelectable);
            header->setData(Qt::UserRole, title);
            if (path.isEmpty() || !QFileInfo::exists(path)) {
                header->setToolTip(tr("Folder not found: %1").arg(path));
                return;
            }
            QDirIterator iterator(path, QDir::Files | QDir::Readable,
                                  QDirIterator::Subdirectories);
            while (iterator.hasNext()) {
                const QFileInfo info(iterator.next());
                auto* item = new QListWidgetItem(icons.icon(info), info.completeBaseName(), m_list);
                item->setData(Qt::UserRole, title);
                item->setData(Qt::UserRole + 1, info.absoluteFilePath());
                item->setToolTip(info.absoluteFilePath());
                ++count;
            }
        };
        addDirectory(tr("Media"), m_mediaPath);
        addDirectory(tr("Templates"), m_templatePath);
        m_status->setText(tr("%1 library item(s)").arg(count));
    });
}

} // namespace ui
} // namespace openvegas
