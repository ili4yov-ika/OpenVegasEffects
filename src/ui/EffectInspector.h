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
namespace media { class MediaManager; }
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
    // Layer sizes for the particle-texture warning come from the media.
    void setMediaManager(std::shared_ptr<media::MediaManager> media) { m_mediaManager = std::move(media); }
    void setSelection(int layerIndex, int clipIndex);
    void refresh();
    // The shown values read again, editors kept (a module set some itself).
    void refreshValues();
    void setCurrentTime(double seconds);

signals:
    void effectParamsChanged();
    void keyFramesChanged();
    void motionTrackingRequested(int layerIndex, int trackIndex);
    // The user set an effect parameter here (not undo/redo): modules that
    // live on an instance of their own are told (Notify 7).
    void effectParameterEdited(int layerIndex, int clipIndex, int effectIndex, int parameterIndex);

private:
    using TransformProp = composition::TransformProperty;
    void buildLayerProperties(const composition::Layer& layer);
    void buildTracks(const composition::Layer& layer);
    void buildMasks(const composition::Layer& layer);
    void buildEffects(const composition::Layer& layer, bool behaviors = false);
    void buildTransformSection(const composition::Layer& layer, int frame);
    void applySearch();
    // Rows of controls a native module switched off are hidden, the others
    // shown again (plugin::nativeControlShown).
    void applyControlStates();
    void updateHeader();

    composition::Layer* layerRef();
    composition::Effect* effectAt(int effectIndex);
    int currentFrame() const;
    bool hasAnimatedParameters() const;
    QVariant typedValue(int effectIndex, int paramIndex, const QVariant& raw) const;
    static double transformValue(const composition::Layer& layer, TransformProp prop,
                                 int axis, int frame);

    // False when the edit changed nothing (or the layer is locked).
    bool editModel(const QString& title, const QString& mergeKey,
                   const std::function<void(composition::Layer&)>& edit,
                   bool keyFrames = false, bool rebuild = false);
    void applyParamValue(int effectIndex, int paramIndex, const QVariant& value);
    void toggleParamKeyFrame(int effectIndex, int paramIndex, bool on);
    void applyTransformValue(TransformProp prop, int axis, double value);
    void toggleTransformKeyFrame(TransformProp prop, bool on);

    plugin::PluginManager* m_pluginManager = nullptr;
    std::shared_ptr<composition::Composition> m_comp;
    std::shared_ptr<media::MediaManager> m_mediaManager;
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
