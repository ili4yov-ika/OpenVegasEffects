#include "model3d/ThreeDsReader.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QtEndian>

#include <array>
#include <cstring>
#include <functional>

namespace openvegas {
namespace model3d {

namespace {

// The chunks this reader looks at (3D Studio's own ids).
enum Chunk : quint16 {
    kMain = 0x4D4D,
    kEditor = 0x3D3D,
    kObject = 0x4000,
    kTriangleMesh = 0x4100,
    kPoints = 0x4110,
    kFaces = 0x4120,
    kFaceMaterial = 0x4130,
    kTextureCoordinates = 0x4140,
    kMaterial = 0xAFFF,
    kKeyframer = 0xB000,
    kObjectNode = 0xB002,
    kNodeHeader = 0xB010,
    kInstanceName = 0xB011,
    kNodeId = 0xB030,
    kAmbientLight = 0x2100,
    kFaceSmoothing = 0x4150,
    kMaterialName = 0xA000,
    kAmbient = 0xA010,
    kDiffuse = 0xA020,
    kSpecular = 0xA030,
    kShininess = 0xA040,
    kShininessStrength = 0xA041,
    kTransparency = 0xA050,
    kSelfIllumination = 0xA084,
    kShading = 0xA100,
    kTextureMap = 0xA200,
    kSpecularMap = 0xA204,
    kOpacityMap = 0xA210,
    kBumpMap = 0xA230,
    kMapFile = 0xA300,
    kColorFloat = 0x0010,
    kColor24 = 0x0011,
    kColor24Gamma = 0x0012,
    kColorFloatGamma = 0x0013,
    kPercentInt = 0x0030,
    kPercentFloat = 0x0031,
};

QString failure(const QString& filePath, const QString& why)
{
    return QCoreApplication::translate("Model3D", "The 3DS model %1 could not be read: %2")
        .arg(QFileInfo(filePath).fileName(), why);
}

class Reader
{
public:
    Reader(const QByteArray& data, const QString& filePath)
        : m_data(data), m_dir(QFileInfo(filePath).absolutePath()) {}

    bool read(Mesh* mesh)
    {
        m_mesh = mesh;
        if (m_data.size() < 6 || u16(0) != kMain) return false;
        const qint64 end = qMin<qint64>(m_data.size(), u32(2));
        // The keyframer's nodes first, so objects land in its hierarchy.
        chunks(6, end, [this](quint16 id, qint64 at, qint64 stop) {
            if (id == kKeyframer) readHierarchy(at, stop);
        });
        chunks(6, end, [this](quint16 id, qint64 at, qint64 stop) {
            if (id == kEditor) {
                // Materials first: faces name them.
                chunks(at, stop, [this](quint16 inner, qint64 from, qint64 to) {
                    if (inner == kMaterial) readMaterial(from, to);
                    else if (inner == kAmbientLight) m_ambient = colour(from, to, m_ambient);
                });
                chunks(at, stop, [this](quint16 inner, qint64 from, qint64 to) {
                    if (inner == kObject) readObject(from, to);
                });
            }
        });
        // Assimp adds the scene's ambient light to each material's ambient
        // colour, and puts faces without a material on a default one: an
        // existing grey one named "default", else a grey of 0.3.
        for (Material& material : m_mesh->materials) {
            material.ambientColor = QColor::fromRgbF(qMin(1.0f, material.ambientColor.redF() + m_ambient.redF()),
                                                     qMin(1.0f, material.ambientColor.greenF() + m_ambient.greenF()),
                                                     qMin(1.0f, material.ambientColor.blueF() + m_ambient.blueF()));
        }
        bool bare = false;
        for (const Triangle& triangle : std::as_const(m_mesh->triangles)) bare = bare || triangle.material < 0;
        if (bare) {
            int fallback = -1;
            for (int i = 0; i < m_mesh->materials.size() && fallback < 0; ++i) {
                const Material& material = m_mesh->materials.at(i);
                const QColor d = material.diffuseColor;
                if (material.name.toLower().contains(QLatin1String("default")) && d.red() == d.green()
                    && d.green() == d.blue() && material.diffuseMap.isEmpty() && material.specularMap.isEmpty()
                    && material.bumpMap.isEmpty())
                    fallback = i;
            }
            if (fallback < 0) {
                Material material = assimpMaterial();
                material.name = QStringLiteral("%%%DEFAULT");
                material.diffuseColor = QColor::fromRgbF(0.3f, 0.3f, 0.3f);
                material.ambientColor = QColor::fromRgbF(m_ambient.redF(), m_ambient.greenF(), m_ambient.blueF());
                fallback = m_mesh->materials.size();
                m_mesh->materials.append(material);
            }
            for (Triangle& triangle : m_mesh->triangles)
                if (triangle.material < 0) triangle.material = fallback;
        }
        return true;
    }

private:
    quint16 u16(qint64 at) const
    {
        return at + 2 <= m_data.size() ? qFromLittleEndian<quint16>(m_data.constData() + at) : 0;
    }
    quint32 u32(qint64 at) const
    {
        return at + 4 <= m_data.size() ? qFromLittleEndian<quint32>(m_data.constData() + at) : 0;
    }
    float f32(qint64 at) const
    {
        if (at + 4 > m_data.size()) return 0.0f;
        const quint32 bits = qFromLittleEndian<quint32>(m_data.constData() + at);
        float value;
        std::memcpy(&value, &bits, 4);
        return value;
    }
    // A zero-terminated name, and where it ends.
    QString name(qint64 at, qint64 stop, qint64* after) const
    {
        qint64 end = at;
        while (end < stop && m_data.at(end) != '\0') ++end;
        *after = qMin(stop, end + 1);
        return QString::fromLatin1(m_data.constData() + at, int(end - at));
    }

    // Each chunk in [begin, end): its id, where its data starts and where
    // it ends.
    void chunks(qint64 begin, qint64 end, const std::function<void(quint16, qint64, qint64)>& each) const
    {
        qint64 at = begin;
        while (at + 6 <= end) {
            const quint16 id = u16(at);
            const quint32 length = u32(at + 2);
            if (length < 6 || at + length > end) return;   // damaged: stop here
            each(id, at + 6, at + length);
            at += length;
        }
    }

    QColor colour(qint64 begin, qint64 end, const QColor& fallback) const
    {
        QColor result = fallback;
        bool linear = false;
        chunks(begin, end, [&](quint16 id, qint64 at, qint64) {
            if ((id == kColorFloat || id == kColorFloatGamma) && !linear) {
                result = QColor::fromRgbF(qBound(0.0f, f32(at), 1.0f), qBound(0.0f, f32(at + 4), 1.0f),
                                          qBound(0.0f, f32(at + 8), 1.0f));
                linear = id == kColorFloat;
            } else if ((id == kColor24 || id == kColor24Gamma) && !linear) {
                const auto* p = reinterpret_cast<const uchar*>(m_data.constData()) + at;
                if (at + 3 <= m_data.size()) result = QColor(p[0], p[1], p[2]);
                linear = id == kColor24;
            }
        });
        return result;
    }

    // A percentage as a fraction: whole percent in an integer chunk, a
    // fraction already in a float one (as Assimp and lib3ds read them).
    double fraction(qint64 begin, qint64 end, double fallback) const
    {
        double result = fallback;
        chunks(begin, end, [&](quint16 id, qint64 at, qint64) {
            if (id == kPercentInt) result = u16(at) / 100.0;
            else if (id == kPercentFloat) result = f32(at);
        });
        return result;
    }

    // Assimp's material before the file says anything (D3DS::Material):
    // diffuse 0.6, no specular, ambient or emission.
    static Material assimpMaterial()
    {
        Material material;
        material.diffuseColor = QColor::fromRgbF(0.6f, 0.6f, 0.6f);
        material.specularColor = QColor(0, 0, 0);
        material.ambientColor = QColor(0, 0, 0);
        material.emissiveColor = QColor(0, 0, 0);
        material.shininess = 1.0;
        return material;
    }

    // A map's file beside the model; 3D Studio wrote short names in any case.
    QString mapFile(qint64 begin, qint64 end) const
    {
        QString file;
        chunks(begin, end, [&](quint16 id, qint64 at, qint64 stop) {
            qint64 after = 0;
            if (id == kMapFile) file = name(at, stop, &after);
        });
        if (file.isEmpty()) return file;
        const QString plain = QFileInfo(QString(file).replace('\\', '/')).fileName();
        const QDir dir(m_dir);
        if (dir.exists(plain)) return dir.filePath(plain);
        for (const QString& entry : dir.entryList(QDir::Files))
            if (entry.compare(plain, Qt::CaseInsensitive) == 0) return dir.filePath(entry);
        return dir.filePath(plain);
    }

    void readMaterial(qint64 begin, qint64 end)
    {
        Material material = assimpMaterial();
        double exponent = 0.0, strength = 1.0;
        int shading = 2;   // Gouraud
        chunks(begin, end, [&](quint16 id, qint64 at, qint64 stop) {
            qint64 after = 0;
            switch (id) {
            case kMaterialName: material.name = name(at, stop, &after); break;
            case kAmbient: material.ambientColor = colour(at, stop, material.ambientColor); break;
            case kDiffuse: material.diffuseColor = colour(at, stop, material.diffuseColor); break;
            case kSpecular: material.specularColor = colour(at, stop, material.specularColor); break;
            // The shininess percentage is Assimp's exponent, its strength a
            // fraction.
            case kShininess: exponent = fraction(at, stop, 0.0) * 100.0; break;
            case kShininessStrength: strength = fraction(at, stop, 0.0); break;
            case kTransparency: material.opacity = qBound(0.0, 1.0 - fraction(at, stop, 0.0), 1.0); break;
            case kSelfIllumination: {
                const float glow = float(qBound(0.0, fraction(at, stop, 0.0), 1.0));
                material.emissiveColor = QColor::fromRgbF(glow, glow, glow);
                break;
            }
            case kShading: shading = u16(at); break;
            case kTextureMap: material.diffuseMap = mapFile(at, stop); break;
            case kSpecularMap: material.specularMap = mapFile(at, stop); break;
            case kBumpMap: material.bumpMap = mapFile(at, stop); break;
            default: break;
            }
        });
        // A shininess only for Phong or Metal shading with both numbers; the
        // reference multiplies them and, without one, has no highlight.
        if ((shading == 3 || shading == 4) && exponent > 0.0 && strength > 0.0) {
            material.shininess = qMax(1.0, exponent * strength);
        } else {
            material.shininess = 1.0;
            material.specularColor = QColor(0, 0, 0);
        }
        m_materials.insert(material.name, m_mesh->materials.size());
        m_mesh->materials.append(material);
    }

    // The keyframer's object nodes as nodes, nested as Assimp's 3DS loader
    // nests them: each header names its parent by hierarchy position (its
    // NODE_ID, else its place among the nodes), -1 for none. A dummy
    // ("$$$DUMMY") takes its instance name; the others are the objects of
    // the same name.
    void readHierarchy(qint64 begin, qint64 end)
    {
        QHash<int, int> byPosition;   // hierarchy position -> node
        int order = 0;
        chunks(begin, end, [&](quint16 id, qint64 at, qint64 stop) {
            if (id != kObjectNode) return;
            QString objectName, instance;
            int parent = -1, position = order++;
            chunks(at, stop, [&](quint16 inner, qint64 from, qint64 to) {
                qint64 after = 0;
                if (inner == kNodeHeader) {
                    objectName = name(from, to, &after);
                    if (after + 6 <= to) parent = int(qint16(u16(after + 4)));
                } else if (inner == kInstanceName) {
                    instance = name(from, to, &after);
                } else if (inner == kNodeId) {
                    position = u16(from);
                }
            });
            const bool dummy = objectName == QLatin1String("$$$DUMMY");
            const QString label = dummy ? (instance.isEmpty() ? QStringLiteral("Dummy") : instance) : objectName;
            if (label.isEmpty() || (!dummy && m_objectNodes.contains(label))) return;
            const int node = m_mesh->addNode(label, parent >= 0 ? byPosition.value(parent, -1) : -1);
            byPosition.insert(position, node);
            if (!dummy) m_objectNodes.insert(label, node);
        });
    }

    void readObject(qint64 begin, qint64 end)
    {
        qint64 at = begin;
        const QString objectName = name(begin, end, &at);
        chunks(at, end, [&](quint16 id, qint64 from, qint64 to) {
            if (id == kTriangleMesh) readTriangles(objectName, from, to);
        });
    }

    void readTriangles(const QString& objectName, qint64 begin, qint64 end)
    {
        const int base = m_mesh->positions.size();
        const int uvBase = m_mesh->uvs.size();
        int points = 0, coordinates = 0;
        QVector<std::array<int, 3>> faces;
        QVector<int> faceMaterials;
        QVector<quint32> faceGroups;   // smoothing groups
        chunks(begin, end, [&](quint16 id, qint64 at, qint64 stop) {
            if (id == kPoints) {
                const int count = u16(at);
                if (at + 2 + qint64(count) * 12 > stop) return;
                for (int i = 0; i < count; ++i) {
                    const qint64 p = at + 2 + qint64(i) * 12;
                    m_mesh->positions.append(QVector3D(f32(p), f32(p + 4), f32(p + 8)));
                }
                points = count;
            } else if (id == kTextureCoordinates) {
                const int count = u16(at);
                if (at + 2 + qint64(count) * 8 > stop) return;
                for (int i = 0; i < count; ++i) {
                    const qint64 p = at + 2 + qint64(i) * 8;
                    m_mesh->uvs.append(QVector2D(f32(p), f32(p + 4)));
                }
                coordinates = count;
            } else if (id == kFaces) {
                const int count = u16(at);
                const qint64 list = at + 2 + qint64(count) * 8;
                if (list > stop) return;
                faceMaterials.fill(-1, count);
                for (int i = 0; i < count; ++i) {
                    const qint64 p = at + 2 + qint64(i) * 8;
                    faces.append({int(u16(p)), int(u16(p + 2)), int(u16(p + 4))});
                }
                // Which faces each material covers, and their smoothing groups.
                chunks(list, stop, [&](quint16 inner, qint64 from, qint64 to) {
                    if (inner == kFaceSmoothing) {
                        for (int f = 0; f < count && from + qint64(f) * 4 + 4 <= to; ++f)
                            faceGroups.append(u32(from + qint64(f) * 4));
                        return;
                    }
                    if (inner != kFaceMaterial) return;
                    qint64 after = 0;
                    const QString material = name(from, to, &after);
                    const int index = m_materials.value(material, -1);
                    const int covered = u16(after);
                    for (int k = 0; k < covered && after + 2 + qint64(k) * 2 + 2 <= to; ++k) {
                        const int face = u16(after + 2 + qint64(k) * 2);
                        if (face < faceMaterials.size()) faceMaterials[face] = index;
                    }
                });
            }
        });
        if (faces.isEmpty() || points == 0) return;
        const QString label = objectName.isEmpty() ? QStringLiteral("Object") : objectName;
        const int node = m_objectNodes.contains(label) ? m_objectNodes.value(label) : m_mesh->addNode(label);
        const int firstTriangle = m_mesh->triangles.size();
        QVector<quint32> groups;
        for (int f = 0; f < faces.size(); ++f) {
            Triangle triangle;
            bool valid = true;
            for (int c = 0; c < 3; ++c) {
                const int index = faces.at(f)[c];
                valid = valid && index < points;
                triangle.position[c] = base + index;
                if (index < coordinates) triangle.uv[c] = uvBase + index;
            }
            if (!valid) continue;
            triangle.material = faceMaterials.value(f, -1);
            triangle.node = node;
            m_mesh->triangles.append(triangle);
            groups.append(faceGroups.value(f, 0));
        }
        smooth(firstTriangle, groups);
    }

    // The object's normals as Assimp's 3DS loader makes them
    // (ComputeNormalsWithSmoothingsGroups with SGSpatialSort): at each
    // corner, the sum of the (area-weighted) normals of the object's faces
    // at the same place whose smoothing groups share a bit with this face's
    // or are none, or of all of them for a face in no group.
    void smooth(int first, const QVector<quint32>& groups)
    {
        const int count = groups.size();
        QVector<QVector3D> faces(count);
        QHash<QByteArray, QVector<int>> meeting;   // place -> faces there
        const auto key = [&](int position) {
            const QVector3D p = m_mesh->positions.value(position);
            const float place[3] = {p.x(), p.y(), p.z()};
            return QByteArray(reinterpret_cast<const char*>(place), int(sizeof(place)));
        };
        for (int f = 0; f < count; ++f) {
            const Triangle& triangle = m_mesh->triangles.at(first + f);
            const QVector3D a = m_mesh->positions.value(triangle.position[0]);
            faces[f] = QVector3D::crossProduct(m_mesh->positions.value(triangle.position[1]) - a,
                                               m_mesh->positions.value(triangle.position[2]) - a);
            for (int c = 0; c < 3; ++c) {
                QVector<int>& list = meeting[key(triangle.position[c])];
                if (list.isEmpty() || list.last() != f) list.append(f);
            }
        }
        for (int f = 0; f < count; ++f) {
            Triangle& triangle = m_mesh->triangles[first + f];
            const quint32 group = groups.at(f);
            for (int c = 0; c < 3; ++c) {
                QVector3D sum;
                for (int other : meeting.value(key(triangle.position[c]))) {
                    const quint32 theirs = groups.at(other);
                    if (group == 0 || theirs == 0 || (theirs & group)) sum += faces.at(other);
                }
                triangle.normal[c] = m_mesh->normals.size();
                m_mesh->normals.append(sum.isNull() ? faces.at(f).normalized() : sum.normalized());
            }
        }
    }

    const QByteArray& m_data;
    QString m_dir;
    Mesh* m_mesh = nullptr;
    QHash<QString, int> m_materials;
    QColor m_ambient = QColor(0, 0, 0);   // the scene's ambient light
    QHash<QString, int> m_objectNodes;     // object -> its keyframer node
};

} // namespace

LoadResult load3ds(const QString& filePath)
{
    LoadResult result;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        result.message = failure(filePath, QCoreApplication::translate("Model3D", "the file could not be opened"));
        return result;
    }
    const QByteArray data = file.readAll();
    Reader reader(data, filePath);
    if (!reader.read(&result.mesh)) {
        result.message = failure(filePath, QCoreApplication::translate("Model3D", "it is not a 3DS file"));
        return result;
    }
    if (result.mesh.isEmpty()) {
        result.message = failure(filePath, QCoreApplication::translate("Model3D", "it has no triangles"));
        return result;
    }
    result.ok = true;
    return result;
}

} // namespace model3d
} // namespace openvegas
