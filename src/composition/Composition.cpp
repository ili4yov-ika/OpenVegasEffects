#include "composition/Composition.h"

#include <algorithm>
#include <cmath>

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

bool Composition::moveLayers(QVector<int> rows, int dropRow, QVector<int>* newRows)
{
    const int count = m_layers.size();
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    if (rows.isEmpty() || rows.first() < 0 || rows.last() >= count) return false;
    dropRow = qBound(0, dropRow, count);
    // The moved layers come out in their order and go back in before the
    // first layer at or after the drop row that is not moved itself.
    QVector<int> order;
    order.reserve(count);
    for (int i = 0; i < count; ++i)
        if (!rows.contains(i)) order.append(i);
    int insertAt = 0;
    while (insertAt < order.size() && order.at(insertAt) < dropRow) ++insertAt;
    for (int k = 0; k < rows.size(); ++k) order.insert(insertAt + k, rows.at(k));
    bool changed = false;
    for (int i = 0; i < count && !changed; ++i) changed = order.at(i) != i;
    if (!changed) return false;
    QVector<Layer> moved;
    moved.reserve(count);
    for (int oldIndex : std::as_const(order)) moved.append(m_layers.at(oldIndex));
    m_layers = moved;
    for (int i = 0; i < m_layers.size(); ++i) m_layers[i].zIndex = i;
    if (newRows) {
        newRows->fill(-1, count);
        for (int i = 0; i < count; ++i) (*newRows)[order.at(i)] = i;
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

void Composition::frameRateFraction(double fps, int* numerator, int* denominator)
{
    int num = 30, den = 1;
    if (fps > 0.0 && std::isfinite(fps)) {
        const double whole = std::round(fps);
        const double ntsc = std::round(fps * 1.001);
        if (std::abs(fps - whole) < 1e-3) {
            num = int(whole);
        } else if (std::abs(fps - ntsc / 1.001) < 5e-3) {
            num = int(ntsc) * 1000;
            den = 1001;
        } else {
            num = int(std::round(fps * 1000.0));
            den = 1000;
        }
    }
    if (numerator) *numerator = num;
    if (denominator) *denominator = den;
}

void Composition::addCompositeShot(const std::shared_ptr<Composition>& shot, int index)
{
    if (!shot || shot.get() == this || compositeShot(shot->id())) return;
    if (index < 0 || index > m_compositeShots.size()) index = m_compositeShots.size();
    m_compositeShots.insert(index, shot);
}

int Composition::removeCompositeShot(const core::Identifier& id)
{
    for (int i = 0; i < m_compositeShots.size(); ++i) {
        if (m_compositeShots.at(i) && m_compositeShots.at(i)->id() == id) {
            m_compositeShots.removeAt(i);
            return i;
        }
    }
    return -1;
}

std::shared_ptr<Composition> Composition::compositeShot(const core::Identifier& id) const
{
    for (const auto& shot : m_compositeShots)
        if (shot && shot->id() == id) return shot;
    return nullptr;
}

void Composition::clear()
{
    m_id = core::Identifier(QUuid::createUuid().toString(QUuid::WithoutBraces));
    m_layers.clear();
    m_durationSeconds = 10.0;
    m_editorSequence = EditorSequence();
    m_renderSettings = CompositionRenderSettings();
    m_currentFrame = 0;
    m_workIn = 0;
    m_workOut = -1;
    m_projectId = core::Identifier(QUuid::createUuid().toString(QUuid::WithoutBraces));
    m_projectSettings = ProjectRenderSettings();
    m_nativeSource.reset();
    m_pixelAspect = SquarePixels;
    m_customPixelAspect = 1.0;
    m_audioSampleRate = 48000;
    m_primary = false;
    m_compositeShots.clear();
    m_openShotIds.clear();
    m_activeShotId.clear();
}

void forEachLayerCurve(Layer& layer, const std::function<void(KeyFrameList&)>& visit)
{
    const auto transformCurves = [&visit](LayerTransform& t) {
        for (KeyFrameList* curve : {&t.positionXCurve, &t.positionYCurve, &t.positionZCurve, &t.scaleXCurve,
                                    &t.scaleYCurve, &t.scaleZCurve, &t.rotationCurve, &t.rotationXCurve,
                                    &t.rotationYCurve, &t.orientationXCurve, &t.orientationYCurve,
                                    &t.orientationZCurve, &t.opacityCurve, &t.audioLevelCurve, &t.targetXCurve,
                                    &t.targetYCurve, &t.targetZCurve})
            visit(*curve);
    };
    transformCurves(layer.transform);
    LightSettings& light = layer.light;
    for (KeyFrameList* curve : {&light.intensityCurve, &light.colorCurve, &light.reachCurve, &light.coneAngleCurve,
                                &light.featherCurve, &light.shadowOpacityCurve, &light.shadowDiffusionCurve,
                                &light.shadowColorCurve})
        visit(*curve);
    CameraSettings& camera = layer.camera;
    for (KeyFrameList* curve : {&camera.zoomCurve, &camera.focusDistanceCurve, &camera.apertureCurve,
                                &camera.blurCurve})
        visit(*curve);
    SurfaceShading& surface = layer.surface;
    for (KeyFrameList* curve : {&surface.ambientCurve, &surface.diffuseCurve, &surface.specularCurve,
                                &surface.shininessCurve, &surface.emissiveCurve, &surface.specularReflectivityCurve,
                                &surface.diffuseReflectivityCurve})
        visit(*curve);
    for (MotionTrack& track : layer.motionTracks) {
        visit(track.xCurve);
        visit(track.yCurve);
    }
    for (ModelNodeTransform& node : layer.modelNodes) transformCurves(node.transform);
    for (ModelAnimationInstance& animation : layer.modelAnimations) {
        visit(animation.strengthCurve);
        visit(animation.playbackCurve);
    }
    for (Clip& clip : layer.clips) {
        for (Effect& effect : clip.effects) {
            for (auto it = effect.animation.begin(); it != effect.animation.end(); ++it) visit(it.value());
        }
    }
}

void shiftLayerKeys(Layer& layer, int frames)
{
    if (frames == 0) return;
    forEachLayerCurve(layer, [frames](KeyFrameList& curve) { curve.shift(frames); });
}

} // namespace composition
} // namespace openvegas
