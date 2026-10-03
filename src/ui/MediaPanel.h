#pragma once

#include <QDockWidget>

#include <QVector>
#include <QPointer>
#include <QStringList>

#include "media/MediaManager.h"
#include "media/MediaAsset.h"

class QLabel;
class QMimeData;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QToolButton;
class QUndoStack;

namespace openvegas {
namespace ui {

// Mirrors the reference "Media" panel: lists imported media assets with
// search/filter and metadata, and supports drag-and-drop (export a file path
// onto the timeline; drop files here to import them).
class MediaPanel : public QDockWidget
{
    Q_OBJECT

public:
    // A composite shot of the project, listed beside the media as the
    // reference lists its CompositionAssets.
    struct CompositeShotEntry
    {
        QString id;
        QString name;
        QSize size;
        double frameRate = 30.0;
        double durationSeconds = 0.0;
        bool primary = false;
        bool open = false;
    };

    explicit MediaPanel(QWidget* parent = nullptr);

    void setMediaManager(media::MediaManager* manager);
    void setCompositeShots(const QVector<CompositeShotEntry>& shots);
    // Id of the composite shot selected in the list, empty for media.
    QString selectedCompositeShotId() const;
    // The drag a row starts: the media's path or the shot's id, for the
    // timeline to make a layer of (the asset's file as a URL too, for the
    // Trimmer and other drop targets).
    QMimeData* dragData(const QListWidgetItem* item) const;
    void setUndoStack(QUndoStack* stack);
    void setMediaLabel(const core::Identifier& id, const QColor& color);
    // The asset's pixel aspect override (MediaOverrideOptions::
    // PixelAspectRatio), one History step.
    void setMediaPixelAspect(const core::Identifier& id, bool overridden, int pixelAspect);
    // Every Media Properties setting of `edited` onto the asset, one History
    // step (named after the property when only one changed).
    void setMediaOverrides(const core::Identifier& id, const media::MediaAsset& edited);
    // Media > Properties (MediaSettingsDialog) for a video or image asset.
    void showMediaProperties(const core::Identifier& id);
    void refresh();
    QString selectedFilePath() const;

signals:
    void mediaActivated(const QString& filePath);
    void importRequested(const QStringList& paths);
    void importCommandRequested();
    void mediaRemoved(const QString& filePath);
    void mediaMetadataModified();
    void relinkRequested(const QString& oldPath, const QString& newPath);
    void proxyModeRequested(const core::Identifier& assetId, media::ProxyMode mode);
    void newCompositeShotRequested();
    void compositeShotActivated(const QString& shotId);
    void compositeShotPropertiesRequested(const QString& shotId);
    void compositeShotRemoveRequested(const QString& shotId);
    void compositeShotSaveRequested(const QString& shotId);
    void primaryCompositeShotRequested(const QString& shotId, bool primary);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
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
    quint64 m_managerEpoch = 0;
    QPointer<QUndoStack> m_undoStack;
    QLineEdit* m_search = nullptr;
    QListWidget* m_list = nullptr;
    QToolButton* m_removeButton = nullptr;
    QLabel* m_countLabel = nullptr;
    QStringList m_virtualFolders;
    QVector<CompositeShotEntry> m_shots;
    QPoint m_dragStart;
    int m_dragRow = -1;
    GroupMode m_groupMode = GroupMode::Folder;
    ArrangeMode m_arrangeMode = ArrangeMode::Name;
    bool m_thumbnailMode = false;
};

} // namespace ui
} // namespace openvegas
