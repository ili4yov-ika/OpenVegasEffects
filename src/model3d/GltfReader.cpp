#include "model3d/GltfReader.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMatrix4x4>
#include <QQuaternion>
#include <QSet>
#include <QUrl>
#include <QtEndian>

#include <cmath>
#include <cstring>

namespace openvegas {
namespace model3d {

namespace {

constexpr quint32 kGlbMagic = 0x46546C67;   // "glTF"
constexpr quint32 kJsonChunk = 0x4E4F534A;  // "JSON"
constexpr quint32 kBinChunk = 0x004E4942;   // "BIN\0"

QString failure(const QString& filePath, const QString& why)
{
    return QCoreApplication::translate("Model3D", "The glTF model %1 could not be read: %2")
        .arg(QFileInfo(filePath).fileName(), why);
}

// A buffer or image named by a URI: a data: URI or a file beside the model.
QByteArray readUri(const QString& uri, const QString& baseDir)
{
    if (uri.startsWith(QLatin1String("data:"))) {
        const int comma = uri.indexOf(QLatin1Char(','));
        if (comma < 0) return {};
        const QByteArray payload = uri.mid(comma + 1).toLatin1();
        return uri.left(comma).endsWith(QLatin1String(";base64"))
            ? QByteArray::fromBase64(payload) : QByteArray::fromPercentEncoding(payload);
    }
    QFile file(QDir(baseDir).filePath(QUrl::fromPercentEncoding(uri.toUtf8())));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

class Document
{
public:
    QJsonObject root;
    QVector<QByteArray> buffers;
    QString baseDir;

    QJsonObject at(const char* list, int index) const
    {
        const QJsonArray items = root.value(QLatin1String(list)).toArray();
        return index >= 0 && index < items.size() ? items.at(index).toObject() : QJsonObject();
    }

    // The bytes of a bufferView, with its stride (0: tightly packed).
    bool view(int index, QByteArray* bytes, int* stride) const
    {
        const QJsonObject view = at("bufferViews", index);
        const int buffer = view.value(QLatin1String("buffer")).toInt(-1);
        if (buffer < 0 || buffer >= buffers.size()) return false;
        const qint64 offset = qint64(view.value(QLatin1String("byteOffset")).toDouble(0));
        const qint64 length = qint64(view.value(QLatin1String("byteLength")).toDouble(0));
        if (offset < 0 || length < 0 || offset + length > buffers.at(buffer).size()) return false;
        *bytes = buffers.at(buffer).mid(offset, length);
        *stride = view.value(QLatin1String("byteStride")).toInt(0);
        return true;
    }

    // An accessor's elements as floats, `components` each (normalized
    // integers become 0..1); empty when it cannot be read.
    QVector<float> floats(int index, int components) const
    {
        const QJsonObject accessor = at("accessors", index);
        const int count = accessor.value(QLatin1String("count")).toInt(0);
        const int type = accessor.value(QLatin1String("componentType")).toInt(0);
        const bool normalized = accessor.value(QLatin1String("normalized")).toBool(false);
        QVector<float> out(count * components, 0.0f);
        if (!accessor.contains(QLatin1String("bufferView"))) return out;   // all zeros
        QByteArray bytes;
        int stride = 0;
        if (!view(accessor.value(QLatin1String("bufferView")).toInt(), &bytes, &stride)) return {};
        const int size = type == 5126 || type == 5125 ? 4 : type == 5123 || type == 5122 ? 2 : 1;
        const qint64 offset = qint64(accessor.value(QLatin1String("byteOffset")).toDouble(0));
        if (stride <= 0) stride = size * components;
        for (int i = 0; i < count; ++i) {
            for (int c = 0; c < components; ++c) {
                const qint64 at = offset + qint64(i) * stride + qint64(c) * size;
                if (at + size > bytes.size()) return {};
                const uchar* p = reinterpret_cast<const uchar*>(bytes.constData()) + at;
                float value = 0.0f;
                switch (type) {
                case 5126: { quint32 bits = qFromLittleEndian<quint32>(p); std::memcpy(&value, &bits, 4); break; }
                case 5125: value = float(qFromLittleEndian<quint32>(p)); break;
                case 5123: value = float(qFromLittleEndian<quint16>(p)) / (normalized ? 65535.0f : 1.0f); break;
                case 5122: value = float(qFromLittleEndian<qint16>(p)) / (normalized ? 32767.0f : 1.0f); break;
                case 5121: value = float(*p) / (normalized ? 255.0f : 1.0f); break;
                case 5120: value = float(qint8(*p)) / (normalized ? 127.0f : 1.0f); break;
                default: return {};
                }
                out[i * components + c] = value;
            }
        }
        return out;
    }

    QImage image(int textureIndex) const
    {
        const QJsonObject texture = at("textures", textureIndex);
        const QJsonObject source = at("images", texture.value(QLatin1String("source")).toInt(-1));
        QByteArray bytes;
        if (source.contains(QLatin1String("uri"))) {
            bytes = readUri(source.value(QLatin1String("uri")).toString(), baseDir);
        } else if (source.contains(QLatin1String("bufferView"))) {
            int stride = 0;
            view(source.value(QLatin1String("bufferView")).toInt(), &bytes, &stride);
        }
        QImage image = QImage::fromData(bytes);
        return image.isNull() ? image : image.convertToFormat(QImage::Format_RGBA8888);
    }
};

QColor colorOf(const QJsonArray& factor, const QColor& fallback)
{
    if (factor.size() < 3) return fallback;
    return QColor::fromRgbF(float(qBound(0.0, factor.at(0).toDouble(), 1.0)),
                            float(qBound(0.0, factor.at(1).toDouble(), 1.0)),
                            float(qBound(0.0, factor.at(2).toDouble(), 1.0)));
}

// A linear glTF factor as the reference's importer stores it: in sRGB
// (FUN_1800af2a0: 12.92 c up to 0.0031308, else 1.055 c^(1/2.4) - 0.055).
double toSrgb(double linear)
{
    const double c = qBound(0.0, linear, 1.0);
    return c > 0.0031308 ? 1.055 * std::pow(c, 0.41666) - 0.055 : c * 12.92;
}

QColor srgbColorOf(const QJsonArray& factor, const QColor& fallback)
{
    if (factor.size() < 3) return fallback;
    QColor color = QColor::fromRgbF(float(toSrgb(factor.at(0).toDouble())), float(toSrgb(factor.at(1).toDouble())),
                                    float(toSrgb(factor.at(2).toDouble())));
    if (factor.size() >= 4) color.setAlphaF(float(qBound(0.0, factor.at(3).toDouble(1.0), 1.0)));
    return color;
}

// A glTF material as the reference's GLTFModel3DImporter makes its
// Model3DMaterial (Project.dll 0x1800af2a0): Metallic-Roughness - or
// Specular-Glossiness under KHR_materials_pbrSpecularGlossiness - with the
// colours in sRGB, a white specular colour, the factors as they are,
// emissive, alpha mode and cut off, occlusion strength and clear coat.
Material materialOf(const Document& document, const QJsonObject& source, int index)
{
    Material material;
    material.name = source.value(QLatin1String("name")).toString(QStringLiteral("Material %1").arg(index));
    material.illuminationModel = IlluminationModel::MetallicRoughness;
    material.specularColor = QColor(Qt::white);
    material.ambientColor = QColor(Qt::black);
    const QJsonObject pbr = source.value(QLatin1String("pbrMetallicRoughness")).toObject();
    const QJsonArray base = pbr.value(QLatin1String("baseColorFactor")).toArray();
    material.diffuseColor = srgbColorOf(base, QColor(Qt::white));
    if (base.size() >= 4) material.opacity = qBound(0.0, base.at(3).toDouble(1.0), 1.0);
    material.diffuseColor.setAlphaF(1.0f);
    material.metallic = qMax(0.0, pbr.value(QLatin1String("metallicFactor")).toDouble(1.0));
    material.roughness = qMax(0.0, pbr.value(QLatin1String("roughnessFactor")).toDouble(1.0));
    material.emissiveColor = srgbColorOf(source.value(QLatin1String("emissiveFactor")).toArray(), QColor(Qt::black));
    const QString alphaMode = source.value(QLatin1String("alphaMode")).toString();
    material.alphaMode = alphaMode == QLatin1String("MASK") ? AlphaMode::Mask
                       : alphaMode == QLatin1String("BLEND") ? AlphaMode::Blend : AlphaMode::Opaque;
    material.alphaCutOff = source.value(QLatin1String("alphaCutoff")).toDouble(0.5);
    const QJsonObject extensions = source.value(QLatin1String("extensions")).toObject();
    const QJsonObject clearCoat = extensions.value(QLatin1String("KHR_materials_clearcoat")).toObject();
    material.clearCoat = clearCoat.value(QLatin1String("clearcoatFactor")).toDouble(0.0);
    material.clearCoatRoughness = clearCoat.value(QLatin1String("clearcoatRoughnessFactor")).toDouble(0.0);
    const auto texture = [&](const QJsonObject& info) {
        return info.contains(QLatin1String("index")) ? document.image(info.value(QLatin1String("index")).toInt())
                                                     : QImage();
    };
    const QJsonObject occlusion = source.value(QLatin1String("occlusionTexture")).toObject();
    material.occlusionStrength = occlusion.value(QLatin1String("strength")).toDouble(1.0);
    material.diffuseImage = texture(pbr.value(QLatin1String("baseColorTexture")).toObject());
    const QJsonObject specularGlossiness =
        extensions.value(QLatin1String("KHR_materials_pbrSpecularGlossiness")).toObject();
    if (extensions.contains(QLatin1String("KHR_materials_pbrSpecularGlossiness"))) {
        material.illuminationModel = IlluminationModel::SpecularGlossiness;
        const QJsonArray diffuse = specularGlossiness.value(QLatin1String("diffuseFactor")).toArray();
        material.diffuseColor = srgbColorOf(diffuse, QColor(Qt::white));
        if (diffuse.size() >= 4) material.opacity = qBound(0.0, diffuse.at(3).toDouble(1.0), 1.0);
        material.diffuseColor.setAlphaF(1.0f);
        material.specularColor = srgbColorOf(specularGlossiness.value(QLatin1String("specularFactor")).toArray(),
                                             QColor(Qt::white));
        material.glossiness = specularGlossiness.value(QLatin1String("glossinessFactor")).toDouble(1.0);
        const QImage diffuseImage = texture(specularGlossiness.value(QLatin1String("diffuseTexture")).toObject());
        if (!diffuseImage.isNull()) material.diffuseImage = diffuseImage;
        material.specularGlossinessImage =
            texture(specularGlossiness.value(QLatin1String("specularGlossinessTexture")).toObject());
    }
    // glTF packs metallic in blue and roughness in green. Keep the image in
    // the metallic slot; the rasterizer samples its channels accordingly.
    material.metallicImage = texture(pbr.value(QLatin1String("metallicRoughnessTexture")).toObject());
    if (!material.metallicImage.isNull()) {
        material.roughnessImage = material.metallicImage;
        material.metallicRoughnessPacked = true;
    }
    material.normalImage = texture(source.value(QLatin1String("normalTexture")).toObject());
    material.emissiveImage = texture(source.value(QLatin1String("emissiveTexture")).toObject());
    material.occlusionImage = texture(source.value(QLatin1String("occlusionTexture")).toObject());
    return material;
}

QMatrix4x4 localMatrix(const QJsonObject& node)
{
    QMatrix4x4 matrix;
    const QJsonArray values = node.value(QLatin1String("matrix")).toArray();
    if (values.size() == 16) {
        float m[16];
        for (int i = 0; i < 16; ++i) m[i] = float(values.at(i).toDouble());
        return QMatrix4x4(m).transposed();   // glTF stores columns
    }
    const QJsonArray t = node.value(QLatin1String("translation")).toArray();
    if (t.size() == 3) matrix.translate(float(t.at(0).toDouble()), float(t.at(1).toDouble()), float(t.at(2).toDouble()));
    const QJsonArray r = node.value(QLatin1String("rotation")).toArray();
    if (r.size() == 4)
        matrix.rotate(QQuaternion(float(r.at(3).toDouble()), float(r.at(0).toDouble()),
                                  float(r.at(1).toDouble()), float(r.at(2).toDouble())));
    const QJsonArray s = node.value(QLatin1String("scale")).toArray();
    if (s.size() == 3) matrix.scale(float(s.at(0).toDouble()), float(s.at(1).toDouble()), float(s.at(2).toDouble()));
    return matrix;
}

// A primitive's triangles, placed by `world` - or, given a skin and its
// joints' matrices (joint world x inverse bind) at rest, each point by its
// weighted joints, and kept in the skin with its place in the mesh.
void addPrimitive(const Document& document, const QJsonObject& primitive, const QMatrix4x4& world,
                  int node, Mesh* mesh, Skin* skin = nullptr, const QVector<QMatrix4x4>& joints = {},
                  int morph = -1)
{
    if (primitive.value(QLatin1String("mode")).toInt(4) != 4) return;   // triangles only
    const QJsonObject attributes = primitive.value(QLatin1String("attributes")).toObject();
    if (!attributes.contains(QLatin1String("POSITION"))) return;
    const QVector<float> positions = document.floats(attributes.value(QLatin1String("POSITION")).toInt(), 3);
    if (positions.isEmpty()) return;
    const int vertexCount = positions.size() / 3;
    const QVector<float> normals = attributes.contains(QLatin1String("NORMAL"))
        ? document.floats(attributes.value(QLatin1String("NORMAL")).toInt(), 3) : QVector<float>();
    const QVector<float> uvs = attributes.contains(QLatin1String("TEXCOORD_0"))
        ? document.floats(attributes.value(QLatin1String("TEXCOORD_0")).toInt(), 2) : QVector<float>();
    const bool hasNormals = normals.size() == vertexCount * 3;
    const bool hasUvs = uvs.size() == vertexCount * 2;
    QVector<float> jointIds, weights;
    if (skin) {
        jointIds = document.floats(attributes.value(QLatin1String("JOINTS_0")).toInt(-1), 4);
        weights = document.floats(attributes.value(QLatin1String("WEIGHTS_0")).toInt(-1), 4);
        if (jointIds.size() != vertexCount * 4 || weights.size() != vertexCount * 4) skin = nullptr;
    }
    // Morph targets: each one's moves of the points and normals. A mesh
    // that is not skinned is stored at its default weights.
    struct TargetData { QVector<float> positions, normals; };
    QVector<TargetData> targets;
    if (morph >= 0 && morph < mesh->morphs.size()) {
        for (const QJsonValue target : primitive.value(QLatin1String("targets")).toArray()) {
            const QJsonObject targetAttributes = target.toObject();
            TargetData data;
            if (targetAttributes.contains(QLatin1String("POSITION")))
                data.positions = document.floats(targetAttributes.value(QLatin1String("POSITION")).toInt(), 3);
            if (targetAttributes.contains(QLatin1String("NORMAL")))
                data.normals = document.floats(targetAttributes.value(QLatin1String("NORMAL")).toInt(), 3);
            if (data.positions.size() != vertexCount * 3) data.positions.clear();
            if (data.normals.size() != vertexCount * 3) data.normals.clear();
            targets.append(data);
        }
        if (mesh->morphs[morph].targets.size() < targets.size()) mesh->morphs[morph].targets.resize(targets.size());
    }
    const QVector<float> restWeights = morph >= 0 && morph < mesh->morphs.size() && !skin
        ? mesh->morphs.at(morph).defaultWeights : QVector<float>();
    QVector<QMatrix4x4> jointNormals;
    for (const QMatrix4x4& joint : joints) {
        bool jointInvertible = false;
        const QMatrix4x4 inverse = joint.inverted(&jointInvertible);
        jointNormals.append(jointInvertible ? inverse.transposed() : joint);
    }

    const int positionBase = mesh->positions.size();
    const int normalBase = mesh->normals.size();
    const int uvBase = mesh->uvs.size();
    bool invertible = false;
    const QMatrix4x4 normalMatrix = world.inverted(&invertible).transposed();
    for (int i = 0; i < vertexCount; ++i) {
        QVector3D point(positions[3 * i], positions[3 * i + 1], positions[3 * i + 2]);
        QVector3D normal = hasNormals ? QVector3D(normals[3 * i], normals[3 * i + 1], normals[3 * i + 2])
                                      : QVector3D();
        const QVector3D fileNormal = normal;
        for (int t = 0; t < targets.size(); ++t) {
            const TargetData& data = targets.at(t);
            MorphTarget& target = mesh->morphs[morph].targets[t];
            const float weight = restWeights.value(t, 0.0f);
            if (!data.positions.isEmpty()) {
                const QVector3D delta(data.positions[3 * i], data.positions[3 * i + 1], data.positions[3 * i + 2]);
                if (!delta.isNull()) {
                    target.positions.append(positionBase + i);
                    target.positionDeltas.append(delta);
                    point += weight * delta;
                }
            }
            if (!data.normals.isEmpty() && hasNormals) {
                const QVector3D delta(data.normals[3 * i], data.normals[3 * i + 1], data.normals[3 * i + 2]);
                if (!delta.isNull()) {
                    target.normals.append(normalBase + i);
                    target.normalDeltas.append(delta);
                    if (!skin) mesh->morphs[morph].baseNormals.insert(normalBase + i, fileNormal);
                    normal += weight * delta;
                }
            }
        }
        if (skin) {
            SkinInfluence influence;
            float total = 0.0f;
            for (int k = 0; k < 4; ++k) {
                const int joint = int(jointIds[4 * i + k]);
                const float weight = weights[4 * i + k];
                if (weight <= 0.0f || joint < 0 || joint >= joints.size()) continue;
                influence.joints[k] = joint;
                influence.weights[k] = weight;
                total += weight;
            }
            if (total > 0.0f) for (float& w : influence.weights) w /= total;
            QVector3D placed, turned;
            for (int k = 0; k < 4; ++k) {
                if (influence.joints[k] < 0) continue;
                placed += influence.weights[k] * joints.at(influence.joints[k]).map(point);
                turned += influence.weights[k] * jointNormals.at(influence.joints[k]).mapVector(normal);
            }
            skin->positions.append(mesh->positions.size());
            skin->bindPositions.append(point);
            skin->positionInfluences.append(influence);
            mesh->positions.append(total > 0.0f ? placed : point);
            if (hasNormals) {
                skin->normals.append(mesh->normals.size());
                skin->bindNormals.append(normal);
                skin->normalInfluences.append(influence);
                mesh->normals.append((total > 0.0f ? turned : normal).normalized());
            }
        } else {
            mesh->positions.append(world.map(point));
            if (hasNormals) mesh->normals.append((invertible ? normalMatrix : world).mapVector(normal).normalized());
        }
        // glTF's v runs down the image; the port's up, as OBJ's does.
        if (hasUvs) mesh->uvs.append(QVector2D(uvs[2 * i], 1.0f - uvs[2 * i + 1]));
    }

    QVector<quint32> indices;
    if (primitive.contains(QLatin1String("indices"))) {
        const QVector<float> read = document.floats(primitive.value(QLatin1String("indices")).toInt(), 1);
        indices.reserve(read.size());
        for (float value : read) indices.append(quint32(value));
    } else {
        for (int i = 0; i < vertexCount; ++i) indices.append(quint32(i));
    }
    // A matrix that mirrors turns the winding over.
    const bool mirrored = !skin && world.determinant() < 0.0;
    const int material = primitive.value(QLatin1String("material")).toInt(-1);
    for (int t = 0; t + 2 < indices.size(); t += 3) {
        Triangle triangle;
        for (int c = 0; c < 3; ++c) {
            const quint32 index = indices.at(t + (mirrored ? 2 - c : c));
            if (index >= quint32(vertexCount)) return;
            triangle.position[c] = positionBase + int(index);
            if (hasNormals) triangle.normal[c] = normalBase + int(index);
            if (hasUvs) triangle.uv[c] = uvBase + int(index);
        }
        triangle.material = material >= 0 && material < mesh->materials.size() ? material : -1;
        triangle.node = node;
        mesh->triangles.append(triangle);
    }
}

// A skinned mesh's primitive, read once every node's place is known.
struct PendingSkinned
{
    QJsonObject primitive;
    int node = -1;
    int skin = -1;   // glTF skins[] index
    int morph = -1;  // Mesh::morphs
};

void addNode(const Document& document, int index, const QMatrix4x4& parentWorld, int parentNode,
             QSet<int>* visited, Mesh* mesh, QHash<int, int>* nodeOf, QVector<PendingSkinned>* pending)
{
    if (visited->contains(index)) return;   // a cycle in a broken file
    visited->insert(index);
    const QJsonObject node = document.at("nodes", index);
    const QMatrix4x4 local = localMatrix(node);
    const QMatrix4x4 world = parentWorld * local;
    const QString name = node.value(QLatin1String("name")).toString(QStringLiteral("Node %1").arg(index));
    const int own = mesh->addNode(name, parentNode);
    nodeOf->insert(index, own);
    mesh->nodes[own].sourceNode = index;
    mesh->nodes[own].baseLocal = local;
    mesh->nodes[own].baseWorld = world;
    const QJsonArray translation = node.value(QLatin1String("translation")).toArray();
    if (translation.size() == 3)
        mesh->nodes[own].baseTranslation = QVector3D(float(translation.at(0).toDouble()),
                                                     float(translation.at(1).toDouble()),
                                                     float(translation.at(2).toDouble()));
    const QJsonArray rotation = node.value(QLatin1String("rotation")).toArray();
    mesh->nodes[own].baseRotation = rotation.size() == 4
        ? QQuaternion(float(rotation.at(3).toDouble()), float(rotation.at(0).toDouble()),
                      float(rotation.at(1).toDouble()), float(rotation.at(2).toDouble()))
        : QQuaternion();
    const QJsonArray scale = node.value(QLatin1String("scale")).toArray();
    if (scale.size() == 3)
        mesh->nodes[own].baseScale = QVector3D(float(scale.at(0).toDouble()),
                                               float(scale.at(1).toDouble()),
                                               float(scale.at(2).toDouble()));
    if (!mesh->nodeNames.contains(name)) mesh->nodeNames.append(name);
    if (node.contains(QLatin1String("mesh"))) {
        const QJsonObject source = document.at("meshes", node.value(QLatin1String("mesh")).toInt());
        const int skin = node.value(QLatin1String("skin")).toInt(-1);
        // Morph targets: the weights it rests at (the node's, else the
        // mesh's) and the targets' names.
        int morph = -1;
        for (const QJsonValue primitive : source.value(QLatin1String("primitives")).toArray()) {
            if (!primitive.toObject().value(QLatin1String("targets")).toArray().isEmpty()) {
                Morph entry;
                entry.node = own;
                entry.sourceNode = index;
                entry.placement = world;
                const QJsonArray weights = node.contains(QLatin1String("weights"))
                    ? node.value(QLatin1String("weights")).toArray() : source.value(QLatin1String("weights")).toArray();
                for (const QJsonValue weight : weights) entry.defaultWeights.append(float(weight.toDouble()));
                const QJsonArray names = source.value(QLatin1String("extras")).toObject()
                                             .value(QLatin1String("targetNames")).toArray();
                for (int t = 0; t < names.size(); ++t) {
                    if (entry.targets.size() <= t) entry.targets.resize(t + 1);
                    entry.targets[t].name = names.at(t).toString();
                }
                mesh->morphs.append(entry);
                morph = mesh->morphs.size() - 1;
                break;
            }
        }
        for (const QJsonValue primitive : source.value(QLatin1String("primitives")).toArray()) {
            const QJsonObject attributes = primitive.toObject().value(QLatin1String("attributes")).toObject();
            // A skinned mesh is placed by its joints (its node's own
            // transform does not count), so it waits for all of them.
            if (skin >= 0 && attributes.contains(QLatin1String("JOINTS_0"))
                && attributes.contains(QLatin1String("WEIGHTS_0"))) {
                pending->append({primitive.toObject(), own, skin, morph});
                mesh->nodes[own].skinned = true;
            } else {
                addPrimitive(document, primitive.toObject(), world, own, mesh, nullptr, {}, morph);
            }
        }
    }
    for (const QJsonValue child : node.value(QLatin1String("children")).toArray())
        addNode(document, child.toInt(), world, own, visited, mesh, nodeOf, pending);
}

// The skins the pending primitives use, and the primitives placed by them.
void addSkinned(const Document& document, const QVector<PendingSkinned>& pending,
                const QHash<int, int>& nodeOf, Mesh* mesh)
{
    QHash<int, int> skinOf;   // glTF skin -> Mesh::skins
    for (const PendingSkinned& entry : pending) {
        if (!skinOf.contains(entry.skin)) {
            const QJsonObject source = document.at("skins", entry.skin);
            Skin skin;
            const QJsonArray joints = source.value(QLatin1String("joints")).toArray();
            const QVector<float> inverse = source.contains(QLatin1String("inverseBindMatrices"))
                ? document.floats(source.value(QLatin1String("inverseBindMatrices")).toInt(), 16) : QVector<float>();
            for (int j = 0; j < joints.size(); ++j) {
                skin.joints.append(nodeOf.value(joints.at(j).toInt(), -1));
                QMatrix4x4 bind;
                if (inverse.size() >= (j + 1) * 16) bind = QMatrix4x4(inverse.constData() + 16 * j).transposed();
                skin.inverseBind.append(bind);
            }
            mesh->skins.append(skin);
            skinOf.insert(entry.skin, mesh->skins.size() - 1);
        }
        Skin& skin = mesh->skins[skinOf.value(entry.skin)];
        QVector<QMatrix4x4> joints;
        for (int j = 0; j < skin.joints.size(); ++j) {
            const int node = skin.joints.at(j);
            joints.append((node >= 0 && node < mesh->nodes.size() ? mesh->nodes.at(node).baseWorld : QMatrix4x4())
                          * skin.inverseBind.at(j));
        }
        addPrimitive(document, entry.primitive, QMatrix4x4(), entry.node, mesh, &skin, joints, entry.morph);
    }
}

} // namespace

LoadResult loadGltf(const QString& filePath)
{
    LoadResult result;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        result.message = failure(filePath, QCoreApplication::translate("Model3D", "the file could not be opened"));
        return result;
    }
    const QByteArray data = file.readAll();
    Document document;
    document.baseDir = QFileInfo(filePath).absolutePath();
    QByteArray json;
    QByteArray binary;
    if (data.size() >= 12 && qFromLittleEndian<quint32>(data.constData()) == kGlbMagic) {
        // GLB: a 12-byte header, then chunks of {length, type, data}.
        qsizetype at = 12;
        while (at + 8 <= data.size()) {
            const quint32 length = qFromLittleEndian<quint32>(data.constData() + at);
            const quint32 type = qFromLittleEndian<quint32>(data.constData() + at + 4);
            if (at + 8 + qsizetype(length) > data.size()) break;
            if (type == kJsonChunk && json.isEmpty()) json = data.mid(at + 8, length);
            else if (type == kBinChunk && binary.isEmpty()) binary = data.mid(at + 8, length);
            at += 8 + qsizetype(length);
        }
    } else {
        json = data;
    }
    QJsonParseError error;
    const QJsonDocument parsedJson = QJsonDocument::fromJson(json, &error);
    if (!parsedJson.isObject()) {
        result.message = failure(filePath, error.errorString());
        return result;
    }
    document.root = parsedJson.object();
    const QString version = document.root.value(QLatin1String("asset")).toObject()
                                .value(QLatin1String("version")).toString();
    if (!version.startsWith(QLatin1Char('2'))) {
        result.message = failure(filePath, QCoreApplication::translate("Model3D", "glTF %1 is not supported (2.0 is)")
                                               .arg(version.isEmpty() ? QStringLiteral("?") : version));
        return result;
    }
    const QJsonArray buffers = document.root.value(QLatin1String("buffers")).toArray();
    for (int i = 0; i < buffers.size(); ++i) {
        const QJsonObject buffer = buffers.at(i).toObject();
        document.buffers.append(buffer.contains(QLatin1String("uri"))
                                    ? readUri(buffer.value(QLatin1String("uri")).toString(), document.baseDir)
                                    : (i == 0 ? binary : QByteArray()));
    }

    Mesh& mesh = result.mesh;
    const QJsonArray materials = document.root.value(QLatin1String("materials")).toArray();
    for (int i = 0; i < materials.size(); ++i)
        mesh.materials.append(materialOf(document, materials.at(i).toObject(), i));

    // The scene's roots; without scenes, every node no other names as a child.
    QVector<int> roots;
    const QJsonArray scenes = document.root.value(QLatin1String("scenes")).toArray();
    const int scene = document.root.value(QLatin1String("scene")).toInt(0);
    if (scene >= 0 && scene < scenes.size()) {
        for (const QJsonValue node : scenes.at(scene).toObject().value(QLatin1String("nodes")).toArray())
            roots.append(node.toInt());
    } else {
        const QJsonArray nodes = document.root.value(QLatin1String("nodes")).toArray();
        QSet<int> children;
        for (const QJsonValue node : nodes)
            for (const QJsonValue child : node.toObject().value(QLatin1String("children")).toArray())
                children.insert(child.toInt());
        for (int i = 0; i < nodes.size(); ++i)
            if (!children.contains(i)) roots.append(i);
    }
    QSet<int> visited;
    QHash<int, int> nodeOf;
    QVector<PendingSkinned> pending;
    for (int root : std::as_const(roots))
        addNode(document, root, QMatrix4x4(), -1, &visited, &mesh, &nodeOf, &pending);
    addSkinned(document, pending, nodeOf, &mesh);

    // The animations, each its own (the reference makes an animation asset
    // of each, by AnimationIndex). Channels stay on the file's nodes: the
    // geometry is already placed, so Mesh::animatedNodeMatrices moves it by
    // a node's animated place over its place in the file.
    const QJsonArray animations = document.root.value(QLatin1String("animations")).toArray();
    for (int a = 0; a < animations.size(); ++a) {
        const QJsonObject animation = animations.at(a).toObject();
        ModelAnimation clip;
        clip.name = animation.value(QLatin1String("name")).toString(QStringLiteral("Animation %1").arg(a + 1));
        const QJsonArray samplers = animation.value(QLatin1String("samplers")).toArray();
        for (const QJsonValue& channelValue : animation.value(QLatin1String("channels")).toArray()) {
            const QJsonObject channel = channelValue.toObject();
            const int samplerIndex = channel.value(QLatin1String("sampler")).toInt(-1);
            if (samplerIndex < 0 || samplerIndex >= samplers.size()) continue;
            const QJsonObject sampler = samplers.at(samplerIndex).toObject();
            const QJsonObject target = channel.value(QLatin1String("target")).toObject();
            AnimationChannel::Path path;
            const QString pathName = target.value(QLatin1String("path")).toString();
            if (pathName == QLatin1String("translation")) path = AnimationChannel::Path::Translation;
            else if (pathName == QLatin1String("rotation")) path = AnimationChannel::Path::Rotation;
            else if (pathName == QLatin1String("scale")) path = AnimationChannel::Path::Scale;
            else if (pathName == QLatin1String("weights")) path = AnimationChannel::Path::Weights;
            else continue;
            AnimationChannel parsed;
            parsed.sourceNode = target.value(QLatin1String("node")).toInt(-1);
            parsed.path = path;
            parsed.times = document.floats(sampler.value(QLatin1String("input")).toInt(-1), 1);
            const QString interpolation = sampler.value(QLatin1String("interpolation")).toString();
            parsed.interpolation = interpolation == QLatin1String("STEP")
                ? AnimationChannel::Interpolation::Step
                : interpolation == QLatin1String("CUBICSPLINE") ? AnimationChannel::Interpolation::CubicSpline
                                                                : AnimationChannel::Interpolation::Linear;
            const int perKey = parsed.interpolation == AnimationChannel::Interpolation::CubicSpline ? 3 : 1;
            if (path == AnimationChannel::Path::Weights) {
                // As many weights a key as the morph has targets.
                parsed.weights = document.floats(sampler.value(QLatin1String("output")).toInt(-1), 1);
                const int keys = parsed.times.size() * perKey;
                if (parsed.sourceNode < 0 || keys == 0 || parsed.weights.isEmpty()
                    || parsed.weights.size() % keys != 0) continue;
                parsed.weightCount = parsed.weights.size() / keys;
                clip.duration = qMax(clip.duration, double(parsed.times.last()));
                clip.channels.append(parsed);
                continue;
            }
            const int components = path == AnimationChannel::Path::Rotation ? 4 : 3;
            const QVector<float> values = document.floats(sampler.value(QLatin1String("output")).toInt(-1), components);
            if (parsed.sourceNode < 0 || parsed.times.isEmpty()
                || values.size() != parsed.times.size() * components * perKey) continue;
            const int count = parsed.times.size() * perKey;
            parsed.values.reserve(count);
            for (int i = 0; i < count; ++i)
                parsed.values.append(QVector4D(values[components * i], values[components * i + 1],
                                               values[components * i + 2],
                                               components == 4 ? values[components * i + 3] : 0.0f));
            clip.duration = qMax(clip.duration, double(parsed.times.last()));
            clip.channels.append(parsed);
        }
        if (!clip.channels.isEmpty()) mesh.animations.append(clip);
    }

    if (mesh.isEmpty()) {
        result.message = failure(filePath, QCoreApplication::translate("Model3D", "it has no triangles"));
        return result;
    }
    result.ok = true;
    return result;
}

} // namespace model3d
} // namespace openvegas
