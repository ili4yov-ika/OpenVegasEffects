#include "ui/ModelNodeOverlay.h"

#include <QLineF>
#include <QPainter>
#include <QSet>
#include <QVector4D>

#include <cmath>
#include <limits>

#include "composition/Composition.h"
#include "media/MediaManager.h"
#include "render/LayerGeometry.h"
#include "ui/ViewerWidget.h"

namespace openvegas::ui {

namespace {

const QColor kOutlineColor(255, 170, 40);
// Past this many edges a node is outlined by its first ones only: the frame
// is redrawn on every hover and a dense mesh would make it crawl.
constexpr int kMaxOutlineEdges = 60000;

// Whether `node` is `ancestor` or one of its children, at any depth.
bool within(const model3d::Mesh& mesh, int node, int ancestor)
{
    for (int at = node, steps = 0; at >= 0 && at < mesh.nodes.size() && steps <= mesh.nodes.size();
         at = mesh.nodes.at(at).parent, ++steps) {
        if (at == ancestor) return true;
    }
    return false;
}

// A value written at `frame`: into the property's keys when it is animated.
void writeAtFrame(composition::KeyFrameList& curve, int frame, double value, double* staticValue)
{
    if (curve.isEmpty()) {
        *staticValue = value;
        return;
    }
    const composition::KeyFrame* key = curve.at(frame);
    curve.set(frame, value, key ? key->temporal : composition::TemporalType::Linear);
}

} // namespace

ModelNodeOverlay::ModelNodeOverlay(TargetProvider target, std::shared_ptr<media::MediaManager> media,
                                   Picked picked, DragStep began, DragStep changed,
                                   DragStep finished)
    : m_target(std::move(target)), m_media(std::move(media)), m_picked(std::move(picked)),
      m_began(std::move(began)), m_changed(std::move(changed)), m_finished(std::move(finished))
{
}

bool ModelNodeOverlay::isActive() const
{
    const ViewerWidget* owner = viewer();
    if (owner && owner->isSpherical()) return false;
    if (m_dragging) return true;
    const Target target = m_target ? m_target() : Target();
    return target.composition && target.layer
           && target.layer->kind == composition::LayerKind::Model3D
           && target.layer->modelAssetId.isValid();
}

bool ModelNodeOverlay::acceptsTool(int tool) const
{
    return tool == int(ViewerWidget::ViewerTool::Select);
}

const model3d::Mesh* ModelNodeOverlay::meshFor(const composition::Layer& layer) const
{
    if (!m_media) return nullptr;
    const QString id = layer.modelAssetId.value();
    const quint64 revision = m_media->modelRevision(layer.modelAssetId);
    if (id != m_meshId || revision != m_meshRevision) {
        m_mesh = m_media->modelMesh(layer.modelAssetId);
        m_meshId = id;
        m_meshRevision = revision;
    }
    return m_mesh.isEmpty() ? nullptr : &m_mesh;
}

ModelNodeOverlay::Scene ModelNodeOverlay::sceneFor(const Target& target) const
{
    Scene scene;
    if (!target.composition || !target.layer) return scene;
    scene.mesh = meshFor(*target.layer);
    const QSize canvas = target.composition->displaySize();
    if (!scene.mesh || canvas.isEmpty()) return scene;
    // As RenderWorker::renderModelLayer draws it: the shot's camera, the
    // layer's transform, each node's own on top.
    const model3d::Camera camera = render::sceneCameraOf(target.composition, canvas, target.frame);
    scene.viewProjection = camera.viewProjection(canvas)
                           * render::layerModelMatrixAt(target.layer->transform, target.frame);
    const double fps = double(target.composition->fpsNumerator())
                       / qMax(1, target.composition->fpsDenominator());
    scene.nodeMatrices = render::posedModelNodeMatrices(
        *target.layer, *scene.mesh, target.frame, fps,
        m_media ? m_media->assetById(target.layer->modelAssetId).sourcePath() : QString());
    scene.canvas = QSizeF(canvas);
    scene.valid = true;
    return scene;
}

bool ModelNodeOverlay::project(const Scene& scene, int node, const QVector3D& point,
                               QPointF* screen, double* depth)
{
    const QVector3D placed = node >= 0 && node < scene.nodeMatrices.size()
                                 ? scene.nodeMatrices.at(node).map(point) : point;
    const QVector4D clip = scene.viewProjection * QVector4D(placed, 1.0f);
    if (clip.w() <= 1e-4f) return false;
    const double ndcX = double(clip.x()) / clip.w();
    const double ndcY = double(clip.y()) / clip.w();
    *screen = QPointF((ndcX * 0.5 + 0.5) * scene.canvas.width(),
                      (1.0 - (ndcY * 0.5 + 0.5)) * scene.canvas.height());
    if (depth) *depth = double(clip.z()) / clip.w();
    return true;
}

QString ModelNodeOverlay::nodeAt(const QPointF& canvasPos) const
{
    const Target target = m_target ? m_target() : Target();
    const Scene scene = sceneFor(target);
    if (!scene.valid) return QString();
    const model3d::Mesh& mesh = *scene.mesh;
    double nearest = std::numeric_limits<double>::infinity();
    int found = -2;
    for (const model3d::Triangle& tri : mesh.triangles) {
        QPointF corner[3];
        double depth[3];
        bool visible = true;
        for (int c = 0; c < 3 && visible; ++c) {
            const int p = tri.position[c];
            visible = p >= 0 && p < mesh.positions.size()
                      && project(scene, tri.node, mesh.positions.at(p), &corner[c], &depth[c]);
        }
        if (!visible) continue;
        const auto edge = [](const QPointF& a, const QPointF& b, const QPointF& p) {
            return (p.x() - a.x()) * (b.y() - a.y()) - (p.y() - a.y()) * (b.x() - a.x());
        };
        const double area = edge(corner[0], corner[1], corner[2]);
        if (std::abs(area) < 1e-9) continue;
        const double w0 = edge(corner[1], corner[2], canvasPos) / area;
        const double w1 = edge(corner[2], corner[0], canvasPos) / area;
        const double w2 = edge(corner[0], corner[1], canvasPos) / area;
        if (w0 < 0.0 || w1 < 0.0 || w2 < 0.0) continue;
        const double at = w0 * depth[0] + w1 * depth[1] + w2 * depth[2];
        if (at < nearest) {
            nearest = at;
            found = tri.node;
        }
    }
    if (found == -2) return QString();
    return found >= 0 ? mesh.nodePath(found) : QStringLiteral("");
}

QVector<QLineF> ModelNodeOverlay::outline() const
{
    QVector<QLineF> lines;
    const Target target = m_target ? m_target() : Target();
    if (target.node.isEmpty()) return lines;
    const Scene scene = sceneFor(target);
    if (!scene.valid) return lines;
    const model3d::Mesh& mesh = *scene.mesh;
    const int selected = mesh.nodeByPath(target.node);
    if (selected < 0) return lines;
    QSet<quint64> drawn;
    for (const model3d::Triangle& tri : mesh.triangles) {
        if (!within(mesh, tri.node, selected)) continue;
        QPointF corner[3];
        bool visible = true;
        for (int c = 0; c < 3 && visible; ++c) {
            const int p = tri.position[c];
            visible = p >= 0 && p < mesh.positions.size()
                      && project(scene, tri.node, mesh.positions.at(p), &corner[c], nullptr);
        }
        if (!visible) continue;
        for (int c = 0; c < 3; ++c) {
            const quint32 a = quint32(tri.position[c]);
            const quint32 b = quint32(tri.position[(c + 1) % 3]);
            const quint64 key = (quint64(qMin(a, b)) << 32) | qMax(a, b);
            if (drawn.contains(key)) continue;
            drawn.insert(key);
            lines.append(QLineF(corner[c], corner[(c + 1) % 3]));
            if (lines.size() >= kMaxOutlineEdges) return lines;
        }
    }
    return lines;
}

void ModelNodeOverlay::paint(QPainter& painter, const ViewerMapping& mapping)
{
    if (mapping.isSpherical()) return;
    const QVector<QLineF> lines = outline();
    if (lines.isEmpty()) return;
    QVector<QLineF> shown;
    shown.reserve(lines.size());
    for (const QLineF& line : lines)
        shown.append(QLineF(mapping.toWidget(line.p1()), mapping.toWidget(line.p2())));
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(0, 0, 0, 110), 2.5));
    painter.drawLines(shown);
    painter.setPen(QPen(kOutlineColor, 1.0));
    painter.drawLines(shown);

    // Where the node turns: its anchor point.
    const Target target = m_target ? m_target() : Target();
    const Scene scene = sceneFor(target);
    const int node = scene.valid ? scene.mesh->nodeByPath(target.node) : -1;
    if (node < 0) return;
    QVector3D anchor = scene.mesh->nodes.at(node).pivot;
    if (const composition::ModelNodeTransform* state = target.layer->modelNode(target.node)) {
        anchor = QVector3D(float(state->transform.anchorPoint.x()), float(state->transform.anchorPoint.y()),
                           float(state->transform.anchorPointZ));
    }
    QPointF at;
    if (!project(scene, node, anchor, &at, nullptr)) return;
    const QPointF centre = mapping.toWidget(at);
    painter.drawLine(centre - QPointF(6, 0), centre + QPointF(6, 0));
    painter.drawLine(centre - QPointF(0, 6), centre + QPointF(0, 6));
}

bool ModelNodeOverlay::mousePress(ViewerPointerEvent& event)
{
    if (event.button != Qt::LeftButton || event.mapping.isSpherical() || !isActive()) return false;
    const Target target = m_target ? m_target() : Target();
    const QString hit = nodeAt(event.canvasPos);
    if (hit.isNull()) return false;   // not on the model: the tool's

    const Scene scene = sceneFor(target);
    const int selected = scene.valid ? scene.mesh->nodeByPath(target.node) : -1;
    const int hitNode = scene.valid ? scene.mesh->nodeByPath(hit) : -1;
    if (selected < 0 || hitNode < 0 || !within(*scene.mesh, hitNode, selected)
        || target.layer->locked) {
        // Another node, or the model itself: picked, and the press goes on to
        // the tool, which drags the layer.
        if (m_picked) m_picked(hit);
        requestRepaint();
        return false;
    }

    // A drag of the selected node: its position moves in its parent's space,
    // so the point under the pointer stays under it at the node's depth.
    const model3d::ModelNode& node = scene.mesh->nodes.at(selected);
    const composition::ModelNodeTransform* state = target.layer->modelNode(target.node);
    m_before = state ? state->transform
                     : composition::restingModelNodeTransform(node.pivot.x(), node.pivot.y(), node.pivot.z());
    const QMatrix4x4 parent = node.parent >= 0 && node.parent < scene.nodeMatrices.size()
                                  ? scene.nodeMatrices.at(node.parent) : QMatrix4x4();
    m_toNdc = scene.viewProjection * parent;
    const QPointF position = m_before.positionAt(target.frame);
    const QVector4D clip = m_toNdc * QVector4D(float(position.x()), float(position.y()),
                                               float(m_before.positionZAt(target.frame)), 1.0f);
    if (clip.w() <= 1e-4f) return false;
    m_depth = double(clip.z()) / clip.w();
    m_dragNode = target.node;
    m_pressCanvas = event.canvasPos;
    m_dragging = true;
    m_moved = false;
    if (m_began) m_began();
    event.setsCursor = true;
    event.cursor = Qt::SizeAllCursor;
    return true;
}

void ModelNodeOverlay::mouseMove(ViewerPointerEvent& event)
{
    if (!m_dragging) return;
    const Target target = m_target ? m_target() : Target();
    if (!target.layer || !target.composition) return;
    const QSizeF canvas = QSizeF(target.composition->displaySize());
    if (canvas.isEmpty()) return;
    const int frame = target.frame;

    // From where the drag began, each step.
    const QPointF position = m_before.positionAt(frame);
    const QVector3D start(float(position.x()), float(position.y()), float(m_before.positionZAt(frame)));
    const QVector4D clip = m_toNdc * QVector4D(start, 1.0f);
    if (clip.w() <= 1e-4f) return;
    const QPointF delta = event.canvasPos - m_pressCanvas;
    const double ndcX = double(clip.x()) / clip.w() + 2.0 * delta.x() / canvas.width();
    const double ndcY = double(clip.y()) / clip.w() - 2.0 * delta.y() / canvas.height();
    bool invertible = false;
    const QMatrix4x4 back = m_toNdc.inverted(&invertible);
    if (!invertible) return;
    QVector4D moved = back * QVector4D(float(ndcX), float(ndcY), float(m_depth), 1.0f);
    if (std::abs(moved.w()) < 1e-9f) return;
    moved /= moved.w();

    composition::ModelNodeTransform* state = target.layer->modelNode(m_dragNode);
    if (!state) {
        target.layer->modelNodes.append({m_dragNode, m_before});
        state = &target.layer->modelNodes.last();
    }
    composition::LayerTransform& transform = state->transform;
    transform = m_before;
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

void ModelNodeOverlay::mouseRelease(ViewerPointerEvent& event)
{
    Q_UNUSED(event);
    if (!m_dragging) return;
    m_dragging = false;
    if (m_moved && m_finished) m_finished();
    requestRepaint();
}

} // namespace openvegas::ui
