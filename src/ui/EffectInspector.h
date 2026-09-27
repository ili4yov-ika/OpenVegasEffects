#pragma once

#include <QDockWidget>
#include <QVariant>

#include <functional>
#include <memory>

#include "composition/Composition.h"
#include "plugin/EffectSpec.h"

class QLabel;
class QLineEdit;
class QTreeWidget;
class QUndoStack;

namespace openvegas {
namespace plugin { class PluginManager; }
namespace ui {

// Inspector for the currently selected timeline layer/clip. It intentionally
// presents the same model as TimelineWidget: changes made in either view are
// immediately visible in the other one.
class EffectInspector : public QDockWidget
{
    Q_OBJECT
public:
    explicit EffectInspector(QWidget* parent = nullptr);

    void setPluginManager(plugin::PluginManager* pluginManager);
    void setComposition(std::shared_ptr<composition::Composition> comp);
    void setUndoStack(QUndoStack* stack) { m_undoStack = stack; }
    void setSelection(int layerIndex, int clipIndex);
    void refresh();
    void setCurrentTime(double seconds);

signals:
    void effectParamsChanged();
    void keyFramesChanged();
    void motionTrackingRequested(int layerIndex, int trackIndex);

private:
    using TransformProp = composition::TransformProperty;
    void buildLayerProperties(const composition::Layer& layer);
    void buildTracks(const composition::Layer& layer);
    void buildMasks(const composition::Layer& layer);
    void buildEffects(const composition::Layer& layer, bool behaviors = false);
    void buildTransformSection(const composition::Layer& layer, int frame);
    void applySearch();
    void refreshValues();
    void updateHeader();

    composition::Layer* layerRef();
    composition::Effect* effectAt(int effectIndex);
    int currentFrame() const;
    bool hasAnimatedParameters() const;
    QVariant typedValue(int effectIndex, int paramIndex, const QVariant& raw) const;
    static double transformValue(const composition::Layer& layer, TransformProp prop,
                                 int axis, int frame);

    void editModel(const QString& title, const QString& mergeKey,
                   const std::function<void(composition::Layer&)>& edit,
                   bool keyFrames = false, bool rebuild = false);
    void applyParamValue(int effectIndex, int paramIndex, const QVariant& value);
    void toggleParamKeyFrame(int effectIndex, int paramIndex, bool on);
    void applyTransformValue(TransformProp prop, int axis, double value);
    void toggleTransformKeyFrame(TransformProp prop, bool on);

    plugin::PluginManager* m_pluginManager = nullptr;
    std::shared_ptr<composition::Composition> m_comp;
    QUndoStack* m_undoStack = nullptr;
    int m_layerIndex = -1;
    int m_clipIndex = -1;
    double m_time = 0.0;
    bool m_building = false;
    bool m_scaleLinked = true;
    bool m_followPlayheadClip = false;

    QLabel* m_selectionTitle = nullptr;
    QLabel* m_timeLabel = nullptr;
    QLineEdit* m_search = nullptr;
    QTreeWidget* m_tree = nullptr;
    QVector<std::function<void()>> m_valueReaders;
};

} // namespace ui
} // namespace openvegas
