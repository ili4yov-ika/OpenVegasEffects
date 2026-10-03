#include "render/TextGeometry.h"

#include <QLineF>
#include <QMatrix4x4>
#include <QPolygonF>
#include <QVector2D>
#include <QVector3D>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "render/TextRender.h"

namespace openvegas {
namespace render {

namespace {

using plugin::NativeGeometryBatch;
using plugin::NativeGeometryPolygon;
using plugin::NativeGeometryTriangle;
using plugin::NativeGeometryVertex;

double signedArea(const QPolygonF& loop)
{
    double area = 0.0;
    for (qsizetype i = 0; i < loop.size(); ++i) {
        const QPointF& a = loop.at(i);
        const QPointF& b = loop.at((i + 1) % loop.size());
        area += a.x() * b.y() - b.x() * a.y();
    }
    return area / 2.0;
}

// Flattened subpaths repeat their first point at the end and may repeat
// points where curve segments meet; a loop is its distinct corners.
QPolygonF distinctCorners(const QPolygonF& subpath)
{
    QPolygonF loop;
    for (const QPointF& point : subpath) {
        if (loop.isEmpty() || QLineF(loop.constLast(), point).length() > 1e-6) {
            loop.append(point);
        }
    }
    while (loop.size() > 1 && QLineF(loop.constFirst(), loop.constLast()).length() <= 1e-6) {
        loop.removeLast();
    }
    return loop;
}

// Loops inside an odd number of others are holes - the even-odd reading of a
// glyph, which holds for the non-overlapping contours fonts are made of.
int nestingDepth(const QVector<QPolygonF>& loops, qsizetype index)
{
    const QPointF probe = loops.at(index).constFirst();
    int depth = 0;
    for (qsizetype other = 0; other < loops.size(); ++other) {
        if (other != index && loops.at(other).containsPoint(probe, Qt::OddEvenFill)) {
            ++depth;
        }
    }
    return depth;
}

// --- Filling -----------------------------------------------------------

struct Corner
{
    double x = 0.0;
    double y = 0.0;
    qint32 index = 0;   // vertex of the batch
};

using Loop = QVector<Corner>;

double turn(const Corner& a, const Corner& b, const Corner& c)
{
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

double loopArea(const Loop& loop)
{
    double area = 0.0;
    for (qsizetype i = 0; i < loop.size(); ++i) {
        const Corner& a = loop.at(i);
        const Corner& b = loop.at((i + 1) % loop.size());
        area += a.x * b.y - b.x * a.y;
    }
    return area / 2.0;
}

bool samePlace(const Corner& a, const Corner& b)
{
    return a.x == b.x && a.y == b.y;
}

bool insideLoop(const Loop& loop, double x, double y)
{
    bool inside = false;
    for (qsizetype i = 0, j = loop.size() - 1; i < loop.size(); j = i++) {
        const Corner& a = loop.at(i);
        const Corner& b = loop.at(j);
        if ((a.y > y) != (b.y > y)
            && x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x) {
            inside = !inside;
        }
    }
    return inside;
}

bool insideTriangle(const Corner& p, const Corner& a, const Corner& b, const Corner& c,
                    double tolerance)
{
    return turn(a, b, p) >= -tolerance && turn(b, c, p) >= -tolerance
           && turn(c, a, p) >= -tolerance;
}

// Joins a hole (clockwise) into its counter-clockwise outer loop through a
// bridge from the hole's rightmost corner to a corner of the outer loop it can
// see, so the pair becomes one loop an ear clipper can fill.
bool bridgeHole(Loop& outer, const Loop& hole, double tolerance)
{
    qsizetype rightmost = 0;
    for (qsizetype i = 1; i < hole.size(); ++i) {
        if (hole.at(i).x > hole.at(rightmost).x) rightmost = i;
    }
    const Corner m = hole.at(rightmost);
    // The nearest outer edge hit by a ray from m towards +x.
    double hitX = std::numeric_limits<double>::infinity();
    qsizetype edge = -1;
    for (qsizetype i = 0; i < outer.size(); ++i) {
        const Corner& a = outer.at(i);
        const Corner& b = outer.at((i + 1) % outer.size());
        if ((a.y > m.y) == (b.y > m.y) || a.y == b.y) continue;
        const double x = a.x + (m.y - a.y) * (b.x - a.x) / (b.y - a.y);
        if (x >= m.x && x < hitX) {
            hitX = x;
            edge = i;
        }
    }
    if (edge < 0) return false;
    const qsizetype next = (edge + 1) % outer.size();
    qsizetype target = outer.at(edge).x > outer.at(next).x ? edge : next;
    // A reflex outer corner inside (m, hit, target) would block the bridge;
    // the one closest in angle to the ray is visible from m.
    const Corner hit {hitX, m.y, -1};
    const Corner candidate = outer.at(target);
    double bestAngle = std::numeric_limits<double>::infinity();
    double bestDistance = std::numeric_limits<double>::infinity();
    for (qsizetype i = 0; i < outer.size(); ++i) {
        if (i == target) continue;
        const Corner& previous = outer.at((i + outer.size() - 1) % outer.size());
        const Corner& corner = outer.at(i);
        const Corner& following = outer.at((i + 1) % outer.size());
        if (turn(previous, corner, following) > 0.0) continue;   // convex
        const bool inside = candidate.y >= m.y
            ? insideTriangle(corner, m, hit, candidate, tolerance)
            : insideTriangle(corner, m, candidate, hit, tolerance);
        if (!inside || corner.x < m.x) continue;
        const double dx = corner.x - m.x;
        const double dy = std::abs(corner.y - m.y);
        const double angle = std::atan2(dy, dx);
        const double distance = dx * dx + dy * dy;
        if (angle < bestAngle || (angle == bestAngle && distance < bestDistance)) {
            bestAngle = angle;
            bestDistance = distance;
            target = i;
        }
    }
    Loop merged;
    merged.reserve(outer.size() + hole.size() + 2);
    for (qsizetype i = 0; i <= target; ++i) merged.append(outer.at(i));
    for (qsizetype i = 0; i <= hole.size(); ++i) {
        merged.append(hole.at((rightmost + i) % hole.size()));
    }
    for (qsizetype i = target; i < outer.size(); ++i) merged.append(outer.at(i));
    outer = std::move(merged);
    return true;
}

// Ear clipping of a counter-clockwise loop; the triangles keep that winding.
void clipEars(const Loop& loop, double tolerance, QVector<std::array<qint32, 3>>& triangles)
{
    QVector<qsizetype> remaining(loop.size());
    for (qsizetype i = 0; i < loop.size(); ++i) remaining[i] = i;
    while (remaining.size() > 3) {
        const qsizetype count = remaining.size();
        qsizetype ear = -1;
        bool dropped = false;
        for (qsizetype i = 0; i < count; ++i) {
            const Corner& a = loop.at(remaining.at((i + count - 1) % count));
            const Corner& b = loop.at(remaining.at(i));
            const Corner& c = loop.at(remaining.at((i + 1) % count));
            const double bend = turn(a, b, c);
            if (std::abs(bend) <= tolerance) {
                // Straight or doubled back: the corner adds no area.
                remaining.removeAt(i);
                dropped = true;
                break;
            }
            if (bend < 0.0) continue;
            bool empty = true;
            for (qsizetype j = 0; j < count && empty; ++j) {
                const Corner& p = loop.at(remaining.at(j));
                if (samePlace(p, a) || samePlace(p, b) || samePlace(p, c)) continue;
                if (insideTriangle(p, a, b, c, tolerance)) empty = false;
            }
            if (empty) {
                ear = i;
                break;
            }
        }
        if (dropped) continue;
        if (ear < 0) {
            // Rounding left no clean ear; take the sharpest convex corner so
            // the loop still closes instead of losing the rest of the face.
            double sharpest = 0.0;
            for (qsizetype i = 0; i < count; ++i) {
                const double bend = turn(loop.at(remaining.at((i + count - 1) % count)),
                                         loop.at(remaining.at(i)),
                                         loop.at(remaining.at((i + 1) % count)));
                if (bend > sharpest) {
                    sharpest = bend;
                    ear = i;
                }
            }
            if (ear < 0) return;
        }
        triangles.append({loop.at(remaining.at((ear + count - 1) % count)).index,
                          loop.at(remaining.at(ear)).index,
                          loop.at(remaining.at((ear + 1) % count)).index});
        remaining.removeAt(ear);
    }
    if (remaining.size() == 3) {
        const Corner& a = loop.at(remaining.at(0));
        const Corner& b = loop.at(remaining.at(1));
        const Corner& c = loop.at(remaining.at(2));
        if (turn(a, b, c) > tolerance) triangles.append({a.index, b.index, c.index});
    }
}

QVector3D vectorOf(const float (&values)[3])
{
    return QVector3D(values[0], values[1], values[2]);
}

// Fills the loops of one side and material of a batch.
void fillLoops(NativeGeometryBatch& batch, const QVector<qsizetype>& polygons,
               qint32 material, qint32 group)
{
    const NativeGeometryPolygon& first = batch.polygons.at(polygons.constFirst());
    const auto vertexAt = [&batch](qint32 index) -> const NativeGeometryVertex* {
        return index >= 0 && index < batch.vertices.size() ? &batch.vertices.at(index) : nullptr;
    };
    // Flux takes the plane from the first corner's normal; a loop that came
    // without one gets the normal of its own outline.
    const NativeGeometryVertex* origin = vertexAt(first.indices.constFirst());
    if (!origin) return;
    QVector3D normal = vectorOf(origin->normal);
    if (normal.lengthSquared() < 1e-12f) {
        for (qsizetype i = 0; i < first.indices.size(); ++i) {
            const NativeGeometryVertex* a = vertexAt(first.indices.at(i));
            const NativeGeometryVertex* b =
                vertexAt(first.indices.at((i + 1) % first.indices.size()));
            if (!a || !b) continue;
            normal += QVector3D((a->position[1] - b->position[1]) * (a->position[2] + b->position[2]),
                                (a->position[2] - b->position[2]) * (a->position[0] + b->position[0]),
                                (a->position[0] - b->position[0]) * (a->position[1] + b->position[1]));
        }
    }
    if (normal.lengthSquared() < 1e-12f) return;
    normal.normalize();
    // u, v and the normal are right-handed, so counter-clockwise in (u, v)
    // faces along the normal.
    const QVector3D helper = std::abs(normal.x()) < 0.9f ? QVector3D(1, 0, 0) : QVector3D(0, 1, 0);
    const QVector3D u = QVector3D::crossProduct(helper, normal).normalized();
    const QVector3D v = QVector3D::crossProduct(normal, u);
    const QVector3D base = vectorOf(origin->position);

    QVector<Loop> loops;
    double extent = 0.0;
    for (qsizetype p : polygons) {
        Loop loop;
        for (qint32 index : batch.polygons.at(p).indices) {
            const NativeGeometryVertex* vertex = vertexAt(index);
            if (!vertex) continue;
            const QVector3D offset = vectorOf(vertex->position) - base;
            const Corner corner {QVector3D::dotProduct(offset, u),
                                 QVector3D::dotProduct(offset, v), index};
            if (loop.isEmpty() || !samePlace(loop.constLast(), corner)) loop.append(corner);
            extent = qMax(extent, qMax(std::abs(corner.x), std::abs(corner.y)));
        }
        while (loop.size() > 1 && samePlace(loop.constFirst(), loop.constLast())) {
            loop.removeLast();
        }
        if (loop.size() >= 3 && std::abs(loopArea(loop)) > 0.0) loops.append(loop);
    }
    if (loops.isEmpty()) return;
    const double tolerance = qMax(1e-12, extent * extent * 1e-12);

    QVector<int> depth(loops.size(), 0);
    for (qsizetype i = 0; i < loops.size(); ++i) {
        const Corner& probe = loops.at(i).constFirst();
        for (qsizetype j = 0; j < loops.size(); ++j) {
            if (i != j && insideLoop(loops.at(j), probe.x, probe.y)) ++depth[i];
        }
    }
    QVector<QVector<qsizetype>> holesOf(loops.size());
    for (qsizetype i = 0; i < loops.size(); ++i) {
        if (depth.at(i) % 2 == 0) continue;
        // The innermost outer loop around the hole owns it.
        qsizetype owner = -1;
        for (qsizetype j = 0; j < loops.size(); ++j) {
            if (depth.at(j) != depth.at(i) - 1) continue;
            const Corner& probe = loops.at(i).constFirst();
            if (!insideLoop(loops.at(j), probe.x, probe.y)) continue;
            if (owner < 0 || std::abs(loopArea(loops.at(j))) < std::abs(loopArea(loops.at(owner)))) {
                owner = j;
            }
        }
        if (owner >= 0) holesOf[owner].append(i);
    }

    QVector<std::array<qint32, 3>> triangles;
    for (qsizetype i = 0; i < loops.size(); ++i) {
        if (depth.at(i) % 2 != 0) continue;
        Loop outer = loops.at(i);
        if (loopArea(outer) < 0.0) std::reverse(outer.begin(), outer.end());
        QVector<Loop> holes;
        for (qsizetype h : holesOf.at(i)) {
            Loop hole = loops.at(h);
            if (loopArea(hole) > 0.0) std::reverse(hole.begin(), hole.end());
            holes.append(hole);
        }
        // Rightmost holes first, so later bridges never cross earlier ones.
        const auto rightmost = [](const Loop& loop) {
            double x = -std::numeric_limits<double>::infinity();
            for (const Corner& corner : loop) x = qMax(x, corner.x);
            return x;
        };
        std::sort(holes.begin(), holes.end(), [&](const Loop& a, const Loop& b) {
            return rightmost(a) > rightmost(b);
        });
        for (const Loop& hole : std::as_const(holes)) bridgeHole(outer, hole, tolerance);
        clipEars(outer, tolerance, triangles);
    }
    for (const auto& corners : std::as_const(triangles)) {
        NativeGeometryTriangle triangle;
        triangle.indices[0] = corners[0];
        triangle.indices[1] = corners[1];
        triangle.indices[2] = corners[2];
        triangle.material = material;
        triangle.group = group;
        batch.triangles.append(triangle);
    }
}

} // namespace

QVector<NativeGeometryBatch> textGeometry(const QRectF& box, const composition::TextStyle& style)
{
    QVector<NativeGeometryBatch> geometry;
    const double width = qMax(1.0, box.width());
    const double height = qMax(1.0, box.height());
    for (const GlyphOutline& glyph : styledTextOutlines(box, style)) {
        QVector<QPolygonF> loops;
        for (const QPolygonF& subpath : glyph.path.toSubpathPolygons()) {
            QPolygonF loop = distinctCorners(subpath);
            if (loop.size() >= 3 && std::abs(signedArea(loop)) > 1e-6) loops.append(loop);
        }
        if (loops.isEmpty()) continue;
        NativeGeometryBatch batch;
        double left = std::numeric_limits<double>::infinity();
        double right = -std::numeric_limits<double>::infinity();
        for (qsizetype i = 0; i < loops.size(); ++i) {
            const bool hole = nestingDepth(loops, i) % 2 == 1;
            QPolygonF loop = loops.at(i);
            for (QPointF& point : loop) point.setY(-point.y());   // Y up from here on
            const double area = signedArea(loop);
            if ((area > 0.0) != hole) std::reverse(loop.begin(), loop.end());
            NativeGeometryPolygon polygon;
            polygon.indices.reserve(loop.size());
            for (const QPointF& point : std::as_const(loop)) {
                NativeGeometryVertex vertex;
                vertex.position[0] = float(point.x());
                vertex.position[1] = float(point.y());
                vertex.normal[2] = -1.0f;
                vertex.uv[0] = float((point.x() - box.left()) / width);
                vertex.uv[1] = float((-point.y() - box.top()) / height);
                polygon.indices.append(qint32(batch.vertices.size()));
                batch.vertices.append(vertex);
                left = qMin(left, point.x());
                right = qMax(right, point.x());
            }
            batch.polygons.append(std::move(polygon));
        }
        batch.extents[0] = float(left);
        batch.extents[1] = float(right);
        batch.extents[2] = float(-glyph.lineBottom);
        batch.extents[3] = float(-glyph.lineTop);
        geometry.append(std::move(batch));
    }
    return geometry;
}

void fillGeometryPolygons(QVector<NativeGeometryBatch>& geometry)
{
    for (NativeGeometryBatch& batch : geometry) {
        struct Group
        {
            bool back = false;
            qint32 material = 0;
            qint32 group = 0;
            QVector<qsizetype> polygons;
        };
        QVector<Group> groups;
        for (qsizetype p = 0; p < batch.polygons.size(); ++p) {
            const NativeGeometryPolygon& polygon = batch.polygons.at(p);
            if (polygon.indices.size() < 3) continue;
            // A double-sided loop is filled once, on Flux's back side.
            const bool back = polygon.flags & plugin::NativeGeometryBackFace;
            if (!back && !(polygon.flags & plugin::NativeGeometryFrontFace)) continue;
            auto it = std::find_if(groups.begin(), groups.end(), [&](const Group& group) {
                return group.back == back && group.material == polygon.material;
            });
            if (it == groups.end()) {
                groups.append({back, polygon.material, polygon.group, {}});
                it = groups.end() - 1;
            }
            it->polygons.append(p);
        }
        for (const Group& group : std::as_const(groups)) {
            fillLoops(batch, group.polygons, group.material, group.group);
        }
        batch.polygons.clear();
    }
}

model3d::Mesh meshFromGeometry(const QVector<NativeGeometryBatch>& geometry,
                               const QVector<model3d::Material>& materials)
{
    model3d::Mesh mesh;
    mesh.materials = materials.isEmpty() ? QVector<model3d::Material> {model3d::Material()}
                                         : materials;
    for (const NativeGeometryBatch& batch : geometry) {
        QMatrix4x4 placement;
        std::copy(std::begin(batch.matrix), std::end(batch.matrix), placement.data());
        placement.optimize();
        const qint32 base = qint32(mesh.positions.size());
        for (const NativeGeometryVertex& vertex : batch.vertices) {
            mesh.positions.append(placement.map(vectorOf(vertex.position)));
            const QVector3D normal = placement.mapVector(vectorOf(vertex.normal));
            mesh.normals.append(normal.lengthSquared() > 1e-12f ? normal.normalized() : normal);
            mesh.uvs.append(QVector2D(vertex.uv[0], vertex.uv[1]));
        }
        const qint32 count = qint32(batch.vertices.size());
        for (const NativeGeometryTriangle& source : batch.triangles) {
            if (source.indices[0] < 0 || source.indices[0] >= count
                || source.indices[1] < 0 || source.indices[1] >= count
                || source.indices[2] < 0 || source.indices[2] >= count) {
                continue;
            }
            model3d::Triangle triangle;
            for (int c = 0; c < 3; ++c) {
                const qint32 index = base + source.indices[c];
                triangle.position[c] = index;
                triangle.normal[c] = mesh.normals.at(index).isNull() ? -1 : index;
                triangle.uv[c] = index;
            }
            triangle.material = source.material >= 0 && source.material < mesh.materials.size()
                                    ? source.material : 0;
            mesh.triangles.append(triangle);
        }
    }
    return mesh;
}

} // namespace render
} // namespace openvegas
