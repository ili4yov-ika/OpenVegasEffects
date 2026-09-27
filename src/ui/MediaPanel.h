#pragma once

#include <QDockWidget>

#include <QVector>
#include <QStringList>

#include "media/MediaManager.h"
#include "media/MediaAsset.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QToolButton;

namespace openvegas {
namespace ui {

// Mirrors the reference "Media" panel: lists imported media assets with
// search/filter and metadata, and supports drag-and-drop (export a file path
// onto the timeline; drop files here to import them).
class MediaPanel : public QDockWidget
{
    Q_OBJECT

public:
    explicit MediaPanel(QWidget* parent = nullptr);

    void setMediaManager(media::MediaManager* manager);
    void refresh();
    QString selectedFilePath() const;

signals:
    void mediaActivated(const QString& filePath);
    void importRequested(const QStringList& paths);
    void importCommandRequested();
    void mediaRemoved(const QString& filePath);
    void relinkRequested(const QString& oldPath, const QString& newPath);
    void newCompositeShotRequested();

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void rebuildList();
    void removeSelected();
    void updateCount();
    QListWidgetItem* itemForAsset(int index) const;
    QString metadataText(const media::MediaAsset& asset) const;
    void createFolder();
    void showContextMenu(const QPoint& position);

    enum class GroupMode { None, Folder, Media };
    enum class ArrangeMode { Name, Type };

    media::MediaManager* m_manager = nullptr;
    QLineEdit* m_search = nullptr;
    QListWidget* m_list = nullptr;
    QToolButton* m_removeButton = nullptr;
    QLabel* m_countLabel = nullptr;
    QStringList m_virtualFolders;
    GroupMode m_groupMode = GroupMode::Folder;
    ArrangeMode m_arrangeMode = ArrangeMode::Name;
    bool m_thumbnailMode = false;
};

} // namespace ui
} // namespace openvegas
