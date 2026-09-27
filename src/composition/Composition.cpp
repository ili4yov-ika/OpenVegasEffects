#include "composition/Composition.h"

namespace openvegas {
namespace composition {

Layer& Composition::addLayer(const QString& name)
{
    Layer layer;
    layer.name = name;
    layer.zIndex = m_layers.size();
    m_layers.push_back(layer);
    return m_layers.last();
}

bool Composition::insertLayer(int index, const Layer& layer)
{
    if (index < 0 || index > m_layers.size()) {
        return false;
    }
    m_layers.insert(index, layer);
    for (int i = 0; i < m_layers.size(); ++i) {
        m_layers[i].zIndex = i;
    }
    return true;
}

Layer Composition::layerCopy(int index) const
{
    if (index < 0 || index >= m_layers.size()) {
        return Layer();
    }
    return m_layers.at(index);
}

void Composition::setLayers(const QVector<Layer>& layers)
{
    m_layers = layers;
    for (int i = 0; i < m_layers.size(); ++i) {
        m_layers[i].zIndex = i;
    }
}

bool Composition::removeLayer(int index)
{
    if (index < 0 || index >= m_layers.size()) {
        return false;
    }
    const core::Identifier removedId = m_layers.at(index).id;
    m_layers.removeAt(index);
    // A child cannot retain a dangling parent.  The reference resolves the
    // relationship through layer IDs, so removing the parent promotes its
    // direct children to the composition root.
    for (Layer& layer : m_layers) {
        if (layer.parentLayerId == removedId) {
            layer.parentLayerId = core::Identifier();
        }
    }
    for (int i = 0; i < m_layers.size(); ++i) {
        m_layers[i].zIndex = i;
    }
    return true;
}

bool Composition::swapLayers(int a, int b)
{
    if (a < 0 || a >= m_layers.size() || b < 0 || b >= m_layers.size() || a == b) {
        return false;
    }
    m_layers.swapItemsAt(a, b);
    for (int i = 0; i < m_layers.size(); ++i) {
        m_layers[i].zIndex = i;
    }
    return true;
}

Layer* Composition::layer(const QString& name)
{
    for (Layer& layer : m_layers) {
        if (layer.name == name) {
            return &layer;
        }
    }
    return nullptr;
}

Layer& Composition::layerRef(int index)
{
    return m_layers[index];
}

Clip* Composition::addClip(const QString& layerName, const core::Identifier& mediaId,
                           double startSeconds, double durationSeconds)
{
    Layer* target = layer(layerName);
    if (!target) {
        addLayer(layerName);
        target = &m_layers.last();
    }

    Clip clip;
    clip.mediaId = mediaId;
    clip.startSeconds = startSeconds;
    clip.durationSeconds = durationSeconds;
    target->clips.push_back(clip);

    // Extend project duration to cover the clip if needed.
    const double end = startSeconds + durationSeconds;
    if (end > m_durationSeconds) {
        m_durationSeconds = end;
    }

    return &target->clips.last();
}

Clip* Composition::clipAt(int layerIndex, int clipIndex)
{
    if (layerIndex < 0 || layerIndex >= m_layers.size()) {
        return nullptr;
    }
    Layer& layer = m_layers[layerIndex];
    if (clipIndex < 0 || clipIndex >= layer.clips.size()) {
        return nullptr;
    }
    return &layer.clips[clipIndex];
}

void Composition::clear()
{
    m_layers.clear();
    m_durationSeconds = 10.0;
    m_editorSequence = EditorSequence();
}

} // namespace composition
} // namespace openvegas
