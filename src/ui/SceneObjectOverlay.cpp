#include "ui/SceneObjectOverlay.h"

#include <QImage>
#include <QPainter>
#include <QPen>
#include <QVector4D>
#include <QtMath>

#include <cmath>

#include "composition/Composition.h"
#include "render/LayerGeometry.h"
#include "ui/ViewerWidget.h"

namespace openvegas::ui {

namespace {

// The reference's colour for its 3D controls (BiffColor 0.25, 0.5, 1, 1),
// and their line width.
const QColor kControlColor = QColor::fromRgbF(0.25f, 0.5f, 1.0f, 1.0f);
constexpr double kLineWidth = 1.5;
// A handle's icon, and how near the pointer has to be to take it.
constexpr int kHandleSize = 16;
constexpr double kHandleReach = 8.0;
// How far a spot's cone and a directional light's ray are drawn
// (SpotLightControl's 250).
constexpr float kConeLength = 250.0f;

bool isSceneObject(const composition::Layer& layer)
{
    return layer.kind == composition::LayerKind::Camera || layer.kind == composition::LayerKind::Light
           || layer.kind == composition::LayerKind::Point;
}

QVector3D positionOf(const composition::LayerTransform& t, int frame)
{
    const QPointF p = t.positionAt(frame);
    return QVector3D(float(p.x()), float(p.y()), float(t.positionZAt(frame)));
}

// A value written at `frame`: into the property's keys when it is animated.
QVector3D targetOf(const composition::LayerTransform& t, int frame)
{
    return QVector3D(float(t.targetAt(frame, 0)), float(t.targetAt(frame, 1)), float(t.targetAt(frame, 2)));
}

void writeAtFrame(composition::KeyFrameList& curve, int frame, double value, double* staticValue)
{
    if (curve.isEmpty()) {
        *staticValue = value;
        return;
    }
    const composition::KeyFrame* key = curve.at(frame);
    curve.set(frame, value, key ? key->temporal : composition::TemporalType::Linear);
}

// Two directions square to `axis` and to each other.
void basisAround(const QVector3D& axis, QVector3D* u, QVector3D* v)
{
    const QVector3D helper = std::abs(axis.y()) < 0.99f ? QVector3D(0.0f, 1.0f, 0.0f) : QVector3D(1.0f, 0.0f, 0.0f);
    *u = QVector3D::crossProduct(axis, helper).normalized();
    *v = QVector3D::crossProduct(axis, *u).normalized();
}

// Projects scene geometry into a view's canvas, cutting lines at the eye
// (FluxCamera::ClipLineSegmentNearPlane).
struct Projector
{
    QMatrix4x4 viewProjection;
    QSizeF canvas;

    QPointF toCanvas(const QVector4D& clip) const
    {
        return QPointF((double(clip.x()) / clip.w() * 0.5 + 0.5) * canvas.width(),
                       (0.5 - double(clip.y()) / clip.w() * 0.5) * canvas.height());
    }
    bool point(const QVector3D& p, QPointF* out) const
    {
        const QVector4D clip = viewProjection * QVector4D(p, 1.0f);
        if (clip.w() <= 1e-3f) return false;
        *out = toCanvas(clip);
        return true;
    }
    void line(const QVector3D& a, const QVector3D& b, QVector<QLineF>* lines) const
    {
        QVector4D ca = viewProjection * QVector4D(a, 1.0f);
        QVector4D cb = viewProjection * QVector4D(b, 1.0f);
        constexpr float kNear = 1e-3f;
        if (ca.w() <= kNear && cb.w() <= kNear) return;
        if (ca.w() <= kNear) ca = ca + (cb - ca) * ((kNear - ca.w()) / (cb.w() - ca.w()));
        if (cb.w() <= kNear) cb = cb + (ca - cb) * ((kNear - cb.w()) / (ca.w() - cb.w()));
        lines->append(QLineF(toCanvas(ca), toCanvas(cb)));
    }
    void ring(const QVector3D& centre, const QVector3D& u, const QVector3D& v, float radius, int sides,
              QVector<QLineF>* lines) const
    {
        for (int i = 0; i < sides; ++i) {
            const double a0 = 2.0 * M_PI * i / sides, a1 = 2.0 * M_PI * (i + 1) / sides;
            line(centre + (u * float(std::cos(a0)) + v * float(std::sin(a0))) * radius,
                 centre + (u * float(std::cos(a1)) + v * float(std::sin(a1))) * radius, lines);
        }
    }
};

QImage handleIcon(bool light, bool checked, bool hovered)
{
    if (!light) {
        // OriginControl (type 0) and SpotLightTargetControl draw texture 0,
        // crosshairs.png, and texture 1, crosshairs-hover.png, while hovered
        // or selected (VegasEffects.exe 0x1409ebf80, 0x140a17880; the textures
        // 0x14091d4c0) - crosshairs-checked.png is the mask origin's.
        return QImage(checked || hovered ? QStringLiteral(":/icons/viewer/crosshairs-hover@2x.png")
                                         : QStringLiteral(":/icons/viewer/crosshairs@2x.png"));
    }
    const QString state = checked ? QStringLiteral("-checked") : hovered ? QStringLiteral("-hover") : QString();
    return QImage(QStringLiteral(":/icons/viewer/light%1@2x.png").arg(state));
}

} // namespace

SceneObjectOverlay::SceneObjectOverlay(SceneProvider scene, CameraProvider camera, Picked picked,
                                       DragStep began, DragStep changed, DragStep finished)
    : m_scene(std::move(scene))
    , m_camera(std::move(camera))
    , m_picked(std::move(picked))
    , m_began(std::move(began))
    , m_changed(std::move(changed))
    , m_finished(std::move(finished))
{
}

bool SceneObjectOverlay::isActive() const
{
    const ViewerWidget* owner = viewer();
    if (owner && owner->isSpherical()) return false;
    if (m_dragging) return true;
    const Scene scene = m_scene ? m_scene() : Scene();
    if (!scene.composition) return false;
    for (const composition::Layer& layer : scene.composition->layers())
        if (layer.visible && isSceneObject(layer)) return true;
    return false;
}

bool SceneObjectOverlay::acceptsTool(int tool) const
{
    return tool == int(ViewerWidget::ViewerTool::Select);
}

SceneObjectOverlay::Drawing SceneObjectOverlay::drawing(int view) const
{
    Drawing result;
    const Scene scene = m_scene ? m_scene() : Scene();
    if (!scene.composition || !m_camera) return result;
    const std::optional<ViewCamera> viewCamera = m_camera(view);
    if (!viewCamera) return result;
    const QSizeF canvas(scene.composition->displaySize());
    if (canvas.isEmpty()) return result;
    Projector projector{viewCamera->camera.viewProjection(canvas.toSize()), canvas};
    const model3d::Camera& eye = viewCamera->camera;
    // The plane the view looks at, for a light's reach.
    const QVector3D forward = (eye.target - eye.position).normalized();
    const QVector3D right = QVector3D::crossProduct(forward, eye.up).normalized();
    const QVector3D up = QVector3D::crossProduct(right, forward).normalized();
    const int frame = scene.frame;
    bool shotCameraSkipped = false;
    const QVector<composition::Layer>& layers = scene.composition->layers();
    for (int i = 0; i < layers.size(); ++i) {
        const composition::Layer& layer = layers.at(i);
        if (!layer.visible || !isSceneObject(layer)) continue;
        const composition::LayerTransform& t = layer.transform;
        // Where it stands in the scene, under its parent.
        const QMatrix4x4 parent = render::layerParentMatrixAt(scene.composition, layer, frame);
        const QVector3D at = parent.map(positionOf(t, frame));
        const QMatrix4x4 turn = render::layerTurnAt(scene.composition, layer, frame);
        if (layer.kind == composition::LayerKind::Camera) {
            // The view through the shot's camera does not show that camera.
            if (viewCamera->shotCamera && !shotCameraSkipped) {
                shotCameraSkipped = true;
                continue;
            }
            const QVector3D look = turn.mapVector(QVector3D(0.0f, 0.0f, -1.0f)).normalized();
            const QVector3D cameraUp = turn.mapVector(QVector3D(0.0f, 1.0f, 0.0f)).normalized();
            const QVector3D cameraRight = turn.mapVector(QVector3D(1.0f, 0.0f, 0.0f)).normalized();
            // Its zoom: where the shot's frame fills it.
            const double zoom = layer.camera.zoomAt(frame);
            const float distance = float(zoom > 0.0 ? zoom
                : composition::CameraSettings::lensZoom(composition::CameraSettings::kNewCameraLensAngle, canvas.width()));
            const QVector3D middle = at + look * distance;
            const QVector3D halfWidth = cameraRight * float(canvas.width() / 2.0);
            const QVector3D halfHeight = cameraUp * float(canvas.height() / 2.0);
            const QVector3D corners[4] = {middle - halfWidth + halfHeight, middle + halfWidth + halfHeight,
                                          middle + halfWidth - halfHeight, middle - halfWidth - halfHeight};
            for (int c = 0; c < 4; ++c) {
                projector.line(at, corners[c], &result.lines);
                projector.line(corners[c], corners[(c + 1) % 4], &result.lines);
            }
        } else if (layer.kind == composition::LayerKind::Light) {
            const composition::LightSettings& light = layer.light;
            const QVector3D direction = turn.mapVector(QVector3D(0.0f, 0.0f, -1.0f)).normalized();
            using Type = composition::LightSettings::Type;
            if (light.type == Type::Point && light.falloff != composition::LightSettings::Falloff::None) {
                projector.ring(at, right, up, float(light.reachAt(frame)), 100, &result.lines);
            } else if (light.type == Type::Spot) {
                QVector3D u, v;
                basisAround(direction, &u, &v);
                const double half = qDegreesToRadians(light.coneAngleAt(frame)) / 2.0;
                const double inner = (1.0 - light.featherAt(frame) / 100.0) * half;
                const QVector3D base = at + direction * kConeLength;
                const float outerRadius = kConeLength * float(std::tan(qBound(0.0, half, 1.55)));
                const float innerRadius = kConeLength * float(std::tan(qBound(0.0, inner, 1.55)));
                projector.ring(base, u, v, outerRadius, 64, &result.lines);
                if (innerRadius < outerRadius - 0.5f) projector.ring(base, u, v, innerRadius, 64, &result.lines);
                for (const QVector3D& side : {u, -u, v, -v}) projector.line(at, base + side * outerRadius, &result.lines);
            } else if (light.type == Type::Directional) {
                QVector3D u, v;
                basisAround(direction, &u, &v);
                const QVector3D tip = at + direction * kConeLength;
                projector.line(at, tip, &result.lines);
                projector.line(tip, tip - direction * 30.0f + u * 15.0f, &result.lines);
                projector.line(tip, tip - direction * 30.0f - u * 15.0f, &result.lines);
            }
        }
        QPointF handle;
        if (projector.point(at, &handle))
            result.handles.append({i, handle, layer.kind == composition::LayerKind::Light});
        if (layer.autoOrient == composition::AutoOrient::TowardsTargetPosition) {
            // Its Target: a line there and a handle of its own.
            const QVector3D aim = parent.map(targetOf(t, frame));
            projector.line(at, aim, &result.lines);
            QPointF targetHandle;
            if (projector.point(aim, &targetHandle)) result.handles.append({i, targetHandle, false, true});
        }
    }
    return result;
}

std::optional<SceneObjectOverlay::Handle> SceneObjectOverlay::handleAt(int view, const QPointF& canvasPos,
                                                                      double pixelsPerCanvas) const
{
    const Drawing shown = drawing(view);
    const double reach = kHandleReach / qMax(1e-6, pixelsPerCanvas);
    // The topmost handle: the last drawn.
    for (int i = shown.handles.size() - 1; i >= 0; --i) {
        const QPointF d = shown.handles.at(i).position - canvasPos;
        if (std::abs(d.x()) <= reach && std::abs(d.y()) <= reach) return shown.handles.at(i);
    }
    return std::nullopt;
}

int SceneObjectOverlay::layerAt(int view, const QPointF& canvasPos, double pixelsPerCanvas) const
{
    const std::optional<Handle> handle = handleAt(view, canvasPos, pixelsPerCanvas);
    return handle ? handle->layer : -1;
}

void SceneObjectOverlay::paint(QPainter& painter, const ViewerMapping& mapping)
{
    const Drawing shown = drawing(mapping.view);
    if (shown.lines.isEmpty() && shown.handles.isEmpty()) return;
    const Scene scene = m_scene ? m_scene() : Scene();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setClipRect(mapping.viewRect);
    painter.setPen(QPen(kControlColor, kLineWidth));
    for (const QLineF& line : shown.lines)
        painter.drawLine(mapping.toWidget(line.p1()), mapping.toWidget(line.p2()));
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    for (const Handle& handle : shown.handles) {
        const QImage icon = handleIcon(handle.light, handle.layer == scene.selectedLayer, handle.layer == m_hovered);
        const QPointF centre = mapping.toWidget(handle.position);
        const QRectF box(centre.x() - kHandleSize / 2.0, centre.y() - kHandleSize / 2.0, kHandleSize, kHandleSize);
        if (!icon.isNull()) painter.drawImage(box, icon);
    }
}

bool SceneObjectOverlay::mousePress(ViewerPointerEvent& event)
{
    if (event.button != Qt::LeftButton) return false;
    const Scene scene = m_scene ? m_scene() : Scene();
    if (!scene.composition || !m_camera) return false;
    const double scale = event.mapping.canvasSize.width() > 0.0
        ? event.mapping.imageRect.width() / event.mapping.canvasSize.width() : 1.0;
    const std::optional<Handle> pressed = handleAt(event.mapping.view, event.canvasPos, scale);
    if (!pressed) return false;
    const int layer = pressed->layer;
    const std::optional<ViewCamera> viewCamera = m_camera(event.mapping.view);
    if (!viewCamera) return false;
    if (m_picked) m_picked(layer);
    const Scene picked = m_scene ? m_scene() : Scene();
    if (!picked.composition || layer >= picked.composition->layers().size()) return true;
    m_canvas = QSizeF(picked.composition->displaySize());
    m_viewProjection = viewCamera->camera.viewProjection(m_canvas.toSize());
    m_before = picked.composition->layers().at(layer).transform;
    m_dragParent = render::layerParentMatrixAt(picked.composition, picked.composition->layers().at(layer), picked.frame);
    m_dragTarget = pressed->target;
    const QVector3D grabbed =
        m_dragParent.map(m_dragTarget ? targetOf(m_before, picked.frame) : positionOf(m_before, picked.frame));
    const QVector4D clip = m_viewProjection * QVector4D(grabbed, 1.0f);
    if (clip.w() <= 1e-4f) return true;
    m_depth = double(clip.z()) / clip.w();
    m_dragLayer = layer;
    m_pressCanvas = event.canvasPos;
    m_dragging = true;
    m_moved = false;
    event.setsCursor = true;
    event.cursor = Qt::SizeAllCursor;
    requestRepaint();
    return true;
}

void SceneObjectOverlay::mouseMove(ViewerPointerEvent& event)
{
    if (!m_dragging) return;
    const Scene scene = m_scene ? m_scene() : Scene();
    if (!scene.composition || m_dragLayer >= scene.composition->layers().size() || m_canvas.isEmpty()) return;
    const int frame = scene.frame;
    const QVector3D start = m_dragParent.map(m_dragTarget ? targetOf(m_before, frame) : positionOf(m_before, frame));
    const QVector4D clip = m_viewProjection * QVector4D(start, 1.0f);
    if (clip.w() <= 1e-4f) return;
    const QPointF delta = event.canvasPos - m_pressCanvas;
    const double ndcX = double(clip.x()) / clip.w() + 2.0 * delta.x() / m_canvas.width();
    const double ndcY = double(clip.y()) / clip.w() - 2.0 * delta.y() / m_canvas.height();
    bool invertible = false;
    const QMatrix4x4 back = m_viewProjection.inverted(&invertible);
    if (!invertible) return;
    QVector4D moved = back * QVector4D(float(ndcX), float(ndcY), float(m_depth), 1.0f);
    if (std::abs(moved.w()) < 1e-9f) return;
    moved /= moved.w();
    // Back under the parent.
    moved = QVector4D(m_dragParent.inverted().map(moved.toVector3D()), 1.0f);
    if (!m_moved && m_began) m_began();
    composition::LayerTransform& transform = scene.composition->layerRef(m_dragLayer).transform;
    transform = m_before;
    if (m_dragTarget) {
        // The Target's three axes animate together once any does.
        const bool keyed = !transform.targetXCurve.isEmpty() || !transform.targetYCurve.isEmpty()
                           || !transform.targetZCurve.isEmpty();
        composition::KeyFrameList* curves[3] = {&transform.targetXCurve, &transform.targetYCurve,
                                                &transform.targetZCurve};
        double* values[3] = {&transform.targetX, &transform.targetY, &transform.targetZ};
        const double to[3] = {moved.x(), moved.y(), moved.z()};
        for (int axis = 0; axis < 3; ++axis) {
            if (keyed && curves[axis]->isEmpty()) curves[axis]->setDefaultValue(*values[axis]);
            if (keyed) {
                const composition::KeyFrame* key = curves[axis]->at(frame);
                curves[axis]->set(frame, to[axis], key ? key->temporal : composition::TemporalType::Linear);
            } else {
                *values[axis] = to[axis];
            }
        }
        m_moved = true;
        if (m_changed) m_changed();
        event.setsCursor = true;
        event.cursor = Qt::SizeAllCursor;
        requestRepaint();
        return;
    }
    // x and y animate together once either does, as moving a layer does.
    if (transform.positionXCurve.isEmpty() && transform.positionYCurve.isEmpty()) {
        transform.position = QPointF(moved.x(), moved.y());
    } else {
        if (transform.positionXCurve.isEmpty()) transform.positionXCurve.setDefaultValue(transform.position.x());
        if (transform.positionYCurve.isEmpty()) transform.positionYCurve.setDefaultValue(transform.position.y());
        double unusedX = 0.0, unusedY = 0.0;
        writeAtFrame(transform.positionXCurve, frame, moved.x(), &unusedX);
        writeAtFrame(transform.positionYCurve, frame, moved.y(), &unusedY);
    }
    writeAtFrame(transform.positionZCurve, frame, moved.z(), &transform.positionZ);
    m_moved = true;
    if (m_changed) m_changed();
    event.setsCursor = true;
    event.cursor = Qt::SizeAllCursor;
    requestRepaint();
}

void SceneObjectOverlay::mouseRelease(ViewerPointerEvent& event)
{
    Q_UNUSED(event);
    if (!m_dragging) return;
    m_dragging = false;
    if (m_moved && m_finished) m_finished();
    m_moved = false;
    m_dragLayer = -1;
    m_dragTarget = false;
    requestRepaint();
}

void SceneObjectOverlay::hover(ViewerPointerEvent& event)
{
    const double scale = event.mapping.canvasSize.width() > 0.0
        ? event.mapping.imageRect.width() / event.mapping.canvasSize.width() : 1.0;
    const int layer = layerAt(event.mapping.view, event.canvasPos, scale);
    if (layer != m_hovered) {
        m_hovered = layer;
        requestRepaint();
    }
    if (layer >= 0) {
        event.setsCursor = true;
        event.cursor = Qt::SizeAllCursor;
    }
}

} // namespace openvegas::ui
