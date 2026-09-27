#pragma once

#include <QColor>
#include <QString>
#include <QVector>
#include <QVector3D>
#include <QVector2D>

namespace openvegas {
namespace model3d {

// Surface properties of one material in a model.
//
// The field set is the reference's Model3DMaterial, whose editor lists (in its
// own order) Diffuse Color, Diffuse Reflectivity, Specular Color, Specular
// Reflectivity, Ambient Color, Emissive Color, Shininess, Opacity, Index of
// Refraction and the map slots. Only what the rasteriser can honour is carried
// here; the map slots are kept as paths so the importer can report what a file
// referenced even when nothing samples them yet.
struct Material
{
    QString name;

    QColor diffuseColor = QColor(204, 204, 204);
    QColor specularColor = QColor(255, 255, 255);
    QColor ambientColor = QColor(38, 38, 38);
    QColor emissiveColor = QColor(0, 0, 0);

    double diffuseReflectivity = 1.0;
    double specularReflectivity = 1.0;
    double shininess = 32.0;      // Phong exponent, reference "Shininess"
    double opacity = 1.0;
    double indexOfRefraction = 1.0;

    // Texture paths as the file named them, resolved next to the model.
    QString diffuseMap;
    QString specularMap;
    QString normalMap;
    QString emissiveMap;
    QString occlusionMap;
};

// One triangle. Indices point into the mesh's vertex arrays; -1 in `normal` or
// `uv` means the file gave none for that corner.
struct Triangle
{
    int position[3] = {0, 0, 0};
    int normal[3] = {-1, -1, -1};
    int uv[3] = {-1, -1, -1};
    int material = -1;
};

// A loaded model: one flat vertex pool with triangles indexing into it.
//
// The reference keeps a scene graph of Model3DTransformNodes under the asset
// and instances them per layer. This is the flattened form of that - a single
// mesh in the model's own space - which is what the rasteriser draws. The node
// names are kept so the hierarchy a file declared is not silently lost.
struct Mesh
{
    QVector<QVector3D> positions;
    QVector<QVector3D> normals;
    QVector<QVector2D> uvs;
    QVector<Triangle> triangles;
    QVector<Material> materials;
    // Names of the groups/objects the file declared, in file order. The
    // reference shows these under the layer's "Models" branch.
    QVector<QString> nodeNames;

    bool isEmpty() const { return triangles.isEmpty() || positions.isEmpty(); }
    int triangleCount() const { return triangles.size(); }

    // Axis-aligned bounds of the positions. Returns false for an empty mesh.
    bool bounds(QVector3D* minimum, QVector3D* maximum) const;
    // Longest edge of the bounding box, or 0 when empty. Used to fit a model
    // into the composition the way the reference's Auto Normalize does.
    double boundingSize() const;
    QVector3D centre() const;

    // Fills in per-vertex normals for triangles that came without any.
    // `smoothingAngleDegrees` follows the reference's auto-smoothing: corners
    // whose faces meet at a sharper angle than this are left faceted.
    void generateNormals(double smoothingAngleDegrees, bool unify);
};

} // namespace model3d
} // namespace openvegas
