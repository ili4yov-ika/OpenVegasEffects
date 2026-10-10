#include "model3d/FbxReader.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QMatrix4x4>
#include <QQuaternion>
#include <QSet>
#include <QtEndian>

#include <algorithm>
#include <iterator>
#include <cctype>
#include <cmath>
#include <cstring>

namespace openvegas {
namespace model3d {

namespace {

// FBX counts time in ticks of 1/46186158000 s.
constexpr double kTicksPerSecond = 46186158000.0;
const char kBinaryMagic[] = "Kaydara FBX Binary  ";

QString failure(const QString& filePath, const QString& why)
{
    return QCoreApplication::translate("Model3D", "The FBX model %1 could not be read: %2")
        .arg(QFileInfo(filePath).fileName(), why);
}

// One value of a node: a number (integers kept exact for object ids), a
// string or raw bytes, or an array.
struct Property
{
    char type = 0;
    double number = 0.0;
    qint64 integer = 0;
    QByteArray text;
    QVector<double> array;
};

// The file as a tree of named records with values, the same for the binary
// and the ASCII form.
struct Node
{
    QByteArray name;
    QVector<Property> props;
    QVector<Node> children;

    const Node* child(const char* name) const
    {
        for (const Node& c : children)
            if (c.name == name) return &c;
        return nullptr;
    }
    QByteArray text(int index) const
    {
        return index < props.size() ? props.at(index).text : QByteArray();
    }
    double number(int index, double fallback = 0.0) const
    {
        return index < props.size() ? props.at(index).number : fallback;
    }
    qint64 integer(int index) const
    {
        return index < props.size() ? props.at(index).integer : 0;
    }
    // The values of the child named `name`: its array, or its numbers.
    QVector<double> values(const char* name) const
    {
        const Node* c = child(name);
        if (!c) return {};
        if (c->props.size() == 1 && !c->props.first().array.isEmpty()) return c->props.first().array;
        QVector<double> out;
        for (const Property& p : c->props)
            if (p.text.isEmpty() && p.array.isEmpty()) out.append(p.number);
        return out;
    }
    QByteArray childText(const char* name) const
    {
        const Node* c = child(name);
        return c ? c->text(0) : QByteArray();
    }
};

// --- binary -------------------------------------------------------------------

bool readProperty(const QByteArray& data, qint64& pos, Property* prop)
{
    const qint64 size = data.size();
    if (pos >= size) return false;
    const char type = data.at(pos++);
    prop->type = type;
    const auto bytes = reinterpret_cast<const uchar*>(data.constData());
    const auto need = [&](qint64 n) { return pos + n <= size; };
    switch (type) {
    case 'Y':
        if (!need(2)) return false;
        prop->integer = qFromLittleEndian<qint16>(bytes + pos); prop->number = double(prop->integer); pos += 2;
        return true;
    case 'C':
        if (!need(1)) return false;
        prop->integer = bytes[pos]; prop->number = double(prop->integer); pos += 1;
        return true;
    case 'I':
        if (!need(4)) return false;
        prop->integer = qFromLittleEndian<qint32>(bytes + pos); prop->number = double(prop->integer); pos += 4;
        return true;
    case 'L':
        if (!need(8)) return false;
        prop->integer = qFromLittleEndian<qint64>(bytes + pos); prop->number = double(prop->integer); pos += 8;
        return true;
    case 'F': {
        if (!need(4)) return false;
        const quint32 bits = qFromLittleEndian<quint32>(bytes + pos);
        float value; std::memcpy(&value, &bits, 4);
        prop->number = value; pos += 4;
        return true;
    }
    case 'D': {
        if (!need(8)) return false;
        const quint64 bits = qFromLittleEndian<quint64>(bytes + pos);
        double value; std::memcpy(&value, &bits, 8);
        prop->number = value; pos += 8;
        return true;
    }
    case 'S':
    case 'R': {
        if (!need(4)) return false;
        const quint32 length = qFromLittleEndian<quint32>(bytes + pos); pos += 4;
        if (!need(length)) return false;
        prop->text = data.mid(pos, length); pos += length;
        return true;
    }
    case 'f': case 'd': case 'l': case 'i': case 'b': {
        if (!need(12)) return false;
        const quint32 count = qFromLittleEndian<quint32>(bytes + pos);
        const quint32 encoding = qFromLittleEndian<quint32>(bytes + pos + 4);
        const quint32 stored = qFromLittleEndian<quint32>(bytes + pos + 8);
        pos += 12;
        if (!need(stored)) return false;
        const int element = type == 'd' || type == 'l' ? 8 : type == 'b' ? 1 : 4;
        const qint64 expected = qint64(count) * element;
        QByteArray raw = data.mid(pos, stored);
        pos += stored;
        if (encoding == 1) {
            // zlib; qUncompress wants the length in front, big-endian.
            QByteArray prefixed(4, '\0');
            qToBigEndian<quint32>(quint32(expected), prefixed.data());
            raw = qUncompress(prefixed + raw);
        }
        if (raw.size() < expected) return false;
        prop->array.resize(int(count));
        const auto* in = reinterpret_cast<const uchar*>(raw.constData());
        for (quint32 k = 0; k < count; ++k) {
            const uchar* at = in + qint64(k) * element;
            switch (type) {
            case 'f': { const quint32 b = qFromLittleEndian<quint32>(at); float v; std::memcpy(&v, &b, 4); prop->array[k] = v; break; }
            case 'd': { const quint64 b = qFromLittleEndian<quint64>(at); double v; std::memcpy(&v, &b, 8); prop->array[k] = v; break; }
            case 'l': prop->array[k] = double(qFromLittleEndian<qint64>(at)); break;
            case 'i': prop->array[k] = double(qFromLittleEndian<qint32>(at)); break;
            default: prop->array[k] = at[0]; break;
            }
        }
        return true;
    }
    default:
        return false;
    }
}

bool readBinaryNode(const QByteArray& data, qint64& pos, bool wide, Node* node, bool* null, int depth)
{
    const int header = wide ? 25 : 13;
    if (depth > 64 || pos + header > data.size()) return false;
    const auto* p = reinterpret_cast<const uchar*>(data.constData()) + pos;
    const quint64 end = wide ? qFromLittleEndian<quint64>(p) : qFromLittleEndian<quint32>(p);
    const quint64 count = wide ? qFromLittleEndian<quint64>(p + 8) : qFromLittleEndian<quint32>(p + 4);
    const int nameLength = p[header - 1];
    pos += header;
    if (end == 0) {
        *null = true;
        return true;
    }
    *null = false;
    if (end > quint64(data.size()) || pos + nameLength > data.size()) return false;
    node->name = data.mid(pos, nameLength);
    pos += nameLength;
    for (quint64 i = 0; i < count; ++i) {
        Property prop;
        if (!readProperty(data, pos, &prop)) return false;
        node->props.append(std::move(prop));
    }
    while (pos < qint64(end)) {
        Node child;
        bool childNull = false;
        if (!readBinaryNode(data, pos, wide, &child, &childNull, depth + 1)) return false;
        if (childNull) break;
        node->children.append(std::move(child));
    }
    pos = qint64(end);
    return true;
}

bool parseBinary(const QByteArray& data, Node* root, quint32* version)
{
    if (data.size() < 27) return false;
    *version = qFromLittleEndian<quint32>(data.constData() + 23);
    const bool wide = *version >= 7500;
    qint64 pos = 27;
    while (pos < data.size()) {
        Node node;
        bool null = false;
        if (!readBinaryNode(data, pos, wide, &node, &null, 0)) return root->children.size() > 0;
        if (null) break;
        root->children.append(std::move(node));
    }
    return true;
}

// --- ASCII --------------------------------------------------------------------

class AsciiParser
{
public:
    explicit AsciiParser(const QByteArray& text) : m_text(text) {}

    bool parse(Node* root)
    {
        next();
        parseList(root);
        return m_kind == Kind::End;
    }

private:
    enum class Kind { End, Key, String, Value, Star, Comma, Open, Close };

    void next()
    {
        const qsizetype size = m_text.size();
        while (m_pos < size) {
            const char c = m_text.at(m_pos);
            if (c == ';') {
                while (m_pos < size && m_text.at(m_pos) != '\n') ++m_pos;
            } else if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                ++m_pos;
            } else {
                break;
            }
        }
        m_token.clear();
        if (m_pos >= size) { m_kind = Kind::End; return; }
        const char c = m_text.at(m_pos);
        if (c == ',') { m_kind = Kind::Comma; ++m_pos; return; }
        if (c == '{') { m_kind = Kind::Open; ++m_pos; return; }
        if (c == '}') { m_kind = Kind::Close; ++m_pos; return; }
        if (c == '"') {
            const qsizetype end = m_text.indexOf('"', m_pos + 1);
            const qsizetype stop = end < 0 ? size : end;
            m_token = m_text.mid(m_pos + 1, stop - m_pos - 1);
            m_pos = stop + 1;
            m_kind = Kind::String;
            return;
        }
        if (c == '*') {
            qsizetype end = m_pos + 1;
            while (end < size && std::isdigit(uchar(m_text.at(end)))) ++end;
            m_token = m_text.mid(m_pos + 1, end - m_pos - 1);
            m_pos = end;
            m_kind = Kind::Star;
            return;
        }
        qsizetype end = m_pos;
        while (end < size) {
            const char d = m_text.at(end);
            if (d == ',' || d == '{' || d == '}' || d == ' ' || d == '\t' || d == '\r' || d == '\n' || d == ';')
                break;
            if (d == ':') { ++end; break; }
            ++end;
        }
        m_token = m_text.mid(m_pos, end - m_pos);
        m_pos = end;
        if (m_token.endsWith(':')) {
            m_token.chop(1);
            m_kind = Kind::Key;
        } else {
            m_kind = Kind::Value;
        }
    }

    void parseList(Node* parent)
    {
        while (m_kind == Kind::Key) {
            Node node;
            node.name = m_token;
            next();
            bool array = false;
            while (m_kind == Kind::String || m_kind == Kind::Value || m_kind == Kind::Star) {
                Property prop;
                if (m_kind == Kind::String) {
                    prop.type = 'S';
                    prop.text = m_token;
                } else if (m_kind == Kind::Star) {
                    array = true;
                } else {
                    prop.type = 'D';
                    bool isInteger = false;
                    prop.integer = m_token.toLongLong(&isInteger);
                    prop.number = isInteger ? double(prop.integer) : m_token.toDouble();
                    if (!isInteger) prop.integer = qint64(prop.number);
                }
                if (m_kind != Kind::Star) node.props.append(prop);
                next();
                if (m_kind != Kind::Comma) break;
                next();
            }
            if (m_kind == Kind::Open) {
                next();
                parseList(&node);
                if (m_kind == Kind::Close) next();
            }
            if (array) {
                // "*N { a: v, v, ... }": the values become the node's array.
                Property values;
                values.type = 'd';
                if (const Node* a = node.child("a"))
                    for (const Property& p : a->props) values.array.append(p.number);
                node.children.clear();
                node.props = {values};
            }
            parent->children.append(std::move(node));
        }
    }

    QByteArray m_text;
    qsizetype m_pos = 0;
    Kind m_kind = Kind::End;
    QByteArray m_token;
};

// --- the scene ----------------------------------------------------------------

// An object's own name: "Cube\0\1Model" in binary files, "Model::Cube" in
// ASCII ones.
QString objectName(const QByteArray& raw)
{
    const qsizetype binary = raw.indexOf(QByteArray("\x00\x01", 2));
    if (binary >= 0) return QString::fromUtf8(raw.left(binary));
    const qsizetype ascii = raw.indexOf("::");
    return QString::fromUtf8(ascii >= 0 ? raw.mid(ascii + 2) : raw);
}

struct Object
{
    QByteArray type;      // Model, Geometry, Material, ...
    QByteArray subclass;  // Mesh, Null, ...
    QString name;
    const Node* node = nullptr;
};

struct Link
{
    qint64 child = 0;
    qint64 parent = 0;
    QByteArray property;
};

// A value of an object's Properties70: the numbers after its four fields.
QVector<double> property70(const Node* object, const char* name)
{
    const Node* properties = object ? object->child("Properties70") : nullptr;
    if (!properties) return {};
    for (const Node& p : properties->children) {
        if (p.name != "P" || p.text(0) != name) continue;
        QVector<double> out;
        for (int i = 4; i < p.props.size(); ++i) out.append(p.props.at(i).number);
        return out;
    }
    return {};
}

QVector3D vector70(const Node* object, const char* name, const QVector3D& fallback)
{
    const QVector<double> v = property70(object, name);
    return v.size() >= 3 ? QVector3D(float(v[0]), float(v[1]), float(v[2])) : fallback;
}

double scalar70(const Node* object, const char* name, double fallback)
{
    const QVector<double> v = property70(object, name);
    return v.isEmpty() ? fallback : v.first();
}

// Euler angles in degrees, turned in the model's RotationOrder (eEulerXYZ by
// default: X first, then Y, then Z).
QQuaternion eulerRotation(const QVector3D& degrees, int order)
{
    const QQuaternion x = QQuaternion::fromAxisAndAngle(1, 0, 0, degrees.x());
    const QQuaternion y = QQuaternion::fromAxisAndAngle(0, 1, 0, degrees.y());
    const QQuaternion z = QQuaternion::fromAxisAndAngle(0, 0, 1, degrees.z());
    switch (order) {
    case 1: return y * z * x;   // XZY
    case 2: return x * z * y;   // YZX
    case 3: return z * x * y;   // YXZ
    case 4: return y * x * z;   // ZXY
    case 5: return x * y * z;   // ZYX
    default: return z * y * x;  // XYZ
    }
}

// A model's pivots and offsets, around which its rotation and scale turn.
// FBX places a model by T * Roff * Rp * Rpre * R * Rpost^-1 * Rp^-1 * Soff *
// Sp * S * Sp^-1 (what FbxNode::EvaluateGlobalTransform, the reference's,
// works out); pre- and post-rotation are in XYZ order.
struct Pivots
{
    QVector3D rotationOffset, rotationPivot, scalingOffset, scalingPivot;
    QQuaternion pre, post;
    int order = 0;   // RotationOrder, of Lcl Rotation

    bool moved() const
    {
        return !rotationOffset.isNull() || !rotationPivot.isNull() || !scalingOffset.isNull()
               || !scalingPivot.isNull();
    }
};

Pivots pivotsOf(const Node* model)
{
    Pivots p;
    p.rotationOffset = vector70(model, "RotationOffset", QVector3D());
    p.rotationPivot = vector70(model, "RotationPivot", QVector3D());
    p.scalingOffset = vector70(model, "ScalingOffset", QVector3D());
    p.scalingPivot = vector70(model, "ScalingPivot", QVector3D());
    p.pre = eulerRotation(vector70(model, "PreRotation", QVector3D()), 0);
    p.post = eulerRotation(vector70(model, "PostRotation", QVector3D()), 0);
    p.order = int(scalar70(model, "RotationOrder", 0));
    return p;
}

// That product as a translation, a rotation and the scale S: the rotation is
// Rpre * R * Rpost^-1, and the pivots and offsets move the translation by
// Roff + Rp + rotation(Soff + Sp - Rp - S * Sp).
void localPlace(const Pivots& p, const QVector3D& translation, const QVector3D& degrees, const QVector3D& scale,
                QVector3D* placedTranslation, QQuaternion* placedRotation)
{
    const QQuaternion rotation = p.pre * eulerRotation(degrees, p.order) * p.post.inverted();
    const QVector3D rest = p.scalingOffset + p.scalingPivot - p.rotationPivot - scale * p.scalingPivot;
    *placedRotation = rotation;
    *placedTranslation = translation + p.rotationOffset + p.rotationPivot + rotation.rotatedVector(rest);
}

QMatrix4x4 placement(const QVector3D& translation, const QQuaternion& rotation, const QVector3D& scale)
{
    QMatrix4x4 m;
    m.translate(translation);
    m.rotate(rotation);
    m.scale(scale);
    return m;
}

// A key-framed value: linear between keys, held outside them.
struct Curve
{
    QVector<double> times;   // seconds
    QVector<double> values;
    double at(double seconds) const
    {
        if (times.isEmpty()) return 0.0;
        if (seconds <= times.first()) return values.first();
        if (seconds >= times.last()) return values.last();
        const int b = int(std::upper_bound(times.cbegin(), times.cend(), seconds) - times.cbegin());
        const int a = b - 1;
        const double span = times.at(b) - times.at(a);
        const double t = span > 0.0 ? (seconds - times.at(a)) / span : 0.0;
        return values.at(a) + (values.at(b) - values.at(a)) * t;
    }
};

class Scene
{
public:
    Scene(const Node& root, const QString& filePath) : m_filePath(filePath)
    {
        m_dir = QFileInfo(filePath).absolutePath();
        m_settings = root.child("GlobalSettings");
        if (const Node* objects = root.child("Objects")) {
            for (const Node& node : objects->children) {
                if (node.props.isEmpty()) continue;
                Object object;
                object.type = node.name;
                object.name = objectName(node.text(1));
                object.subclass = node.text(2);
                object.node = &node;
                m_objects.insert(node.integer(0), object);
                m_order.append(node.integer(0));
            }
        }
        if (const Node* connections = root.child("Connections")) {
            for (const Node& c : connections->children) {
                if (c.name != "C" && c.name != "Connect") continue;
                Link link;
                link.child = c.integer(1);
                link.parent = c.integer(2);
                link.property = c.text(3);
                m_links.append(link);
            }
        }
    }

    void build(Mesh* mesh)
    {
        m_mesh = mesh;
        // Models whose parent is not a model are the roots, in file order.
        for (qint64 id : std::as_const(m_order)) {
            if (m_objects.value(id).type != "Model") continue;
            if (parentModel(id) == 0) visit(id, -1, QMatrix4x4());
        }
        // Geometry once every model's place is known: a skin's bones can
        // come anywhere in the hierarchy.
        for (const GeometryJob& job : std::as_const(m_geometry)) {
            const qint64 skin = skinOf(job.geometry);
            m_geometryId = job.geometry;
            m_geometryModel = job.model;
            addGeometry(*m_objects.value(job.geometry).node, job.matrix, job.materials, job.node, skin);
        }
        readAnimations();
    }

private:
    // A mesh to read once the hierarchy is placed.
    struct GeometryJob
    {
        qint64 geometry = 0;
        QMatrix4x4 matrix;
        QVector<qint64> materials;
        int node = -1;
        qint64 model = 0;
    };

    // The Skin deformer on a geometry, if any.
    qint64 skinOf(qint64 geometry) const
    {
        for (qint64 deformer : childrenOf(geometry, "Deformer"))
            if (m_objects.value(deformer).subclass == "Skin") return deformer;
        return 0;
    }

    // An FBX matrix: 16 values, column after column.
    static QMatrix4x4 matrixOf(const QVector<double>& values)
    {
        if (values.size() < 16) return QMatrix4x4();
        float m[16];
        for (int i = 0; i < 16; ++i) m[i] = float(values.at(i));
        return QMatrix4x4(m).transposed();
    }

    qint64 parentModel(qint64 id) const
    {
        for (const Link& link : m_links)
            if (link.child == id && link.property.isEmpty() && m_objects.value(link.parent).type == "Model")
                return link.parent;
        return 0;
    }

    // Objects of `type` connected to `parent` (object to object), in order.
    QVector<qint64> childrenOf(qint64 parent, const char* type) const
    {
        QVector<qint64> out;
        for (const Link& link : m_links)
            if (link.parent == parent && link.property.isEmpty() && m_objects.value(link.child).type == type)
                out.append(link.child);
        return out;
    }

    void visit(qint64 id, int parentNode, const QMatrix4x4& parentWorld)
    {
        const Object model = m_objects.value(id);
        const Node* node = model.node;
        const Pivots pivots = pivotsOf(node);
        const QVector3D scale = vector70(node, "Lcl Scaling", QVector3D(1, 1, 1));
        QVector3D translation;
        QQuaternion rotation;
        localPlace(pivots, vector70(node, "Lcl Translation", QVector3D()), vector70(node, "Lcl Rotation", QVector3D()),
                   scale, &translation, &rotation);
        const QMatrix4x4 local = placement(translation, rotation, scale);
        const QMatrix4x4 world = parentWorld * local;
        const int own = m_mesh->addNode(model.name.isEmpty() ? QStringLiteral("Model") : model.name, parentNode);
        ModelNode& entry = m_mesh->nodes[own];
        entry.sourceNode = m_sources.size();
        entry.baseLocal = local;
        entry.baseWorld = world;
        entry.baseTranslation = translation;
        entry.baseRotation = rotation;
        entry.baseScale = scale;
        m_sources.insert(id, entry.sourceNode);
        m_nodeOf.insert(id, own);
        m_pivots.insert(id, pivots);

        // Geometry is placed by the model and its own geometric transform,
        // which does not reach the model's children.
        const QMatrix4x4 geometric = placement(
            vector70(node, "GeometricTranslation", QVector3D()),
            eulerRotation(vector70(node, "GeometricRotation", QVector3D()), 0),
            vector70(node, "GeometricScaling", QVector3D(1, 1, 1)));
        const QVector<qint64> materials = childrenOf(id, "Material");
        for (qint64 geometry : childrenOf(id, "Geometry")) {
            if (m_objects.value(geometry).subclass != "Mesh") continue;
            m_geometry.append({geometry, world * geometric, materials, own, id});
        }
        for (qint64 child : childrenOf(id, "Model")) visit(child, own, world);
    }

    // A layer element's value for a polygon corner: by corner, by control
    // point, by polygon or one for all, directly or through its index array.
    static int elementIndex(const Node* element, const QVector<double>& indices, int corner, int vertex,
                            int polygon)
    {
        const QByteArray mapping = element->childText("MappingInformationType");
        int at = corner;
        if (mapping == "ByVertice" || mapping == "ByVertex" || mapping == "ByControlPoint") at = vertex;
        else if (mapping == "ByPolygon") at = polygon;
        else if (mapping == "AllSame") at = 0;
        const QByteArray reference = element->childText("ReferenceInformationType");
        if (reference == "IndexToDirect" || reference == "Index") {
            if (at < 0 || at >= indices.size()) return -1;
            at = int(indices.at(at));
        }
        return at;
    }

    // A skin's clusters as a Skin: each bone a joint (its inverse bind
    // TransformLink^-1 x Transform), and per control point its four
    // heaviest joints, their weights summing to one.
    Skin skinFor(qint64 skin, int points, QVector<SkinInfluence>* influences) const
    {
        Skin out;
        QVector<QVector<QPair<int, float>>> perPoint(points);
        for (qint64 cluster : childrenOf(skin, "Deformer")) {
            if (m_objects.value(cluster).subclass != "Cluster") continue;
            const QVector<qint64> bones = childrenOf(cluster, "Model");
            if (bones.isEmpty()) continue;
            const Node* node = m_objects.value(cluster).node;
            const int joint = out.joints.size();
            out.joints.append(m_nodeOf.value(bones.first(), -1));
            out.inverseBind.append(matrixOf(node->values("TransformLink")).inverted() * matrixOf(node->values("Transform")));
            const QVector<double> indexes = node->values("Indexes");
            const QVector<double> weights = node->values("Weights");
            for (int i = 0; i < indexes.size() && i < weights.size(); ++i) {
                const int point = int(indexes.at(i));
                if (point >= 0 && point < points && weights.at(i) > 0.0)
                    perPoint[point].append({joint, float(weights.at(i))});
            }
        }
        influences->resize(points);
        for (int p = 0; p < points; ++p) {
            QVector<QPair<int, float>>& list = perPoint[p];
            std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
            float total = 0.0f;
            SkinInfluence& influence = (*influences)[p];
            for (int k = 0; k < 4 && k < list.size(); ++k) {
                influence.joints[k] = list.at(k).first;
                influence.weights[k] = list.at(k).second;
                total += list.at(k).second;
            }
            if (total > 0.0f) for (float& w : influence.weights) w /= total;
        }
        return out;
    }

    void addGeometry(const Node& geometry, const QMatrix4x4& matrix, const QVector<qint64>& materials, int node,
                     qint64 skinId = 0)
    {
        const QVector<double> vertices = geometry.values("Vertices");
        const QVector<double> polygons = geometry.values("PolygonVertexIndex");
        if (vertices.size() < 3 || polygons.isEmpty()) return;
        const Node* normals = geometry.child("LayerElementNormal");
        const Node* uvs = geometry.child("LayerElementUV");
        const Node* materialElement = geometry.child("LayerElementMaterial");
        const QVector<double> normalData = normals ? normals->values("Normals") : QVector<double>();
        QVector<double> normalIndex = normals ? normals->values("NormalsIndex") : QVector<double>();
        if (normals && normalIndex.isEmpty()) normalIndex = normals->values("NormalIndex");
        const QVector<double> uvData = uvs ? uvs->values("UV") : QVector<double>();
        const QVector<double> uvIndex = uvs ? uvs->values("UVIndex") : QVector<double>();
        const QVector<double> materialIndex = materialElement ? materialElement->values("Materials")
                                                              : QVector<double>();

        const int base = m_mesh->positions.size();
        const int points = vertices.size() / 3;
        // A skinned mesh: its points where its bones put them (its model's
        // own place does not count), and kept for them to move.
        Skin skin;
        QVector<SkinInfluence> influences;
        QVector<QMatrix4x4> joints, jointNormals;
        if (skinId) {
            skin = skinFor(skinId, points, &influences);
            for (int j = 0; j < skin.joints.size(); ++j) {
                const int bone = skin.joints.at(j);
                joints.append((bone >= 0 ? m_mesh->nodes.at(bone).baseWorld : QMatrix4x4()) * skin.inverseBind.at(j));
                bool invertibleJoint = false;
                const QMatrix4x4 inverse = joints.last().inverted(&invertibleJoint);
                jointNormals.append(invertibleJoint ? inverse.transposed() : joints.last());
            }
            if (joints.isEmpty()) skinId = 0;
            else m_mesh->nodes[node].skinned = true;
        }
        const auto carried = [&](const SkinInfluence& influence, const QVector3D& value, bool normal) {
            QVector3D sum;
            for (int k = 0; k < 4; ++k) {
                const int j = influence.joints[k];
                if (j < 0 || j >= joints.size()) continue;
                sum += influence.weights[k] * (normal ? jointNormals.at(j).mapVector(value) : joints.at(j).map(value));
            }
            return sum;
        };
        for (int i = 0; i < points; ++i) {
            const QVector3D point{float(vertices[3 * i]), float(vertices[3 * i + 1]), float(vertices[3 * i + 2])};
            if (skinId && influences.at(i).joints[0] >= 0) {
                skin.positions.append(m_mesh->positions.size());
                skin.bindPositions.append(point);
                skin.positionInfluences.append(influences.at(i));
                m_mesh->positions.append(carried(influences.at(i), point, false));
            } else {
                m_mesh->positions.append(matrix.map(point));
            }
        }
        bool invertible = false;
        const QMatrix4x4 normalMatrix = matrix.inverted(&invertible).transposed();
        const bool mirrored = !skinId && matrix.determinant() < 0.0;
        // Blend shapes: a morph target per channel, its shape's moves of the
        // control points; one that is not skinned rests at DeformPercent.
        int morphIndex = -1;
        struct ShapeTarget { QVector<double> indexes, deltas, normals; float weight = 0.0f; };
        QVector<ShapeTarget> shapes;
        for (qint64 blend : childrenOf(m_geometryId, "Deformer")) {
            if (m_objects.value(blend).subclass != "BlendShape") continue;
            for (qint64 channel : childrenOf(blend, "Deformer")) {
                if (m_objects.value(channel).subclass != "BlendShapeChannel") continue;
                qint64 shape = 0;
                for (qint64 candidate : childrenOf(channel, "Geometry"))
                    if (m_objects.value(candidate).subclass == "Shape") { shape = candidate; break; }
                if (!shape) continue;
                if (morphIndex < 0) {
                    Morph morph;
                    morph.node = node;
                    morph.sourceNode = m_sources.value(m_geometryModel, -1);
                    morph.placement = matrix;
                    m_mesh->morphs.append(morph);
                    morphIndex = m_mesh->morphs.size() - 1;
                }
                const Node* channelNode = m_objects.value(channel).node;
                const Node* shapeNode = m_objects.value(shape).node;
                QVector<double> percent = channelNode->values("DeformPercent");
                ShapeTarget target;
                target.weight = float((percent.isEmpty() ? scalar70(channelNode, "DeformPercent", 0.0)
                                                         : percent.first()) / 100.0);
                target.indexes = shapeNode->values("Indexes");
                target.deltas = shapeNode->values("Vertices");
                target.normals = shapeNode->values("Normals");
                shapes.append(target);
                Morph& morph = m_mesh->morphs[morphIndex];
                MorphTarget entry;
                entry.name = m_objects.value(channel).name;
                morph.targets.append(entry);
                morph.defaultWeights.append(target.weight);
                m_morphChannels.insert(channel, qMakePair(morphIndex, int(morph.targets.size()) - 1));
                for (int k = 0; k < target.indexes.size() && 3 * k + 2 < target.deltas.size(); ++k) {
                    const int point = int(target.indexes.at(k));
                    if (point < 0 || point >= points) continue;
                    const QVector3D delta{float(target.deltas[3 * k]), float(target.deltas[3 * k + 1]),
                                          float(target.deltas[3 * k + 2])};
                    MorphTarget& added = morph.targets.last();
                    added.positions.append(base + point);
                    added.positionDeltas.append(delta);
                    // A mesh that is not skinned rests at its weights.
                    if (!m_mesh->nodes.at(node).skinned)
                        m_mesh->positions[base + point] += matrix.mapVector(target.weight * delta);
                }
            }
        }
        QVector<QVector<int>> cornerNormals(morphIndex >= 0 ? points : 0);
        QHash<int, QVector3D> fileNormals;   // the corners' normals as the file has them

        struct Corner { int position, normal, uv; };
        QVector<Corner> corners;
        int polygon = 0;
        for (int k = 0; k < polygons.size(); ++k) {
            const int raw = int(polygons.at(k));
            const bool last = raw < 0;
            const int vertex = last ? ~raw : raw;
            if (vertex < 0 || vertex >= points) return;   // damaged
            Corner corner{base + vertex, -1, -1};
            if (normals) {
                const int n = elementIndex(normals, normalIndex, k, vertex, polygon);
                if (n >= 0 && 3 * n + 2 < normalData.size()) {
                    const QVector3D v{float(normalData[3 * n]), float(normalData[3 * n + 1]),
                                      float(normalData[3 * n + 2])};
                    corner.normal = m_mesh->normals.size();
                    if (!cornerNormals.isEmpty()) {
                        cornerNormals[vertex].append(corner.normal);
                        fileNormals.insert(corner.normal, v);
                    }
                    if (skinId && influences.at(vertex).joints[0] >= 0) {
                        skin.normals.append(corner.normal);
                        skin.bindNormals.append(v);
                        skin.normalInfluences.append(influences.at(vertex));
                        m_mesh->normals.append(carried(influences.at(vertex), v, true).normalized());
                    } else {
                        m_mesh->normals.append((invertible ? normalMatrix : matrix).mapVector(v).normalized());
                    }
                }
            }
            if (uvs) {
                const int u = elementIndex(uvs, uvIndex, k, vertex, polygon);
                if (u >= 0 && 2 * u + 1 < uvData.size()) {
                    corner.uv = m_mesh->uvs.size();
                    m_mesh->uvs.append(QVector2D(float(uvData[2 * u]), float(uvData[2 * u + 1])));
                }
            }
            corners.append(corner);
            if (!last) continue;
            int material = -1;
            if (materialElement && !materialIndex.isEmpty()) {
                const QByteArray mapping = materialElement->childText("MappingInformationType");
                const int slot = int(materialIndex.value(mapping == "AllSame" ? 0 : polygon, 0));
                if (slot >= 0 && slot < materials.size()) material = materialFor(materials.at(slot));
            } else if (!materials.isEmpty()) {
                material = materialFor(materials.first());
            }
            // Fanned into triangles.
            for (int c = 1; c + 1 < corners.size(); ++c) {
                const Corner fan[3] = {corners.at(0), corners.at(c), corners.at(c + 1)};
                Triangle triangle;
                for (int i = 0; i < 3; ++i) {
                    const Corner& at = fan[mirrored ? 2 - i : i];
                    triangle.position[i] = at.position;
                    triangle.normal[i] = at.normal;
                    triangle.uv[i] = at.uv;
                }
                triangle.material = material;
                triangle.node = node;
                m_mesh->triangles.append(triangle);
            }
            corners.clear();
            ++polygon;
        }
        if (skinId && !skin.positions.isEmpty()) m_mesh->skins.append(skin);
        // The shapes' normals, on every corner of the points they move (and,
        // not skinned, turned to the weights the mesh rests at).
        if (morphIndex >= 0) {
            const bool resting = !m_mesh->nodes.at(node).skinned;
            QHash<int, QVector3D> turned;
            const int first = m_mesh->morphs.at(morphIndex).targets.size() - shapes.size();
            for (int t = 0; t < shapes.size(); ++t) {
                const ShapeTarget& shape = shapes.at(t);
                MorphTarget& target = m_mesh->morphs[morphIndex].targets[first + t];
                for (int k = 0; k < shape.indexes.size() && 3 * k + 2 < shape.normals.size(); ++k) {
                    const int point = int(shape.indexes.at(k));
                    if (point < 0 || point >= cornerNormals.size()) continue;
                    const QVector3D delta{float(shape.normals[3 * k]), float(shape.normals[3 * k + 1]),
                                          float(shape.normals[3 * k + 2])};
                    for (int normal : cornerNormals.at(point)) {
                        target.normals.append(normal);
                        target.normalDeltas.append(delta);
                        if (!resting) continue;
                        Morph& morph = m_mesh->morphs[morphIndex];
                        if (!morph.baseNormals.contains(normal))
                            morph.baseNormals.insert(normal, fileNormals.value(normal));
                        auto sum = turned.find(normal);
                        if (sum == turned.end()) sum = turned.insert(normal, fileNormals.value(normal));
                        *sum += shape.weight * delta;
                    }
                }
            }
            for (auto sum = turned.cbegin(); sum != turned.cend(); ++sum)
                m_mesh->normals[sum.key()] = (invertible ? normalMatrix : matrix).mapVector(sum.value()).normalized();
        }
    }

    // The mesh material made of a Material object, made once.
    int materialFor(qint64 id)
    {
        const auto found = m_materials.constFind(id);
        if (found != m_materials.constEnd()) return found.value();
        const Object object = m_objects.value(id);
        const Node* node = object.node;
        Material material;
        material.name = object.name;
        const auto colour = [&](const char* name, const char* factor, const QColor& fallback) {
            const QVector<double> v = property70(node, name);
            if (v.size() < 3) return fallback;
            const double f = scalar70(node, factor, 1.0);
            return QColor::fromRgbF(float(qBound(0.0, v[0] * f, 1.0)), float(qBound(0.0, v[1] * f, 1.0)),
                                    float(qBound(0.0, v[2] * f, 1.0)));
        };
        material.diffuseColor = colour("DiffuseColor", "DiffuseFactor", material.diffuseColor);
        material.specularColor = colour("SpecularColor", "SpecularFactor", material.specularColor);
        material.emissiveColor = colour("EmissiveColor", "EmissiveFactor", material.emissiveColor);
        material.ambientColor = colour("AmbientColor", "AmbientFactor", material.ambientColor);
        material.shininess = qBound(1.0, scalar70(node, "ShininessExponent", scalar70(node, "Shininess", 32.0)),
                                    1024.0);
        const QVector<double> opacity = property70(node, "Opacity");
        if (!opacity.isEmpty()) material.opacity = qBound(0.0, opacity.first(), 1.0);
        else material.opacity = qBound(0.0, 1.0 - scalar70(node, "TransparencyFactor", 0.0), 1.0);
        for (const Link& link : std::as_const(m_links)) {
            if (link.parent != id || link.property.isEmpty()) continue;
            if (m_objects.value(link.child).type != "Texture") continue;
            QString* path = nullptr;
            QImage* image = nullptr;
            const QByteArray slot = link.property;
            if (slot == "DiffuseColor" || slot == "Diffuse") { path = &material.diffuseMap; image = &material.diffuseImage; }
            else if (slot == "SpecularColor" || slot == "SpecularFactor") { path = &material.specularMap; image = &material.specularImage; }
            else if (slot == "NormalMap") { path = &material.normalMap; image = &material.normalImage; }
            else if (slot == "Bump" || slot == "BumpFactor") { path = &material.bumpMap; image = &material.bumpImage; }
            else if (slot == "EmissiveColor" || slot == "EmissiveFactor") { path = &material.emissiveMap; image = &material.emissiveImage; }
            else if (slot == "AmbientColor" || slot == "AmbientFactor") { path = &material.occlusionMap; image = &material.occlusionImage; }
            if (!path) continue;
            readTexture(link.child, path, image);
        }
        m_mesh->materials.append(material);
        const int index = m_mesh->materials.size() - 1;
        m_materials.insert(id, index);
        return index;
    }

    // A texture's file - beside the model by its relative name, where the
    // file says, or its name beside the model - or its embedded picture.
    void readTexture(qint64 texture, QString* path, QImage* image) const
    {
        const Node* node = m_objects.value(texture).node;
        for (const Link& link : m_links) {
            if (link.parent != texture || m_objects.value(link.child).type != "Video") continue;
            const Node* video = m_objects.value(link.child).node;
            const Node* content = video ? video->child("Content") : nullptr;
            if (content && !content->props.isEmpty() && !content->props.first().text.isEmpty()) {
                const QImage embedded = QImage::fromData(content->props.first().text);
                if (!embedded.isNull()) {
                    *image = embedded.convertToFormat(QImage::Format_RGBA8888);
                    return;
                }
            }
        }
        const QString relative = QString::fromUtf8(node->childText("RelativeFilename")).replace('\\', '/');
        const QString absolute = QString::fromUtf8(node->childText("FileName")).replace('\\', '/');
        const QStringList candidates{QDir(m_dir).filePath(relative), absolute,
                                     QDir(m_dir).filePath(QFileInfo(absolute).fileName()),
                                     QDir(m_dir).filePath(QFileInfo(relative).fileName())};
        for (const QString& candidate : candidates) {
            if (!candidate.isEmpty() && QFileInfo(candidate).isFile()) {
                *path = candidate;
                return;
            }
        }
        *path = relative.isEmpty() ? absolute : relative;
    }

    // An AnimationCurve: its keys in seconds.
    Curve curve(qint64 id) const
    {
        Curve out;
        const Node* node = m_objects.value(id).node;
        if (!node) return out;
        const Node* times = node->child("KeyTime");
        const QVector<double> values = node->values("KeyValueFloat");
        if (!times || times->props.isEmpty()) return out;
        // Ticks are 64-bit integers; read exactly where the file wrote them so.
        QVector<double> ticks = times->props.first().array;
        if (ticks.isEmpty()) for (const Property& p : times->props) ticks.append(double(p.integer));
        const int count = qMin(ticks.size(), values.size());
        for (int i = 0; i < count; ++i) {
            out.times.append(ticks.at(i) / kTicksPerSecond);
            out.values.append(values.at(i));
        }
        return out;
    }

    // The file's frame rate: GlobalSettings TimeMode (FbxTime::EMode, through
    // FbxTime::GetFrameRate as the reference reads it; the default mode is 30
    // a second) or, for a custom one, CustomFrameRate.
    double fileFrameRate() const
    {
        static const double rates[] = {30.0, 120.0, 100.0, 60.0, 50.0, 48.0, 30.0, 30.0, 30000.0 / 1001.0,
                                       30000.0 / 1001.0, 25.0, 24.0, 1000.0, 24000.0 / 1001.0, 0.0, 96.0, 72.0,
                                       60000.0 / 1001.0, 120000.0 / 1001.0};
        const int mode = int(scalar70(m_settings, "TimeMode", 0.0));
        double rate = mode >= 0 && mode < int(std::size(rates)) ? rates[mode] : 30.0;
        if (mode == 14) rate = scalar70(m_settings, "CustomFrameRate", 0.0);
        return rate > 0.0 ? rate : 30.0;
    }

    void readAnimations()
    {
        const double rate = fileFrameRate();
        for (qint64 stack : std::as_const(m_order)) {
            if (m_objects.value(stack).type != "AnimationStack") continue;
            // Per model and property ("Lcl Translation", ...): the curves of
            // x, y and z and the values without one.
            struct Property3 { Curve axes[3]; bool keyed[3] = {false, false, false}; QVector3D fallback; };
            QHash<qint64, QHash<QByteArray, Property3>> animated;
            QHash<qint64, Curve> deforms;   // BlendShapeChannel -> its DeformPercent curve
            double duration = 0.0;
            for (qint64 layer : childrenOf(stack, "AnimationLayer")) {
                for (qint64 curveNode : childrenOf(layer, "AnimationCurveNode")) {
                    for (const Link& target : std::as_const(m_links)) {
                        if (target.child != curveNode || target.property.isEmpty()) continue;
                        if (target.property == "DeformPercent" && m_morphChannels.contains(target.parent)) {
                            for (const Link& axis : std::as_const(m_links)) {
                                if (axis.parent != curveNode || axis.property != "d|DeformPercent") continue;
                                if (m_objects.value(axis.child).type != "AnimationCurve") continue;
                                const Curve keys = curve(axis.child);
                                if (keys.times.isEmpty()) continue;
                                deforms.insert(target.parent, keys);
                                duration = qMax(duration, keys.times.last());
                            }
                            continue;
                        }
                        if (!m_sources.contains(target.parent)) continue;
                        const QByteArray property = target.property;
                        if (property != "Lcl Translation" && property != "Lcl Rotation" && property != "Lcl Scaling")
                            continue;
                        Property3& entry = animated[target.parent][property];
                        const Node* node = m_objects.value(curveNode).node;
                        const QVector3D base = property == "Lcl Scaling" ? QVector3D(1, 1, 1) : QVector3D();
                        const QVector3D own = vector70(m_objects.value(target.parent).node, property.constData(), base);
                        entry.fallback = QVector3D(float(scalar70(node, "d|X", own.x())),
                                                   float(scalar70(node, "d|Y", own.y())),
                                                   float(scalar70(node, "d|Z", own.z())));
                        for (const Link& axis : std::as_const(m_links)) {
                            if (axis.parent != curveNode || axis.property.isEmpty()) continue;
                            if (m_objects.value(axis.child).type != "AnimationCurve") continue;
                            const int a = axis.property == "d|X" ? 0 : axis.property == "d|Y" ? 1
                                        : axis.property == "d|Z" ? 2 : -1;
                            if (a < 0) continue;
                            entry.axes[a] = curve(axis.child);
                            entry.keyed[a] = !entry.axes[a].times.isEmpty();
                            if (entry.keyed[a]) duration = qMax(duration, entry.axes[a].times.last());
                        }
                    }
                }
            }
            if (animated.isEmpty() && deforms.isEmpty()) continue;
            ModelAnimation clip;
            clip.name = m_objects.value(stack).name.isEmpty() ? QStringLiteral("Take") : m_objects.value(stack).name;
            // As long as its stack says (LocalStop, the reference's time span),
            // else to its last key.
            const double stop = scalar70(m_objects.value(stack).node, "LocalStop", 0.0) / kTicksPerSecond;
            if (stop > 0.0) duration = stop;
            clip.duration = duration;
            clip.frameRate = rate;
            // Sampled at the file's frames: Euler curves and pivots turn into
            // translations and quaternions, interpolated between samples.
            const int samples = qMax(1, int(std::ceil(duration * rate - 1e-6))) + 1;
            QVector<float> times;
            for (int i = 0; i < samples; ++i)
                times.append(float(qMin(duration, i / rate)));
            for (auto model = animated.cbegin(); model != animated.cend(); ++model) {
                const Node* own = m_objects.value(model.key()).node;
                const Pivots pivots = m_pivots.value(model.key());
                const auto valueAt = [&](const char* name, const QVector3D& fallback, double seconds) {
                    const auto found = model->constFind(QByteArray(name));
                    if (found == model->cend()) return vector70(own, name, fallback);
                    const Property3& entry = found.value();
                    float v[3];
                    for (int a = 0; a < 3; ++a) v[a] = float(entry.keyed[a] ? entry.axes[a].at(seconds) : entry.fallback[a]);
                    return QVector3D(v[0], v[1], v[2]);
                };
                const bool turns = model->contains("Lcl Rotation");
                const bool scales = model->contains("Lcl Scaling");
                const bool moves = model->contains("Lcl Translation") || (pivots.moved() && (turns || scales));
                AnimationChannel translation, rotation, scale;
                translation.path = AnimationChannel::Path::Translation;
                rotation.path = AnimationChannel::Path::Rotation;
                scale.path = AnimationChannel::Path::Scale;
                for (AnimationChannel* channel : {&translation, &rotation, &scale}) {
                    channel->sourceNode = m_sources.value(model.key());
                    channel->times = times;
                }
                for (float t : std::as_const(times)) {
                    const QVector3D s = valueAt("Lcl Scaling", QVector3D(1, 1, 1), t);
                    QVector3D placedTranslation;
                    QQuaternion placedRotation;
                    localPlace(pivots, valueAt("Lcl Translation", QVector3D(), t), valueAt("Lcl Rotation", QVector3D(), t),
                               s, &placedTranslation, &placedRotation);
                    translation.values.append(QVector4D(placedTranslation, 0.0f));
                    rotation.values.append(QVector4D(placedRotation.x(), placedRotation.y(), placedRotation.z(),
                                                     placedRotation.scalar()));
                    scale.values.append(QVector4D(s, 0.0f));
                }
                if (moves) clip.channels.append(translation);
                if (turns) clip.channels.append(rotation);
                if (scales) clip.channels.append(scale);
            }
            // Blend shape weights: one channel per morph, its targets' curves
            // (DeformPercent / 100) or their resting weights.
            QHash<int, bool> keyedMorphs;
            for (auto it = deforms.cbegin(); it != deforms.cend(); ++it)
                keyedMorphs.insert(m_morphChannels.value(it.key()).first, true);
            for (auto it = keyedMorphs.cbegin(); it != keyedMorphs.cend(); ++it) {
                const Morph& morph = m_mesh->morphs.at(it.key());
                AnimationChannel channel;
                channel.path = AnimationChannel::Path::Weights;
                channel.sourceNode = morph.sourceNode;
                channel.times = times;
                channel.weightCount = morph.targets.size();
                for (float t : std::as_const(times)) {
                    for (int target = 0; target < morph.targets.size(); ++target) {
                        float weight = morph.defaultWeights.value(target);
                        for (auto curveIt = deforms.cbegin(); curveIt != deforms.cend(); ++curveIt) {
                            const QPair<int, int> slot = m_morphChannels.value(curveIt.key());
                            if (slot.first == it.key() && slot.second == target)
                                weight = float(curveIt.value().at(t) / 100.0);
                        }
                        channel.weights.append(weight);
                    }
                }
                clip.channels.append(channel);
            }
            m_mesh->animations.append(clip);
        }
    }

    QString m_filePath;
    QString m_dir;
    QHash<qint64, Object> m_objects;
    QVector<qint64> m_order;
    QVector<Link> m_links;
    Mesh* m_mesh = nullptr;
    QHash<qint64, int> m_sources;           // model -> ModelNode::sourceNode
    QHash<qint64, int> m_nodeOf;            // model -> its ModelNode
    qint64 m_geometryId = 0;                // the geometry addGeometry reads
    qint64 m_geometryModel = 0;             // and its model
    QHash<qint64, QPair<int, int>> m_morphChannels;   // BlendShapeChannel -> (morph, target)
    QVector<GeometryJob> m_geometry;
    QHash<qint64, Pivots> m_pivots;
    const Node* m_settings = nullptr;   // GlobalSettings
    QHash<qint64, int> m_materials;         // Material object -> mesh material
};

} // namespace

LoadResult loadFbx(const QString& filePath)
{
    LoadResult result;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        result.message = failure(filePath, QCoreApplication::translate("Model3D", "the file could not be opened"));
        return result;
    }
    const QByteArray data = file.readAll();
    Node root;
    quint32 version = 0;
    if (data.startsWith(kBinaryMagic)) {
        if (!parseBinary(data, &root, &version)) {
            result.message = failure(filePath, QCoreApplication::translate("Model3D", "the file is damaged"));
            return result;
        }
    } else {
        AsciiParser parser(data);
        parser.parse(&root);
        const Node* header = root.child("FBXHeaderExtension");
        version = header ? quint32(header->values("FBXVersion").value(0, 0.0)) : 0;
        if (root.children.isEmpty()) {
            result.message = failure(filePath, QCoreApplication::translate("Model3D", "it is not an FBX file"));
            return result;
        }
    }
    if (version != 0 && version < 7000) {
        result.message = failure(filePath, QCoreApplication::translate("Model3D", "FBX %1 is not supported (7.0 or later is)")
                                               .arg(QString::number(version / 1000.0, 'f', 1)));
        return result;
    }
    Scene scene(root, filePath);
    scene.build(&result.mesh);
    if (result.mesh.isEmpty()) {
        result.message = failure(filePath, QCoreApplication::translate("Model3D", "it has no triangles"));
        return result;
    }
    result.ok = true;
    return result;
}

} // namespace model3d
} // namespace openvegas
