#include "model3d/Mesh.h"

#include <QHash>

#include <cmath>

namespace openvegas {
namespace model3d {

bool Mesh::bounds(QVector3D* minimum, QVector3D* maximum) const
{
    if (positions.isEmpty()) {
        return false;
    }
    QVector3D lo = positions.first();
    QVector3D hi = positions.first();
    for (const QVector3D& p : positions) {
        lo.setX(qMin(lo.x(), p.x()));
        lo.setY(qMin(lo.y(), p.y()));
        lo.setZ(qMin(lo.z(), p.z()));
        hi.setX(qMax(hi.x(), p.x()));
        hi.setY(qMax(hi.y(), p.y()));
        hi.setZ(qMax(hi.z(), p.z()));
    }
    if (minimum) {
        *minimum = lo;
    }
    if (maximum) {
        *maximum = hi;
    }
    return true;
}

double Mesh::boundingSize() const
{
    QVector3D lo;
    QVector3D hi;
    if (!bounds(&lo, &hi)) {
        return 0.0;
    }
    const QVector3D span = hi - lo;
    return qMax(span.x(), qMax(span.y(), span.z()));
}

QVector3D Mesh::centre() const
{
    QVector3D lo;
    QVector3D hi;
    if (!bounds(&lo, &hi)) {
        return QVector3D();
    }
    return (lo + hi) * 0.5f;
}

void Mesh::generateNormals(double smoothingAngleDegrees, bool unify)
{
    if (triangles.isEmpty() || positions.isEmpty()) {
        return;
    }

    // Face normals first: they are both the faceted answer and the input to
    // the smoothing pass below.
    QVector<QVector3D> faceNormals;
    faceNormals.reserve(triangles.size());
    for (const Triangle& tri : triangles) {
        const QVector3D& a = positions.at(tri.position[0]);
        const QVector3D& b = positions.at(tri.position[1]);
        const QVector3D& c = positions.at(tri.position[2]);
        QVector3D n = QVector3D::crossProduct(b - a, c - a);
        if (!n.isNull()) {
            n.normalize();
        }
        faceNormals.append(n);
    }

    // Unify Normals: make every face agree with the first one it shares an
    // edge with, which is what the reference's checkbox does for a model whose
    // winding is inconsistent. Approximated here by flipping any face pointing
    // away from the average.
    if (unify) {
        QVector3D average;
        for (const QVector3D& n : faceNormals) {
            average += n;
        }
        if (!average.isNull()) {
            average.normalize();
            for (int i = 0; i < faceNormals.size(); ++i) {
                if (QVector3D::dotProduct(faceNormals.at(i), average) < 0.0f) {
                    faceNormals[i] = -faceNormals.at(i);
                }
            }
        }
    }

    // Smoothing: a corner averages the faces meeting there, but only those
    // within the angle threshold of its own face. Beyond it the corner keeps
    // the face normal, which is what leaves a hard edge hard.
    const float cosLimit = static_cast<float>(std::cos(smoothingAngleDegrees * M_PI / 180.0));

    QHash<int, QVector<int>> facesAtVertex;
    for (int t = 0; t < triangles.size(); ++t) {
        for (int c = 0; c < 3; ++c) {
            facesAtVertex[triangles.at(t).position[c]].append(t);
        }
    }

    normals.clear();
    normals.reserve(triangles.size() * 3);
    for (int t = 0; t < triangles.size(); ++t) {
        const QVector3D& face = faceNormals.at(t);
        for (int c = 0; c < 3; ++c) {
            QVector3D sum;
            const QVector<int>& neighbours = facesAtVertex.value(triangles.at(t).position[c]);
            for (int other : neighbours) {
                if (QVector3D::dotProduct(faceNormals.at(other), face) >= cosLimit) {
                    sum += faceNormals.at(other);
                }
            }
            if (sum.isNull()) {
                sum = face;
            } else {
                sum.normalize();
            }
            triangles[t].normal[c] = normals.size();
            normals.append(sum);
        }
    }
}

} // namespace model3d
} // namespace openvegas
