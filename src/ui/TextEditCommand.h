#pragma once
#include "composition/Composition.h"
#include <QUndoCommand>
#include <QDateTime>
#include <functional>
#include <memory>

namespace openvegas::ui {
// Index-based targeting survives vector reallocation; structural edits are
// undone first by the shared undo stack, restoring these indexes.
class TextEditCommand : public QUndoCommand
{
public:
    TextEditCommand(std::shared_ptr<composition::Composition> composition,
                    int layer, int clip, int effect, QStringList before, QStringList after,
                    std::function<void()> changed,
                    const QString& title = QObject::tr("Set Text Format"))
        : QUndoCommand(title), m_composition(std::move(composition)),
          m_layer(layer), m_clip(clip), m_effect(effect), m_before(std::move(before)),
          m_after(std::move(after)), m_changed(std::move(changed)),
          m_time(QDateTime::currentMSecsSinceEpoch())
    {
        for (int i = 0; i < qMax(m_before.size(), m_after.size()); ++i)
            if (m_before.value(i) != m_after.value(i)) m_fields.append(i);
    }
    void undo() override { apply(m_before); }
    void redo() override { apply(m_after); }
    int id() const override { return 0x545854; }
    bool mergeWith(const QUndoCommand* command) override
    {
        const auto* next = static_cast<const TextEditCommand*>(command);
        if (m_composition != next->m_composition || m_layer != next->m_layer
            || m_clip != next->m_clip || m_effect != next->m_effect || m_fields != next->m_fields
            || next->m_time - m_time > 500 || m_after != next->m_before) return false;
        m_after = next->m_after;
        m_time = next->m_time;
        setObsolete(m_before == m_after);
        return true;
    }
private:
    void apply(const QStringList& values)
    {
        auto* clip = m_composition->clipAt(m_layer, m_clip);
        if (!clip || m_effect < 0 || m_effect >= clip->effects.size()) return;
        clip->effects[m_effect].parameterValues = values;
        if (m_changed) m_changed();
    }
    std::shared_ptr<composition::Composition> m_composition;
    int m_layer, m_clip, m_effect;
    QStringList m_before, m_after;
    QList<int> m_fields;
    std::function<void()> m_changed;
    qint64 m_time;
};
}
