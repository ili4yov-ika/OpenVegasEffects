#pragma once

#include <QColor>
#include <QDockWidget>
#include <QImage>
#include <QVector>
#include <functional>
#include <memory>

#include "composition/Composition.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QUndoStack;

namespace openvegas::media { class MediaManager; }
namespace openvegas::render { class RenderManager; }
namespace openvegas::ui {

// Selected-layer viewer and editor. TrackPanel owns the separate composition
// layer list that was previously (and incorrectly) implemented here.
class LayerPanel : public QDockWidget
{
    Q_OBJECT
public:
    explicit LayerPanel(QWidget* parent = nullptr);
    void bindModel(std::shared_ptr<composition::Composition> composition);
    void setUndoStack(QUndoStack* stack) { m_undoStack = stack; }
    void setSelection(int layerIndex);
    void setSelection(const QVector<int>& layerIndexes);
    int selectedLayer() const;
    void refresh();
    void setMediaManager(std::shared_ptr<media::MediaManager> media);
    void setCurrentTime(double seconds);
    std::shared_ptr<composition::Composition> previewComposition() const;
    QImage previewFrame() const { return m_previewFrame; }

signals:
    void layersModified();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void buildEditor(QFormLayout* form);
    void showMessage(const QString& text);
    void editLayer(const QString& title, const std::function<void(composition::Layer&)>& change, bool allowLocked = false);
    void chooseColor(bool planeColor);
    void updateColorButton(QPushButton* button, const QColor& color);
    bool wouldCreateParentCycle(int candidate) const;

    void requestPreview();
    void updatePreviewPixmap();
    std::shared_ptr<media::MediaManager> m_media;
    render::RenderManager* m_previewRenderer = nullptr;
    QLabel* m_preview = nullptr;
    QImage m_previewFrame;
    double m_time = 0.0;
    int m_previewGeneration = 0;
    std::shared_ptr<composition::Composition> m_composition;
    QUndoStack* m_undoStack = nullptr;
    QVector<int> m_selection;
    bool m_updating = false;
    QStackedWidget* m_stack = nullptr;
    QWidget* m_pageLayer = nullptr;
    QWidget* m_pageNoLayer = nullptr;
    QLabel* m_message = nullptr;
    QLabel* m_breadcrumb = nullptr;
    QLabel* m_kind = nullptr;
    QLabel* m_id = nullptr;
    QLabel* m_planeColorLabel = nullptr;
    QLineEdit* m_name = nullptr;
    QCheckBox* m_visible = nullptr;
    QCheckBox* m_muted = nullptr;
    QCheckBox* m_locked = nullptr;
    QComboBox* m_blend = nullptr;
    QComboBox* m_dimension = nullptr;
    QComboBox* m_parent = nullptr;
    QDoubleSpinBox* m_opacity = nullptr;
    QDoubleSpinBox* m_cameraFov = nullptr;
    QLabel* m_cameraFovLabel = nullptr;
    QPushButton* m_labelColor = nullptr;
    QPushButton* m_planeColor = nullptr;
};

} // namespace openvegas::ui
