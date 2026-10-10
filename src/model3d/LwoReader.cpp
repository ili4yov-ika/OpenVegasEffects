#include "model3d/LwoReader.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QtEndian>

#include <cmath>
#include <cstring>

namespace openvegas {
namespace model3d {

namespace {

QString failure(const QString& filePath, const QString& why)
{
    return QCoreApplication::translate("Model3D", "The LightWave model %1 could not be read: %2")
        .arg(QFileInfo(filePath).fileName(), why);
}

constexpr quint32 id4(const char (&text)[5])
{
    return (quint32(uchar(text[0])) << 24) | (quint32(uchar(text[1])) << 16) | (quint32(uchar(text[2])) << 8)
           | quint32(uchar(text[3]));
}

class Reader
{
public:
    Reader(const QByteArray& data, const QString& filePath)
        : m_data(data), m_dir(QFileInfo(filePath).absolutePath()) {}

    // False when it is not an LWO2 or LWOB file.
    bool read(Mesh* mesh)
    {
        m_mesh = mesh;
        if (m_data.size() < 12 || u32(0) != id4("FORM")) return false;
        m_lwob = u32(8) == id4("LWOB");
        if (!m_lwob && u32(8) != id4("LWO2")) return false;
        const qint64 end = qMin<qint64>(m_data.size(), 8 + qint64(u32(4)));
        qint64 at = 12;
        while (at + 8 <= end) {
            const quint32 id = u32(at);
            const qint64 size = u32(at + 4);
            const qint64 data = at + 8;
            const qint64 stop = qMin(end, data + size);
            if (m_lwob) {
                // LightWave 5: one layer, surfaces named in SRFS and counted
                // from 1 by the polygons.
                switch (id) {
                case id4("PNTS"): readPoints(data, stop); break;
                case id4("SRFS"): if (m_tags.isEmpty()) readTags(data, stop); break;
                case id4("POLS"): readOldPolygons(data, stop); break;
                case id4("SURF"): readOldSurface(data, stop); break;
                default: break;
                }
                at = data + size + (size & 1);
                continue;
            }
            switch (id) {
            case id4("TAGS"): readTags(data, stop); break;
            case id4("LAYR"): finishLayer(); readLayer(data, stop); break;
            case id4("PNTS"): readPoints(data, stop); break;
            case id4("VMAP"): readMap(data, stop, false); break;
            case id4("VMAD"): readMap(data, stop, true); break;
            case id4("POLS"): readPolygons(data, stop); break;
            case id4("PTAG"): readPolygonTags(data, stop); break;
            case id4("SURF"): readSurface(data, stop); break;
            case id4("CLIP"): readClip(data, stop); break;
            default: break;
            }
            at = data + size + (size & 1);
        }
        finishLayer();
        resolve();
        return true;
    }

private:
    struct Polygon
    {
        QVector<int> points;
        int tag = -1;
    };
    struct Layer
    {
        bool open = false;
        int node = -1;
        int base = 0;                       // first of its points in the mesh
        int points = 0;
        QVector<QVector2D> pointUvs;
        QVector<bool> hasPointUv;
        QHash<qint64, QVector2D> cornerUvs; // (polygon << 32) | point
        QString uvName;
        QVector<Polygon> polygons;
    };
    // A surface's values, with Assimp's defaults (LWO::Surface).
    struct Surface
    {
        QVector3D colour{0.78431f, 0.78431f, 0.78431f};
        float diffuse = 1.0f;
        float specular = 0.0f;
        float gloss = 0.4f;
        float luminosity = 0.0f;
        float transparency = 0.0f;
        float highlights = 0.0f;   // colour highlights
        float smoothing = 0.0f;    // radians; none, faceted
    };
    // A texture of a surface waiting for its clip, which may come later.
    struct PendingMap
    {
        QString surface;
        quint32 channel = 0;
        quint32 clip = 0;
    };

    quint16 u16(qint64 at) const
    {
        return at + 2 <= m_data.size() ? qFromBigEndian<quint16>(m_data.constData() + at) : 0;
    }
    quint32 u32(qint64 at) const
    {
        return at + 4 <= m_data.size() ? qFromBigEndian<quint32>(m_data.constData() + at) : 0;
    }
    float f32(qint64 at) const
    {
        const quint32 bits = u32(at);
        float value;
        std::memcpy(&value, &bits, 4);
        return value;
    }
    // A variable-length index: two bytes, or four when the first is 0xFF.
    quint32 vx(qint64& at) const
    {
        if (at < m_data.size() && uchar(m_data.at(at)) == 0xFF) {
            const quint32 value = u32(at) & 0x00FFFFFF;
            at += 4;
            return value;
        }
        const quint32 value = u16(at);
        at += 2;
        return value;
    }
    // A zero-terminated string padded to an even length.
    QString string(qint64& at, qint64 stop) const
    {
        const qint64 start = at;
        qint64 end = at;
        while (end < stop && m_data.at(end) != '\0') ++end;
        const QString text = QString::fromLatin1(m_data.constData() + at, int(end - at));
        at = end + 1;
        if ((at - start) & 1) ++at;   // to an even length
        return text;
    }

    void readTags(qint64 at, qint64 stop)
    {
        while (at < stop) m_tags.append(string(at, stop));
    }

    void openLayer(const QString& name, int parent)
    {
        m_layer = Layer();
        m_layer.open = true;
        int parentNode = -1;
        if (parent >= 0) parentNode = m_layerNodes.value(parent, -1);
        m_layer.node = m_mesh->addNode(name, parentNode);
        m_layer.base = m_mesh->positions.size();
    }

    void readLayer(qint64 at, qint64 stop)
    {
        const int number = u16(at);
        qint64 cursor = at + 4 + 12;   // number, flags, pivot
        QString name = string(cursor, stop);
        const int parent = cursor + 2 <= stop ? int(qint16(u16(cursor))) : -1;
        if (name.isEmpty()) name = QStringLiteral("Layer %1").arg(number + 1);
        openLayer(name, parent);
        m_layerNodes.insert(number, m_layer.node);
    }

    void readPoints(qint64 at, qint64 stop)
    {
        if (!m_layer.open) openLayer(QStringLiteral("Layer 1"), -1);
        // LightWave's space is left-handed: z points away.
        for (qint64 p = at; p + 12 <= stop; p += 12) {
            m_mesh->positions.append(QVector3D(f32(p), f32(p + 4), -f32(p + 8)));
            ++m_layer.points;
        }
        m_layer.pointUvs.resize(m_layer.points);
        m_layer.hasPointUv.resize(m_layer.points);
    }

    void readMap(qint64 at, qint64 stop, bool perPolygon)
    {
        if (!m_layer.open || u32(at) != id4("TXUV")) return;
        const int dimension = u16(at + 4);
        qint64 cursor = at + 6;
        const QString name = string(cursor, stop);
        // The first UV map only, per point and broken per polygon.
        if (m_layer.uvName.isEmpty()) m_layer.uvName = name;
        if (name != m_layer.uvName || dimension < 2) return;
        while (cursor < stop) {
            const quint32 point = vx(cursor);
            const quint32 polygon = perPolygon ? vx(cursor) : 0;
            if (cursor + qint64(dimension) * 4 > stop) break;
            const QVector2D uv(f32(cursor), f32(cursor + 4));
            cursor += qint64(dimension) * 4;
            if (point >= quint32(m_layer.points)) continue;
            if (perPolygon) {
                m_layer.cornerUvs.insert((qint64(polygon) << 32) | point, uv);
            } else {
                m_layer.pointUvs[int(point)] = uv;
                m_layer.hasPointUv[int(point)] = true;
            }
        }
    }

    void readPolygons(qint64 at, qint64 stop)
    {
        if (!m_layer.open) return;
        const quint32 type = u32(at);
        // Faces, and subdivision patches drawn by their cage.
        if (type != id4("FACE") && type != id4("PTCH") && type != id4("SUBD")) return;
        // Polygon indices in PTAG and VMAD count from the start of the list.
        m_layer.polygons.clear();
        qint64 cursor = at + 4;
        while (cursor + 2 <= stop) {
            const int count = u16(cursor) & 0x03FF;
            cursor += 2;
            Polygon polygon;
            for (int i = 0; i < count && cursor < stop; ++i) polygon.points.append(int(vx(cursor)));
            m_layer.polygons.append(polygon);
        }
    }

    // LWOB polygons: a count, that many two-byte points and the surface, from
    // 1. A negative surface is followed by the number of that polygon's
    // detail polygons, which come next written the same way.
    void readOldPolygons(qint64 at, qint64 stop)
    {
        if (!m_layer.open) openLayer(QStringLiteral("Layer 1"), -1);
        qint64 cursor = at;
        while (cursor + 2 <= stop) {
            const int count = u16(cursor);
            cursor += 2;
            Polygon polygon;
            for (int i = 0; i < count && cursor + 2 <= stop; ++i, cursor += 2) polygon.points.append(u16(cursor));
            if (cursor + 2 > stop) break;
            int surface = int(qint16(u16(cursor)));
            cursor += 2;
            if (surface < 0) {
                surface = -surface;
                cursor += 2;   // the detail polygons' count
            }
            polygon.tag = surface - 1;
            m_layer.polygons.append(polygon);
        }
    }

    void readPolygonTags(qint64 at, qint64 stop)
    {
        if (!m_layer.open || u32(at) != id4("SURF")) return;
        qint64 cursor = at + 4;
        while (cursor + 4 <= stop) {
            const quint32 polygon = vx(cursor);
            const int tag = u16(cursor);
            cursor += 2;
            if (polygon < quint32(m_layer.polygons.size())) m_layer.polygons[int(polygon)].tag = tag;
        }
    }

    // The layer's polygons, fanned into triangles, once its maps and tags
    // are all read.
    void finishLayer()
    {
        if (!m_layer.open) return;
        for (int p = 0; p < m_layer.polygons.size(); ++p) {
            const Polygon& polygon = m_layer.polygons.at(p);
            if (polygon.points.size() < 3) continue;
            QVector<int> positions, uvs;
            bool valid = true;
            for (int point : polygon.points) {
                valid = valid && point >= 0 && point < m_layer.points;
                if (!valid) break;
                positions.append(m_layer.base + point);
                const auto corner = m_layer.cornerUvs.constFind((qint64(p) << 32) | point);
                if (corner != m_layer.cornerUvs.constEnd()) {
                    uvs.append(m_mesh->uvs.size());
                    m_mesh->uvs.append(corner.value());
                } else if (m_layer.hasPointUv.at(point)) {
                    uvs.append(m_mesh->uvs.size());
                    m_mesh->uvs.append(m_layer.pointUvs.at(point));
                } else {
                    uvs.append(-1);
                }
            }
            if (!valid) continue;
            const QString surface = polygon.tag >= 0 && polygon.tag < m_tags.size() ? m_tags.at(polygon.tag) : QString();
            // Mirrored out of the left-handed space, so turned the other way
            // round to keep facing out.
            for (int c = 1; c + 1 < positions.size(); ++c) {
                const int corners[3] = {0, c + 1, c};
                Triangle triangle;
                for (int i = 0; i < 3; ++i) {
                    triangle.position[i] = positions.at(corners[i]);
                    triangle.uv[i] = uvs.at(corners[i]);
                }
                triangle.node = m_layer.node;
                m_mesh->triangles.append(triangle);
                m_surfaces.append(surface);
                m_triangleLayers.append(m_layer.node);
            }
        }
        m_layer = Layer();
    }

    void readSurface(qint64 at, qint64 stop)
    {
        qint64 cursor = at;
        const QString name = string(cursor, stop);
        string(cursor, stop);   // the surface it is derived from
        Surface surface;
        while (cursor + 6 <= stop) {
            const quint32 id = u32(cursor);
            const qint64 size = u16(cursor + 4);
            const qint64 data = cursor + 6;
            const qint64 end = qMin(stop, data + size);
            switch (id) {
            case id4("COLR"): surface.colour = QVector3D(f32(data), f32(data + 4), f32(data + 8)); break;
            case id4("DIFF"): surface.diffuse = f32(data); break;
            case id4("LUMI"): surface.luminosity = f32(data); break;
            case id4("SPEC"): surface.specular = f32(data); break;
            case id4("GLOS"): surface.gloss = f32(data); break;
            case id4("TRAN"): surface.transparency = f32(data); break;
            case id4("CLRH"): surface.highlights = f32(data); break;
            case id4("SMAN"): surface.smoothing = std::fabs(f32(data)); break;
            case id4("BLOK"): readBlock(name, data, end); break;
            default: break;
            }
            cursor = data + size + (size & 1);
        }
        addSurface(name, surface);
    }

    // An LWOB surface: one-byte colour, two-byte percentages (of 255), the
    // glossiness as LightWave 5's exponent and the smoothing flag.
    void readOldSurface(qint64 at, qint64 stop)
    {
        qint64 cursor = at;
        const QString name = string(cursor, stop);
        Surface surface;
        while (cursor + 6 <= stop) {
            const quint32 id = u32(cursor);
            const qint64 size = u16(cursor + 4);
            const qint64 data = cursor + 6;
            switch (id) {
            case id4("COLR"):
                if (size >= 3)
                    surface.colour = QVector3D(uchar(m_data.at(data)) / 255.0f, uchar(m_data.at(data + 1)) / 255.0f,
                                               uchar(m_data.at(data + 2)) / 255.0f);
                break;
            case id4("FLAG"): {
                const int flags = u16(data);
                if (flags & 0x4) surface.smoothing = 1.56207f;   // smoothing, about 89.5 degrees
                if (flags & 0x8) surface.highlights = 1.0f;      // colour highlights
                break;
            }
            case id4("LUMI"): surface.luminosity = u16(data) / 255.0f; break;
            case id4("DIFF"): surface.diffuse = u16(data) / 255.0f; break;
            case id4("SPEC"): surface.specular = u16(data) / 255.0f; break;
            case id4("TRAN"): surface.transparency = u16(data) / 255.0f; break;
            case id4("GLOS"): surface.gloss = float(u16(data)); break;
            case id4("SMAN"): if (size >= 4) surface.smoothing = std::fabs(f32(data)); break;
            default: break;
            }
            cursor = data + size + (size & 1);
        }
        addSurface(name, surface);
    }

    // A surface as the reference's material: Assimp's LWO conversion
    // (LWOImporter::ConvertMaterial), then how the reference reads it
    // (AssimpModel3DImporter: shininess times its strength, at least 1, and
    // no highlight without a shininess).
    void addSurface(const QString& name, const Surface& surface)
    {
        const auto rgb = [](const QVector3D& v) {
            return QColor::fromRgbF(qBound(0.0f, v.x(), 1.0f), qBound(0.0f, v.y(), 1.0f), qBound(0.0f, v.z(), 1.0f));
        };
        Material material;
        material.name = name;
        material.diffuseColor = rgb(surface.colour * surface.diffuse);
        material.ambientColor = rgb(surface.colour * 0.2f);
        if (surface.specular != 0.0f && surface.gloss != 0.0f) {
            double exponent;
            if (!m_lwob) exponent = std::pow(surface.gloss * 10.0 + 2.0, 2.0);
            else if (surface.gloss <= 16.0f) exponent = 6.0;
            else if (surface.gloss <= 64.0f) exponent = 20.0;
            else if (surface.gloss <= 256.0f) exponent = 50.0;
            else exponent = 80.0;
            material.shininess = qMax(1.0, exponent * surface.specular);
            const QVector3D white(1, 1, 1);
            material.specularColor = rgb(white + (surface.colour - white) * surface.highlights);
        } else {
            material.shininess = 1.0;
            material.specularColor = QColor(0, 0, 0);
        }
        const float glow = surface.luminosity * 0.8f;
        material.emissiveColor = rgb(QVector3D(glow, glow, glow));
        material.opacity = qBound(0.0, 1.0 - double(surface.transparency), 1.0);
        m_materials.insert(name, m_mesh->materials.size());
        m_mesh->materials.append(material);
        m_smoothing.insert(name, surface.smoothing);
    }

    // A texture block: kept when it is an image mapped by UVs onto the
    // colour, bump, specular or luminosity channel.
    void readBlock(const QString& surface, qint64 at, qint64 stop)
    {
        if (at + 6 > stop || u32(at) != id4("IMAP")) return;
        quint32 channel = id4("COLR");
        int projection = -1;
        quint32 clip = 0;
        bool hasClip = false;
        qint64 cursor = at;
        while (cursor + 6 <= stop) {
            const quint32 id = u32(cursor);
            const qint64 size = u16(cursor + 4);
            const qint64 data = cursor + 6;
            const qint64 end = qMin(stop, data + size);
            if (id == id4("IMAP")) {
                // Its header: an ordinal, then the channel among others.
                qint64 inner = data;
                string(inner, end);
                while (inner + 6 <= end) {
                    const quint32 sub = u32(inner);
                    const qint64 subSize = u16(inner + 4);
                    if (sub == id4("CHAN")) channel = u32(inner + 6);
                    inner += 6 + subSize + (subSize & 1);
                }
            } else if (id == id4("PROJ")) {
                projection = u16(data);
            } else if (id == id4("IMAG")) {
                qint64 index = data;
                clip = vx(index);
                hasClip = true;
            }
            cursor = data + size + (size & 1);
        }
        if (projection == 5 && hasClip) m_pending.append({surface, channel, clip});
    }

    void readClip(qint64 at, qint64 stop)
    {
        const quint32 index = u32(at);
        qint64 cursor = at + 4;
        while (cursor + 6 <= stop) {
            const quint32 id = u32(cursor);
            const qint64 size = u16(cursor + 4);
            const qint64 data = cursor + 6;
            if (id == id4("STIL")) {
                qint64 name = data;
                m_clips.insert(index, string(name, qMin(stop, data + size)));
            }
            cursor = data + size + (size & 1);
        }
    }

    // A clip's file beside the object, where it says, or by name beside it.
    QString clipFile(const QString& stored) const
    {
        QString path = stored;
        path.replace('\\', '/');
        // LightWave writes "Device:dir/file" for drive paths.
        if (path.size() > 2 && path.at(1) == QLatin1Char(':') && path.at(2) != QLatin1Char('/'))
            path.insert(2, QLatin1Char('/'));
        const QDir dir(m_dir);
        for (const QString& candidate : {dir.filePath(path), path, dir.filePath(QFileInfo(path).fileName())}) {
            if (QFileInfo(candidate).isFile()) return candidate;
        }
        return dir.filePath(path);
    }

    // Surfaces and clips can come after the geometry that names them.
    void resolve()
    {
        QHash<int, QString> diffuseOnly;   // material -> its diffuse-channel image
        for (int t = 0; t < m_mesh->triangles.size() && t < m_surfaces.size(); ++t)
            m_mesh->triangles[t].material = m_materials.value(m_surfaces.at(t), -1);
        for (const PendingMap& map : std::as_const(m_pending)) {
            const int index = m_materials.value(map.surface, -1);
            if (index < 0 || !m_clips.contains(map.clip)) continue;
            Material& material = m_mesh->materials[index];
            const QString file = clipFile(m_clips.value(map.clip));
            // Colour, else diffuse, as Assimp's diffuse texture; specular;
            // bump as its height map. The reference reads no others.
            if (map.channel == id4("COLR")) material.diffuseMap = file;
            else if (map.channel == id4("DIFF") && material.diffuseMap.isEmpty()) diffuseOnly.insert(index, file);
            else if (map.channel == id4("BUMP")) material.bumpMap = file;
            else if (map.channel == id4("SPEC")) material.specularMap = file;
        }
        for (auto it = diffuseOnly.cbegin(); it != diffuseOnly.cend(); ++it)
            if (m_mesh->materials[it.key()].diffuseMap.isEmpty()) m_mesh->materials[it.key()].diffuseMap = it.value();
        makeNormals();
    }

    // Normals as Assimp's LWO loader makes them (LWOImporter::ComputeNormals):
    // each face's own, or, where its surface has a smoothing angle, the mean
    // of the faces of the same surface and layer meeting at the point whose
    // normals are within that angle of it (all of them from 3 radians up).
    void makeNormals()
    {
        const int count = m_mesh->triangles.size();
        QVector<QVector3D> faces(count);
        for (int t = 0; t < count; ++t) {
            const Triangle& triangle = m_mesh->triangles.at(t);
            const QVector3D a = m_mesh->positions.value(triangle.position[0]);
            const QVector3D b = m_mesh->positions.value(triangle.position[1]);
            const QVector3D c = m_mesh->positions.value(triangle.position[2]);
            faces[t] = QVector3D::crossProduct(b - a, c - a).normalized();
        }
        // The faces at each point (by place), per surface and layer.
        QHash<QString, int> surfaceIds;
        for (const QString& surface : std::as_const(m_surfaces))
            if (!surfaceIds.contains(surface)) surfaceIds.insert(surface, surfaceIds.size());
        QHash<QByteArray, QVector<int>> meeting;
        const auto key = [&](int t, int corner) {
            const QVector3D p = m_mesh->positions.value(m_mesh->triangles.at(t).position[corner]);
            const qint32 values[2] = {qint32(surfaceIds.value(m_surfaces.value(t))), qint32(m_triangleLayers.value(t))};
            const float place[3] = {p.x(), p.y(), p.z()};
            QByteArray bytes(int(sizeof(values) + sizeof(place)), Qt::Uninitialized);
            std::memcpy(bytes.data(), values, sizeof(values));
            std::memcpy(bytes.data() + sizeof(values), place, sizeof(place));
            return bytes;
        };
        for (int t = 0; t < count; ++t)
            if (m_smoothing.value(m_surfaces.value(t), 0.0f) > 0.0f)
                for (int corner = 0; corner < 3; ++corner) meeting[key(t, corner)].append(t);
        for (int t = 0; t < count; ++t) {
            Triangle& triangle = m_mesh->triangles[t];
            const float angle = m_smoothing.value(m_surfaces.value(t), 0.0f);
            if (angle <= 0.0f) {
                const int normal = m_mesh->normals.size();
                m_mesh->normals.append(faces.at(t));
                for (int corner = 0; corner < 3; ++corner) triangle.normal[corner] = normal;
                continue;
            }
            const float limit = std::cos(angle);
            for (int corner = 0; corner < 3; ++corner) {
                QVector3D sum;
                for (int other : meeting.value(key(t, corner)))
                    if (angle >= 3.0f || QVector3D::dotProduct(faces.at(other), faces.at(t)) >= limit) sum += faces.at(other);
                triangle.normal[corner] = m_mesh->normals.size();
                m_mesh->normals.append(sum.isNull() ? faces.at(t) : sum.normalized());
            }
        }
    }

    const QByteArray& m_data;
    bool m_lwob = false;
    QString m_dir;
    Mesh* m_mesh = nullptr;
    QStringList m_tags;
    Layer m_layer;
    QHash<int, int> m_layerNodes;          // layer number -> node
    QVector<QString> m_surfaces;           // per triangle, until resolved
    QVector<int> m_triangleLayers;         // per triangle, its layer's node
    QHash<QString, float> m_smoothing;     // surface -> smoothing angle (radians)
    QHash<QString, int> m_materials;       // surface -> material
    QHash<quint32, QString> m_clips;
    QVector<PendingMap> m_pending;
};

} // namespace

LoadResult loadLwo(const QString& filePath)
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
        result.message = failure(filePath, QCoreApplication::translate("Model3D", "it is not a LightWave object"));
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
