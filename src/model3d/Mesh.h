#pragma once

#include <QColor>
#include <QHash>
#include <QImage>
#include <QMatrix4x4>
#include <QQuaternion>
#include <QString>
#include <QVector>
#include <QVector3D>
#include <QVector2D>
#include <QVector4D>

namespace openvegas {
namespace model3d {

// How a material answers light: the reference's IlluminationType, which is
// Flux's illuminationType - Phong, Cook-Torrance (a Beckmann distribution
// with a Fresnel term), and the two physically based models of glTF.
enum class IlluminationModel
{
    Phong = 0,
    CookTorrance = 1,
    MetallicRoughness = 2,
    SpecularGlossiness = 3,
};

// What a material's alpha means (the reference's AlphaMode; its glTF
// importer turns MASK into Mask, BLEND into Blend and anything else into
// Opaque): Mask cuts the surface away below Alpha Cut Off, Opaque ignores
// the alpha, Blend and Guess blend with it.
enum class AlphaMode
{
    Guess = 0,
    Opaque = 1,
    Blend = 2,
    Mask = 3,
};

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
    IlluminationModel illuminationModel = IlluminationModel::Phong;
    // Metallic-Roughness: MR_MetallicFactor and MR_RoughnessFactor (the
    // reference keeps them in percent, Flux divides by 100), times their maps.
    double metallic = 0.0;
    double roughness = 1.0;
    // Cook-Torrance (CK_Roughness, CK_SpecularReflectance, CK_Fresnel), with
    // the reference's defaults (Model3DMaterial's constructor, Project.dll
    // 0x1802c1e90: 5 %, 5 %, (0.9132, 0.9215, 0.9245)).
    double cookTorranceRoughness = 0.05;
    double cookTorranceSpecularReflectance = 0.05;
    QVector3D cookTorranceFresnel = QVector3D(0.91318f, 0.92149f, 0.92452f);
    // Specular-Glossiness: SG_GlossinessFactor, times the map's alpha.
    double glossiness = 1.0;
    // Metallic-Roughness clear coat (MR_ClearCoatFactor,
    // MR_ClearCoatRoughnessFactor); it only changes reflections, which are
    // not drawn here.
    double clearCoat = 0.0;
    double clearCoatRoughness = 0.0;
    AlphaMode alphaMode = AlphaMode::Guess;
    double alphaCutOff = 0.5;
    // How much of the occlusion map shows (Occlusion Strength).
    double occlusionStrength = 1.0;
    double refractionAmount = 0.0;
    double shininess = 32.0;      // Phong exponent, reference "Shininess"
    double opacity = 1.0;
    double indexOfRefraction = 1.0;

    // Texture paths as the file named them, resolved next to the model.
    QString diffuseMap;
    QString specularMap;
    QString normalMap;
    QString emissiveMap;
    QString occlusionMap;
    // PBR maps. glTF stores metallic in B and roughness in G of one packed
    // image; other importers may provide separate maps or specular-glossiness.
    QString metallicMap;
    QString roughnessMap;
    QString specularGlossinessMap;
    // A height map (MTL "bump"/"map_Bump"), with its -bm multiplier.
    QString bumpMap;
    double bumpScale = 1.0;

    // The maps' pixels (RGBA8888, straight alpha), read when the model is
    // loaded and used where the faces have UVs: the diffuse map times
    // diffuseColor, the specular map times specularColor, the emissive map
    // times emissiveColor (or alone when that colour is black), the
    // occlusion map times the ambient term; the normal map (tangent space,
    // Y up) and the bump map (height, its red channel) turn the normal.
    QImage diffuseImage;
    QImage specularImage;
    QImage normalImage;
    QImage bumpImage;
    QImage emissiveImage;
    QImage occlusionImage;
    QImage metallicImage;
    QImage roughnessImage;
    QImage specularGlossinessImage;
    bool metallicRoughnessPacked = false;
};

// One triangle. Indices point into the mesh's vertex arrays; -1 in `normal` or
// `uv` means the file gave none for that corner.
struct Triangle
{
    int position[3] = {0, 0, 0};
    int normal[3] = {-1, -1, -1};
    int uv[3] = {-1, -1, -1};
    int material = -1;
    int node = -1;   // the ModelNode it belongs to; -1 is the model itself
};

// One object or group a model file declared - the reference's Model3DGroup,
// which a model layer shows under its Models branch and gives a transform of
// its own (Model3DTransformNode). Its pivot is the centre of the bounds of
// its own geometry (Model3DGroup::AddGeometryBatch keeps that box), which is
// where the node's anchor point and position start, so an untouched node
// stays where the file put it.
struct ModelNode
{
    QString name;
    int parent = -1;
    QVector3D pivot;
    // The file's own node (glTF nodes[] index) and its pose there, which an
    // animation starts from: its matrix below its parent, that matrix as
    // translation/rotation/scale, and where it put the node's geometry (in
    // the file's space, before the import settings).
    int sourceNode = -1;
    QMatrix4x4 baseLocal;
    QMatrix4x4 baseWorld;
    QVector3D baseTranslation;
    QQuaternion baseRotation = QQuaternion(1.0f, 0.0f, 0.0f, 0.0f);
    QVector3D baseScale = QVector3D(1.0f, 1.0f, 1.0f);
    // Its geometry is carried by a skeleton (a skinned mesh): an animation
    // moves it through its joints, not by the node's own motion.
    bool skinned = false;
};

// Up to four joints a point (or a normal) of a skin follows, and how much.
struct SkinInfluence
{
    int joints[4] = {-1, -1, -1, -1};   // indices into Skin::joints
    float weights[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};

// A skeleton and the geometry it carries (glTF skins, FBX skin clusters):
// each point and normal kept where it sits in its mesh's own space at bind
// time, and the joints that move it. A point's place is the sum of
// weight x (joint world x inverse bind) x its bind place, in the file's
// space; the mesh's own positions hold the rest pose, after the import
// settings.
struct Skin
{
    QVector<int> joints;               // ModelNodes that are its bones
    QVector<QMatrix4x4> inverseBind;   // per joint: mesh space -> its space at bind
    QVector<int> positions;            // Mesh::positions it carries
    QVector<QVector3D> bindPositions;
    QVector<SkinInfluence> positionInfluences;
    QVector<int> normals;              // Mesh::normals it carries
    QVector<QVector3D> bindNormals;
    QVector<SkinInfluence> normalInfluences;
};

// One animated property of one node: keyed translation, rotation (a
// quaternion, x y z w) or scale, as glTF animation channels and samplers
// carry it.
struct AnimationChannel
{
    // Weights: a morph's target weights (glTF "weights", FBX DeformPercent
    // / 100), `weightCount` of them a key in `weights`.
    enum class Path { Translation, Rotation, Scale, Weights };
    enum class Interpolation { Linear, Step, CubicSpline };
    int sourceNode = -1;
    Path path = Path::Translation;
    Interpolation interpolation = Interpolation::Linear;
    QVector<float> times;          // seconds
    // One value a key; CubicSpline keeps in-tangent, value, out-tangent.
    QVector<QVector4D> values;
    // Weights: the values, `weightCount` a key (three times that for
    // CubicSpline: in-tangents, values, out-tangents).
    QVector<float> weights;
    int weightCount = 0;

    // The value at `seconds`, held before the first key and after the last.
    QVector4D sample(double seconds) const;
    // Weights at `seconds`, the same way.
    QVector<float> sampleWeights(double seconds) const;
};

// One shape a morph can take: how far it moves each point and normal it
// changes, in its mesh's own space, at weight 1.
struct MorphTarget
{
    QString name;
    QVector<int> positions;            // Mesh::positions it moves
    QVector<QVector3D> positionDeltas;
    QVector<int> normals;              // Mesh::normals it turns
    QVector<QVector3D> normalDeltas;
};

// A mesh's morph targets (glTF targets, FBX blend shapes) and the weights it
// rests at. A mesh that is not skinned is stored at those weights, placed by
// its node; a skinned one is stored without them, its targets moving the
// points its skin then carries.
struct Morph
{
    int node = -1;                     // the ModelNode holding the mesh
    int sourceNode = -1;               // the file's node, which animations name
    QMatrix4x4 placement;              // mesh space -> the file's space (not skinned)
    QVector<MorphTarget> targets;
    QVector<float> defaultWeights;
    // Not skinned: the normals its targets turn as the file has them, before
    // the weights it rests at, so they can be turned again exactly.
    QHash<int, QVector3D> baseNormals;
};

// One animation a file holds - glTF's animations[], the reference's
// Model3DAnimationAsset with its AnimationIndex.
// The reference's glTF importer counts an animation in frames of 24 a second
// (GLTFModel3DImporter::SetCurrentAnimationIndex, Project.dll 1800ab6f0:
// frames = duration x 24; its frame rate is the same constant).
constexpr double kModelAnimationFrameRate = 24.0;

struct ModelAnimation
{
    QString name;
    double duration = 0.0;         // seconds, to its last key (FBX: its stack's end)
    // The rate its frames are counted in: 24 for glTF, an FBX file's own
    // (GlobalSettings TimeMode, as FilmBoxModel3DImporter takes it).
    double frameRate = kModelAnimationFrameRate;
    QVector<AnimationChannel> channels;

    int frameCount() const { return qMax(1, int(duration * frameRate)); }
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
    // The same groups as a hierarchy, which the triangles point into.
    QVector<ModelNode> nodes;
    // The animations the file holds.
    QVector<ModelAnimation> animations;
    // Its skeletons and the geometry they carry.
    QVector<Skin> skins;
    // Its morph targets.
    QVector<Morph> morphs;
    // What the import settings did to the file's positions - axes, scale,
    // centring: model space = importTransform x file space.
    QMatrix4x4 importTransform;

    bool isEmpty() const { return triangles.isEmpty() || positions.isEmpty(); }
    int triangleCount() const { return triangles.size(); }

    // Axis-aligned bounds of the positions. Returns false for an empty mesh.
    bool bounds(QVector3D* minimum, QVector3D* maximum) const;
    // Longest edge of the bounding box, or 0 when empty. Used to fit a model
    // into the composition the way the reference's Auto Normalize does.
    double boundingSize() const;
    QVector3D centre() const;

    // The node named `name` under `parent`, added when new.
    int addNode(const QString& name, int parent = -1);
    // A node's names from the top, joined by '/': how a layer keeps what was
    // changed on it, since a model is read again from its file on load.
    QString nodePath(int index) const;
    int nodeByPath(const QString& path) const;
    // Sets every node's pivot: the centre of the bounds of its own triangles,
    // or of its children's when it holds none.
    void computeNodePivots();
    // The mesh with each node's triangles carried by its matrix (in model
    // space, a parent's included); vertices nodes share are split. A node
    // without a matrix, or with the identity, stays where it is.
    Mesh posed(const QVector<QMatrix4x4>& nodeMatrices) const;
    // What animation `animation` does to each node at `seconds` (held at its
    // ends), as a matrix on the model as imported - the node's place in the
    // animation over its place in the file, a moving parent's included -
    // for posed(). The identity for a node the animation leaves alone.
    QVector<QMatrix4x4> animatedNodeMatrices(int animation, double seconds) const;
    QVector<QMatrix4x4> animatedNodeMatrices(const ModelAnimation& clip, double seconds) const;
    // Each node's world matrix in the file's space under `clip` at
    // `seconds`; its place in the file for a node the clip leaves alone.
    QVector<QMatrix4x4> animatedNodeWorlds(const ModelAnimation& clip, double seconds) const;
    // Every node where the file puts it (ModelNode::baseWorld).
    QVector<QMatrix4x4> restNodeWorlds() const;
    // The mesh with its skins posed by node worlds in the file's space
    // (animatedNodeWorlds, restNodeWorlds): the points and normals they carry
    // moved, then carried through the import settings.
    Mesh skinned(const QVector<QMatrix4x4>& nodeWorlds) const;
    // Each morph's weights under `clip` at `seconds`; the morph's own where
    // the clip does not key them.
    QVector<QVector<float>> morphWeights(const ModelAnimation& clip, double seconds) const;
    // The mesh with its morphs at `weights` (morphWeights) and its skins
    // posed by `nodeWorlds`: a morph moves a mesh that is not skinned by how
    // far its weights are from those it rests at, and a skinned one's points
    // before its skin carries them.
    Mesh deformed(const QVector<QMatrix4x4>& nodeWorlds, const QVector<QVector<float>>& weights) const;
    // Its length in the reference's frames: duration x 24, at least one.
    int animationFrameCount(int animation) const;

    // Fills in per-vertex normals for triangles that came without any.
    // `smoothingAngleDegrees` follows the reference's auto-smoothing: corners
    // whose faces meet at a sharper angle than this are left faceted.
    void generateNormals(double smoothingAngleDegrees, bool unify);
};

// `clip`, an animation of the nodes of `from`, put on the nodes of `to` with
// the same path: an animation read from another file plays on a model whose
// nodes it shares (the reference pairs them, Model3DAnimationAssetPair::
// CanCombineAnimations). Channels for nodes `to` lacks are left out.
ModelAnimation retargetAnimation(const ModelAnimation& clip, const Mesh& from, const Mesh& to);

// The settings of a material the reference lets one edit in its Materials
// tab (Model3DMaterialItemModel): colours, Shininess, reflectivities,
// Opacity, refraction, Illumination Model and its settings, alpha, Occlusion
// Strength and Max Bump Depth - everything but its name and maps.
void copyMaterialSettings(const Material& from, Material& to);
bool sameMaterialSettings(const Material& a, const Material& b);
// Puts the settings of `edited` on the materials of `mesh` with the same
// names (a model read again with other import settings keeps its edits).
void applyMaterialSettings(Mesh& mesh, const QVector<Material>& edited);

} // namespace model3d
} // namespace openvegas
