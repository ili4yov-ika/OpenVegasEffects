#include "model3d/Mesh.h"

#include <QHash>
#include <QSet>
#include <QStringList>

#include <cmath>
#include <algorithm>

namespace openvegas {
namespace model3d {

void copyMaterialSettings(const Material& from, Material& to)
{
    to.diffuseColor = from.diffuseColor;
    to.specularColor = from.specularColor;
    to.ambientColor = from.ambientColor;
    to.emissiveColor = from.emissiveColor;
    to.diffuseReflectivity = from.diffuseReflectivity;
    to.specularReflectivity = from.specularReflectivity;
    to.shininess = from.shininess;
    to.opacity = from.opacity;
    to.indexOfRefraction = from.indexOfRefraction;
    to.refractionAmount = from.refractionAmount;
    to.reflectionMode = from.reflectionMode;
    to.illuminationModel = from.illuminationModel;
    to.metallic = from.metallic;
    to.roughness = from.roughness;
    to.cookTorranceRoughness = from.cookTorranceRoughness;
    to.cookTorranceSpecularReflectance = from.cookTorranceSpecularReflectance;
    to.cookTorranceFresnel = from.cookTorranceFresnel;
    to.glossiness = from.glossiness;
    to.clearCoat = from.clearCoat;
    to.clearCoatRoughness = from.clearCoatRoughness;
    to.alphaMode = from.alphaMode;
    to.alphaCutOff = from.alphaCutOff;
    to.occlusionStrength = from.occlusionStrength;
    to.bumpScale = from.bumpScale;
}

bool sameMaterialSettings(const Material& a, const Material& b)
{
    Material copy = b;
    copyMaterialSettings(a, copy);
    return copy.diffuseColor == b.diffuseColor && copy.specularColor == b.specularColor
        && copy.ambientColor == b.ambientColor && copy.emissiveColor == b.emissiveColor
        && copy.diffuseReflectivity == b.diffuseReflectivity && copy.specularReflectivity == b.specularReflectivity
        && copy.shininess == b.shininess && copy.opacity == b.opacity
        && copy.indexOfRefraction == b.indexOfRefraction && copy.refractionAmount == b.refractionAmount
        && copy.reflectionMode == b.reflectionMode
        && copy.illuminationModel == b.illuminationModel && copy.metallic == b.metallic
        && copy.roughness == b.roughness && copy.cookTorranceRoughness == b.cookTorranceRoughness
        && copy.cookTorranceSpecularReflectance == b.cookTorranceSpecularReflectance
        && copy.cookTorranceFresnel == b.cookTorranceFresnel && copy.glossiness == b.glossiness
        && copy.clearCoat == b.clearCoat && copy.clearCoatRoughness == b.clearCoatRoughness
        && copy.alphaMode == b.alphaMode && copy.alphaCutOff == b.alphaCutOff
        && copy.occlusionStrength == b.occlusionStrength && copy.bumpScale == b.bumpScale;
}

void applyMaterialSettings(Mesh& mesh, const QVector<Material>& edited)
{
    for (const Material& settings : edited)
        for (Material& material : mesh.materials)
            if (material.name == settings.name) copyMaterialSettings(settings, material);
}

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

int Mesh::addNode(const QString& name, int parent)
{
    for (int i = 0; i < nodes.size(); ++i) {
        if (nodes.at(i).name == name && nodes.at(i).parent == parent) return i;
    }
    ModelNode node;
    node.name = name;
    node.parent = parent;
    nodes.append(node);
    if (!nodeNames.contains(name)) nodeNames.append(name);
    return nodes.size() - 1;
}

QString Mesh::nodePath(int index) const
{
    QStringList names;
    for (int at = index; at >= 0 && at < nodes.size() && names.size() <= nodes.size();
         at = nodes.at(at).parent) {
        names.prepend(nodes.at(at).name);
    }
    return names.join(QLatin1Char('/'));
}

int Mesh::nodeByPath(const QString& path) const
{
    for (int i = 0; i < nodes.size(); ++i) {
        if (nodePath(i) == path) return i;
    }
    return -1;
}

QVector4D AnimationChannel::sample(double seconds) const
{
    const int keys = times.size();
    const bool cubic = interpolation == Interpolation::CubicSpline && values.size() == keys * 3;
    if (keys == 0 || (!cubic && values.size() != keys)) return QVector4D();
    const auto value = [&](int k) { return cubic ? values.at(3 * k + 1) : values.at(k); };
    if (seconds <= times.first()) return value(0);
    if (seconds >= times.last()) return value(keys - 1);
    const int b = int(std::upper_bound(times.cbegin(), times.cend(), float(seconds)) - times.cbegin());
    const int a = qMax(0, b - 1);
    const double span = double(times.at(b)) - double(times.at(a));
    if (interpolation == Interpolation::Step || span <= 0.0) return value(a);
    const float t = float((seconds - times.at(a)) / span);
    if (cubic) {
        // Hermite between the two keys, the tangents scaled by the gap
        // (glTF's cubic spline).
        const float t2 = t * t, t3 = t2 * t;
        const float dt = float(span);
        const QVector4D result = (2 * t3 - 3 * t2 + 1) * values.at(3 * a + 1)
            + (t3 - 2 * t2 + t) * dt * values.at(3 * a + 2)
            + (-2 * t3 + 3 * t2) * values.at(3 * b + 1)
            + (t3 - t2) * dt * values.at(3 * b);
        return path == Path::Rotation ? result.normalized() : result;
    }
    if (path == Path::Rotation) {
        const QVector4D& qa = values.at(a);
        const QVector4D& qb = values.at(b);
        const QQuaternion turned = QQuaternion::slerp(QQuaternion(qa.w(), qa.x(), qa.y(), qa.z()),
                                                      QQuaternion(qb.w(), qb.x(), qb.y(), qb.z()), t);
        return QVector4D(turned.x(), turned.y(), turned.z(), turned.scalar());
    }
    return values.at(a) * (1.0f - t) + values.at(b) * t;
}

QVector<QMatrix4x4> Mesh::animatedNodeMatrices(int animation, double seconds) const
{
    if (animation < 0 || animation >= animations.size()) return QVector<QMatrix4x4>(nodes.size());
    return animatedNodeMatrices(animations.at(animation), seconds);
}

QVector<float> AnimationChannel::sampleWeights(double seconds) const
{
    const int keys = times.size();
    const int n = weightCount;
    const bool cubic = interpolation == Interpolation::CubicSpline && weights.size() == keys * n * 3;
    if (keys == 0 || n <= 0 || (!cubic && weights.size() != keys * n)) return {};
    const auto at = [&](int key, int part, int k) {
        return cubic ? weights.at((3 * key + part) * n + k) : weights.at(key * n + k);
    };
    QVector<float> out(n);
    if (seconds <= times.first() || seconds >= times.last()) {
        const int key = seconds <= times.first() ? 0 : keys - 1;
        for (int k = 0; k < n; ++k) out[k] = at(key, 1, k);
        return out;
    }
    const int b = int(std::upper_bound(times.cbegin(), times.cend(), float(seconds)) - times.cbegin());
    const int a = qMax(0, b - 1);
    const double span = double(times.at(b)) - double(times.at(a));
    const float t = span > 0.0 ? float((seconds - times.at(a)) / span) : 0.0f;
    for (int k = 0; k < n; ++k) {
        if (interpolation == Interpolation::Step || span <= 0.0) {
            out[k] = at(a, 1, k);
        } else if (cubic) {
            const float t2 = t * t, t3 = t2 * t, dt = float(span);
            out[k] = (2 * t3 - 3 * t2 + 1) * at(a, 1, k) + (t3 - 2 * t2 + t) * dt * at(a, 2, k)
                     + (-2 * t3 + 3 * t2) * at(b, 1, k) + (t3 - t2) * dt * at(b, 0, k);
        } else {
            out[k] = at(a, 1, k) * (1.0f - t) + at(b, 1, k) * t;
        }
    }
    return out;
}

QVector<float> channelTargetWeights(const QVector<float>& fullWeights, float weight)
{
    QVector<float> out(fullWeights.size(), 0.0f);
    if (fullWeights.isEmpty() || !(weight > 0.0f)) return out;
    if (weight <= fullWeights.first()) {
        out[0] = fullWeights.first() > 0.0f ? weight / fullWeights.first() : 1.0f;
        return out;
    }
    for (int i = 0; i + 1 < fullWeights.size(); ++i) {
        const float from = fullWeights.at(i), to = fullWeights.at(i + 1);
        if (from < weight && weight < to) {
            const float t = (weight - from) / (to - from);
            out[i] = 1.0f - t;
            out[i + 1] = t;
            break;
        }
    }
    return out;
}

QVector<QVector<float>> Mesh::morphWeights(const ModelAnimation& clip, double seconds) const
{
    QVector<QVector<float>> result;
    const double time = qBound(0.0, seconds, clip.duration);
    for (const Morph& morph : morphs) {
        QVector<float> weights = morph.defaultWeights;
        weights.resize(morph.targets.size());
        for (const AnimationChannel& channel : clip.channels) {
            if (channel.path != AnimationChannel::Path::Weights || channel.sourceNode != morph.sourceNode)
                continue;
            const QVector<float> keyed = channel.sampleWeights(time);
            if (morph.channels.isEmpty()) {
                for (int k = 0; k < keyed.size() && k < weights.size(); ++k) weights[k] = keyed.at(k);
                continue;
            }
            // FBX: the channels' weights, spread over their shapes.
            for (int c = 0; c < morph.channels.size() && c < keyed.size(); ++c) {
                const Morph::Channel& stages = morph.channels.at(c);
                const QVector<float> spread = channelTargetWeights(stages.fullWeights, keyed.at(c));
                for (int s = 0; s < spread.size() && stages.first + s < weights.size(); ++s)
                    weights[stages.first + s] = spread.at(s);
            }
        }
        result.append(weights);
    }
    return result;
}

Mesh Mesh::deformed(const QVector<QMatrix4x4>& nodeWorlds, const QVector<QVector<float>>& weights) const
{
    Mesh out = *this;
    QHash<int, QVector3D> bindOffsets, bindNormalOffsets;
    bool invertible = false;
    const QMatrix4x4 importInverse = importTransform.inverted(&invertible);
    const QMatrix4x4 importNormal = invertible ? importInverse.transposed() : importTransform;
    QSet<int> turnedNormals;
    for (int m = 0; m < morphs.size(); ++m) {
        const Morph& morph = morphs.at(m);
        const bool skinnedMesh = morph.node >= 0 && morph.node < nodes.size() && nodes.at(morph.node).skinned;
        const QMatrix4x4& world = morph.placement;
        bool worldInvertible = false;
        const QMatrix4x4 worldNormal = world.inverted(&worldInvertible).transposed();
        const QVector<float> w = weights.value(m);
        bool moved = false;
        for (int t = 0; t < morph.targets.size() && !moved; ++t)
            moved = !qFuzzyCompare(1.0f + w.value(t, morph.defaultWeights.value(t)),
                                   1.0f + (skinnedMesh ? 0.0f : morph.defaultWeights.value(t)));
        if (!moved) continue;
        // The normals of a mesh that is not skinned, turned from the file's
        // by every target's whole weight.
        QHash<int, QVector3D> normalSums;
        for (int t = 0; t < morph.targets.size(); ++t) {
            // A skinned mesh is stored without its morphs; any other at
            // the weights it rests at.
            const float whole = w.value(t, morph.defaultWeights.value(t));
            const float weight = whole - (skinnedMesh ? 0.0f : morph.defaultWeights.value(t));
            const MorphTarget& target = morph.targets.at(t);
            if (!skinnedMesh) {
                for (int k = 0; k < target.normals.size(); ++k) {
                    const int index = target.normals.at(k);
                    if (!morph.baseNormals.contains(index)) continue;
                    auto sum = normalSums.find(index);
                    if (sum == normalSums.end()) sum = normalSums.insert(index, morph.baseNormals.value(index));
                    *sum += whole * target.normalDeltas.value(k);
                }
            }
            if (qFuzzyIsNull(weight)) continue;
            for (int k = 0; k < target.positions.size(); ++k) {
                const int index = target.positions.at(k);
                if (index < 0 || index >= out.positions.size()) continue;
                const QVector3D delta = weight * target.positionDeltas.value(k);
                if (skinnedMesh) bindOffsets[index] += delta;
                else out.positions[index] += importTransform.mapVector(world.mapVector(delta));
            }
            for (int k = 0; k < target.normals.size(); ++k) {
                const int index = target.normals.at(k);
                if (index < 0 || index >= out.normals.size()) continue;
                const QVector3D delta = weight * target.normalDeltas.value(k);
                if (skinnedMesh) {
                    bindNormalOffsets[index] += delta;
                } else if (!morph.baseNormals.contains(index)) {
                    out.normals[index] += importNormal.mapVector(
                        (worldInvertible ? worldNormal : world).mapVector(delta));
                    turnedNormals.insert(index);
                }
            }
        }
        for (auto sum = normalSums.cbegin(); sum != normalSums.cend(); ++sum) {
            if (sum.key() < 0 || sum.key() >= out.normals.size()) continue;
            out.normals[sum.key()] = importNormal.mapVector(
                (worldInvertible ? worldNormal : world).mapVector(sum.value())).normalized();
        }
    }
    for (int index : std::as_const(turnedNormals)) out.normals[index] = out.normals.at(index).normalized();
    if (skins.isEmpty()) return out;
    // The skins carry the points, the morphs' moves included.
    Mesh carried = out;
    for (Skin& skin : carried.skins) {
        for (int k = 0; k < skin.positions.size(); ++k) {
            const auto found = bindOffsets.constFind(skin.positions.at(k));
            if (found != bindOffsets.constEnd()) skin.bindPositions[k] += found.value();
        }
        for (int k = 0; k < skin.normals.size(); ++k) {
            const auto found = bindNormalOffsets.constFind(skin.normals.at(k));
            if (found != bindNormalOffsets.constEnd()) skin.bindNormals[k] += found.value();
        }
    }
    Mesh result = carried.skinned(nodeWorlds);
    result.skins = skins;
    return result;
}

QVector<QMatrix4x4> Mesh::restNodeWorlds() const
{
    QVector<QMatrix4x4> worlds;
    worlds.reserve(nodes.size());
    for (const ModelNode& node : nodes) worlds.append(node.baseWorld);
    return worlds;
}

QVector<QMatrix4x4> Mesh::animatedNodeMatrices(const ModelAnimation& clip, double seconds) const
{
    QVector<QMatrix4x4> result(nodes.size());
    if (nodes.isEmpty() || clip.channels.isEmpty()) return result;
    bool invertible = false;
    const QMatrix4x4 importInverse = importTransform.inverted(&invertible);
    const auto near = [](const QMatrix4x4& a, const QMatrix4x4& b) {
        for (int i = 0; i < 16; ++i)
            if (std::abs(a.constData()[i] - b.constData()[i]) > 1e-6f) return false;
        return true;
    };
    const QVector<QMatrix4x4> worlds = animatedNodeWorlds(clip, seconds);
    for (int i = 0; i < nodes.size(); ++i) {
        const ModelNode& node = nodes.at(i);
        // Skinned geometry follows its joints instead (skinned()).
        if (node.skinned) continue;
        bool baseInvertible = false;
        const QMatrix4x4 baseInverse = node.baseWorld.inverted(&baseInvertible);
        if (!baseInvertible) continue;
        const QMatrix4x4 change = worlds.at(i) * baseInverse;
        if (near(change, QMatrix4x4())) continue;
        // The geometry is the file's after the import settings, so the
        // change is made in the file's space between them.
        result[i] = invertible ? importTransform * change * importInverse : change;
    }
    return result;
}

QVector<QMatrix4x4> Mesh::animatedNodeWorlds(const ModelAnimation& clip, double seconds) const
{
    QVector<QMatrix4x4> worlds(nodes.size());
    if (nodes.isEmpty()) return worlds;
    const double time = qBound(0.0, seconds, clip.duration);
    // Parents are listed before their children, so one pass carries a
    // parent's motion down to them.
    for (int i = 0; i < nodes.size(); ++i) {
        const ModelNode& node = nodes.at(i);
        QMatrix4x4 local = node.baseLocal;
        if (node.sourceNode >= 0) {
            QVector3D translation = node.baseTranslation;
            QQuaternion rotation = node.baseRotation;
            QVector3D scale = node.baseScale;
            bool animated = false;
            for (const AnimationChannel& channel : clip.channels) {
                if (channel.sourceNode != node.sourceNode || channel.path == AnimationChannel::Path::Weights)
                    continue;
                const QVector4D value = channel.sample(time);
                if (channel.path == AnimationChannel::Path::Translation) translation = value.toVector3D();
                else if (channel.path == AnimationChannel::Path::Scale) scale = value.toVector3D();
                else rotation = QQuaternion(value.w(), value.x(), value.y(), value.z()).normalized();
                animated = true;
            }
            if (animated) {
                local.setToIdentity();
                local.translate(translation);
                local.rotate(rotation);
                local.scale(scale);
            }
        }
        const int parent = node.parent;
        worlds[i] = parent >= 0 && parent < i ? worlds.at(parent) * local : local;
    }
    return worlds;
}

namespace {

// A rigid motion as a unit dual quaternion: `real` the rotation, `dual`
// half the translation times it.
struct DualQuaternion
{
    QQuaternion real;
    QQuaternion dual;

    // The rotation and translation of `matrix`; its scale is left out, as
    // FbxDualQuaternion is made of an FbxAMatrix's rotation and translation.
    static DualQuaternion fromMatrix(const QMatrix4x4& matrix)
    {
        QMatrix3x3 rotation;
        for (int row = 0; row < 3; ++row) {
            const QVector3D column(matrix(0, row), matrix(1, row), matrix(2, row));
            const float length = column.length();
            for (int r = 0; r < 3; ++r)
                rotation(r, row) = length > 1e-12f ? matrix(r, row) / length : (r == row ? 1.0f : 0.0f);
        }
        DualQuaternion out;
        out.real = QQuaternion::fromRotationMatrix(rotation).normalized();
        const QVector3D t = matrix.column(3).toVector3D();
        out.dual = 0.5f * QQuaternion(0.0f, t) * out.real;
        return out;
    }

    QVector3D map(const QVector3D& point) const
    {
        const QQuaternion translation = 2.0f * dual * real.conjugated();
        return real.rotatedVector(point) + translation.vector();
    }
};

} // namespace

Mesh Mesh::skinned(const QVector<QMatrix4x4>& nodeWorlds) const
{
    Mesh out = *this;
    if (skins.isEmpty()) return out;
    bool invertible = false;
    const QMatrix4x4 importInverse = importTransform.inverted(&invertible);
    const QMatrix4x4 importNormal = invertible ? importInverse.transposed() : importTransform;
    for (const Skin& skin : skins) {
        QVector<QMatrix4x4> joints(skin.joints.size());
        QVector<QMatrix4x4> jointNormals(skin.joints.size());
        for (int j = 0; j < skin.joints.size(); ++j) {
            const int node = skin.joints.at(j);
            const QMatrix4x4 world = node >= 0 && node < nodeWorlds.size() ? nodeWorlds.at(node) : QMatrix4x4();
            joints[j] = world * skin.inverseBind.value(j);
            bool jointInvertible = false;
            const QMatrix4x4 inverse = joints.at(j).inverted(&jointInvertible);
            jointNormals[j] = jointInvertible ? inverse.transposed() : joints.at(j);
        }
        const auto blend = [&](const SkinInfluence& influence, const QVector3D& bind, bool normal, bool* any) {
            QVector3D sum;
            *any = false;
            for (int k = 0; k < 4; ++k) {
                const int j = influence.joints[k];
                const float w = influence.weights[k];
                if (j < 0 || j >= joints.size() || w <= 0.0f) continue;
                sum += w * (normal ? jointNormals.at(j).mapVector(bind) : joints.at(j).map(bind));
                *any = true;
            }
            return sum;
        };
        // Dual quaternion skinning (the FBX SDK's ComputeDualQuaternion-
        // Deformation): each joint's rigid motion as a unit dual quaternion,
        // the point's joints summed by weight - turned to the first one's
        // hemisphere - and normalised by the rotation's length.
        const bool dual = skin.method != Skin::Method::Linear;
        QVector<DualQuaternion> jointDuals;
        if (dual) {
            jointDuals.reserve(joints.size());
            for (const QMatrix4x4& joint : std::as_const(joints)) jointDuals.append(DualQuaternion::fromMatrix(joint));
        }
        const auto dualBlend = [&](const SkinInfluence& influence, bool* any) {
            DualQuaternion sum{QQuaternion(0, 0, 0, 0), QQuaternion(0, 0, 0, 0)};
            QQuaternion first;
            bool started = false;
            *any = false;
            for (int k = 0; k < 4; ++k) {
                const int j = influence.joints[k];
                float w = influence.weights[k];
                if (j < 0 || j >= jointDuals.size() || w <= 0.0f) continue;
                const DualQuaternion& d = jointDuals.at(j);
                if (!started) { first = d.real; started = true; }
                else if (QQuaternion::dotProduct(first, d.real) < 0.0f) w = -w;
                sum.real += w * d.real;
                sum.dual += w * d.dual;
                *any = true;
            }
            const float length = sum.real.length();
            if (length > 1e-12f) { sum.real /= length; sum.dual /= length; }
            return sum;
        };
        for (int k = 0; k < skin.positions.size(); ++k) {
            const int index = skin.positions.at(k);
            if (index < 0 || index >= out.positions.size()) continue;
            bool any = false;
            const QVector3D bind = skin.bindPositions.value(k);
            QVector3D placed = blend(skin.positionInfluences.value(k), bind, false, &any);
            if (dual) {
                bool dualAny = false;
                const QVector3D moved = dualBlend(skin.positionInfluences.value(k), &dualAny).map(bind);
                const float share = skin.method == Skin::Method::Blend ? skin.positionBlend.value(k) : 1.0f;
                if (dualAny) placed = moved * share + placed * (1.0f - share);
            }
            if (any) out.positions[index] = importTransform.map(placed);
        }
        for (int k = 0; k < skin.normals.size(); ++k) {
            const int index = skin.normals.at(k);
            if (index < 0 || index >= out.normals.size()) continue;
            bool any = false;
            const QVector3D bind = skin.bindNormals.value(k);
            QVector3D turned = blend(skin.normalInfluences.value(k), bind, true, &any);
            if (dual) {
                bool dualAny = false;
                const QVector3D rotated = dualBlend(skin.normalInfluences.value(k), &dualAny).real.rotatedVector(bind);
                const float share = skin.method == Skin::Method::Blend ? skin.normalBlend.value(k) : 1.0f;
                if (dualAny) turned = rotated * share + turned.normalized() * (1.0f - share);
            }
            if (any) out.normals[index] = importNormal.mapVector(turned).normalized();
        }
    }
    return out;
}

ModelAnimation retargetAnimation(const ModelAnimation& clip, const Mesh& from, const Mesh& to)
{
    QHash<int, int> sources;   // from's file node -> to's
    for (int i = 0; i < from.nodes.size(); ++i) {
        const int match = to.nodeByPath(from.nodePath(i));
        if (match >= 0 && from.nodes.at(i).sourceNode >= 0 && to.nodes.at(match).sourceNode >= 0)
            sources.insert(from.nodes.at(i).sourceNode, to.nodes.at(match).sourceNode);
    }
    ModelAnimation result = clip;
    result.channels.clear();
    for (AnimationChannel channel : clip.channels) {
        const auto found = sources.constFind(channel.sourceNode);
        if (found == sources.constEnd()) continue;
        channel.sourceNode = found.value();
        result.channels.append(channel);
    }
    return result;
}

int Mesh::animationFrameCount(int animation) const
{
    if (animation < 0 || animation >= animations.size()) return 1;
    return animations.at(animation).frameCount();
}

void Mesh::computeNodePivots()
{
    const int count = nodes.size();
    QVector<QVector3D> low(count, QVector3D(1e30f, 1e30f, 1e30f));
    QVector<QVector3D> high(count, QVector3D(-1e30f, -1e30f, -1e30f));
    QVector<bool> own(count, false);
    for (const Triangle& tri : triangles) {
        if (tri.node < 0 || tri.node >= count) continue;
        for (int c = 0; c < 3; ++c) {
            if (tri.position[c] < 0 || tri.position[c] >= positions.size()) continue;
            const QVector3D& p = positions.at(tri.position[c]);
            QVector3D& a = low[tri.node];
            QVector3D& b = high[tri.node];
            a = QVector3D(qMin(a.x(), p.x()), qMin(a.y(), p.y()), qMin(a.z(), p.z()));
            b = QVector3D(qMax(b.x(), p.x()), qMax(b.y(), p.y()), qMax(b.z(), p.z()));
            own[tri.node] = true;
        }
    }
    // A node without geometry of its own takes its children's bounds; parents
    // come before their children, so a pass from the end gathers upwards.
    QVector<bool> filled = own;
    for (int i = count - 1; i >= 0; --i) {
        const int parent = nodes.at(i).parent;
        if (!filled.at(i) || parent < 0 || parent >= count || own.at(parent)) continue;
        QVector3D& a = low[parent];
        QVector3D& b = high[parent];
        a = QVector3D(qMin(a.x(), low.at(i).x()), qMin(a.y(), low.at(i).y()), qMin(a.z(), low.at(i).z()));
        b = QVector3D(qMax(b.x(), high.at(i).x()), qMax(b.y(), high.at(i).y()), qMax(b.z(), high.at(i).z()));
        filled[parent] = true;
    }
    for (int i = 0; i < count; ++i) {
        nodes[i].pivot = filled.at(i) ? (low.at(i) + high.at(i)) * 0.5f : QVector3D();
    }
}

Mesh Mesh::posed(const QVector<QMatrix4x4>& nodeMatrices) const
{
    Mesh out = *this;
    QVector<bool> moves(nodeMatrices.size(), false);
    QVector<QMatrix4x4> normalMatrices(nodeMatrices.size());
    bool any = false;
    for (int i = 0; i < nodeMatrices.size(); ++i) {
        if (nodeMatrices.at(i).isIdentity()) continue;
        moves[i] = any = true;
        bool invertible = false;
        const QMatrix4x4 inverted = nodeMatrices.at(i).inverted(&invertible);
        normalMatrices[i] = invertible ? inverted.transposed() : nodeMatrices.at(i);
    }
    if (!any) return out;
    // (vertex, node) -> its moved copy.
    QHash<quint64, int> movedPositions;
    QHash<quint64, int> movedNormals;
    const auto key = [](int index, int node) {
        return (quint64(quint32(index)) << 32) | quint32(node);
    };
    for (Triangle& tri : out.triangles) {
        const int node = tri.node;
        if (node < 0 || node >= moves.size() || !moves.at(node)) continue;
        for (int c = 0; c < 3; ++c) {
            const int p = tri.position[c];
            if (p >= 0 && p < positions.size()) {
                auto it = movedPositions.constFind(key(p, node));
                if (it == movedPositions.constEnd()) {
                    out.positions.append(nodeMatrices.at(node).map(positions.at(p)));
                    it = movedPositions.insert(key(p, node), out.positions.size() - 1);
                }
                tri.position[c] = it.value();
            }
            const int n = tri.normal[c];
            if (n >= 0 && n < normals.size()) {
                auto it = movedNormals.constFind(key(n, node));
                if (it == movedNormals.constEnd()) {
                    out.normals.append(normalMatrices.at(node).mapVector(normals.at(n)).normalized());
                    it = movedNormals.insert(key(n, node), out.normals.size() - 1);
                }
                tri.normal[c] = it.value();
            }
        }
    }
    return out;
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
