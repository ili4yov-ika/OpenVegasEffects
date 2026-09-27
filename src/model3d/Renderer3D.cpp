#include "model3d/Renderer3D.h"

#include <QPainter>
#include <QVector4D>

#include <cmath>
#include <limits>

namespace openvegas {
namespace model3d {

namespace {

// A triangle corner after transform: clip-space position plus what shading
// needs interpolated across the face.
struct Vertex
{
    QVector4D clip;
    QVector3D world;
    QVector3D normal;
};

// Screen-space corner: pixel x/y, 1/w for perspective-correct interpolation,
// and depth in 0..1.
struct ScreenVertex
{
    double x = 0.0;
    double y = 0.0;
    double invW = 0.0;
    double depth = 0.0;
    QVector3D world;
    QVector3D normal;
};

double edgeFunction(double ax, double ay, double bx, double by, double px, double py)
{
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

// Phong: ambient + diffuse + specular, with the material's own reflectivities.
// Matches the reference's "Illumination Model: Phong" entry, which is what a
// model without a physically based material set is shaded with.
QColor shade(const Material& material, const QVector3D& normal, const QVector3D& world,
             const QVector3D& eye, const Light& light)
{
    QVector3D n = normal;
    if (n.isNull()) {
        return material.diffuseColor;
    }
    n.normalize();

    QVector3D toLight = -light.direction;
    if (toLight.isNull()) {
        toLight = QVector3D(0.0f, 0.0f, 1.0f);
    }
    toLight.normalize();

    // Two-sided: a face whose winding points away is still lit, which keeps an
    // open or inconsistently wound mesh from turning black.
    double lambert = QVector3D::dotProduct(n, toLight);
    if (lambert < 0.0) {
        n = -n;
        lambert = -lambert;
    }

    QVector3D toEye = eye - world;
    if (!toEye.isNull()) {
        toEye.normalize();
    }
    const QVector3D halfway = (toLight + toEye).normalized();
    const double specular =
        std::pow(qMax(0.0, double(QVector3D::dotProduct(n, halfway))), qMax(1.0, material.shininess));

    const double energy = light.intensity;
    const double lr = light.color.redF() * energy;
    const double lg = light.color.greenF() * energy;
    const double lb = light.color.blueF() * energy;

    const double kd = material.diffuseReflectivity * lambert;
    const double ks = material.specularReflectivity * specular;

    const double r = material.ambientColor.redF() * light.ambient
        + material.diffuseColor.redF() * kd * lr + material.specularColor.redF() * ks * lr
        + material.emissiveColor.redF();
    const double g = material.ambientColor.greenF() * light.ambient
        + material.diffuseColor.greenF() * kd * lg + material.specularColor.greenF() * ks * lg
        + material.emissiveColor.greenF();
    const double b = material.ambientColor.blueF() * light.ambient
        + material.diffuseColor.blueF() * kd * lb + material.specularColor.blueF() * ks * lb
        + material.emissiveColor.blueF();

    return QColor::fromRgbF(qBound(0.0, r, 1.0), qBound(0.0, g, 1.0), qBound(0.0, b, 1.0));
}

const Material& materialFor(const Mesh& mesh, int index, const Material& fallback)
{
    if (index >= 0 && index < mesh.materials.size()) {
        return mesh.materials.at(index);
    }
    return fallback;
}

} // namespace

QMatrix4x4 Camera::viewProjection(const QSize& canvas) const
{
    const double aspect = canvas.height() > 0
        ? double(canvas.width()) / double(canvas.height())
        : 1.0;
    QMatrix4x4 projection;
    projection.perspective(float(fieldOfViewDegrees), float(aspect), float(nearPlane),
                           float(farPlane));
    QMatrix4x4 view;
    view.lookAt(position, target, up);
    return projection * view;
}

Camera defaultCameraFor(const QVector3D& centre, double radius, const QSize& canvas)
{
    Camera camera;
    camera.target = centre;
    // Back off far enough that a sphere of this radius fits the vertical field
    // of view, with a little air around it.
    const double halfFov = camera.fieldOfViewDegrees * 0.5 * M_PI / 180.0;
    const double distance = radius > 0.0 ? (radius * 1.6) / std::sin(halfFov) : 1000.0;
    camera.position = centre + QVector3D(0.0f, 0.0f, float(distance));
    Q_UNUSED(canvas);
    return camera;
}

Camera defaultCameraForCanvas(const QSize& canvas)
{
    Camera camera;
    const double halfFov = camera.fieldOfViewDegrees * 0.5 * M_PI / 180.0;
    const double halfHeight = qMax(1, canvas.height()) * 0.5;
    const double distance = halfHeight / std::tan(halfFov);
    camera.position = QVector3D(0.0f, 0.0f, float(distance));
    camera.target = QVector3D(0.0f, 0.0f, 0.0f);
    return camera;
}

QImage renderMesh(const Mesh& mesh, const QMatrix4x4& modelMatrix, const Camera& camera,
                  const QSize& canvas, const Light& light, ShadingMode mode, double opacity)
{
    QImage image(canvas, QImage::Format_RGBA8888);
    image.fill(Qt::transparent);
    if (mesh.isEmpty() || canvas.isEmpty()) {
        return image;
    }

    const QMatrix4x4 viewProjection = camera.viewProjection(canvas);
    const QMatrix4x4 mvp = viewProjection * modelMatrix;
    // Normals transform by the inverse transpose, not by the model matrix:
    // a non-uniform scale would otherwise tilt them away from the surface.
    // mapVector applies the 3x3 part, leaving the translation out.
    bool invertible = false;
    const QMatrix4x4 inverted = modelMatrix.inverted(&invertible);
    const QMatrix4x4 normalMatrix = invertible ? inverted.transposed() : modelMatrix;

    const int width = canvas.width();
    const int height = canvas.height();
    QVector<double> depthBuffer(size_t(width) * size_t(height),
                                std::numeric_limits<double>::infinity());

    const Material defaultMaterial;
    const double alpha = qBound(0.0, opacity, 1.0);

    QPainter wirePainter;
    if (mode == ShadingMode::Wireframe) {
        wirePainter.begin(&image);
        wirePainter.setRenderHint(QPainter::Antialiasing, true);
        wirePainter.setPen(QPen(QColor(220, 220, 230, int(alpha * 255.0)), 1.0));
    }

    for (const Triangle& tri : mesh.triangles) {
        Vertex vertices[3];
        bool behindCamera = false;
        for (int c = 0; c < 3; ++c) {
            const QVector3D& local = mesh.positions.at(tri.position[c]);
            vertices[c].world = modelMatrix.map(local);
            vertices[c].clip = mvp * QVector4D(local, 1.0f);
            if (vertices[c].clip.w() <= 0.0f) {
                behindCamera = true;
            }
            const int ni = tri.normal[c];
            if (ni >= 0 && ni < mesh.normals.size()) {
                vertices[c].normal = normalMatrix.mapVector(mesh.normals.at(ni)).normalized();
            }
        }
        // Anything crossing the eye plane is dropped rather than clipped: a
        // near-plane clip would mean splitting triangles, and for a preview the
        // model is in front of the camera anyway.
        if (behindCamera) {
            continue;
        }

        ScreenVertex screen[3];
        for (int c = 0; c < 3; ++c) {
            const QVector4D& clip = vertices[c].clip;
            const double invW = 1.0 / double(clip.w());
            const double ndcX = double(clip.x()) * invW;
            const double ndcY = double(clip.y()) * invW;
            const double ndcZ = double(clip.z()) * invW;
            screen[c].x = (ndcX * 0.5 + 0.5) * width;
            screen[c].y = (1.0 - (ndcY * 0.5 + 0.5)) * height;
            screen[c].invW = invW;
            screen[c].depth = ndcZ;
            screen[c].world = vertices[c].world;
            screen[c].normal = vertices[c].normal;
        }

        if (mode == ShadingMode::Wireframe) {
            for (int c = 0; c < 3; ++c) {
                const ScreenVertex& a = screen[c];
                const ScreenVertex& b = screen[(c + 1) % 3];
                wirePainter.drawLine(QPointF(a.x, a.y), QPointF(b.x, b.y));
            }
            continue;
        }

        // Face normal, used when the file carried none for this corner.
        QVector3D faceNormal = QVector3D::crossProduct(screen[1].world - screen[0].world,
                                                       screen[2].world - screen[0].world);
        if (!faceNormal.isNull()) {
            faceNormal.normalize();
        }

        const double area = edgeFunction(screen[0].x, screen[0].y, screen[1].x, screen[1].y,
                                         screen[2].x, screen[2].y);
        if (qFuzzyIsNull(area)) {
            continue;
        }
        const double invArea = 1.0 / area;

        int minX = int(std::floor(qMin(screen[0].x, qMin(screen[1].x, screen[2].x))));
        int maxX = int(std::ceil(qMax(screen[0].x, qMax(screen[1].x, screen[2].x))));
        int minY = int(std::floor(qMin(screen[0].y, qMin(screen[1].y, screen[2].y))));
        int maxY = int(std::ceil(qMax(screen[0].y, qMax(screen[1].y, screen[2].y))));
        minX = qMax(0, minX);
        minY = qMax(0, minY);
        maxX = qMin(width - 1, maxX);
        maxY = qMin(height - 1, maxY);
        if (minX > maxX || minY > maxY) {
            continue;
        }

        const Material& material = materialFor(mesh, tri.material, defaultMaterial);
        const double surfaceAlpha = alpha * qBound(0.0, material.opacity, 1.0);
        if (surfaceAlpha <= 0.0) {
            continue;
        }

        for (int y = minY; y <= maxY; ++y) {
            uchar* scanline = image.scanLine(y);
            for (int x = minX; x <= maxX; ++x) {
                const double px = x + 0.5;
                const double py = y + 0.5;
                double w0 = edgeFunction(screen[1].x, screen[1].y, screen[2].x, screen[2].y, px, py)
                    * invArea;
                double w1 = edgeFunction(screen[2].x, screen[2].y, screen[0].x, screen[0].y, px, py)
                    * invArea;
                double w2 = edgeFunction(screen[0].x, screen[0].y, screen[1].x, screen[1].y, px, py)
                    * invArea;
                if (w0 < 0.0 || w1 < 0.0 || w2 < 0.0) {
                    continue;
                }

                const double depth =
                    w0 * screen[0].depth + w1 * screen[1].depth + w2 * screen[2].depth;
                double& stored = depthBuffer[size_t(y) * size_t(width) + size_t(x)];
                if (depth >= stored) {
                    continue;
                }
                stored = depth;

                // Perspective-correct weights for everything interpolated in
                // world space; the depth above is already in screen space.
                const double invW = w0 * screen[0].invW + w1 * screen[1].invW + w2 * screen[2].invW;
                QVector3D normal = faceNormal;
                QVector3D world;
                if (invW > 0.0) {
                    const double p0 = w0 * screen[0].invW / invW;
                    const double p1 = w1 * screen[1].invW / invW;
                    const double p2 = w2 * screen[2].invW / invW;
                    world = screen[0].world * float(p0) + screen[1].world * float(p1)
                        + screen[2].world * float(p2);
                    const QVector3D interpolated = screen[0].normal * float(p0)
                        + screen[1].normal * float(p1) + screen[2].normal * float(p2);
                    if (!interpolated.isNull()) {
                        normal = interpolated;
                    }
                }

                const QColor lit = shade(material, normal, world, camera.position, light);
                uchar* pixel = scanline + size_t(x) * 4;
                pixel[0] = uchar(lit.red());
                pixel[1] = uchar(lit.green());
                pixel[2] = uchar(lit.blue());
                pixel[3] = uchar(qBound(0, int(surfaceAlpha * 255.0 + 0.5), 255));
            }
        }
    }

    if (wirePainter.isActive()) {
        wirePainter.end();
    }
    return image;
}

} // namespace model3d
} // namespace openvegas
