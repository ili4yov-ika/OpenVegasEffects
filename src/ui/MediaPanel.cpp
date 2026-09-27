#include "ui/MediaPanel.h"
#include "ui_Media.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QFileDialog>
#include <QFileInfo>
#include <QDateTime>
#include <QImageReader>
#include <QPixmapCache>
#include <QInputDialog>
#include <QMenu>
#include <QMimeData>
#include <QPixmap>
#include <QSet>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace openvegas {
namespace ui {

namespace {
QString kindText(media::MediaKind kind)
{
    switch (kind) {
    case media::MediaKind::Video:
        return QStringLiteral("Video");
    case media::MediaKind::Image:
        return QStringLiteral("Image");
    case media::MediaKind::Audio:
        return QStringLiteral("Audio");
    case media::MediaKind::Composition:
        return QStringLiteral("Composition");
    }
    return QString();
}

// Formats a duration in seconds as HH:MM:SS.mmm for the metadata line.
QString formatDuration(double seconds)
{
    if (seconds < 0.0) {
        seconds = 0.0;
    }
    const int totalMs = static_cast<int>(seconds * 1000.0 + 0.5);
    const int ms = totalMs % 1000;
    const int totalSeconds = totalMs / 1000;
    const int h = totalSeconds / 3600;
    const int m = (totalSeconds % 3600) / 60;
    const int s = totalSeconds % 60;
    return QStringLiteral("%1:%2:%3.%4")
        .arg(h, 2, 10, QLatin1Char('0'))
        .arg(m, 2, 10, QLatin1Char('0'))
        .arg(s, 2, 10, QLatin1Char('0'))
        .arg(ms, 3, 10, QLatin1Char('0'));
}
} // namespace

MediaPanel::MediaPanel(QWidget* parent)
    : QDockWidget(parent)
{
    Ui::MediaPanel form;
    form.setupUi(this);
    m_search = form.media_panel_search;
    m_removeButton = form.MediaPanelTrashButton;
    m_countLabel = form.media_panel_count;
    m_list = form.listView;
    // uic sanitises '-' into '_' in both the generated member and the runtime
    // name.  Restore the reference names used by styles and automation.
    m_search->setObjectName(QStringLiteral("media-panel-search"));
    m_countLabel->setObjectName(QStringLiteral("media-panel-count"));

    connect(m_search, &QLineEdit::textChanged, this, [this](const QString&) { rebuildList(); });
    connect(form.toolButtonImport, &QToolButton::clicked,
            this, &MediaPanel::importCommandRequested);
    form.toolButtonImport->setIcon(QIcon(QStringLiteral(":/icons/open.svg")));
    form.MediaPanelNewFolderButton->setIcon(QIcon(QStringLiteral(":/icons/add.svg")));
    form.MediaPanelFolderButton->setIcon(QIcon(QStringLiteral(":/icons/open.svg")));
    form.MediaPanelCompositeShotButton->setIcon(
        QIcon(QStringLiteral(":/icons/render-timeline.svg")));
    m_removeButton->setIcon(QIcon(QStringLiteral(":/text-icons/remove.svg")));
    form.toolButtonMediaOptions->setIcon(QIcon(QStringLiteral(":/icons/list.svg")));
    form.toolButtonMediaThumbnails->setIcon(QIcon(QStringLiteral(":/icons/grid.svg")));

    connect(form.MediaPanelFolderButton, &QToolButton::clicked,
            this, &MediaPanel::createFolder);
    connect(form.MediaPanelCompositeShotButton, &QToolButton::clicked,
            this, &MediaPanel::newCompositeShotRequested);
    connect(m_removeButton, &QToolButton::clicked, this, &MediaPanel::removeSelected);

    auto* viewModes = new QButtonGroup(this);
    viewModes->setExclusive(true);
    viewModes->addButton(form.toolButtonMediaOptions);
    viewModes->addButton(form.toolButtonMediaThumbnails);
    connect(form.toolButtonMediaOptions, &QToolButton::clicked, this, [this] {
        m_thumbnailMode = false;
        m_list->setViewMode(QListView::ListMode);
        m_list->setIconSize(QSize(72, 48));
        rebuildList();
    });
    connect(form.toolButtonMediaThumbnails, &QToolButton::clicked, this, [this] {
        m_thumbnailMode = true;
        m_list->setViewMode(QListView::IconMode);
        m_list->setIconSize(QSize(96, 54));
        m_list->setResizeMode(QListView::Adjust);
        rebuildList();
    });

    QMenu* arrangeMenu = new QMenu(form.mediaArrangeButton);
    QAction* byName = arrangeMenu->addAction(tr("Name"));
    QAction* byType = arrangeMenu->addAction(tr("Type"));
    form.mediaArrangeButton->setMenu(arrangeMenu);
    connect(byName, &QAction::triggered, this, [this, button = form.mediaArrangeButton] {
        m_arrangeMode = ArrangeMode::Name;
        button->setText(tr("Arrange By: Name"));
        rebuildList();
    });
    connect(byType, &QAction::triggered, this, [this, button = form.mediaArrangeButton] {
        m_arrangeMode = ArrangeMode::Type;
        button->setText(tr("Arrange By: Type"));
        rebuildList();
    });

    QMenu* groupMenu = new QMenu(form.mediaGroupButton);
    QAction* noGroup = groupMenu->addAction(tr("None"));
    QAction* folderGroup = groupMenu->addAction(tr("Folder"));
    QAction* mediaGroup = groupMenu->addAction(tr("Media"));
    form.mediaGroupButton->setMenu(groupMenu);
    connect(noGroup, &QAction::triggered, this, [this, button = form.mediaGroupButton] {
        m_groupMode = GroupMode::None;
        button->setText(tr("Group By: None"));
        rebuildList();
    });
    connect(folderGroup, &QAction::triggered, this, [this, button = form.mediaGroupButton] {
        m_groupMode = GroupMode::Folder;
        button->setText(tr("Group By: Folder"));
        rebuildList();
    });
    connect(mediaGroup, &QAction::triggered, this, [this, button = form.mediaGroupButton] {
        m_groupMode = GroupMode::Media;
        button->setText(tr("Group By: Media"));
        rebuildList();
    });

    QMenu* newMenu = new QMenu(form.MediaPanelNewFolderButton);
    QAction* newFolder = newMenu->addAction(tr("Folder"));
    QAction* newComposite = newMenu->addAction(tr("Composite Shot"));
    form.MediaPanelNewFolderButton->setMenu(newMenu);
    form.MediaPanelNewFolderButton->setPopupMode(QToolButton::InstantPopup);
    connect(newFolder, &QAction::triggered, this, &MediaPanel::createFolder);
    connect(newComposite, &QAction::triggered, this, &MediaPanel::newCompositeShotRequested);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_list, &QListWidget::customContextMenuRequested,
            this, &MediaPanel::showContextMenu);
    setAcceptDrops(true);

    connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        if (!item || !m_manager) {
            return;
        }
        const int index = item->data(Qt::UserRole).toInt();
        const QVector<media::MediaAsset> assets = m_manager->assets();
        if (index >= 0 && index < assets.size()) {
            emit mediaActivated(assets.at(index).filePath());
        }
    });
}

void MediaPanel::setMediaManager(media::MediaManager* manager)
{
    m_manager = manager;
    refresh();
}

void MediaPanel::refresh()
{
    rebuildList();
}

QString MediaPanel::selectedFilePath() const
{
    if (!m_manager || !m_list) {
        return QString();
    }
    QListWidgetItem* item = m_list->currentItem();
    if (!item) {
        return QString();
    }
    const int index = item->data(Qt::UserRole).toInt();
    const QVector<media::MediaAsset> assets = m_manager->assets();
    if (index >= 0 && index < assets.size()) {
        return assets.at(index).filePath();
    }
    return QString();
}

QListWidgetItem* MediaPanel::itemForAsset(int index) const
{
    if (!m_manager || index < 0 || index >= m_manager->assets().size()) {
        return nullptr;
    }
    for (int row = 0; row < m_list->count(); ++row) {
        QListWidgetItem* item = m_list->item(row);
        if (item->data(Qt::UserRole).isValid() && item->data(Qt::UserRole).toInt() == index)
            return item;
    }
    return nullptr;
}

QString MediaPanel::metadataText(const media::MediaAsset& asset) const
{
    QString meta = kindText(asset.kind());
    if (asset.frameSize().isValid() && !asset.frameSize().isEmpty()) {
        meta += QStringLiteral("  %1x%2").arg(asset.frameSize().width()).arg(asset.frameSize().height());
    }
    meta += QStringLiteral("  %1").arg(formatDuration(asset.durationSeconds()));
    return meta;
}

void MediaPanel::rebuildList()
{
    if (!m_list || !m_manager) {
        return;
    }

    const QString filter = m_search ? m_search->text().trimmed().toLower() : QString();
    m_list->clear();

    const QVector<media::MediaAsset> assets = m_manager->assets();
    QVector<int> indices;
    for (int i = 0; i < assets.size(); ++i) {
        const media::MediaAsset& asset = assets.at(i);
        if (!filter.isEmpty() && !asset.fileName().toLower().contains(filter)) {
            continue;
        }
        indices.append(i);
    }
    std::sort(indices.begin(), indices.end(), [&](int a, int b) {
        if (m_arrangeMode == ArrangeMode::Type) {
            const int typeCompare = kindText(assets.at(a).kind()).compare(
                kindText(assets.at(b).kind()), Qt::CaseInsensitive);
            if (typeCompare != 0) return typeCompare < 0;
        }
        return assets.at(a).fileName().compare(assets.at(b).fileName(),
                                               Qt::CaseInsensitive) < 0;
    });
    QSet<QString> visibleGroups;
    if (m_groupMode != GroupMode::None) {
        for (int i : indices) {
            const media::MediaAsset& asset = assets.at(i);
            visibleGroups.insert(m_groupMode == GroupMode::Folder
                                     ? QFileInfo(asset.filePath()).dir().dirName()
                                     : kindText(asset.kind()));
        }
    }
    // A heading that merely repeats the one and only group wastes an asset
    // row and makes the item count misleading.  The reference only needs the
    // separators once grouping actually divides the contents.
    const bool showGroupHeaders = visibleGroups.size() > 1;
    QString currentGroup;
    for (int i : indices) {
        const media::MediaAsset& asset = assets.at(i);
        QString group;
        if (m_groupMode == GroupMode::Folder) group = QFileInfo(asset.filePath()).dir().dirName();
        if (m_groupMode == GroupMode::Media) group = kindText(asset.kind());
        if (showGroupHeaders && !group.isEmpty() && group != currentGroup) {
            auto* header = new QListWidgetItem(group, m_list);
            header->setFlags(header->flags() & ~Qt::ItemIsSelectable & ~Qt::ItemIsDragEnabled);
            currentGroup = group;
        }
        const QString label = asset.fileName() + QStringLiteral("  ") + metadataText(asset);
        QListWidgetItem* item = new QListWidgetItem(label, m_list);
        item->setData(Qt::UserRole, i);
        item->setToolTip(asset.filePath());
        item->setData(Qt::UserRole + 1, asset.filePath()); // MIME payload on drag
        if (!QFileInfo::exists(asset.filePath())) item->setForeground(QColor(210, 80, 70));
        if (m_thumbnailMode && asset.kind() == media::MediaKind::Image) {
            const QFileInfo info(asset.filePath());
            const QString key = QStringLiteral("media-thumb:%1:%2:%3")
                .arg(info.absoluteFilePath()).arg(info.lastModified().toMSecsSinceEpoch()).arg(info.size());
            QPixmap thumbnail;
            if (!QPixmapCache::find(key, &thumbnail)) {
                QImageReader reader(asset.filePath());
                reader.setAutoTransform(true);
                const QSize size = reader.size();
                if (size.isValid()) reader.setScaledSize(size.scaled(QSize(72, 48), Qt::KeepAspectRatio));
                thumbnail = QPixmap::fromImage(reader.read());
                if (!thumbnail.isNull()) QPixmapCache::insert(key, thumbnail);
            }
            if (!thumbnail.isNull()) item->setIcon(QIcon(thumbnail));
        }
    }
    for (const QString& folder : m_virtualFolders) {
        auto* header = new QListWidgetItem(folder, m_list);
        header->setFlags(header->flags() & ~Qt::ItemIsSelectable & ~Qt::ItemIsDragEnabled);
        header->setToolTip(tr("Project media folder"));
    }
    updateCount();
}

void MediaPanel::createFolder()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New Folder"), tr("Folder name:"),
                                               QLineEdit::Normal, tr("New Folder"), &ok).trimmed();
    if (ok && !name.isEmpty() && !m_virtualFolders.contains(name)) {
        m_virtualFolders.append(name);
        rebuildList();
    }
}

void MediaPanel::showContextMenu(const QPoint& position)
{
    QListWidgetItem* item = m_list->itemAt(position);
    if (!item || !item->data(Qt::UserRole + 1).isValid()) return;
    const QString oldPath = item->data(Qt::UserRole + 1).toString();
    QMenu menu(this);
    QAction* relink = menu.addAction(tr("Relink Media..."));
    QAction* chosen = menu.exec(m_list->viewport()->mapToGlobal(position));
    if (chosen != relink) return;
    const QString newPath = QFileDialog::getOpenFileName(this, tr("Relink Media"),
                                                         QFileInfo(oldPath).absolutePath());
    if (!newPath.isEmpty()) emit relinkRequested(oldPath, newPath);
}

void MediaPanel::removeSelected()
{
    if (!m_manager || !m_list) {
        return;
    }
    QListWidgetItem* item = m_list->currentItem();
    if (!item) {
        return;
    }
    const int index = item->data(Qt::UserRole).toInt();
    const QVector<media::MediaAsset> assets = m_manager->assets();
    if (index < 0 || index >= assets.size()) {
        return;
    }
    const QString filePath = assets.at(index).filePath();
    m_manager->removeAsset(assets.at(index).id());
    rebuildList();
    emit mediaRemoved(filePath);
}

void MediaPanel::updateCount()
{
    if (!m_countLabel || !m_manager) {
        return;
    }
    m_countLabel->setText(
        tr("%1 item(s)").arg(m_manager->assets().size()));
}

void MediaPanel::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void MediaPanel::dropEvent(QDropEvent* event)
{
    const QMimeData* mime = event->mimeData();
    if (!mime || !mime->hasUrls()) {
        event->ignore();
        return;
    }
    QStringList paths;
    const QList<QUrl> urls = mime->urls();
    for (const QUrl& url : urls) {
        if (url.isLocalFile()) {
            paths.append(url.toLocalFile());
        }
    }
    if (paths.isEmpty()) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
    emit importRequested(paths);
}

} // namespace ui
} // namespace openvegas
