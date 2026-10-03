#pragma once

#include <functional>
#include <memory>
#include <utility>
#include <QUndoCommand>
#include "composition/Composition.h"

namespace openvegas::ui {

// Keep the current key's identity, interpolation and handles when Layout
// changes an animated property. Unanimated properties remain static.
inline void writeLayoutValue(composition::KeyFrameList& curve, int frame,
                             double value, double& fallback)
{
    if (curve.isEmpty()) fallback = value;
    else if (auto* key = curve.keyAt(frame)) key->value = value;
    else curve.set(frame, value);
}

inline void writeLayoutPoint(composition::KeyFrameList& xCurve,
                             composition::KeyFrameList& yCurve, int frame,
                             const QPointF& value, QPointF& fallback)
{
    if (xCurve.isEmpty() && yCurve.isEmpty()) {
        fallback = value;
        return;
    }
    if (xCurve.isEmpty()) {
        xCurve.setDefaultValue(fallback.x());
        xCurve.set(frame, value.x());
    } else {
        double unused = fallback.x();
        writeLayoutValue(xCurve, frame, value.x(), unused);
    }
    if (yCurve.isEmpty()) {
        yCurve.setDefaultValue(fallback.y());
        yCurve.set(frame, value.y());
    } else {
        double unused = fallback.y();
        writeLayoutValue(yCurve, frame, value.y(), unused);
    }
}

struct LayoutTransformChange {
    core::Identifier layerId;
    composition::LayerTransform before;
    composition::LayerTransform after;
};

// One Layout action is one history entry for the entire selection. Resolve
// layers by ID so a later stack reorder cannot redirect Undo to another layer.
class LayoutTransformCommand final : public QUndoCommand {
public:
    LayoutTransformCommand(std::shared_ptr<composition::Composition> composition,
                           QVector<LayoutTransformChange> changes,
                           std::function<void()> changed, const QString& title)
        : QUndoCommand(title), m_composition(std::move(composition)),
          m_changes(std::move(changes)), m_changed(std::move(changed)) {}

    void undo() override { apply(false); }
    void redo() override { apply(true); }

private:
    void apply(bool forward) {
        if (!m_composition) return;
        for (const auto& change : m_changes) {
            for (int index = 0; index < m_composition->layers().size(); ++index) {
                if (m_composition->layers()[index].id != change.layerId) continue;
                m_composition->layerRef(index).transform = forward ? change.after : change.before;
                break;
            }
        }
        if (m_changed) m_changed();
    }
    std::shared_ptr<composition::Composition> m_composition;
    QVector<LayoutTransformChange> m_changes;
    std::function<void()> m_changed;
};

} // namespace openvegas::ui
