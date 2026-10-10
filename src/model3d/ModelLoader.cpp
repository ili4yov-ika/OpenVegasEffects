#include "model3d/ModelLoader.h"
#include "model3d/FbxReader.h"
#include "model3d/GltfReader.h"
#include "model3d/LwoReader.h"
#include "model3d/ThreeDsReader.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QHash>
#include <QMutex>
#include <QRegularExpression>
#include <QTextStream>

#ifdef OPENVEGAS_HAVE_ALEMBIC
#include "model3d/AlembicReader.h"
#endif

namespace openvegas {
namespace model3d {

namespace {

// Verbatim from the reference's own dialog filters: 1412d0000 for models and
// 1412cff50 for animations. Order kept, because the first entry is what the
// dialog opens on.
const char* const kModelExtensions[] = {"3ds", "lwo", "obj", "fbx", "abc", "gltf", "glb"};
const char* const kAnimationExtensions[] = {"fbx", "abc", "gltf", "glb"};

QStringList toList(const char* const* items, int count)
{
    QStringList list;
    list.reserve(count);
    for (int i = 0; i < count; ++i) {
        list.append(QString::fromLatin1(items[i]));
    }
    return list;
}

// The reference reads OBJ through Assimp, whose material starts at a
// diffuse of 0.6 with no ambient, specular or emission and a shininess of 0
// (ObjFile::Material); the reference makes a shininess under 1 into 1.
Material assimpObjMaterial()
{
    Material material;
    material.diffuseColor = QColor::fromRgbF(0.6f, 0.6f, 0.6f);
    material.ambientColor = QColor(0, 0, 0);
    material.specularColor = QColor(0, 0, 0);
    material.emissiveColor = QColor(0, 0, 0);
    material.shininess = 1.0;
    return material;
}

// One "v/vt/vn" corner of an OBJ face. Missing fields come back as -1.
struct ObjCorner
{
    int position = -1;
    int uv = -1;
    int normal = -1;
};

// OBJ indices are 1-based and may be negative, counting back from the end of
// what has been read so far.
int resolveIndex(int raw, int count)
{
    if (raw > 0) {
        return raw - 1;
    }
    if (raw < 0) {
        return count + raw;
    }
    return -1;
}

ObjCorner parseCorner(const QString& token, int positionCount, int uvCount, int normalCount)
{
    ObjCorner corner;
    const QStringList parts = token.split(QLatin1Char('/'));
    if (parts.isEmpty()) {
        return corner;
    }
    corner.position = resolveIndex(parts.value(0).toInt(), positionCount);
    if (parts.size() > 1 && !parts.at(1).isEmpty()) {
        corner.uv = resolveIndex(parts.at(1).toInt(), uvCount);
    }
    if (parts.size() > 2 && !parts.at(2).isEmpty()) {
        corner.normal = resolveIndex(parts.at(2).toInt(), normalCount);
    }
    return corner;
}

QColor colorFromTokens(const QStringList& tokens, int first, const QColor& fallback)
{
    if (tokens.size() < first + 3) {
        return fallback;
    }
    const double r = tokens.at(first).toDouble();
    const double g = tokens.at(first + 1).toDouble();
    const double b = tokens.at(first + 2).toDouble();
    return QColor::fromRgbF(qBound(0.0, r, 1.0), qBound(0.0, g, 1.0), qBound(0.0, b, 1.0));
}

// Map paths in an .mtl are written relative to the .mtl itself. Returns the
// path as written when the file is not there, so the caller can report it.
QString resolveMapPath(const QString& baseDir, const QString& written)
{
    if (written.isEmpty()) {
        return QString();
    }
    const QFileInfo info(QDir(baseDir), written);
    return info.exists() ? info.absoluteFilePath() : written;
}

// Reads the material library beside an .obj. Unknown statements are skipped:
// .mtl is open-ended and a file carrying extensions should still load.
void loadMaterialLibrary(const QString& path, Mesh* mesh, QHash<QString, int>* byName,
                         QStringList* problems)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        problems->append(QCoreApplication::translate(
            "Model3D", "Material library could not be opened: %1").arg(QFileInfo(path).fileName()));
        return;
    }
    const QString baseDir = QFileInfo(path).absolutePath();
    const QRegularExpression spaces(QStringLiteral("\\s+"));

    QTextStream in(&file);
    int current = -1;
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        const QStringList tokens = line.split(spaces, Qt::SkipEmptyParts);
        if (tokens.isEmpty()) {
            continue;
        }
        const QString keyword = tokens.first().toLower();

        if (keyword == QLatin1String("newmtl")) {
            Material material = assimpObjMaterial();
            material.name = tokens.mid(1).join(QLatin1Char(' '));
            byName->insert(material.name, mesh->materials.size());
            mesh->materials.append(material);
            current = mesh->materials.size() - 1;
            continue;
        }
        if (current < 0) {
            continue;
        }
        Material& material = mesh->materials[current];

        if (keyword == QLatin1String("kd")) {
            material.diffuseColor = colorFromTokens(tokens, 1, material.diffuseColor);
        } else if (keyword == QLatin1String("ks")) {
            material.specularColor = colorFromTokens(tokens, 1, material.specularColor);
        } else if (keyword == QLatin1String("ka")) {
            material.ambientColor = colorFromTokens(tokens, 1, material.ambientColor);
        } else if (keyword == QLatin1String("ke")) {
            material.emissiveColor = colorFromTokens(tokens, 1, material.emissiveColor);
        } else if (keyword == QLatin1String("ns")) {
            material.shininess = qMax(1.0, tokens.value(1).toDouble());
        } else if (keyword == QLatin1String("ni")) {
            material.indexOfRefraction = tokens.value(1).toDouble();
        } else if (keyword == QLatin1String("d")) {
            material.opacity = qBound(0.0, tokens.value(1).toDouble(), 1.0);
        } else if (keyword == QLatin1String("tr")) {
            // The inverse convention of "d", and both are found in the wild.
            material.opacity = qBound(0.0, 1.0 - tokens.value(1).toDouble(), 1.0);
        } else if (keyword == QLatin1String("map_kd")) {
            material.diffuseMap = resolveMapPath(baseDir, tokens.last());
        } else if (keyword == QLatin1String("map_ks")) {
            material.specularMap = resolveMapPath(baseDir, tokens.last());
        } else if (keyword == QLatin1String("map_pm") || keyword == QLatin1String("map_metallic")) {
            material.metallicMap = resolveMapPath(baseDir, tokens.last());
        } else if (keyword == QLatin1String("map_pr") || keyword == QLatin1String("map_roughness")) {
            material.roughnessMap = resolveMapPath(baseDir, tokens.last());
        } else if (keyword == QLatin1String("map_specgloss") || keyword == QLatin1String("map_sg")) {
            material.specularGlossinessMap = resolveMapPath(baseDir, tokens.last());
        } else if (keyword == QLatin1String("map_ke")) {
            material.emissiveMap = resolveMapPath(baseDir, tokens.last());
        } else if (keyword == QLatin1String("map_ka")) {
            material.occlusionMap = resolveMapPath(baseDir, tokens.last());
        } else if (keyword == QLatin1String("norm")) {
            material.normalMap = resolveMapPath(baseDir, tokens.last());
        } else if (keyword == QLatin1String("map_bump") || keyword == QLatin1String("bump")) {
            // A height map; "-bm s" scales it.
            material.bumpMap = resolveMapPath(baseDir, tokens.last());
            const int bm = tokens.indexOf(QStringLiteral("-bm"));
            if (bm > 0 && bm + 1 < tokens.size() - 1) material.bumpScale = tokens.at(bm + 1).toDouble();
        }
    }
}

LoadResult loadObj(const QString& filePath)
{
    LoadResult result;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.message = QCoreApplication::translate("Model3D", "The file could not be opened.");
        return result;
    }
    const QString baseDir = QFileInfo(filePath).absolutePath();

    Mesh& mesh = result.mesh;
    QHash<QString, int> materialsByName;
    QStringList problems;
    int currentMaterial = -1;
    int currentNode = -1;

    QTextStream in(&file);
    const QRegularExpression spaces(QStringLiteral("\\s+"));
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        const QStringList tokens = line.split(spaces, Qt::SkipEmptyParts);
        if (tokens.isEmpty()) {
            continue;
        }
        const QString keyword = tokens.first();

        if (keyword == QLatin1String("v") && tokens.size() >= 4) {
            mesh.positions.append(QVector3D(tokens.at(1).toFloat(), tokens.at(2).toFloat(),
                                            tokens.at(3).toFloat()));
        } else if (keyword == QLatin1String("vn") && tokens.size() >= 4) {
            mesh.normals.append(QVector3D(tokens.at(1).toFloat(), tokens.at(2).toFloat(),
                                          tokens.at(3).toFloat()));
        } else if (keyword == QLatin1String("vt") && tokens.size() >= 3) {
            mesh.uvs.append(QVector2D(tokens.at(1).toFloat(), tokens.at(2).toFloat()));
        } else if (keyword == QLatin1String("f") && tokens.size() >= 4) {
            // Fan triangulation: an OBJ face is any convex polygon, and the
            // rasteriser only deals in triangles.
            QVector<ObjCorner> corners;
            corners.reserve(tokens.size() - 1);
            for (int i = 1; i < tokens.size(); ++i) {
                corners.append(parseCorner(tokens.at(i), mesh.positions.size(), mesh.uvs.size(),
                                           mesh.normals.size()));
            }
            for (int i = 1; i + 1 < corners.size(); ++i) {
                const ObjCorner& a = corners.at(0);
                const ObjCorner& b = corners.at(i);
                const ObjCorner& c = corners.at(i + 1);
                if (a.position < 0 || b.position < 0 || c.position < 0) {
                    continue;
                }
                Triangle tri;
                tri.position[0] = a.position;
                tri.position[1] = b.position;
                tri.position[2] = c.position;
                tri.normal[0] = a.normal;
                tri.normal[1] = b.normal;
                tri.normal[2] = c.normal;
                tri.uv[0] = a.uv;
                tri.uv[1] = b.uv;
                tri.uv[2] = c.uv;
                tri.material = currentMaterial;
                tri.node = currentNode;
                mesh.triangles.append(tri);
            }
        } else if (keyword == QLatin1String("o") || keyword == QLatin1String("g")) {
            // Each object or group is a node of the model, as the reference's
            // importer makes a Model3DGroup of it; the faces after it are its.
            const QString name = tokens.mid(1).join(QLatin1Char(' '));
            if (!name.isEmpty()) currentNode = mesh.addNode(name);
        } else if (keyword == QLatin1String("usemtl")) {
            currentMaterial = materialsByName.value(tokens.mid(1).join(QLatin1Char(' ')), -1);
        } else if (keyword == QLatin1String("mtllib")) {
            const QString name = tokens.mid(1).join(QLatin1Char(' '));
            loadMaterialLibrary(QDir(baseDir).absoluteFilePath(name), &mesh, &materialsByName,
                                &problems);
        }
    }

    if (mesh.triangles.isEmpty()) {
        result.message = QCoreApplication::translate("Model3D", "The file contains no geometry.");
        return result;
    }
    // Faces with no material, or one the libraries do not have, take
    // Assimp's default material, as they do in the reference.
    bool bare = false;
    for (const Triangle& triangle : std::as_const(mesh.triangles)) bare = bare || triangle.material < 0;
    if (bare) {
        Material fallback = assimpObjMaterial();
        fallback.name = QStringLiteral("DefaultMaterial");
        const int index = mesh.materials.size();
        mesh.materials.append(fallback);
        for (Triangle& triangle : mesh.triangles)
            if (triangle.material < 0) triangle.material = index;
    }

    result.ok = true;
    result.message = problems.join(QLatin1Char('\n'));
    return result;
}

// Applies the import settings to geometry that is already in memory, so every
// format goes through the same treatment.
void applySettings(Mesh* mesh, const ImportSettings& settings)
{
    if (mesh->positions.isEmpty()) {
        return;
    }

    // The reference's Model3DPostProcessor::Process (Project.dll 1802c5490).
    // What happens to the positions is kept as a matrix too, so animations
    // in the file's space can be carried into the imported model's.
    //
    // Flip YZ Axis first (SwapAxes): (x, y, z) becomes (-x, z, y), the
    // normals alike.
    QMatrix4x4 placement;
    if (settings.flipYZAxis) {
        placement = QMatrix4x4(-1, 0, 0, 0,
                               0, 0, 1, 0,
                               0, 1, 0, 0,
                               0, 0, 0, 1);
        for (QVector3D& p : mesh->positions) {
            p = QVector3D(-p.x(), p.z(), p.y());
        }
        for (QVector3D& n : mesh->normals) {
            n = QVector3D(-n.x(), n.z(), n.y());
        }
    }

    // Then the scale, s = Single Unit Scale x the unit's pixels, about the
    // bounding box's centre c. Auto Normalize (N = 500 / (|size / 2| * s))
    // sizes the model so half its box's diagonal is 500 pixels; centred, a
    // point goes to (p - c) * s * N, otherwise to (p * s - c) * N + c. Without
    // it, (p - c) * s centred, p * s otherwise. (The reference measures the
    // box before the flip turns its x round, which puts a flipped, centred
    // model off centre; this centres it.)
    const double s = (settings.singleUnitScale > 0.0 ? settings.singleUnitScale : 1.0) * settings.unitScale();
    QVector3D lo, hi;
    mesh->bounds(&lo, &hi);
    const QVector3D centre = (lo + hi) * 0.5f;
    const QVector3D size = hi - lo;
    double factor = s;              // what a point is multiplied by
    QVector3D offset;               // and what is added after
    if (settings.autoNormalize) {
        const bool flat = qAbs(size.x()) <= 1e-5f && qAbs(size.y()) <= 1e-5f && qAbs(size.z()) <= 1e-5f;
        const double half = (size * 0.5f).length() * s;
        const double n = flat || half <= 0.0 ? 1.0 : 500.0 / half;
        factor = s * n;
        offset = settings.centerAnchorPoint ? -centre * float(s * n) : centre * float(1.0 - n);
    } else if (settings.centerAnchorPoint) {
        offset = -centre * float(s);
    }
    if (!qFuzzyCompare(factor, 1.0) || !offset.isNull()) {
        for (QVector3D& p : mesh->positions) {
            p = p * float(factor) + offset;
        }
        QMatrix4x4 scaling;
        scaling.translate(offset);
        scaling.scale(float(factor));
        placement = scaling * placement;
    }
    mesh->importTransform = placement;

    const bool wantGenerated = settings.normalMethod == ImportSettings::NormalMethod::GenerateFaceted
        || settings.normalMethod == ImportSettings::NormalMethod::GenerateFacetedAutoSmoothing;
    const bool wantSmoothing =
        settings.normalMethod == ImportSettings::NormalMethod::FromFileAutoSmoothing
        || settings.normalMethod == ImportSettings::NormalMethod::GenerateFacetedAutoSmoothing;

    if (wantGenerated || mesh->normals.isEmpty()) {
        // Faceted means every corner takes its own face normal, which is what
        // a zero smoothing angle produces. A file without normals gets smooth
        // ones up to the smoothing angle, as the reference's Assimp import
        // makes them (aiProcess_GenSmoothNormals, PP_GSN_MAX_SMOOTHING_ANGLE
        // set from the angle).
        const double angle = !wantGenerated || wantSmoothing ? settings.autoSmoothingAngle : 0.0;
        mesh->generateNormals(angle, settings.unifyNormals);
    }

    if (settings.flipNormals) {
        for (QVector3D& n : mesh->normals) {
            n = -n;
        }
    }
    if (settings.flipUVCoordinates) {
        for (QVector2D& uv : mesh->uvs) {
            uv.setY(1.0f - uv.y());
        }
    }
}

} // namespace

QStringList modelExtensions()
{
    return toList(kModelExtensions, int(sizeof(kModelExtensions) / sizeof(kModelExtensions[0])));
}

QStringList animationExtensions()
{
    return toList(kAnimationExtensions,
                  int(sizeof(kAnimationExtensions) / sizeof(kAnimationExtensions[0])));
}

QString modelFileFilter()
{
    // Reference string 1412d0000, kept whole so the dialog reads the same.
    return QCoreApplication::translate(
        "Model3D",
        "All Supported 3D Models (*.3ds *.lwo *.obj *.fbx *.abc *.gltf *.glb);;"
        "Autodesk 3ds Max (*.3ds);;NewTek LightWave 3D (*.lwo);;"
        "Wavefront Technologies (*.obj);;Autodesk FilmBox (*.fbx);;Alembic (*.abc);;"
        "GL Transmission Format (*.gltf);;GL Transmission Format Binary (*.glb)");
}

QString animationFileFilter()
{
    // Reference string 1412cff50.
    return QCoreApplication::translate(
        "Model3D",
        "All Supported Animation Files (*.fbx *.abc *.gltf *.glb);;"
        "Autodesk FilmBox (*.fbx);;Alembic (*.abc);;"
        "GL Transmission Format (*.gltf);;GL Transmission Format Binary (*.glb)");
}

std::shared_ptr<const Mesh> animationSource(const QString& filePath)
{
    struct Entry { QDateTime modified; qint64 size = -1; std::shared_ptr<const Mesh> mesh; };
    static QMutex mutex;
    static QHash<QString, Entry> sources;
    const QFileInfo info(filePath);
    if (!info.exists()) return nullptr;
    const QString key = info.absoluteFilePath();
    {
        QMutexLocker lock(&mutex);
        const auto found = sources.constFind(key);
        if (found != sources.constEnd() && found->modified == info.lastModified()
            && found->size == info.size())
            return found->mesh;
    }
    // Read as a model; only its nodes and animations are kept.
    LoadResult loaded = loadModel(key, ImportSettings());
    std::shared_ptr<const Mesh> mesh;
    if (loaded.ok && !loaded.mesh.animations.isEmpty()) {
        auto kept = std::make_shared<Mesh>();
        kept->nodes = loaded.mesh.nodes;
        kept->animations = loaded.mesh.animations;
        mesh = kept;
    }
    QMutexLocker lock(&mutex);
    sources.insert(key, Entry{info.lastModified(), info.size(), mesh});
    return mesh;
}

bool canReadExtension(const QString& suffix)
{
    const QString lower = suffix.toLower();
    if (lower == QLatin1String("obj") || lower == QLatin1String("fbx") || lower == QLatin1String("gltf")
        || lower == QLatin1String("glb") || lower == QLatin1String("3ds") || lower == QLatin1String("lwo")) {
        return true;
    }
#ifdef OPENVEGAS_HAVE_ALEMBIC
    if (lower == QLatin1String("abc")) {
        return true;
    }
#endif
    return false;
}

QString readableFormats()
{
    QStringList formats{QStringLiteral("3ds"), QStringLiteral("lwo"), QStringLiteral("obj"), QStringLiteral("fbx"),
                        QStringLiteral("gltf"), QStringLiteral("glb")};
#ifdef OPENVEGAS_HAVE_ALEMBIC
    formats.append(QStringLiteral("abc"));
#endif
    return formats.join(QStringLiteral(", "));
}

LoadResult loadModel(const QString& filePath, const ImportSettings& settings)
{
    LoadResult result;
    const QString suffix = QFileInfo(filePath).suffix().toLower();

    if (suffix == QLatin1String("obj")) {
        result = loadObj(filePath);
    } else if (suffix == QLatin1String("gltf") || suffix == QLatin1String("glb")) {
        result = loadGltf(filePath);
    } else if (suffix == QLatin1String("fbx")) {
        result = loadFbx(filePath);
    } else if (suffix == QLatin1String("3ds")) {
        result = load3ds(filePath);
    } else if (suffix == QLatin1String("lwo")) {
        result = loadLwo(filePath);
    }
#ifdef OPENVEGAS_HAVE_ALEMBIC
    else if (suffix == QLatin1String("abc")) {
        result = loadAlembic(filePath);
    }
#endif
    else {
        // The dialog offers every format the reference does, so a file this
        // build cannot parse has to say which ones it can rather than failing
        // blankly.
        result.message = QCoreApplication::translate(
            "Model3D", "The 3D model could not be imported: .%1 is not supported by this build "
                       "(readable formats: %2).").arg(suffix, readableFormats());
        return result;
    }

    if (result.ok) {
        applySettings(&result.mesh, settings);
        result.mesh.computeNodePivots();
        // The maps, read once here rather than at every frame - those the
        // format did not carry inside the file already (glTF does).
        const auto read = [](const QString& path) {
            if (path.isEmpty() || !QFileInfo::exists(path)) return QImage();
            const QImage image(path);
            return image.isNull() ? image : image.convertToFormat(QImage::Format_RGBA8888);
        };
        for (Material& material : result.mesh.materials) {
            const auto fill = [&read](QImage& image, const QString& path) {
                if (image.isNull()) image = read(path);
            };
            fill(material.diffuseImage, material.diffuseMap);
            fill(material.specularImage, material.specularMap);
            fill(material.metallicImage, material.metallicMap);
            fill(material.roughnessImage, material.roughnessMap);
            fill(material.specularGlossinessImage, material.specularGlossinessMap);
            fill(material.normalImage, material.normalMap);
            fill(material.bumpImage, material.bumpMap);
            fill(material.emissiveImage, material.emissiveMap);
            fill(material.occlusionImage, material.occlusionMap);
        }
    }
    return result;
}

} // namespace model3d
} // namespace openvegas
