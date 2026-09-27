#pragma once

#include <QDockWidget>
#include <memory>

#include "composition/Composition.h"

class QTreeWidget;

namespace openvegas::ui {

// Track is the compact composition-layer list shown beside Media/Text/Effects.
// It is separate from the reference LayerPanelWidget, which inspects one
// selected layer and is implemented by LayerPanel.
class TrackPanel : public QDockWidget
{
    Q_OBJECT
public:
    explicit TrackPanel(QWidget* parent = nullptr);
    void bindModel(std::shared_ptr<composition::Composition> composition);
    void refresh();
    void applyFilter(const QString& filter);
    void setSelectedLayer(int row);

signals:
    void layersModified();
    void layerSelected(int row);
    void newLayerRequested(composition::LayerKind kind);

private:
    int selectedRow() const;
    void onDeleteLayer();
    void onMoveLayer(int delta);
    void onItemChanged(int row, int column);
    void emitModified();

    std::shared_ptr<composition::Composition> m_composition;
    QTreeWidget* m_tree = nullptr;
    QString m_filter;
    bool m_updating = false;
};

} // namespace openvegas::ui
