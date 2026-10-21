#include "ui/NativeInstanceHost.h"

#include <QMatrix4x4>
#include <QThread>
#include <QTransform>

#include <cmath>

#include <QCoreApplication>

#include "composition/Composition.h"
#include "media/MediaManager.h"
#include "media/VideoProbe.h"
#include "render/LayerGeometry.h"
#include "render/RenderManager.h"

namespace openvegas::ui {

namespace {

// The steps AutomaticStabilizer and MotionLock name (BeginTransaction).
[[maybe_unused]] constexpr const char* kModuleSteps[] = {
    QT_TRANSLATE_NOOP("openvegas::ui::NativeInstanceHost", "Create Point Layer"),
    QT_TRANSLATE_NOOP("openvegas::ui::NativeInstanceHost", "Transform Layer"),
};

// Decoders kept open for footage analysis: one per file, a couple at most.
constexpr std::size_t kMaxDecoders = 2;
// Longest pause between background calls, whatever the module asks.
constexpr int kMaxBackgroundDelayMs = 1000;

int frameAt(double seconds, double rate)
{
    return qMax(0, qRound(seconds * rate));
}

const composition::Layer* findLayer(const composition::Composition& comp, const QString& id)
{
    for (const composition::Layer& layer : comp.layers()) {
        if (layer.id.value() == id) return &layer;
    }
    return nullptr;
}

// Of `layer`'s clips, the one that overlaps [start, end) most - the first
// when none does.
int overlappingClip(const composition::Layer& layer, double start, double end)
{
    int best = layer.clips.isEmpty() ? -1 : 0;
    double bestOverlap = 0.0;
    for (int i = 0; i < layer.clips.size(); ++i) {
        const composition::Clip& clip = layer.clips.at(i);
        const double overlap = qMin(end, clip.endSeconds()) - qMax(start, clip.startSeconds);
        if (overlap > bestOverlap) {
            bestOverlap = overlap;
            best = i;
        }
    }
    return best;
}

// The pixels of a clip's content: its footage, a nested shot's frame, or the
// canvas for content drawn on it (text, planes).
QSize contentSize(const composition::Composition& comp, const composition::Clip& clip,
                  const media::MediaManager* media)
{
    if (clip.nestedComposition) return clip.nestedComposition->displaySize();
    if (media && clip.mediaId.isValid()) {
        const media::MediaAsset asset = media->assetById(clip.mediaId);
        if (asset.isValid() && asset.frameSize().isValid()) return asset.frameSize();
    }
    return comp.displaySize();
}

// Content pixels (Y down, from the top-left corner) to the render canvas
// (display pixels, Y down), as RenderWorker::renderClip places media - a
// non-square pixel `aspect` times as wide.
QTransform contentPlacement(const composition::Composition& comp, const composition::Layer& layer,
                            const QSize& content, const QSize& canvas, int frame, double aspect = 1.0)
{
    const composition::LayerTransform& t = layer.transform;
    const QPointF scale = t.scaleAt(frame);
    const QPointF position = t.positionAt(frame);
    QTransform placement;
    placement.translate(canvas.width() / 2.0 + position.x(), canvas.height() / 2.0 - position.y());
    const double angle = t.rotationAt(frame);
    if (!qFuzzyIsNull(angle)) {
        placement.translate(t.anchorPoint.x(), -t.anchorPoint.y());
        placement.rotate(angle);
        placement.translate(-t.anchorPoint.x(), t.anchorPoint.y());
    }
    placement.scale(scale.x() / 100.0 * aspect, scale.y() / 100.0);
    placement.translate(-content.width() / 2.0, -content.height() / 2.0);
    // Then where its parent puts it, as renderClip draws it.
    return placement * render::layerCanvasParentAt(&comp, layer, frame, QSizeF(canvas.width(), canvas.height()));
}

std::array<float, 16> columnMajor(const QTransform& t)
{
    return {float(t.m11()), float(t.m12()), 0.0f, 0.0f,
            float(t.m21()), float(t.m22()), 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            float(t.dx()), float(t.dy()), 0.0f, 1.0f};
}

QStringList valuesAt(const composition::Effect& fx, int frame)
{
    QStringList values;
    values.reserve(fx.parameterValues.size());
    for (int p = 0; p < fx.parameterValues.size(); ++p) {
        values.append(fx.parameterAt(p, frame).toString());
    }
    return values;
}

} // namespace

NativeInstanceHost::NativeInstanceHost(CompositionProvider composition, MediaProvider media,
                                       QObject* parent)
    : QObject(parent)
    , m_composition(std::move(composition))
    , m_media(std::move(media))
{
    m_background.setSingleShot(true);
    connect(&m_background, &QTimer::timeout, this, &NativeInstanceHost::runBackground);
    // The services are only installed on the instance calls made here and by
    // the viewer's custom UI, both on the GUI thread; anything else gets no
    // answer rather than a look at the shot from another thread.
    plugin::NativeSourceHost host;
    host.layerInfo = [this](const QString& id) {
        return QThread::currentThread() == thread() ? layerInfo(id) : plugin::NativeLayerInfo();
    };
    host.assetInfo = [this](const QString& id) {
        return QThread::currentThread() == thread() ? assetInfo(id) : plugin::NativeAssetInfo();
    };
    host.assetFrame = [this](const QString& id, int frame) {
        return QThread::currentThread() == thread() ? assetFrame(id, frame) : QImage();
    };
    host.layers = [this] {
        QVector<plugin::NativeLayerEntry> layers;
        const auto comp = scope();
        if (!comp || QThread::currentThread() != thread()) return layers;
        for (const composition::Layer& layer : comp->layers()) layers.append({layer.id.value(), layer.name});
        return layers;
    };
    plugin::setNativeSourceHost(host);
}

NativeInstanceHost::~NativeInstanceHost()
{
    plugin::setNativeSourceHost({});
}

QString NativeInstanceHost::instanceKey(const Ref& ref)
{
    return render::nativeInstanceKey(ref.layerId, ref.clip, ref.effect);
}

std::array<float, 16> NativeInstanceHost::canvasMotion(const std::array<float, 16>& footageMotion,
                                                       const QTransform& placement,
                                                       const QSize& canvas)
{
    const std::array<float, 16>& m = footageMotion;
    // Into the footage's pixels, Y down; on the canvas that motion is seen
    // through the footage layer's placement; the renderer takes it in the
    // layer's terms.
    const QTransform flip = QTransform::fromScale(1.0, -1.0);
    const QTransform motion = flip * QTransform(m[0], m[1], m[4], m[5], m[12], m[13]) * flip;
    const QTransform onCanvas = placement.inverted() * motion * placement;
    const QTransform yUp = QTransform::fromTranslate(-canvas.width() / 2.0, -canvas.height() / 2.0)
                           * flip;
    return columnMajor(yUp.inverted() * onCanvas * yUp);
}

std::shared_ptr<composition::Composition> NativeInstanceHost::scope() const
{
    if (m_scope) return m_scope;
    return m_composition ? m_composition() : nullptr;
}

double NativeInstanceHost::frameRate() const
{
    const auto comp = scope();
    return comp && comp->fpsDenominator() > 0
        ? double(comp->fpsNumerator()) / comp->fpsDenominator() : 30.0;
}

composition::Effect* NativeInstanceHost::effectFor(const Ref& ref) const
{
    const auto comp = scope();
    if (!comp || !ref.isValid()) return nullptr;
    for (int i = 0; i < comp->layers().size(); ++i) {
        if (comp->layers().at(i).id != ref.layerId) continue;
        const composition::Layer& layer = comp->layers().at(i);
        if (ref.clip >= layer.clips.size() || ref.effect >= layer.clips.at(ref.clip).effects.size())
            return nullptr;
        return &comp->layerRef(i).clips[ref.clip].effects[ref.effect];
    }
    return nullptr;
}

bool NativeInstanceHost::describe(const Ref& ref, double seconds, core::Identifier* pluginId,
                                  QStringList* values, plugin::NativeCustomUiView* view) const
{
    const auto comp = scope();
    if (!comp || !ref.isValid()) return false;
    const composition::Layer* layer = findLayer(*comp, ref.layerId.value());
    if (!layer || ref.clip >= layer->clips.size()) return false;
    const composition::Clip& clip = layer->clips.at(ref.clip);
    if (ref.effect >= clip.effects.size()) return false;
    const composition::Effect& fx = clip.effects.at(ref.effect);

    // The calls that follow are made for this instance: the source host
    // answers about its clip.
    m_context = ref;

    const double rate = frameRate();
    const int clipStart = frameAt(clip.startSeconds, rate);
    const int frame = seconds < 0.0 ? clipStart : frameAt(seconds, rate);
    const QStringList at = valuesAt(fx, frame);

    plugin::NativeCustomUiView v;
    v.layerId = layer->id;
    v.instanceKey = instanceKey(ref);
    v.frameRate = rate;
    v.frame = frame;
    v.parameterFrame = frame;
    v.layerFrame = qMax(0, frame - clipStart);
    v.layerFrameEnd = qMax(1, frameAt(clip.durationSeconds, rate));
    // Pointer positions and the overlay are the viewer's canvas pixels - the
    // shot's display pixels, as the renderer's.
    v.target = comp->displaySize();
    v.area = v.target;
    // GetEffectIndex: among the clip's effects that draw (not its
    // transitions or Behaviors).
    v.effectIndex = 0;
    for (int e = 0; e < ref.effect; ++e) {
        const composition::Effect& before = clip.effects.at(e);
        if (before.isTransition() || plugin::nativeBehaviorRenderingVerified(before.pluginId)
            || plugin::nativeBehaviorUsesInstance(before.pluginId))
            continue;
        ++v.effectIndex;
    }

    // Points and drawing live in the tracked footage's pixels, placed on the
    // canvas as the renderer places that layer.
    QSize content;
    QTransform placement;
    if (footagePlacement(ref, frame, &content, &placement)) {
        v.area = content;
        v.matrix = columnMajor(placement);
    }

    if (pluginId) *pluginId = fx.pluginId;
    if (values) *values = at;
    if (view) *view = v;
    return true;
}

void NativeInstanceHost::prepare(const Ref& ref)
{
    const QString key = instanceKey(ref);
    if (m_restored.contains(key)) return;
    const composition::Effect* fx = effectFor(ref);
    if (!fx || !plugin::nativeEffectUsesInstance(fx->pluginId)) return;
    m_restored.insert(key);
    core::Identifier id;
    QStringList values;
    plugin::NativeCustomUiView view;
    if (!describe(ref, -1.0, &id, &values, &view)) return;
    if (!fx->instanceData.isEmpty()) {
        const auto restored = plugin::nativeRestoreInstanceData(id, values, view, fx->instanceData);
        if (!restored.enabled.isEmpty()) plugin::setNativeControlStates(key, restored.enabled);
    }
    // Put on its layer: the module checks whether its data still covers the
    // layer and starts analysing it when not.
    if (plugin::nativeEffectAnalysesOwnLayer(id) && !m_parented.contains(key)) {
        m_parented.insert(key);
        apply(ref, plugin::nativePropertyChanged(id, values, view,
                                                 QString::fromLatin1(plugin::kNativeParentLayerKey)),
              true);
    }
}

void NativeInstanceHost::propertyChanged(const Ref& ref, int parameter, double seconds)
{
    const composition::Effect* fx = effectFor(ref);
    if (!fx || !plugin::nativeEffectUsesInstance(fx->pluginId)) return;
    const auto parameters = plugin::nativeParameters(fx->pluginId);
    if (parameter < 0 || parameter >= parameters.size()) return;
    const plugin::EffectParameterSpec& spec = parameters.at(parameter);
    if (spec.type == QLatin1String("label")) return;
    core::Identifier id;
    QStringList values;
    plugin::NativeCustomUiView view;
    prepare(ref);
    if (!describe(ref, seconds, &id, &values, &view)) return;
    // A button is a bool the inspector raises and drops again: the press is
    // the change.
    if (spec.type == QLatin1String("button")) {
        const QString value = values.value(parameter);
        if (value != QLatin1String("true") && value != QLatin1String("1")) return;
    }
    apply(ref, plugin::nativePropertyChanged(id, values, view, spec.name), true);
}

void NativeInstanceHost::handleResult(const Ref& ref, const plugin::NativeCustomUiResult& result)
{
    apply(ref, result, false);
}

void NativeInstanceHost::apply(const Ref& ref, const plugin::NativeCustomUiResult& result,
                               bool settle)
{
    const QString key = instanceKey(ref);
    composition::Effect* fx = effectFor(ref);
    if (!fx) {
        m_pending.remove(key);
        return;
    }
    const auto comp = scope();
    QVector<composition::Layer> before;
    if (!result.layerRequests.isEmpty() && comp) before = comp->layers();
    bool changed = false;
    if (!result.enabled.isEmpty()) {
        plugin::setNativeControlStates(key, result.enabled);
        changed = true;
    }
    for (auto it = result.values.cbegin(); it != result.values.cend(); ++it) {
        if (it.key() < 0) continue;
        while (fx->parameterValues.size() <= it.key()) fx->parameterValues.append(QString());
        if (fx->parameterValues.at(it.key()) != it.value()) {
            fx->parameterValues[it.key()] = it.value();
            changed = true;
        }
    }
    if (!before.isEmpty() && applyLayerRequests(ref, result.layerRequests)) {
        // The step the module named (BeginTransaction): "Create Point Layer",
        // "Transform Layer".
        const QByteArray name = result.transaction.toUtf8();
        const QString text = name.isEmpty() ? tr("Transform Layer")
                                            : QCoreApplication::translate("openvegas::ui::NativeInstanceHost",
                                                                          name.constData());
        emit layersEdited(before, text);
        fx = effectFor(ref);
        changed = true;
        if (!fx) return;
    }
    if (result.backgroundRequested) {
        m_pending.insert(key, ref);
        const int delay = qBound(0, result.backgroundDelayMs, kMaxBackgroundDelayMs);
        if (!m_background.isActive() || m_background.remainingTime() > delay) {
            m_background.start(delay);
        }
    } else if (settle) {
        finish(ref);
        changed = false;
    }
    if (changed) emit effectChanged();
}

void NativeInstanceHost::runBackground()
{
    const QHash<QString, Ref> pending = m_pending;
    m_pending.clear();
    for (const Ref& ref : pending) {
        core::Identifier id;
        QStringList values;
        plugin::NativeCustomUiView view;
        if (!describe(ref, -1.0, &id, &values, &view)) continue;
        // Background work ends when the module stops asking for more: then
        // its data is kept and its motion worked out.
        apply(ref, plugin::nativeBackgroundProcess(id, values, view), true);
    }
}

void NativeInstanceHost::finish(const Ref& ref)
{
    composition::Effect* fx = effectFor(ref);
    if (!fx) return;
    core::Identifier id;
    QStringList values;
    plugin::NativeCustomUiView view;
    if (!describe(ref, -1.0, &id, &values, &view)) return;
    const QByteArray data = plugin::nativeInstanceData(id, values, view);
    if (!data.isEmpty()) {
        fx->instanceData = data;
        m_baked.insert(instanceKey(ref), qHash(data));
    }
    if (plugin::nativeBehaviorUsesInstance(id)) bake(ref);
    emit effectChanged();
}

void NativeInstanceHost::bake(const Ref& ref)
{
    const QString key = instanceKey(ref);
    const auto comp = scope();
    core::Identifier id;
    QStringList values;
    plugin::NativeCustomUiView view;
    if (!comp || !describe(ref, -1.0, &id, &values, &view)) {
        plugin::clearNativeInstanceTransforms(key);
        return;
    }
    const composition::Layer* layer = findLayer(*comp, ref.layerId.value());
    const composition::Clip& clip = layer->clips.at(ref.clip);
    const composition::Effect& fx = clip.effects.at(ref.effect);
    const double rate = frameRate();
    const int first = frameAt(clip.startSeconds, rate);
    const int count = qMax(1, frameAt(clip.durationSeconds, rate));

    const QSize canvas = comp->displaySize();
    QVector<std::array<float, 16>> matrices;
    matrices.reserve(count);
    bool any = false;
    for (int i = 0; i < count; ++i) {
        const int frame = first + i;
        plugin::NativeCustomUiView at = view;
        at.parameterFrame = frame;
        std::array<float, 16> m {};
        m_givenWorlds.clear();
        m_worldFrame = frame;
        const bool moved = plugin::nativeInstanceTransformation(id, valuesAt(fx, frame), at, frame, i, &m);
        m_worldFrame = -1;
        if (!moved) {
            matrices.append({1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1});
            continue;
        }
        any = true;
        // The module answers in the world of the footage's layer, as
        // GetLayerInfoV2 told it: that world times the footage's motion
        // (its own layer's world does not enter). Back to the motion alone.
        const auto given = m_givenWorlds.constFind(footageLayerId(ref, frame));
        if (given != m_givenWorlds.cend()) m = withoutWorld(m, *given);
        QSize content;
        QTransform placement;
        if (!footagePlacement(ref, frame, &content, &placement)) placement = QTransform();
        matrices.append(canvasMotion(m, placement, canvas));
    }
    if (any) plugin::setNativeInstanceTransforms(key, first, matrices);
    else plugin::clearNativeInstanceTransforms(key);
}

namespace {

// Every instance-backed effect of the shot that has data - or, for the 2D
// effects analysing their own layer, any - by instance key.
QHash<QString, NativeInstanceHost::Ref> instanceEffects(const composition::Composition& comp)
{
    QHash<QString, NativeInstanceHost::Ref> found;
    for (const composition::Layer& layer : comp.layers()) {
        for (int c = 0; c < layer.clips.size(); ++c) {
            for (int e = 0; e < layer.clips.at(c).effects.size(); ++e) {
                const composition::Effect& fx = layer.clips.at(c).effects.at(e);
                const bool own = plugin::nativeEffectAnalysesOwnLayer(fx.pluginId);
                if (!own && (fx.instanceData.isEmpty() || !plugin::nativeBehaviorUsesInstance(fx.pluginId)))
                    continue;
                const NativeInstanceHost::Ref ref {layer.id, c, e};
                found.insert(NativeInstanceHost::instanceKey(ref), ref);
            }
        }
    }
    return found;
}

} // namespace

void NativeInstanceHost::restoreAll()
{
    // Analysis under way keeps going: its instance is newer than its data.
    const QHash<QString, Ref> busy = m_pending;
    m_restored.clear();
    for (auto it = busy.cbegin(); it != busy.cend(); ++it) m_restored.insert(it.key());
    m_baked.clear();
    plugin::clearNativeInstanceTransforms();
    // The open shot's instances, and those of every shot nested in it - the
    // reference keeps its instances by project, so an effect in a shot that
    // was never opened moves its layer too.
    for (const auto& [shot, found] : instancesInTree()) {
        m_scope = shot;
        for (auto it = found.cbegin(); it != found.cend(); ++it) {
            const composition::Effect* fx = effectFor(it.value());
            if (!fx) continue;
            m_baked.insert(it.key(), qHash(fx->instanceData));
            prepare(it.value());
            if (plugin::nativeBehaviorUsesInstance(fx->pluginId)) bake(it.value());
        }
        m_scope.reset();
    }
}

QVector<std::pair<std::shared_ptr<composition::Composition>, QHash<QString, NativeInstanceHost::Ref>>>
NativeInstanceHost::instancesInTree() const
{
    QVector<std::pair<std::shared_ptr<composition::Composition>, QHash<QString, Ref>>> result;
    const auto open = m_composition ? m_composition() : nullptr;
    if (!open) return result;
    QVector<std::shared_ptr<composition::Composition>> shots {open};
    for (int i = 0; i < shots.size(); ++i) {
        for (const composition::Layer& layer : shots.at(i)->layers())
            for (const composition::Clip& clip : layer.clips)
                if (clip.nestedComposition && !shots.contains(clip.nestedComposition))
                    shots.append(clip.nestedComposition);
    }
    for (const auto& shot : shots) result.append({shot, instanceEffects(*shot)});
    return result;
}

void NativeInstanceHost::sync()
{
    int count = 0;
    bool same = true;
    for (const auto& [shot, found] : instancesInTree()) {
        m_scope = shot;
        count += found.size();
        for (auto it = found.cbegin(); same && it != found.cend(); ++it) {
            const composition::Effect* fx = effectFor(it.value());
            const auto baked = m_baked.constFind(it.key());
            same = fx && baked != m_baked.cend() && *baked == qHash(fx->instanceData);
        }
        m_scope.reset();
    }
    if (!same || count != m_baked.size()) restoreAll();
}

const composition::Layer* NativeInstanceHost::layerById(const QString& layerId,
                                                        int* clipIndex) const
{
    const auto comp = scope();
    if (!comp) return nullptr;
    const composition::Layer* layer = findLayer(*comp, layerId);
    if (!layer || layer->clips.isEmpty()) return nullptr;
    int index = 0;
    const composition::Layer* own = m_context.isValid()
        ? findLayer(*comp, m_context.layerId.value()) : nullptr;
    if (own && m_context.clip < own->clips.size()) {
        const composition::Clip& clip = own->clips.at(m_context.clip);
        index = layer == own ? m_context.clip
                             : overlappingClip(*layer, clip.startSeconds, clip.endSeconds());
    }
    if (clipIndex) *clipIndex = qBound(0, index, int(layer->clips.size()) - 1);
    return layer;
}

QString NativeInstanceHost::footageLayerId(const Ref& ref, int frame) const
{
    const auto comp = scope();
    if (!comp || !ref.isValid()) return QString();
    const composition::Layer* layer = findLayer(*comp, ref.layerId.value());
    if (!layer || ref.clip >= layer->clips.size()) return QString();
    const composition::Clip& clip = layer->clips.at(ref.clip);
    if (ref.effect >= clip.effects.size()) return QString();
    const composition::Effect& fx = clip.effects.at(ref.effect);
    const auto parameters = plugin::nativeParameters(fx.pluginId);
    QString source;
    for (int p = 0; p < parameters.size() && p < fx.parameterValues.size(); ++p) {
        if (parameters.at(p).name == QLatin1String("motionFromLayer"))
            source = fx.parameterAt(p, frame).toString();
    }
    // AutomaticStabilizer and MotionLock work on their own layer.
    if (source.isEmpty() && plugin::nativeEffectAnalysesOwnLayer(fx.pluginId)) source = layer->id.value();
    return source;
}

bool NativeInstanceHost::footagePlacement(const Ref& ref, int frame, QSize* content,
                                          QTransform* placement) const
{
    const auto comp = scope();
    const QString source = footageLayerId(ref, frame);
    if (!comp || source.isEmpty()) return false;
    const composition::Layer* layer = findLayer(*comp, ref.layerId.value());
    const composition::Clip& clip = layer->clips.at(ref.clip);
    const composition::Layer* footage = findLayer(*comp, source);
    const int sourceClip = footage ? overlappingClip(*footage, clip.startSeconds, clip.endSeconds()) : -1;
    if (sourceClip < 0) return false;
    const auto media = m_media ? m_media() : nullptr;
    const composition::Clip& footageClip = footage->clips.at(sourceClip);
    const QSize size = contentSize(*comp, footageClip, media.get());
    const double aspect = !footageClip.nestedComposition && media && footageClip.mediaId.isValid()
        ? media->assetById(footageClip.mediaId).pixelAspectValue() : 1.0;
    if (content) *content = size;
    if (placement) *placement = contentPlacement(*comp, *footage, size, comp->displaySize(), frame, aspect);
    return true;
}

std::array<float, 16> NativeInstanceHost::withoutWorld(const std::array<float, 16>& motion,
                                                       const std::array<float, 16>& world)
{
    // nativeInstanceTransformation gives W*R, the module's pixels and the
    // port's layer space both running Y up: R = W^-1*result.
    QMatrix4x4 w;
    QMatrix4x4 result;
    std::copy(world.cbegin(), world.cend(), w.data());
    std::copy(motion.cbegin(), motion.cend(), result.data());
    bool invertible = false;
    const QMatrix4x4 inverse = w.inverted(&invertible);
    if (!invertible) return motion;
    const QMatrix4x4 alone = inverse * result;
    std::array<float, 16> out {};
    std::copy(alone.constData(), alone.constData() + 16, out.begin());
    return out;
}

plugin::NativeLayerInfo NativeInstanceHost::layerInfo(const QString& layerId) const
{
    plugin::NativeLayerInfo info;
    int clipIndex = -1;
    const composition::Layer* layer = layerById(layerId, &clipIndex);
    const auto comp = scope();
    if (!layer || !comp) return info;
    const composition::Clip& clip = layer->clips.at(clipIndex);
    const double rate = frameRate();
    const auto media = m_media ? m_media() : nullptr;
    info.valid = true;
    info.type = 0;
    info.startFrame = frameAt(clip.startSeconds, rate);
    info.durationFrames = qMax(1, frameAt(clip.durationSeconds, rate));
    info.size = contentSize(*comp, clip, media.get());
    if (m_worldFrame >= 0) {
        info.world = render::nativeLayerWorld(comp.get(), *layer, m_worldFrame);
        m_givenWorlds.insert(layerId, info.world);
    }
    return info;
}

plugin::NativeAssetInfo NativeInstanceHost::assetInfo(const QString& layerId) const
{
    int clipIndex = -1;
    const composition::Layer* layer = layerById(layerId, &clipIndex);
    const auto media = m_media ? m_media() : nullptr;
    if (!layer || !media) return plugin::NativeAssetInfo();
    // Video frames as the shot reads them: at its rate, by time in the
    // source; a nested shot (type 3) likewise, its frames rendered.
    return render::nativeFootageInfo(layer->clips.at(clipIndex), media.get(), frameRate());
}

int NativeInstanceHost::layerStartFrame(const QString& layerId) const
{
    int clipIndex = -1;
    const composition::Layer* layer = layerById(layerId, &clipIndex);
    if (layer && composition::layerHasSpan(*layer)) return frameAt(layer->spanStartSeconds, frameRate());
    if (!layer || clipIndex < 0) return 0;
    return frameAt(layer->clips.at(clipIndex).startSeconds, frameRate());
}

void NativeInstanceHost::applyLayerTransforms(composition::Layer& layer,
                                              const QVector<plugin::NativeLayerTransform>& transforms,
                                              int firstFrame, double frameRate)
{
    if (transforms.isEmpty()) return;
    composition::LayerTransform& t = layer.transform;
    const bool depth = layer.dimension == composition::LayerDimension::ThreeD;
    const bool keyed = transforms.size() > 1;
    for (const plugin::NativeLayerTransform& at : transforms) {
        const int frame = firstFrame + int(std::lround(double(at.timeMs) * frameRate / 1000.0));
        const auto put = [&](composition::KeyFrameList& curve, double& value, double v) {
            if (keyed || !curve.isEmpty()) curve.set(frame, v);
            else value = v;
        };
        double x = t.position.x(), y = t.position.y();
        put(t.positionXCurve, x, at.x);
        put(t.positionYCurve, y, at.y);
        t.position = QPointF(x, y);
        // "position" is a 3D point (x, y, 0) and "scale" a Scale3D (s, s, s).
        if (depth) put(t.positionZCurve, t.positionZ, 0.0);
        put(t.rotationCurve, t.rotationDegrees, at.rotation);
        double sx = t.scalePercent.x(), sy = t.scalePercent.y();
        put(t.scaleXCurve, sx, at.scale);
        put(t.scaleYCurve, sy, at.scale);
        t.scalePercent = QPointF(sx, sy);
        if (depth) put(t.scaleZCurve, t.scaleZPercent, at.scale);
    }
}

bool NativeInstanceHost::applyLayerRequests(const Ref& ref,
                                            const QVector<plugin::NativeLayerRequest>& requests)
{
    const auto comp = scope();
    if (!comp) return false;
    // The clip of the module's own layer is the one being worked on.
    m_context = ref;
    const double rate = frameRate();
    bool changed = false;
    for (const plugin::NativeLayerRequest& request : requests) {
        const composition::Layer* source = findLayer(*comp, request.sourceLayerId);
        if (!source) continue;
        const int sourceStart = layerStartFrame(request.sourceLayerId);
        if (request.kind == plugin::NativeLayerRequest::Kind::CreatePointLayer) {
            // PointLayer::Create, over the source layer's frames, at the top of
            // the stack: its span is the source's, its keys at the source's
            // start plus each time.
            composition::Layer point;
            point.id = core::Identifier(request.targetLayerId);
            point.name = request.name;
            point.kind = composition::LayerKind::Point;
            point.visible = true;
            point.blendMode = QStringLiteral("None");
            point.opacity = 1.0;
            int sourceClip = -1;
            layerById(request.sourceLayerId, &sourceClip);
            if (composition::layerHasSpan(*source)) {
                point.spanStartSeconds = source->spanStartSeconds;
                point.spanEndSeconds = source->spanEndSeconds;
            } else if (sourceClip >= 0 && sourceClip < source->clips.size()) {
                point.spanStartSeconds = source->clips.at(sourceClip).startSeconds;
                point.spanEndSeconds = source->clips.at(sourceClip).endSeconds();
            }
            applyLayerTransforms(point, request.transforms, sourceStart, rate);
            comp->insertLayer(0, point);
            changed = true;
            continue;
        }
        composition::Layer* target = nullptr;
        for (int i = 0; i < comp->layers().size(); ++i) {
            if (comp->layers().at(i).id.value() == request.targetLayerId) target = &comp->layerRef(i);
        }
        if (!target) continue;
        // Keys at each time plus StartTime(target) - StartTime(source), on
        // the target's own timeline.
        const int targetStart = layerStartFrame(request.targetLayerId);
        applyLayerTransforms(*target, request.transforms, targetStart + (targetStart - sourceStart), rate);
        changed = true;
    }
    return changed;
}

QImage NativeInstanceHost::assetFrame(const QString& layerId, int frame)
{
    int clipIndex = -1;
    const composition::Layer* layer = layerById(layerId, &clipIndex);
    const auto media = m_media ? m_media() : nullptr;
    if (!layer || !media || frame < 0) return QImage();
    const composition::Clip& clip = layer->clips.at(clipIndex);
    if (clip.nestedComposition) {
        // A nested shot as footage: its frame at that time of its own,
        // rendered as the shot draws it (a pre-render when there is one).
        const double seconds = frame / frameRate();
        const QString key = render::renderStateKey(*clip.nestedComposition, media.get(), seconds,
                                                   clip.nestedComposition->displaySize(), true, true,
                                                   QStringLiteral("footage"));
        const auto kept = m_nestedFrames.constFind(key);
        if (kept != m_nestedFrames.cend()) return kept.value();
        const QImage picture = render::renderCompositionFrame(clip.nestedComposition, media, seconds, m_preRenders);
        m_nestedFrames.insert(key, picture);
        m_nestedFrameOrder.append(key);
        while (m_nestedFrameOrder.size() > 48) m_nestedFrames.remove(m_nestedFrameOrder.takeFirst());
        return picture;
    }
    const media::MediaAsset asset = media->assetById(clip.mediaId);
    if (!asset.isValid()) return QImage();
    if (asset.isImageSequence()) {
        const double rate = asset.sequenceFrameRate() > 0.0 ? asset.sequenceFrameRate() : frameRate();
        return asset.interpretFrame(QImage(asset.sequenceFileAt((frame + 0.5) / rate)));
    }
    if (asset.kind() != media::MediaKind::Video) return QImage();
    const double seconds = asset.sourceSecondsAt(frame / frameRate());
    // The render's own key for that position, so frames decoded here are
    // shown too, and frames already shown are not decoded again.
    const int milliseconds = qMax(0, int(std::lround(seconds * 1000.0)));
    QImage picture = media->videoFrame(asset.id(), milliseconds);
    if (!picture.isNull()) return asset.interpretFrame(picture);
    auto it = m_decoders.find(asset.filePath());
    if (it == m_decoders.end()) {
        if (m_decoders.size() >= kMaxDecoders) m_decoders.erase(m_decoders.begin());
        it = m_decoders.emplace(asset.filePath(),
                                std::make_unique<media::VideoDecoder>(asset.filePath(), 4000,
                                                                      asset.hardwareDecoding())).first;
    }
    picture = it->second->isValid() ? it->second->frameAt(seconds) : QImage();
    if (!picture.isNull()) media->putVideoFrame(asset.id(), milliseconds, picture);
    return asset.interpretFrame(picture);
}

} // namespace openvegas::ui
