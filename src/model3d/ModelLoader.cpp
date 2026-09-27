#include "model3d/ModelLoader.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
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
            Material material;
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
        } else if (keyword == QLatin1String("map_ke")) {
            material.emissiveMap = resolveMapPath(baseDir, tokens.last());
        } else if (keyword == QLatin1String("map_ka")) {
            material.occlusionMap = resolveMapPath(baseDir, tokens.last());
        } else if (keyword == QLatin1String("norm") || keyword == QLatin1String("map_bump")
                   || keyword == QLatin1String("bump")) {
            material.normalMap = resolveMapPath(baseDir, tokens.last());
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
                mesh.triangles.append(tri);
            }
        } else if (keyword == QLatin1String("o") || keyword == QLatin1String("g")) {
            const QString name = tokens.mid(1).join(QLatin1Char(' '));
            if (!name.isEmpty() && !mesh.nodeNames.contains(name)) {
                mesh.nodeNames.append(name);
            }
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

    // Coordinate system, before anything measures the model: Y-up and Z-up
    // authoring tools disagree, and the reference's Flip YZ Axis is the switch
    // between them.
    if (settings.flipYZAxis) {
        for (QVector3D& p : mesh->positions) {
            p = QVector3D(p.x(), p.z(), -p.y());
        }
        for (QVector3D& n : mesh->normals) {
            n = QVector3D(n.x(), n.z(), -n.y());
        }
    }

    // Unit scale, then Auto Normalize on top: the first puts the model in
    // metres, the second sizes it to something a composition can show whatever
    // the file's own scale was.
    double scale = settings.unitScale();
    if (settings.autoNormalize) {
        const double span = mesh->boundingSize() * scale;
        if (span > 0.0) {
            // 400 units across, roughly a third of a 1080-tall frame, which is
            // about where the reference drops a normalised model in.
            scale *= 400.0 / span;
        }
    }
    scale *= settings.singleUnitScale > 0.0 ? settings.singleUnitScale : 1.0;
    if (!qFuzzyCompare(scale, 1.0)) {
        for (QVector3D& p : mesh->positions) {
            p *= static_cast<float>(scale);
        }
    }

    if (settings.centerAnchorPoint) {
        const QVector3D centre = mesh->centre();
        for (QVector3D& p : mesh->positions) {
            p -= centre;
        }
    }

    const bool wantGenerated = settings.normalMethod == ImportSettings::NormalMethod::GenerateFaceted
        || settings.normalMethod == ImportSettings::NormalMethod::GenerateFacetedAutoSmoothing;
    const bool wantSmoothing =
        settings.normalMethod == ImportSettings::NormalMethod::FromFileAutoSmoothing
        || settings.normalMethod == ImportSettings::NormalMethod::GenerateFacetedAutoSmoothing;

    if (wantGenerated || mesh->normals.isEmpty()) {
        // Faceted means every corner takes its own face normal, which is what
        // a zero smoothing angle produces.
        mesh->generateNormals(wantSmoothing ? settings.autoSmoothingAngle : 0.0,
                              settings.unifyNormals);
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

bool canReadExtension(const QString& suffix)
{
    const QString lower = suffix.toLower();
    if (lower == QLatin1String("obj")) {
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
    QStringList formats{QStringLiteral("obj")};
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
    }
    return result;
}

} // namespace model3d
} // namespace openvegas
