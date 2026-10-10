#include "render/LayerGeometry.h"

#include <QFileInfo>
#include <QHash>
#include <QLineF>
#include <QVector4D>

#include <cmath>

#include "composition/Composition.h"
#include "composition/TextStyle.h"
#include "media/MediaManager.h"
#include "model3d/ModelLoader.h"
#include "render/TextRender.h"

namespace openvegas::render {

model3d::Camera sceneCameraOf(const composition::Composition* composition, const QSize& canvas,
                              int frame)
{
    model3d::Camera camera = model3d::defaultCameraForCanvas(canvas);
    if (!composition) return camera;
    for (const composition::Layer& layer : composition->layers()) {
        if (layer.kind != composition::LayerKind::Camera || !layer.visible) continue;
        // The lens by its zoom against the shot's own frame.
        const QSizeF shot = composition->displaySize().isEmpty() ? QSizeF(canvas) : QSizeF(composition->displaySize());
        const double zoom = layer.camera.zoomAt(frame) > 0.0
            ? layer.camera.zoomAt(frame)
            : composition::CameraSettings::lensZoom(composition::CameraSettings::kNewCameraLensAngle, shot.width());
        camera.fieldOfViewDegrees = composition::CameraSettings::verticalFieldOfView(zoom, shot.height());
        camera.nearPlane = qMax(0.001, layer.camera.nearClip);
        camera.farPlane = qMax(camera.nearPlane + 1.0, layer.camera.farClip);
        const composition::LayerTransform& t = layer.transform;
        const QPointF position = t.positionAt(frame);
        camera.position = QVector3D(float(position.x()), float(position.y()),
                                    float(t.positionZAt(frame)));
        // The camera looks down its own -Z, so its turn aims that axis and
        // the target follows from where it ends up pointing.
        const QMatrix4x4 aim = layerRotationAt(t, frame);
        const QVector3D forward = aim.map(QVector3D(0.0f, 0.0f, -1.0f));
        camera.target = camera.position + forward;
        camera.up = aim.map(QVector3D(0.0f, 1.0f, 0.0f));
        break;
    }
    return camera;
}

QVector<model3d::Light> sceneLightsOf(const composition::Composition* composition, int frame)
{
    QVector<model3d::Light> lights;
    if (!composition) return lights;
    for (const composition::Layer& layer : composition->layers()) {
        if (layer.kind != composition::LayerKind::Light || !layer.visible) continue;
        const composition::LayerTransform& t = layer.transform;
        const composition::LightSettings& settings = layer.light;
        model3d::Light light;
        light.type = model3d::Light::Type(int(settings.type));
        const QPointF position = t.positionAt(frame);
        light.position = QVector3D(float(position.x()), float(position.y()), float(t.positionZAt(frame)));
        light.direction = layerRotationAt(t, frame).mapVector(QVector3D(0.0f, 0.0f, -1.0f)).normalized();
        light.color = settings.colorAt(frame);
        light.intensity = model3d::Light::intensityOf(settings.intensityAt(frame));
        light.falloff = model3d::Light::Falloff(int(settings.falloff));
        light.reach = settings.reachAt(frame);
        light.setCone(settings.coneAngleAt(frame), settings.featherAt(frame));
        light.castShadows = settings.castShadows;
        light.shadowOpacity = settings.shadowOpacityAt(frame) / 100.0;
        light.shadowDiffusion = settings.shadowDiffusionAt(frame) / 100.0;
        light.shadowColor = settings.shadowColorAt(frame);
        lights.append(light);
    }
    return lights;
}

model3d::SurfaceLighting surfaceLightingOf(const composition::Layer& layer, int frame)
{
    const composition::SurfaceShading& surface = layer.surface;
    model3d::SurfaceLighting lighting;
    lighting.illuminated = surface.illuminated;
    lighting.receiveShadows = surface.receiveShadows;
    if (layer.kind == composition::LayerKind::Model3D) {
        lighting.ambient = lighting.diffuse = lighting.specular = lighting.shininess = 1.0;
        lighting.emissive = QColor(0, 0, 0);
        const auto bias = [](double value) { return (qBound(1.0, value, 1000.0) - 1.0) / 999.0 * 0.00999 + 0.00001; };
        lighting.shadowBias = bias(surface.selfShadowBias) / bias(1.0);
        return lighting;
    }
    lighting.billboardedLights = surface.billboardedLights;
    lighting.ambient = surface.ambientAt(frame) / 100.0;
    lighting.diffuse = surface.diffuseAt(frame) / 100.0;
    lighting.specular = surface.specularAt(frame) / 100.0;
    lighting.shininess = surface.shininessAt(frame) / 100.0;
    lighting.emissive = surface.emissiveAt(frame);
    return lighting;
}

ViewCameraState defaultViewCameraState(ViewType type)
{
    constexpr float kFar = 1000000.0f;
    constexpr float kHalfPi = 1.5707963f, kPi = 3.1415925f;
    ViewCameraState state;
    switch (type) {
    case ViewType::Perspective: state.position = QVector3D(0.0f, 100.0f, 1000.0f); break;
    case ViewType::Front: state.position = QVector3D(0.0f, 0.0f, kFar); break;
    case ViewType::Back:
        state.position = QVector3D(0.0f, 0.0f, -kFar);
        state.rotation = QVector3D(0.0f, kPi, 0.0f);
        break;
    case ViewType::Left:
        state.position = QVector3D(-kFar, 0.0f, 0.0f);
        state.rotation = QVector3D(0.0f, -kHalfPi, 0.0f);
        break;
    case ViewType::Right:
        state.position = QVector3D(kFar, 0.0f, 0.0f);
        state.rotation = QVector3D(0.0f, kHalfPi, 0.0f);
        break;
    case ViewType::Top:
        state.position = QVector3D(0.0f, kFar, 0.0f);
        state.rotation = QVector3D(-kHalfPi, 0.0f, 0.0f);
        break;
    case ViewType::Bottom:
        state.position = QVector3D(0.0f, -kFar, 0.0f);
        state.rotation = QVector3D(kHalfPi, 0.0f, -kPi);
        break;
    case ViewType::TwoD:
    case ViewType::ActiveCamera:
        break;
    }
    return state;
}

QMatrix4x4 viewCameraAim(const ViewCameraState& state)
{
    // The view's own Z turn comes first, then Y, then X: that is what has
    // Bottom's (pi/2, 0, -pi) look up at the scene rather than away from it.
    QMatrix4x4 aim;
    aim.rotate(qRadiansToDegrees(state.rotation.x()), 1.0f, 0.0f, 0.0f);
    aim.rotate(qRadiansToDegrees(state.rotation.y()), 0.0f, 1.0f, 0.0f);
    aim.rotate(qRadiansToDegrees(state.rotation.z()), 0.0f, 0.0f, 1.0f);
    return aim;
}

ViewCameraState zoomedView(ViewType type, ViewCameraState state, double steps, const QPointF& anchor)
{
    const QMatrix4x4 aim = viewCameraAim(state);
    if (type == ViewType::Perspective) {
        // PerspectiveCameraController's wheel: 120 units a notch.
        state.position += aim.map(QVector3D(0.0f, 0.0f, -1.0f)) * float(steps * 120.0);
    } else if (viewHasOwnCamera(type)) {
        // OrthoCameraController: zoom * (notches * 0.1 + 1), no less than
        // 0.0002, the point under `anchor` kept where it is.
        const double before = state.scale > 0.0 ? state.scale : 1.0;
        const double after = qMax(0.0002, before * (steps * 0.1 + 1.0));
        const QVector3D right = aim.map(QVector3D(1.0f, 0.0f, 0.0f));
        const QVector3D up = aim.map(QVector3D(0.0f, 1.0f, 0.0f));
        const QVector3D offset = right * float(anchor.x()) - up * float(anchor.y());
        state.position += offset * float(1.0 / before - 1.0 / after);
        state.scale = after;
    }
    return state;
}

ViewCameraState pannedView(ViewType type, ViewCameraState state, const QPointF& canvasPixels)
{
    if (!viewHasOwnCamera(type)) return state;
    const QMatrix4x4 aim = viewCameraAim(state);
    const QVector3D right = aim.map(QVector3D(1.0f, 0.0f, 0.0f));
    const QVector3D up = aim.map(QVector3D(0.0f, 1.0f, 0.0f));
    // A picture pixel is 1 / scale units across an axis view; the
    // perspective one moves a unit a pixel.
    const float units = type == ViewType::Perspective ? 1.0f : float(1.0 / (state.scale > 0.0 ? state.scale : 1.0));
    state.position += (-right * float(canvasPixels.x()) + up * float(canvasPixels.y())) * units;
    return state;
}

ViewCameraState orbitedView(ViewType type, ViewCameraState state, const QPointF& pixels)
{
    if (type != ViewType::Perspective) return state;
    const float distance = state.position.length();
    constexpr float kRadiansPerPixel = 0.0052359878f;   // 0.3 degrees, as the reference orbits
    state.rotation.setY(state.rotation.y() - float(pixels.x()) * kRadiansPerPixel);
    state.rotation.setX(qBound(-1.55f, state.rotation.x() - float(pixels.y()) * kRadiansPerPixel, 1.55f));
    state.position = viewCameraAim(state).map(QVector3D(0.0f, 0.0f, distance));
    return state;
}

bool viewHasOwnCamera(ViewType type)
{
    return type != ViewType::TwoD && type != ViewType::ActiveCamera;
}

model3d::Camera viewCamera(ViewType type, const ViewCameraState& state,
                           const composition::Composition* composition, const QSize& canvas, int frame)
{
    if (!viewHasOwnCamera(type)) return sceneCameraOf(composition, canvas, frame);
    model3d::Camera camera;
    const QMatrix4x4 aim = viewCameraAim(state);
    camera.position = state.position;
    camera.target = state.position + aim.map(QVector3D(0.0f, 0.0f, -1.0f));
    camera.up = aim.map(QVector3D(0.0f, 1.0f, 0.0f));
    if (type == ViewType::Perspective) {
        // The reference's perspective view camera: 60 degrees, 0.1 to 100000.
        camera.fieldOfViewDegrees = 60.0;
        camera.nearPlane = 0.1;
        camera.farPlane = 100000.0;
    } else {
        // A million units out: everything lies between the planes.
        camera.orthographic = true;
        const double height = composition && !composition->displaySize().isEmpty()
            ? composition->displaySize().height() : qMax(1, canvas.height());
        camera.orthographicHeight = height / (state.scale > 0.0 ? state.scale : 1.0);
        camera.nearPlane = 1.0;
        camera.farPlane = 4000000.0;
    }
    return camera;
}

QMatrix4x4 layerRotationAt(const composition::LayerTransform& t, int frame)
{
    // The quaternions qX * qY * qZ make Rx * Ry * Rz (Z applied first), and
    // the rotations are multiplied on after it in the same order.
    QMatrix4x4 turn;
    turn.rotate(float(-t.orientationAt(frame, 0)), 1.0f, 0.0f, 0.0f);
    turn.rotate(float(-t.orientationAt(frame, 1)), 0.0f, 1.0f, 0.0f);
    turn.rotate(float(-t.orientationAt(frame, 2)), 0.0f, 0.0f, 1.0f);
    turn.rotate(float(-t.rotationXAt(frame)), 1.0f, 0.0f, 0.0f);
    turn.rotate(float(-t.rotationYAt(frame)), 0.0f, 1.0f, 0.0f);
    turn.rotate(float(-t.rotationAt(frame)), 0.0f, 0.0f, 1.0f);
    return turn;
}

QMatrix4x4 layerPlaneMatrixAt(const composition::LayerTransform& t, int frame)
{
    // Placement on the layer's plane, as the 2D path places the layer: about
    // the anchor for the turns, about the centre for the scale.
    const QPointF position = t.positionAt(frame);
    const QPointF scale = t.scaleAt(frame);
    QMatrix4x4 model;
    model.translate(float(position.x()), float(position.y()), float(t.positionZAt(frame)));
    model.translate(float(t.anchorPoint.x()), float(t.anchorPoint.y()), float(t.anchorPointZ));
    model *= layerRotationAt(t, frame);
    model.translate(float(-t.anchorPoint.x()), float(-t.anchorPoint.y()), float(-t.anchorPointZ));
    model.scale(float(scale.x() / 100.0), float(scale.y() / 100.0), float(t.scaleZAt(frame) / 100.0));
    return model;
}

QMatrix4x4 layerModelMatrixAt(const composition::LayerTransform& transform, int frame)
{
    // Order matters and follows the reference: the layer is placed, then aimed
    // by its orientation, then spun about its own three axes, then scaled, and
    // all of it turns about the anchor point rather than the layer's centre.
    const QPointF position = transform.positionAt(frame);
    const QPointF scale = transform.scaleAt(frame);

    QMatrix4x4 matrix;
    matrix.translate(float(position.x()), float(position.y()),
                     float(transform.positionZAt(frame)));
    matrix *= layerRotationAt(transform, frame);
    matrix.scale(float(scale.x() / 100.0), float(scale.y() / 100.0),
                 float(transform.scaleZAt(frame) / 100.0));
    matrix.translate(float(-transform.anchorPoint.x()), float(-transform.anchorPoint.y()),
                     float(-transform.anchorPointZ));
    return matrix;
}

QVector<QMatrix4x4> modelNodeMatrices(const composition::Layer& layer, const model3d::Mesh& mesh,
                                      int frame)
{
    QVector<QMatrix4x4> matrices(mesh.nodes.size());
    if (layer.modelNodes.isEmpty()) return matrices;
    // Parents are listed before their children, so one pass composes them.
    for (int i = 0; i < mesh.nodes.size(); ++i) {
        const int parent = mesh.nodes.at(i).parent;
        QMatrix4x4 own;
        if (const composition::ModelNodeTransform* node = layer.modelNode(mesh.nodePath(i)))
            own = layerModelMatrixAt(node->transform, frame);
        matrices[i] = parent >= 0 && parent < i ? matrices.at(parent) * own : own;
    }
    return matrices;
}

namespace {

// What plays on a model layer at a frame: each animation's clip (the
// model's own, or another file's put on its nodes), the time into it and
// its share.
struct PlayingAnimation
{
    model3d::ModelAnimation clip;
    double seconds = 0.0;
    double strength = 0.0;
};

QVector<PlayingAnimation> playingAnimations(const composition::Layer& layer, const model3d::Mesh& mesh,
                                            int frame, double fps, const QString& modelFile)
{
    QVector<PlayingAnimation> playing;
    const QString ownFile = QFileInfo(modelFile).absoluteFilePath();
    for (const composition::ModelAnimationInstance& instance : layer.modelAnimations) {
        PlayingAnimation entry;
        entry.strength = instance.strengthAt(frame) / 100.0;
        if (qFuzzyIsNull(entry.strength)) continue;
        if (instance.filePath.isEmpty()
            || QFileInfo(instance.filePath).absoluteFilePath() == ownFile) {
            if (instance.animationIndex < 0 || instance.animationIndex >= mesh.animations.size()) continue;
            entry.clip = mesh.animations.at(instance.animationIndex);
        } else {
            const std::shared_ptr<const model3d::Mesh> source = model3d::animationSource(instance.filePath);
            if (!source || instance.animationIndex < 0 || instance.animationIndex >= source->animations.size())
                continue;
            entry.clip = model3d::retargetAnimation(source->animations.at(instance.animationIndex), *source, mesh);
        }
        const double rate = entry.clip.frameRate > 0.0 ? entry.clip.frameRate : model3d::kModelAnimationFrameRate;
        entry.seconds = instance.animationFrame(frame, fps, entry.clip.frameCount(), rate) / rate;
        playing.append(entry);
    }
    return playing;
}

} // namespace

QVector<QMatrix4x4> modelAnimationMatrices(const composition::Layer& layer, const model3d::Mesh& mesh,
                                           int frame, double fps, const QString& modelFile)
{
    // Each animation adds its Strength's share of its motion, as the
    // reference adds each instance's displacements (AddDisplacements): a
    // node moves by I + sum of strength x (its animated matrix - I).
    QVector<QMatrix4x4> total(mesh.nodes.size());
    if (layer.modelAnimations.isEmpty() || mesh.nodes.isEmpty()) return total;
    QVector<QMatrix4x4> motion(mesh.nodes.size(), QMatrix4x4());
    for (QMatrix4x4& m : motion) m.fill(0.0f);
    bool moved = false;
    for (const PlayingAnimation& playing : playingAnimations(layer, mesh, frame, fps, modelFile)) {
        const double strength = playing.strength;
        const QVector<QMatrix4x4> matrices = mesh.animatedNodeMatrices(playing.clip, playing.seconds);
        for (int i = 0; i < matrices.size() && i < motion.size(); ++i) {
            if (matrices.at(i).isIdentity()) continue;
            motion[i] += (matrices.at(i) - QMatrix4x4()) * float(strength);
            moved = true;
        }
    }
    if (!moved) return total;
    for (int i = 0; i < total.size(); ++i) {
        bool still = true;
        for (int k = 0; k < 16 && still; ++k) still = motion.at(i).constData()[k] == 0.0f;
        if (!still) total[i] = QMatrix4x4() + motion.at(i);
    }
    return total;
}

model3d::Mesh posedModelMesh(const composition::Layer& layer, const model3d::Mesh& mesh, int frame,
                             double fps, const QString& modelFile)
{
    model3d::Mesh out = mesh;
    if ((!mesh.skins.isEmpty() || !mesh.morphs.isEmpty()) && !layer.modelAnimations.isEmpty()) {
        // Skinned and morphed points move by each animation's share of what
        // its skeleton and weights do to them (the reference's
        // displacements).
        QHash<int, QVector3D> positionMotion, normalMotion;
        for (const PlayingAnimation& playing : playingAnimations(layer, mesh, frame, fps, modelFile)) {
            const model3d::Mesh moved = mesh.deformed(mesh.animatedNodeWorlds(playing.clip, playing.seconds),
                                                      mesh.morphWeights(playing.clip, playing.seconds));
            for (int index = 0; index < mesh.positions.size(); ++index) {
                const QVector3D change = moved.positions.at(index) - mesh.positions.at(index);
                if (!change.isNull()) positionMotion[index] += float(playing.strength) * change;
            }
            for (int index = 0; index < mesh.normals.size(); ++index) {
                const QVector3D change = moved.normals.at(index) - mesh.normals.at(index);
                if (!change.isNull()) normalMotion[index] += float(playing.strength) * change;
            }
        }
        for (auto it = positionMotion.cbegin(); it != positionMotion.cend(); ++it)
            out.positions[it.key()] += it.value();
        for (auto it = normalMotion.cbegin(); it != normalMotion.cend(); ++it)
            out.normals[it.key()] = (out.normals.at(it.key()) + it.value()).normalized();
    }
    const QVector<QMatrix4x4> nodes = posedModelNodeMatrices(layer, mesh, frame, fps, modelFile);
    return nodes.isEmpty() ? out : out.posed(nodes);
}

QVector<QMatrix4x4> posedModelNodeMatrices(const composition::Layer& layer, const model3d::Mesh& mesh,
                                           int frame, double fps, const QString& modelFile)
{
    // Both move the model's flattened geometry, so they compose: the
    // animation first, the node's own transform on top of it.
    QVector<QMatrix4x4> matrices = modelAnimationMatrices(layer, mesh, frame, fps, modelFile);
    const QVector<QMatrix4x4> overrides = modelNodeMatrices(layer, mesh, frame);
    if (matrices.size() < overrides.size()) matrices.resize(overrides.size());
    for (int i = 0; i < overrides.size(); ++i) {
        if (overrides.at(i).isIdentity()) continue;
        matrices[i] = matrices.at(i).isIdentity() ? overrides.at(i) : overrides.at(i) * matrices.at(i);
    }
    return matrices;
}

const composition::Effect* textEffectOfClip(const composition::Clip& clip)
{
    for (const composition::Effect& fx : clip.effects) {
        if (fx.pluginId.value() == QStringLiteral("text")
            || fx.name.compare(QLatin1String("Text"), Qt::CaseInsensitive) == 0) {
            return fx.parameterValues.isEmpty() ? nullptr : &fx;
        }
    }
    return nullptr;
}

namespace {

// The clip a layer shows at `frame`, else its first: Layout works on a layer
// whether or not the playhead is over one of its clips.
const composition::Clip* clipAt(const composition::Composition& composition,
                                const composition::Layer& layer, int frame)
{
    if (layer.clips.isEmpty()) return nullptr;
    const double fps = composition.fpsDenominator() > 0
        ? double(composition.fpsNumerator()) / composition.fpsDenominator() : 30.0;
    const double seconds = frame / fps;
    for (const composition::Clip& clip : layer.clips) {
        if (seconds >= clip.startSeconds && seconds < clip.endSeconds()) return &clip;
    }
    return &layer.clips.first();
}

QPolygonF corners(const QRectF& rect)
{
    return QPolygonF({rect.topLeft(), rect.topRight(), rect.bottomRight(), rect.bottomLeft()});
}

} // namespace

LayerGeometry layerGeometry(const composition::Composition& composition,
                            const composition::Layer& layer, const media::MediaManager* media,
                            int frame)
{
    LayerGeometry geometry;
    const QSize canvas = composition.displaySize();
    const double width = canvas.width();
    const double height = canvas.height();
    const QRectF frameBox(-width / 2.0, -height / 2.0, width, height);
    using composition::LayerKind;
    if (layer.kind == LayerKind::Grade) {
        // An adjustment layer acts on the whole frame.
        geometry.valid = true;
        geometry.local = frameBox;
        geometry.toCanvas = QTransform::fromTranslate(width / 2.0, height / 2.0);
        geometry.quad = geometry.toCanvas.map(corners(geometry.local));
        return geometry;
    }
    if (layer.kind == LayerKind::Model3D) {
        // The model as RenderWorker::renderModelLayer poses and places it,
        // each point seen through the camera: their box on the canvas.
        if (!media || !layer.modelAssetId.isValid()) return geometry;
        const model3d::Mesh mesh = media->modelMesh(layer.modelAssetId);
        if (mesh.isEmpty()) return geometry;
        const double fps = composition.fpsDenominator() > 0
            ? double(composition.fpsNumerator()) / composition.fpsDenominator() : 30.0;
        const model3d::Mesh posed = posedModelMesh(layer, mesh, frame, fps,
                                                   media->assetById(layer.modelAssetId).sourcePath());
        const model3d::Camera camera = sceneCameraOf(&composition, canvas, frame);
        const QMatrix4x4 place = layerModelMatrixAt(layer.transform, frame);
        const QMatrix4x4 viewProjection = camera.viewProjection(canvas);
        const auto toCanvas = [&](const QVector3D& world, QPointF* point) {
            const QVector4D clipPos = viewProjection * QVector4D(world, 1.0f);
            if (clipPos.w() <= 1e-3f) return false;
            *point = QPointF((double(clipPos.x()) / clipPos.w() * 0.5 + 0.5) * width,
                             (1.0 - (double(clipPos.y()) / clipPos.w() * 0.5 + 0.5)) * height);
            return true;
        };
        double left = 0.0, top = 0.0, right = 0.0, bottom = 0.0;
        bool any = false;
        // Only the points faces use: posing leaves moved nodes' old ones.
        QVector<bool> used(posed.positions.size(), false);
        for (const model3d::Triangle& triangle : posed.triangles)
            for (int c = 0; c < 3; ++c)
                if (triangle.position[c] >= 0 && triangle.position[c] < used.size()) used[triangle.position[c]] = true;
        QVector3D low, high;
        bool anyPoint = false;
        for (int i = 0; i < posed.positions.size(); ++i) {
            if (!used.at(i)) continue;
            const QVector3D& p = posed.positions.at(i);
            if (!anyPoint) { low = high = p; anyPoint = true; }
            low = QVector3D(qMin(low.x(), p.x()), qMin(low.y(), p.y()), qMin(low.z(), p.z()));
            high = QVector3D(qMax(high.x(), p.x()), qMax(high.y(), p.y()), qMax(high.z(), p.z()));
            QPointF point;
            if (!toCanvas(place.map(p), &point)) continue;
            if (!any) { left = right = point.x(); top = bottom = point.y(); any = true; continue; }
            left = qMin(left, point.x()); right = qMax(right, point.x());
            top = qMin(top, point.y()); bottom = qMax(bottom, point.y());
        }
        if (!any) return geometry;
        geometry.local = QRectF(low.x(), -high.y(), high.x() - low.x(), high.y() - low.y());
        geometry.quad = corners(QRectF(QPointF(left, top), QPointF(right, bottom)));
        // How far a pixel reaches at the model's centre, along the camera's
        // right: what a move of its box takes back into the scene.
        const QVector3D centre = place.map((low + high) * 0.5f);
        const QVector3D across = QVector3D::crossProduct(camera.target - camera.position, camera.up).normalized();
        QPointF from, to;
        if (toCanvas(centre, &from) && toCanvas(centre + across * 100.0f, &to)) {
            const double pixels = QLineF(from, to).length();
            if (pixels > 1e-6) geometry.unitsPerPixel = 100.0 / pixels;
        }
        geometry.inScene = true;
        geometry.valid = true;
        return geometry;
    }
    if (layer.kind != LayerKind::Media && layer.kind != LayerKind::Plane
        && layer.kind != LayerKind::Text) {
        return geometry;
    }
    const composition::Clip* clip = clipAt(composition, layer, frame);
    const composition::Effect* text = clip ? textEffectOfClip(*clip) : nullptr;

    // The content in its own units, centred on the layer origin.
    bool anchored = false;
    if (layer.kind == LayerKind::Plane) {
        geometry.local = frameBox;
    } else if (text) {
        QStringList values = text->parameterValues;
        for (int p = 0; p < values.size(); ++p) values[p] = text->parameterAt(p, frame).toString();
        const composition::TextStyle style = composition::textStyleFromParameters(values);
        if (style.text.isEmpty()) return geometry;
        // The same box TextTransformOverlay frames: the frame-sized box on the
        // origin, with the style's horizontal/vertical scale.
        geometry.local = QTransform::fromScale(qMax(0.01, style.horizontalScale / 100.0),
                                               qMax(0.01, style.verticalScale / 100.0))
                             .mapRect(styledTextBounds(frameBox, style));
    } else if (clip) {
        QSizeF size;
        if (clip->nestedComposition) {
            size = clip->nestedComposition->displaySize();
        } else if (media && clip->mediaId.isValid()) {
            const media::MediaAsset asset = media->assetById(clip->mediaId);
            if (asset.frameSize().isValid())
                size = QSizeF(asset.frameSize().width() * asset.pixelAspectValue(),
                              asset.frameSize().height());
        }
        if (size.isEmpty()) return geometry;
        geometry.local = QRectF(-size.width() / 2.0, -size.height() / 2.0, size.width(), size.height());
        anchored = true;
    }
    if (geometry.local.isEmpty()) return geometry;

    const composition::LayerTransform& t = layer.transform;
    if (layer.dimension == composition::LayerDimension::ThreeD) {
        // The sheet carried into the scene and seen through the camera, as
        // renderClipIn3D draws it.
        geometry.inScene = true;
        const QMatrix4x4 mvp = sceneCameraOf(&composition, canvas, frame).viewProjection(canvas)
                               * layerPlaneMatrixAt(t, frame);
        for (const QPointF& corner : corners(geometry.local)) {
            const QVector4D clipPos = mvp * QVector4D(float(corner.x()), float(-corner.y()), 0.0f, 1.0f);
            if (clipPos.w() <= 1e-3f) return LayerGeometry();
            const double ndcX = double(clipPos.x()) / clipPos.w();
            const double ndcY = double(clipPos.y()) / clipPos.w();
            geometry.quad << QPointF((ndcX * 0.5 + 0.5) * width, (1.0 - (ndcY * 0.5 + 0.5)) * height);
        }
        geometry.valid = true;
        return geometry;
    }

    // RenderWorker::renderClip: centred on (W/2 + x, H/2 - y), turned about
    // the anchor (footage) or the centre (plane, text), scaled about the origin.
    const QPointF position = t.positionAt(frame);
    const QPointF scale = t.scaleAt(frame);
    QTransform toCanvas;
    toCanvas.translate(width / 2.0 + position.x(), height / 2.0 - position.y());
    const double angle = t.rotationAt(frame);
    if (anchored) {
        geometry.pivotOffset = QPointF(t.anchorPoint.x(), -t.anchorPoint.y());
        toCanvas.translate(t.anchorPoint.x(), -t.anchorPoint.y());
        toCanvas.rotate(angle);
        toCanvas.translate(-t.anchorPoint.x(), t.anchorPoint.y());
    } else {
        toCanvas.rotate(angle);
    }
    toCanvas.scale(scale.x() / 100.0, scale.y() / 100.0);
    geometry.toCanvas = toCanvas;
    geometry.quad = toCanvas.map(corners(geometry.local));
    geometry.valid = true;
    return geometry;
}

bool fitLayerToBox(const composition::Composition& composition, const composition::Layer& layer,
                   const media::MediaManager* media, int frame, const QRectF& box,
                   QPointF* position, QPointF* scale)
{
    const LayerGeometry before = layerGeometry(composition, layer, media, frame);
    const QRectF old = before.bounds();
    if (!before.valid || old.width() <= 0.0 || old.height() <= 0.0 || box.isEmpty()) return false;
    const composition::LayerTransform& t = layer.transform;
    const double rx = box.width() / old.width();
    const double ry = box.height() / old.height();
    QPointF newScale = t.scaleAt(frame);
    QPointF newPosition = t.positionAt(frame);
    if (before.inScene) {
        newScale = QPointF(newScale.x() * rx, newScale.y() * ry);
        // On-screen pixels per scene unit at the sheet: its top edge on the
        // canvas against the same edge in the scene.
        const double sceneEdge = before.local.width() * qAbs(t.scaleAt(frame).x()) / 100.0;
        const double screenEdge = QLineF(before.quad.at(0), before.quad.at(1)).length();
        const double unitsPerPixel = before.unitsPerPixel > 0.0 ? before.unitsPerPixel
                                     : screenEdge > 0.0 ? sceneEdge / screenEdge : 1.0;
        const QPointF shift = box.center() - old.center();
        newPosition += QPointF(shift.x() * unitsPerPixel, -shift.y() * unitsPerPixel);
        if (before.unitsPerPixel > 0.0) {
            // A model's box need not be centred on its position, and grows
            // about it: once more for what is left.
            composition::Layer moved = layer;
            moved.transform.scaleXCurve = composition::KeyFrameList();
            moved.transform.scaleYCurve = composition::KeyFrameList();
            moved.transform.scalePercent = newScale;
            moved.transform.positionXCurve = composition::KeyFrameList();
            moved.transform.positionYCurve = composition::KeyFrameList();
            moved.transform.position = newPosition;
            const LayerGeometry after = layerGeometry(composition, moved, media, frame);
            if (after.valid && after.unitsPerPixel > 0.0) {
                const QPointF rest = box.center() - after.bounds().center();
                newPosition += QPointF(rest.x() * after.unitsPerPixel, -rest.y() * after.unitsPerPixel);
            }
        }
    } else {
        // A box turned by a quarter swaps its axes; any other angle scales
        // evenly by the area.
        const double angle = std::fmod(std::abs(t.rotationAt(frame)), 180.0);
        if (angle < 0.01 || angle > 179.99) {
            newScale = QPointF(newScale.x() * rx, newScale.y() * ry);
        } else if (std::abs(angle - 90.0) < 0.01) {
            newScale = QPointF(newScale.x() * ry, newScale.y() * rx);
        } else {
            const double even = std::sqrt(rx * ry);
            newScale *= even;
        }
        // The box's centre need not be the origin (text, anchor): scale
        // first, then move by what is left.
        composition::Layer scaled = layer;
        scaled.transform.scaleXCurve = composition::KeyFrameList();
        scaled.transform.scaleYCurve = composition::KeyFrameList();
        scaled.transform.scalePercent = newScale;
        scaled.transform.positionXCurve = composition::KeyFrameList();
        scaled.transform.positionYCurve = composition::KeyFrameList();
        scaled.transform.position = newPosition;
        const LayerGeometry after = layerGeometry(composition, scaled, media, frame);
        if (after.valid) {
            const QPointF shift = box.center() - after.bounds().center();
            newPosition += QPointF(shift.x(), -shift.y());
        }
    }
    if (position) *position = newPosition;
    if (scale) *scale = newScale;
    return true;
}

} // namespace openvegas::render
