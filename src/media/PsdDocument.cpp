#include "media/PsdDocument.h"

#include <QBuffer>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QList>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

// The layout followed here is Adobe's "Photoshop File Formats Specification":
// a header, colour mode data, image resources, the layer and mask section
// (layer records, then every layer's channel data, then document-level
// tagged blocks) and the flattened image. All numbers are big-endian. A .psb
// differs only in the width of some lengths, noted where they are read.

namespace openvegas {
namespace media {

namespace {

// A bounds-checked big-endian reader over the document's bytes. Reading past
// the end (or past a section's end) fails once and from then on returns zeros,
// so a damaged file ends in a parse error rather than a crash.
class Reader
{
public:
    Reader(const QByteArray& data, qint64 pos = 0, qint64 end = -1)
        : m_data(data), pos(pos), end(end < 0 ? data.size() : qMin<qint64>(end, data.size())) {}

    bool ok() const { return m_ok; }
    bool atEnd() const { return pos >= end; }
    qint64 remaining() const { return end - pos; }
    bool need(qint64 count)
    {
        if (!m_ok || count < 0 || pos + count > end) {
            m_ok = false;
            return false;
        }
        return true;
    }
    quint8 u8() { return need(1) ? quint8(m_data.at(pos++)) : 0; }
    quint16 u16() { return read<quint16>(); }
    qint16 i16() { return qint16(read<quint16>()); }
    quint32 u32() { return read<quint32>(); }
    qint32 i32() { return qint32(read<quint32>()); }
    quint64 u64() { return read<quint64>(); }
    double f64()
    {
        const quint64 bits = u64();
        double value = 0.0;
        std::memcpy(&value, &bits, sizeof value);
        return value;
    }
    float f32()
    {
        const quint32 bits = u32();
        float value = 0.0f;
        std::memcpy(&value, &bits, sizeof value);
        return value;
    }
    // A length that is 8 bytes wide in a .psb and 4 in a .psd.
    qint64 length(bool wide) { return wide ? qint64(u64()) : qint64(u32()); }
    QByteArray bytes(qint64 count)
    {
        if (!need(count)) return {};
        const QByteArray out = m_data.mid(pos, count);
        pos += count;
        return out;
    }
    QString key() { return QString::fromLatin1(bytes(4)); }
    void skip(qint64 count) { if (need(count)) pos += count; }
    // A Unicode string: a count of UTF-16 code units, then the units.
    QString unicode()
    {
        const quint32 count = u32();
        if (count > 0x100000 || !need(qint64(count) * 2)) {
            m_ok = false;
            return {};
        }
        QString text(int(count), Qt::Uninitialized);
        for (quint32 i = 0; i < count; ++i) text[int(i)] = QChar(u16());
        while (text.endsWith(QChar(0))) text.chop(1);
        return text;
    }
    // A descriptor key or class: a length, or 0 and a four-character code.
    QString idKey()
    {
        const quint32 count = u32();
        if (count == 0) return key();
        if (count > 0x10000) {
            m_ok = false;
            return {};
        }
        return QString::fromLatin1(bytes(count));
    }
    bool signatureAhead() const
    {
        if (pos + 4 > end) return false;
        const char* p = m_data.constData() + pos;
        return std::memcmp(p, "8BIM", 4) == 0 || std::memcmp(p, "8B64", 4) == 0;
    }

private:
    template <typename T>
    T read()
    {
        if (!need(qint64(sizeof(T)))) return T(0);
        const T value = qFromBigEndian<T>(m_data.constData() + pos);
        pos += qint64(sizeof(T));
        return value;
    }

    const QByteArray& m_data;
    bool m_ok = true;

public:
    qint64 pos;
    qint64 end;
};

// Tagged blocks whose length is 8 bytes wide in a .psb.
bool wideBlockKey(const QString& key)
{
    static const QStringList keys = {QStringLiteral("LMsk"), QStringLiteral("Lr16"), QStringLiteral("Lr32"),
                                     QStringLiteral("Layr"), QStringLiteral("Mt16"), QStringLiteral("Mt32"),
                                     QStringLiteral("Mtrn"), QStringLiteral("Alph"), QStringLiteral("FMsk"),
                                     QStringLiteral("lnk2"), QStringLiteral("FEid"), QStringLiteral("FXid"),
                                     QStringLiteral("PxSD")};
    return keys.contains(key);
}

// After a tagged block, the next one starts at once or after up to three
// bytes of padding (the specification's rounding is not applied uniformly by
// every writer); false when no further block follows.
bool seekNextBlock(Reader& r)
{
    for (int pad = 0; pad < 4; ++pad) {
        Reader probe = r;
        probe.pos = r.pos + pad;
        if (probe.signatureAhead()) {
            r.pos = probe.pos;
            return true;
        }
    }
    return false;
}

QVariant readItem(Reader& r, const QString& type, int depth);

QVariantMap readDescriptorBody(Reader& r, int depth)
{
    QVariantMap map;
    if (depth > 64) {
        r.need(-1);
        return map;
    }
    r.unicode();                      // the class's display name
    map.insert(QStringLiteral("_class"), r.idKey());
    const quint32 count = r.u32();
    for (quint32 i = 0; i < count && r.ok(); ++i) {
        const QString key = r.idKey();
        const QString type = r.key();
        map.insert(key, readItem(r, type, depth + 1));
    }
    return map;
}

QVariant readItem(Reader& r, const QString& type, int depth)
{
    if (type == QLatin1String("Objc") || type == QLatin1String("GlbO")) return readDescriptorBody(r, depth);
    if (type == QLatin1String("VlLs")) {
        const quint32 count = r.u32();
        QVariantList list;
        for (quint32 i = 0; i < count && r.ok(); ++i) list.append(readItem(r, r.key(), depth + 1));
        return list;
    }
    if (type == QLatin1String("doub")) return r.f64();
    if (type == QLatin1String("UntF")) {
        QVariantMap unit;
        unit.insert(QStringLiteral("_unit"), r.key());
        unit.insert(QStringLiteral("value"), r.f64());
        return unit;
    }
    if (type == QLatin1String("UnFl")) {
        QVariantMap unit;
        unit.insert(QStringLiteral("_unit"), r.key());
        const quint32 count = r.u32();
        QVariantList values;
        for (quint32 i = 0; i < count && r.ok(); ++i) values.append(r.f64());
        unit.insert(QStringLiteral("values"), values);
        return unit;
    }
    if (type == QLatin1String("TEXT")) return r.unicode();
    if (type == QLatin1String("enum")) {
        r.idKey();                    // the enumeration's type
        return r.idKey();
    }
    if (type == QLatin1String("long")) return r.i32();
    if (type == QLatin1String("comp")) return qint64(r.u64());
    if (type == QLatin1String("bool")) return r.u8() != 0;
    if (type == QLatin1String("type") || type == QLatin1String("GlbC")) {
        r.unicode();
        return r.idKey();
    }
    if (type == QLatin1String("alis") || type == QLatin1String("tdta") || type == QLatin1String("Pth ")) {
        const quint32 count = r.u32();
        return r.bytes(count);
    }
    if (type == QLatin1String("ObAr")) {
        // An object array: shared keys, each with a list of values.
        r.u32();
        r.unicode();
        QVariantMap map;
        map.insert(QStringLiteral("_class"), r.idKey());
        const quint32 count = r.u32();
        for (quint32 i = 0; i < count && r.ok(); ++i) {
            const QString key = r.idKey();
            const QString itemType = r.key();
            map.insert(key, readItem(r, itemType, depth + 1));
        }
        return map;
    }
    if (type == QLatin1String("obj ")) {
        // A reference: kept only as the names it points at.
        const quint32 count = r.u32();
        QVariantList parts;
        for (quint32 i = 0; i < count && r.ok(); ++i) {
            const QString form = r.key();
            if (form == QLatin1String("prop")) { r.unicode(); r.idKey(); parts.append(r.idKey()); }
            else if (form == QLatin1String("Clss")) { r.unicode(); parts.append(r.idKey()); }
            else if (form == QLatin1String("Enmr")) { r.unicode(); r.idKey(); r.idKey(); parts.append(r.idKey()); }
            else if (form == QLatin1String("rele")) { r.unicode(); r.idKey(); parts.append(r.u32()); }
            else if (form == QLatin1String("Idnt") || form == QLatin1String("indx")) parts.append(r.u32());
            else if (form == QLatin1String("name")) { r.unicode(); r.idKey(); parts.append(r.unicode()); }
            else { r.need(-1); break; }
        }
        return parts;
    }
    r.need(-1);   // an unknown type cannot be skipped: its size is not known
    return {};
}

// PackBits, the run-length coding of compression 1, into `out` (sized).
bool unpackBits(Reader& r, qint64 packedLength, char* out, int outLength)
{
    const qint64 stop = r.pos + packedLength;
    int written = 0;
    while (r.pos < stop && written < outLength && r.ok()) {
        const qint8 n = qint8(r.u8());
        if (n >= 0) {
            const int count = qMin(int(n) + 1, outLength - written);
            const QByteArray literal = r.bytes(int(n) + 1);
            std::memcpy(out + written, literal.constData(), size_t(qMin(count, int(literal.size()))));
            written += count;
        } else if (n != -128) {
            const int count = qMin(1 - int(n), outLength - written);
            const char value = char(r.u8());
            std::memset(out + written, value, size_t(count));
            written += count;
        }
    }
    r.pos = stop;
    return r.ok();
}

// Undoes ZIP compression's prediction: each sample was stored as the
// difference to the one before it in its row. 32-bit rows are byte planes.
void undoPrediction(QByteArray& data, int width, int height, int bytesPerSample)
{
    uchar* p = reinterpret_cast<uchar*>(data.data());
    if (bytesPerSample == 1) {
        for (int y = 0; y < height; ++y) {
            uchar* row = p + qint64(y) * width;
            for (int x = 1; x < width; ++x) row[x] = uchar(row[x] + row[x - 1]);
        }
    } else if (bytesPerSample == 2) {
        for (int y = 0; y < height; ++y) {
            uchar* row = p + qint64(y) * width * 2;
            quint16 previous = qFromBigEndian<quint16>(row);
            for (int x = 1; x < width; ++x) {
                const quint16 value = quint16(qFromBigEndian<quint16>(row + x * 2) + previous);
                qToBigEndian<quint16>(value, row + x * 2);
                previous = value;
            }
        }
    } else if (bytesPerSample == 4) {
        QByteArray rowCopy(width * 4, Qt::Uninitialized);
        for (int y = 0; y < height; ++y) {
            uchar* row = p + qint64(y) * width * 4;
            for (int x = 1; x < width * 4; ++x) row[x] = uchar(row[x] + row[x - 1]);
            // Planes of first, second, third and fourth bytes back to floats.
            for (int x = 0; x < width; ++x)
                for (int b = 0; b < 4; ++b) rowCopy[x * 4 + b] = char(row[b * width + x]);
            std::memcpy(row, rowCopy.constData(), size_t(width) * 4);
        }
    }
}

// One sample of any depth as an 8-bit value. 32-bit documents are linear
// light; their colour samples are brought to sRGB, alpha stays coverage.
uchar sample8(const uchar* p, int depth, bool linearToSrgb)
{
    switch (depth) {
    case 16: return uchar((int(qFromBigEndian<quint16>(p)) * 255 + 32767) / 65535);
    case 32: {
        float value = 0.0f;
        const quint32 bits = qFromBigEndian<quint32>(p);
        std::memcpy(&value, &bits, sizeof value);
        double c = std::isfinite(value) ? qBound(0.0, double(value), 1.0) : 0.0;
        if (linearToSrgb) c = c <= 0.0031308 ? 12.92 * c : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
        return uchar(qBound(0, int(std::lround(c * 255.0)), 255));
    }
    default: return *p;
    }
}

QByteArray toEightBit(const QByteArray& raw, int width, int height, int depth, bool colour)
{
    const qint64 count = qint64(width) * height;
    if (depth == 8) return raw.left(count);
    QByteArray out(count, Qt::Uninitialized);
    if (depth == 1) {
        const int stride = (width + 7) / 8;
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                const uchar byte = uchar(raw.at(y * stride + x / 8));
                out[y * width + x] = char((byte >> (7 - x % 8)) & 1 ? 0 : 255);   // 1 is black
            }
        return out;
    }
    const int bytes = depth / 8;
    const uchar* p = reinterpret_cast<const uchar*>(raw.constData());
    for (qint64 i = 0; i < count; ++i) out[i] = char(sample8(p + i * bytes, depth, colour));
    return out;
}

// Rows of one plane in compression `compression` starting at the reader's
// position: raw, RLE (row byte counts given), or ZIP with or without
// prediction. Returns the samples at their depth.
QByteArray decodePlane(Reader& r, int compression, int width, int height, int depth, qint64 dataLength,
                       const QVector<qint64>& rowLengths)
{
    const int bytesPerSample = depth == 1 ? 0 : depth / 8;
    const qint64 rowBytes = depth == 1 ? (width + 7) / 8 : qint64(width) * bytesPerSample;
    const qint64 total = rowBytes * height;
    if (total <= 0 || total > (qint64(1) << 31)) return {};
    switch (compression) {
    case 0:
        return r.bytes(total);
    case 1: {
        QByteArray out(total, '\0');
        for (int y = 0; y < height && r.ok(); ++y)
            unpackBits(r, rowLengths.value(y), out.data() + y * rowBytes, int(rowBytes));
        return r.ok() ? out : QByteArray();
    }
    case 2:
    case 3: {
        QByteArray packed(4, '\0');
        qToBigEndian<quint32>(quint32(total), packed.data());
        packed += r.bytes(dataLength);
        QByteArray out = qUncompress(packed);
        if (out.size() != total) return {};
        if (compression == 3 && bytesPerSample > 0) undoPrediction(out, width, height, bytesPerSample);
        return out;
    }
    default:
        return {};
    }
}

// The colour channels a mode has.
int colourChannelCount(int mode)
{
    switch (mode) {
    case 3: return 3;   // RGB
    case 4: return 4;   // CMYK
    case 9: return 3;   // Lab
    default: return 1;  // grayscale, duotone, bitmap, indexed
    }
}

// A vector mask's path records: knots in fractions of the document size.
QPainterPath readVectorPath(Reader& r, const QSize& document)
{
    QPainterPath path;
    path.setFillRule(Qt::OddEvenFill);
    struct Knot { QPointF before, anchor, after; };
    QVector<Knot> knots;
    bool closed = false;
    int expected = 0;
    const auto point = [&r, &document] {
        const double y = double(r.i32()) / double(1 << 24) * document.height();
        const double x = double(r.i32()) / double(1 << 24) * document.width();
        return QPointF(x, y);
    };
    const auto flush = [&] {
        if (knots.isEmpty()) return;
        path.moveTo(knots.first().anchor);
        for (int i = 1; i < knots.size(); ++i)
            path.cubicTo(knots.at(i - 1).after, knots.at(i).before, knots.at(i).anchor);
        if (closed) {
            path.cubicTo(knots.last().after, knots.first().before, knots.first().anchor);
            path.closeSubpath();
        }
        knots.clear();
    };
    while (r.remaining() >= 26 && r.ok()) {
        const qint64 start = r.pos;
        const quint16 selector = r.u16();
        if (selector == 0 || selector == 3) {
            flush();
            closed = selector == 0;
            expected = r.u16();
        } else if (selector == 1 || selector == 2 || selector == 4 || selector == 5) {
            Knot knot;
            knot.before = point();
            knot.anchor = point();
            knot.after = point();
            knots.append(knot);
            if (--expected <= 0) flush();
        }
        r.pos = start + 26;
    }
    flush();
    return path;
}

} // namespace

QVariantMap readPsdDescriptor(const QByteArray& data, qint64 offset, bool* ok)
{
    Reader r(data, offset);
    QVariantMap map = readDescriptorBody(r, 0);
    if (ok) *ok = r.ok();
    return map;
}

double psdNumber(const QVariant& value, double fallback)
{
    if (value.typeId() == QMetaType::QVariantMap) {
        const QVariantMap map = value.toMap();
        return map.contains(QStringLiteral("value")) ? map.value(QStringLiteral("value")).toDouble() : fallback;
    }
    bool ok = false;
    const double number = value.toDouble(&ok);
    return ok ? number : fallback;
}

QColor psdColor(const QVariant& value, const QColor& fallback)
{
    const QVariantMap map = value.toMap();
    const QString kind = map.value(QStringLiteral("_class")).toString();
    if (kind == QLatin1String("RGBC")) {
        const auto channel = [&map](const char* key, const char* alternative) {
            const QVariant v = map.contains(QLatin1String(key)) ? map.value(QLatin1String(key))
                                                                 : map.value(QLatin1String(alternative));
            return qBound(0, int(std::lround(psdNumber(v))), 255);
        };
        return QColor(channel("Rd  ", "redFloat"), channel("Grn ", "greenFloat"), channel("Bl  ", "blueFloat"));
    }
    if (kind == QLatin1String("HSBC")) {
        const double h = psdNumber(map.value(QStringLiteral("H   ")));
        return QColor::fromHsvF(std::fmod(h / 360.0 + 1.0, 1.0),
                                qBound(0.0, psdNumber(map.value(QStringLiteral("Strt"))) / 100.0, 1.0),
                                qBound(0.0, psdNumber(map.value(QStringLiteral("Brgh"))) / 100.0, 1.0));
    }
    if (kind == QLatin1String("CMYC")) {
        return QColor::fromCmykF(qBound(0.0, psdNumber(map.value(QStringLiteral("Cyn "))) / 100.0, 1.0),
                                 qBound(0.0, psdNumber(map.value(QStringLiteral("Mgnt"))) / 100.0, 1.0),
                                 qBound(0.0, psdNumber(map.value(QStringLiteral("Ylw "))) / 100.0, 1.0),
                                 qBound(0.0, psdNumber(map.value(QStringLiteral("Blck"))) / 100.0, 1.0)).toRgb();
    }
    if (kind == QLatin1String("Grsc")) {
        const int gray = qBound(0, int(std::lround(255.0 - psdNumber(map.value(QStringLiteral("Gry "))) * 2.55)), 255);
        return QColor(gray, gray, gray);
    }
    return fallback;
}

// ---------------------------------------------------------------- PsdDocument

bool PsdDocument::load(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        m_error = file.errorString();
        return false;
    }
    return loadData(file.readAll());
}

bool PsdDocument::loadData(const QByteArray& data)
{
    m_data = data;
    m_layers.clear();
    m_linked.clear();
    m_mergedTransparency = false;
    m_error.clear();
    if (!parse()) {
        if (m_error.isEmpty()) m_error = QStringLiteral("The Photoshop document is damaged.");
        return false;
    }
    return true;
}

int PsdDocument::indexOfLayerId(quint32 id) const
{
    for (int i = 0; i < m_layers.size(); ++i)
        if (m_layers.at(i).id == id) return i;
    return -1;
}

bool PsdDocument::parse()
{
    Reader r(m_data);
    if (r.bytes(4) != "8BPS") {
        m_error = QStringLiteral("Not a Photoshop document.");
        return false;
    }
    m_version = r.u16();
    if (m_version != 1 && m_version != 2) {
        m_error = QStringLiteral("Unknown Photoshop document version %1.").arg(m_version);
        return false;
    }
    const bool wide = m_version == 2;
    r.skip(6);
    m_channels = r.u16();
    const quint32 height = r.u32();
    const quint32 width = r.u32();
    m_depth = r.u16();
    m_colorMode = r.u16();
    if (!r.ok() || width == 0 || height == 0 || width > 300000 || height > 300000
        || (m_depth != 1 && m_depth != 8 && m_depth != 16 && m_depth != 32)) {
        m_error = QStringLiteral("Unsupported Photoshop document header.");
        return false;
    }
    m_size = QSize(int(width), int(height));
    r.skip(r.u32());                               // colour mode data

    // Image resources: only the global light angle matters here (1037),
    // which layer styles that "use global light" take their angle from.
    const quint32 resourcesLength = r.u32();
    const qint64 resourcesEnd = r.pos + resourcesLength;
    while (r.pos + 12 <= resourcesEnd && r.ok()) {
        if (r.bytes(4) != "8BIM") break;
        const quint16 id = r.u16();
        const quint8 nameLength = r.u8();
        r.skip(nameLength + ((nameLength + 1) % 2));
        const quint32 size = r.u32();
        const qint64 next = r.pos + size + (size % 2);
        if (id == 1037 && size >= 4) m_globalAngle = r.i32();
        r.pos = next;
    }
    r.pos = resourcesEnd;
    if (!r.ok()) return false;

    const qint64 sectionLength = r.length(wide);
    const qint64 sectionStart = r.pos;
    const qint64 sectionEnd = sectionStart + sectionLength;
    if (sectionLength > 0) {
        const qint64 layerInfoLength = r.length(wide);
        const qint64 layerInfoStart = r.pos;
        if (layerInfoLength > 0 && !parseLayerInfo(layerInfoStart, layerInfoStart + layerInfoLength, wide))
            return false;
        r.pos = layerInfoStart + layerInfoLength;
        if (r.pos + 4 <= sectionEnd) {
            const quint32 globalMask = r.u32();
            r.pos += globalMask;
        }
        if (r.pos < sectionEnd) parseDocumentBlocks(r.pos, sectionEnd);
    }
    m_imageDataOffset = sectionEnd <= m_data.size() ? sectionEnd : -1;
    return true;
}

void PsdDocument::parseDocumentBlocks(qint64 begin, qint64 end)
{
    Reader r(m_data, begin, end);
    while (r.remaining() >= 12 && seekNextBlock(r)) {
        r.skip(4);
        const QString key = r.key();
        const qint64 length = r.length(m_version == 2 && wideBlockKey(key));
        const qint64 dataStart = r.pos;
        if (!r.need(length)) break;
        if ((key == QLatin1String("Lr16") || key == QLatin1String("Lr32") || key == QLatin1String("Layr"))
            && m_layers.isEmpty()) {
            parseLayerInfo(dataStart, dataStart + length, m_version == 2);
        } else if (key == QLatin1String("lnk2") || key == QLatin1String("lnkD") || key == QLatin1String("lnk3")) {
            parseLinkedFiles(dataStart, dataStart + length);
        }
        r.pos = dataStart + length;
    }
}

void PsdDocument::parseLinkedFiles(qint64 begin, qint64 end)
{
    Reader r(m_data, begin, end);
    while (r.remaining() > 8 && r.ok()) {
        const qint64 itemLength = qint64(r.u64());
        const qint64 itemStart = r.pos;
        const qint64 itemEnd = itemStart + itemLength;
        if (itemLength <= 0 || itemEnd > end) break;
        Reader item(m_data, itemStart, itemEnd);
        const QString type = item.key();
        const quint32 version = item.u32();
        Q_UNUSED(version);
        const quint8 idLength = item.u8();
        PsdLinkedFile file;
        file.uniqueId = QString::fromLatin1(item.bytes(idLength));
        file.name = item.unicode();
        file.fileType = item.key();
        item.key();                                 // creator
        const qint64 dataLength = qint64(item.u64());
        if (item.u8() != 0) {                       // a file open descriptor
            item.u32();
            readDescriptorBody(item, 0);
        }
        if (type == QLatin1String("liFD") && item.ok() && item.pos + dataLength <= itemEnd) {
            file.offset = item.pos;
            file.length = dataLength;
            m_linked.insert(file.uniqueId, file);
        }
        r.pos = itemEnd;
        while (r.pos % 4 != 0 && r.pos < end) ++r.pos;
    }
}

bool PsdDocument::parseLayerInfo(qint64 begin, qint64 end, bool wide)
{
    Reader r(m_data, begin, end);
    const int signedCount = r.i16();
    if (signedCount < 0) m_mergedTransparency = true;
    const int count = std::abs(signedCount);
    QVector<PsdLayer> layers;
    layers.reserve(count);
    for (int i = 0; i < count && r.ok(); ++i) {
        PsdLayer layer;
        const qint32 top = r.i32(), left = r.i32(), bottom = r.i32(), right = r.i32();
        layer.rect = QRect(QPoint(left, top), QPoint(right - 1, bottom - 1));
        if (right <= left || bottom <= top) layer.rect = QRect(left, top, 0, 0);
        const quint16 channels = r.u16();
        if (channels > 64) {
            m_error = QStringLiteral("A layer record of the Photoshop document is damaged.");
            return false;
        }
        for (int c = 0; c < channels; ++c) {
            PsdChannel channel;
            channel.id = r.i16();
            channel.length = r.length(wide);
            layer.channels.append(channel);
        }
        if (r.bytes(4) != "8BIM") {
            m_error = QStringLiteral("A layer record of the Photoshop document is damaged.");
            return false;
        }
        layer.blendKey = r.key();
        layer.opacity = r.u8();
        layer.clipping = r.u8() != 0;
        const quint8 flags = r.u8();
        layer.visible = !(flags & 0x02);
        r.skip(1);
        const quint32 extraLength = r.u32();
        const qint64 extraEnd = r.pos + extraLength;

        // Layer mask data: the user mask, and with a vector mask also the
        // "real" one (channel -3) with its own rectangle.
        const quint32 maskLength = r.u32();
        const qint64 maskEnd = r.pos + maskLength;
        if (maskLength >= 20) {
            const qint32 mt = r.i32(), ml = r.i32(), mb = r.i32(), mr = r.i32();
            layer.rasterMask.rect = QRect(QPoint(ml, mt), QPoint(mr - 1, mb - 1));
            layer.rasterMask.defaultColor = r.u8();
            const quint8 maskFlags = r.u8();
            layer.rasterMask.disabled = maskFlags & 0x02;
            if (maskLength > 20) {
                if (maskFlags & 0x10) {
                    const quint8 parameters = r.u8();
                    if (parameters & 0x01) r.skip(1);
                    if (parameters & 0x02) r.skip(8);
                    if (parameters & 0x04) r.skip(1);
                    if (parameters & 0x08) r.skip(8);
                }
                if (maskEnd - r.pos >= 18) {
                    const quint8 realFlags = r.u8();
                    const quint8 realBackground = r.u8();
                    const qint32 rt = r.i32(), rl = r.i32(), rb = r.i32(), rr = r.i32();
                    bool hasReal = false;
                    for (const PsdChannel& channel : layer.channels) hasReal = hasReal || channel.id == -3;
                    if (hasReal) {
                        layer.rasterMask.rect = QRect(QPoint(rl, rt), QPoint(rr - 1, rb - 1));
                        layer.rasterMask.defaultColor = realBackground;
                        layer.rasterMask.disabled = realFlags & 0x02;
                        layer.rasterMask.channel = -3;
                    }
                }
            }
            for (const PsdChannel& channel : layer.channels)
                layer.hasRasterMask = layer.hasRasterMask || channel.id == layer.rasterMask.channel;
        }
        r.pos = maskEnd;
        r.skip(r.u32());                            // blending ranges
        const quint8 nameLength = r.u8();
        layer.name = QString::fromLatin1(r.bytes(nameLength));
        r.skip((4 - (1 + nameLength) % 4) % 4);

        while (r.pos + 12 <= extraEnd && r.ok() && seekNextBlock(r)) {
            r.skip(4);
            const QString key = r.key();
            const qint64 length = r.length(wide && wideBlockKey(key));
            const qint64 dataStart = r.pos;
            if (!r.need(length) || dataStart + length > extraEnd) break;
            const QByteArray block = m_data.mid(dataStart, length);
            Reader b(m_data, dataStart, dataStart + length);
            if (key == QLatin1String("luni")) {
                const QString name = b.unicode();
                if (b.ok() && !name.isEmpty()) layer.name = name;
            } else if (key == QLatin1String("lyid")) {
                layer.id = b.u32();
            } else if (key == QLatin1String("lsct") || key == QLatin1String("lsdk")) {
                const quint32 type = b.u32();
                if (type == 1 || type == 2) layer.kind = PsdLayer::Kind::GroupStart;
                else if (type == 3) layer.kind = PsdLayer::Kind::GroupEnd;
                if (b.remaining() >= 8 && b.bytes(4) == "8BIM") layer.blendKey = b.key();
            } else if (key == QLatin1String("iOpa")) {
                layer.fillOpacity = b.u8();
            } else if (key == QLatin1String("lfx2")) {
                b.u32();
                b.u32();
                const QVariantMap effects = readDescriptorBody(b, 0);
                if (b.ok()) layer.effects = effects;
            } else if (key == QLatin1String("SoLd") || key == QLatin1String("SoLE")) {
                b.skip(4);                          // 'soLD'
                b.u32();
                b.u32();
                const QVariantMap smart = readDescriptorBody(b, 0);
                if (b.ok()) {
                    layer.smartObject = smart;
                    if (layer.kind == PsdLayer::Kind::Pixel) layer.kind = PsdLayer::Kind::SmartObject;
                }
            } else if (key == QLatin1String("TySh")) {
                if (layer.kind == PsdLayer::Kind::Pixel) layer.kind = PsdLayer::Kind::Text;
            } else if (key == QLatin1String("vmsk") || key == QLatin1String("vsms")) {
                b.u32();
                const quint32 maskFlags = b.u32();
                layer.hasVectorMask = true;
                layer.vectorMaskInverted = maskFlags & 0x01;
                layer.vectorMaskDisabled = maskFlags & 0x04;
                layer.vectorMask = readVectorPath(b, m_size);
            } else {
                static const QStringList adjustments = {
                    QStringLiteral("levl"), QStringLiteral("curv"), QStringLiteral("brit"),
                    QStringLiteral("blnc"), QStringLiteral("hue2"), QStringLiteral("hue "),
                    QStringLiteral("selc"), QStringLiteral("mixr"), QStringLiteral("grdm"),
                    QStringLiteral("phfl"), QStringLiteral("expA"), QStringLiteral("vibA"),
                    QStringLiteral("blwh"), QStringLiteral("clrL"), QStringLiteral("nvrt"),
                    QStringLiteral("post"), QStringLiteral("thrs"), QStringLiteral("CgEd")};
                // Brightness/Contrast writes both 'brit' and its descriptor
                // 'CgEd'; the descriptor is the one that holds the values.
                if (adjustments.contains(key)
                    && !(key == QLatin1String("brit") && layer.adjustmentKey == QLatin1String("CgEd"))) {
                    layer.kind = PsdLayer::Kind::Adjustment;
                    layer.adjustmentKey = key;
                    layer.adjustmentData = block;
                }
            }
            r.pos = dataStart + length;
        }
        r.pos = extraEnd;
        layers.append(layer);
    }
    if (!r.ok()) {
        m_error = QStringLiteral("The layers of the Photoshop document are damaged.");
        return false;
    }
    // Channel image data follows the records, layer after layer.
    for (PsdLayer& layer : layers) {
        for (PsdChannel& channel : layer.channels) {
            channel.offset = r.pos;
            r.pos += channel.length;
        }
    }
    // Documents from before layer ids ('lyid') get ids by position, which
    // stay the same as long as the file does.
    for (int i = 0; i < layers.size(); ++i)
        if (layers.at(i).id == 0) layers[i].id = 0x80000000u + quint32(i);
    m_layers = layers;
    return true;
}

QByteArray PsdDocument::channelPlane(const PsdChannel& channel, int width, int height) const
{
    if (width <= 0 || height <= 0 || channel.length < 2) return {};
    Reader r(m_data, channel.offset, channel.offset + channel.length);
    const int compression = r.u16();
    QVector<qint64> rows;
    if (compression == 1) {
        rows.reserve(height);
        for (int y = 0; y < height; ++y) rows.append(m_version == 2 ? qint64(r.u32()) : qint64(r.u16()));
    }
    const QByteArray raw = decodePlane(r, compression, width, height, m_depth, channel.length - 2, rows);
    if (raw.isEmpty()) return {};
    return toEightBit(raw, width, height, m_depth, channel.id >= 0);
}

QImage PsdDocument::planesToImage(const QVector<QByteArray>& colour, const QByteArray& alpha, int width,
                                  int height) const
{
    QImage image(width, height, QImage::Format_RGBA8888);
    if (image.isNull()) return {};
    const qint64 count = qint64(width) * height;
    const auto plane = [&](int index, qint64 i, uchar fallback) {
        return index < colour.size() && colour.at(index).size() >= count ? uchar(colour.at(index).at(i)) : fallback;
    };
    for (int y = 0; y < height; ++y) {
        uchar* row = image.scanLine(y);
        for (int x = 0; x < width; ++x) {
            const qint64 i = qint64(y) * width + x;
            int red = 0, green = 0, blue = 0;
            switch (m_colorMode) {
            case 3:
                red = plane(0, i, 0);
                green = plane(1, i, 0);
                blue = plane(2, i, 0);
                break;
            case 4: {
                // CMYK is stored as 255 minus the ink.
                const int k = plane(3, i, 255);
                red = plane(0, i, 255) * k / 255;
                green = plane(1, i, 255) * k / 255;
                blue = plane(2, i, 255) * k / 255;
                break;
            }
            case 9: {
                // Lab (D50) to sRGB, near enough for a preview.
                const double l = plane(0, i, 0) * 100.0 / 255.0;
                const double a = plane(1, i, 128) - 128.0;
                const double b = plane(2, i, 128) - 128.0;
                const double fy = (l + 16.0) / 116.0, fx = fy + a / 500.0, fz = fy - b / 200.0;
                const auto inverse = [](double t) { return t > 6.0 / 29.0 ? t * t * t : 3.0 * 36.0 / 841.0 * (t - 4.0 / 29.0); };
                const double X = 0.9642 * inverse(fx), Y = inverse(fy), Z = 0.8251 * inverse(fz);
                const auto encode = [](double c) {
                    c = qBound(0.0, c, 1.0);
                    c = c <= 0.0031308 ? 12.92 * c : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
                    return int(std::lround(c * 255.0));
                };
                red = encode(3.1339 * X - 1.6169 * Y - 0.4906 * Z);
                green = encode(-0.9785 * X + 1.9160 * Y + 0.0333 * Z);
                blue = encode(0.0720 * X - 0.2290 * Y + 1.4057 * Z);
                break;
            }
            default:
                red = green = blue = plane(0, i, 0);
                break;
            }
            row[x * 4] = uchar(red);
            row[x * 4 + 1] = uchar(green);
            row[x * 4 + 2] = uchar(blue);
            row[x * 4 + 3] = alpha.size() >= count ? uchar(alpha.at(i)) : uchar(255);
        }
    }
    return image;
}

void PsdDocument::applyMasks(QImage& image, const QRect& area, const PsdLayer& owner) const
{
    const int width = image.width(), height = image.height();
    // The raster mask, over its own rectangle with its default value around.
    if (owner.hasRasterMask && !owner.rasterMask.disabled) {
        const QRect& m = owner.rasterMask.rect;
        QByteArray mask;
        for (const PsdChannel& channel : owner.channels)
            if (channel.id == owner.rasterMask.channel) mask = channelPlane(channel, m.width(), m.height());
        const bool decoded = mask.size() >= qint64(m.width()) * m.height();
        for (int y = 0; y < height; ++y) {
            uchar* row = image.scanLine(y);
            for (int x = 0; x < width; ++x) {
                const int dx = area.x() + x - m.x(), dy = area.y() + y - m.y();
                const int value = decoded && dx >= 0 && dy >= 0 && dx < m.width() && dy < m.height()
                    ? uchar(mask.at(qint64(dy) * m.width() + dx)) : owner.rasterMask.defaultColor;
                row[x * 4 + 3] = uchar(row[x * 4 + 3] * value / 255);
            }
        }
    }
    // The vector mask, drawn with antialiasing.
    if (owner.hasVectorMask && !owner.vectorMaskDisabled && !owner.vectorMask.isEmpty()) {
        QImage coverage(width, height, QImage::Format_Alpha8);
        coverage.fill(0);
        {
            QPainter painter(&coverage);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.translate(-area.topLeft());
            painter.fillPath(owner.vectorMask, Qt::black);
        }
        for (int y = 0; y < height; ++y) {
            uchar* row = image.scanLine(y);
            const uchar* cover = coverage.constScanLine(y);
            for (int x = 0; x < width; ++x) {
                const int value = owner.vectorMaskInverted ? 255 - cover[x] : cover[x];
                row[x * 4 + 3] = uchar(row[x * 4 + 3] * value / 255);
            }
        }
    }
}

QVector<int> PsdDocument::enclosingGroups(int index) const
{
    // A group's folder record is above its layers, past the dividers and
    // folders of the groups nested in it.
    QVector<int> groups;
    int depth = 0;
    for (int j = index + 1; j < m_layers.size(); ++j) {
        const PsdLayer::Kind kind = m_layers.at(j).kind;
        if (kind == PsdLayer::Kind::GroupEnd) {
            ++depth;
        } else if (kind == PsdLayer::Kind::GroupStart) {
            if (depth == 0) groups.append(j);
            else --depth;
        }
    }
    return groups;
}

QImage PsdDocument::layerImage(int index) const
{
    if (index < 0 || index >= m_layers.size()) return {};
    const PsdLayer& layer = m_layers.at(index);
    const int width = layer.rect.width(), height = layer.rect.height();
    if (width <= 0 || height <= 0) return {};
    const int colours = colourChannelCount(m_colorMode);
    QVector<QByteArray> colour(colours);
    QByteArray alpha;
    for (const PsdChannel& channel : layer.channels) {
        if (channel.id >= 0 && channel.id < colours) colour[channel.id] = channelPlane(channel, width, height);
        else if (channel.id == -1) alpha = channelPlane(channel, width, height);
    }
    QImage image = planesToImage(colour, alpha, width, height);
    if (image.isNull()) return {};
    // Its own masks, then those of the groups it is in, which mask
    // everything inside them.
    applyMasks(image, layer.rect, layer);
    for (int group : enclosingGroups(index)) applyMasks(image, layer.rect, m_layers.at(group));
    return image;
}

QImage PsdDocument::compositeImage() const
{
    if (m_imageDataOffset < 0) return {};
    Reader r(m_data, m_imageDataOffset);
    const int compression = r.u16();
    const int width = m_size.width(), height = m_size.height();
    const int channels = m_channels;
    QVector<QByteArray> planes;
    if (compression == 1) {
        QVector<qint64> rows;
        for (int i = 0; i < channels * height; ++i) rows.append(m_version == 2 ? qint64(r.u32()) : qint64(r.u16()));
        for (int c = 0; c < channels && r.ok(); ++c)
            planes.append(decodePlane(r, 1, width, height, m_depth, 0, rows.mid(c * height, height)));
    } else if (compression == 0) {
        for (int c = 0; c < channels && r.ok(); ++c)
            planes.append(decodePlane(r, 0, width, height, m_depth, 0, {}));
    } else {
        // One ZIP stream for all channels.
        const int bytesPerSample = qMax(1, m_depth / 8);
        const qint64 planeBytes = qint64(width) * height * bytesPerSample;
        const QByteArray all = decodePlane(r, compression == 3 ? 2 : compression, width, height * channels,
                                           m_depth, r.remaining(), {});
        for (int c = 0; c < channels; ++c) {
            QByteArray plane = all.mid(c * planeBytes, planeBytes);
            if (compression == 3) undoPrediction(plane, width, height, bytesPerSample);
            planes.append(plane);
        }
    }
    if (!r.ok() || planes.isEmpty()) return {};
    const int colours = colourChannelCount(m_colorMode);
    QVector<QByteArray> colour;
    for (int c = 0; c < qMin(colours, int(planes.size())); ++c)
        colour.append(toEightBit(planes.at(c), width, height, m_depth, true));
    // The first extra channel is the document's transparency only when the
    // layer count says so; otherwise the extra channels are saved
    // selections (project_4's has two: a shadow's) and the image is opaque.
    QByteArray alpha;
    if (planes.size() > colours && m_mergedTransparency)
        alpha = toEightBit(planes.at(colours), width, height, m_depth, false);
    return planesToImage(colour, alpha, width, height);
}

QByteArray PsdDocument::linkedFileData(const QString& uniqueId) const
{
    const auto it = m_linked.constFind(uniqueId);
    return it == m_linked.cend() ? QByteArray() : m_data.mid(it->offset, it->length);
}

QByteArray PsdDocument::linkedFileView(const QString& uniqueId) const
{
    const auto it = m_linked.constFind(uniqueId);
    if (it == m_linked.cend() || it->offset < 0 || it->offset + it->length > m_data.size()) return {};
    return QByteArray::fromRawData(m_data.constData() + it->offset, it->length);
}

QVector<bool> psdLayersShown(const PsdDocument& document)
{
    // Groups enclose the records below their folder record, down to their
    // divider, so the hierarchy is walked from the top.
    const QVector<PsdLayer>& layers = document.layers();
    QVector<bool> shown(layers.size(), true);
    QVector<bool> stack{true};
    for (int i = int(layers.size()) - 1; i >= 0; --i) {
        const PsdLayer& layer = layers.at(i);
        const bool parent = stack.last();
        if (layer.kind == PsdLayer::Kind::GroupStart) {
            shown[i] = parent && layer.visible;
            stack.append(shown[i]);
        } else if (layer.kind == PsdLayer::Kind::GroupEnd) {
            shown[i] = parent;
            if (stack.size() > 1) stack.removeLast();
        } else {
            shown[i] = parent && layer.visible;
        }
    }
    return shown;
}

// ------------------------------------------------------------ paths and cache

bool isPsdMarkerPath(const QString& path)
{
    return path.contains(QLatin1String(kPsdLayerMarker)) || path.contains(QLatin1String(kPsdSourceMarker));
}

QString psdFileOfPath(const QString& path)
{
    // The first marker ends the file's part (a nested path has several).
    qsizetype first = -1;
    for (const char* marker : {kPsdLayerMarker, kPsdSourceMarker}) {
        const qsizetype at = path.indexOf(QLatin1String(marker));
        if (at >= 0 && (first < 0 || at < first)) first = at;
    }
    return first >= 0 ? path.left(first) : path;
}

QString psdDocumentOfPath(const QString& path)
{
    const qsizetype layerAt = path.indexOf(QLatin1String(kPsdLayerMarker));
    return layerAt >= 0 ? path.left(layerAt) : path;
}

bool isPsdFile(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QLatin1String("psd") || suffix == QLatin1String("psb");
}

namespace {

// Parsed documents, shared by the media items of their layers. A document is
// read again when its file changes on disk.
std::shared_ptr<const PsdDocument> cachedDocument(const QString& file)
{
    struct Entry
    {
        QString path;
        qint64 size = 0;
        QDateTime modified;
        std::shared_ptr<const PsdDocument> document;
    };
    static QMutex mutex;
    static QList<Entry> entries;
    const QFileInfo info(file);
    if (!info.isFile()) return nullptr;
    QMutexLocker lock(&mutex);
    for (int i = 0; i < entries.size(); ++i) {
        const Entry& entry = entries.at(i);
        if (entry.path == info.absoluteFilePath()) {
            if (entry.size == info.size() && entry.modified == info.lastModified()) {
                entries.move(i, 0);
                return entries.first().document;
            }
            entries.removeAt(i);
            break;
        }
    }
    auto document = std::make_shared<PsdDocument>();
    if (!document->load(info.absoluteFilePath())) return nullptr;
    entries.prepend({info.absoluteFilePath(), info.size(), info.lastModified(), document});
    while (entries.size() > 4) entries.removeLast();
    return document;
}

// What a marked path names: a layer (and the layer it is clipped to), or a
// smart object's embedded source.
struct MarkerTarget
{
    std::shared_ptr<const PsdDocument> document;
    int layer = -1;
    int clipBase = -1;
    QString sourceId;
};

// A layer path: its document and layer. A source path: the document the
// source is embedded in and the source's id.
MarkerTarget resolveMarker(const QString& path)
{
    MarkerTarget target;
    const qsizetype layerAt = path.indexOf(QLatin1String(kPsdLayerMarker));
    if (layerAt >= 0) {
        target.document = openPsdDocument(path.left(layerAt));
        if (!target.document) return target;
        const QStringList parts = path.mid(layerAt + qsizetype(qstrlen(kPsdLayerMarker))).split(QLatin1Char(':'));
        target.layer = target.document->indexOfLayerId(parts.value(0).toUInt());
        if (parts.size() >= 3 && parts.at(1) == QLatin1String("clip"))
            target.clipBase = target.document->indexOfLayerId(parts.at(2).toUInt());
        return target;
    }
    const qsizetype sourceAt = path.lastIndexOf(QLatin1String(kPsdSourceMarker));
    if (sourceAt < 0) return target;
    target.document = openPsdDocument(path.left(sourceAt));
    target.sourceId = path.mid(sourceAt + qsizetype(qstrlen(kPsdSourceMarker)));
    return target;
}

// A document without a flattened image, drawn from its visible pixel layers.
QImage flattenLayers(const PsdDocument& document);

// A smart object's source: an embedded document's flattened image, or the
// image file it embeds.
QImage sourceImage(const QString& path, const PsdDocument& document, const QString& uniqueId)
{
    const QByteArray data = document.linkedFileView(uniqueId);
    if (data.isEmpty()) return {};
    if (data.startsWith("8BPS")) {
        const auto inner = openPsdDocument(path);
        if (!inner) return {};
        const QImage composite = inner->compositeImage();
        return composite.isNull() ? flattenLayers(*inner) : composite;
    }
    QImage image = QImage::fromData(data);
    return image.isNull() ? QImage() : image.convertToFormat(QImage::Format_RGBA8888);
}

// A document without a flattened image, drawn from its visible pixel layers.
QImage flattenLayers(const PsdDocument& document)
{
    QImage canvas(document.size(), QImage::Format_RGBA8888);
    canvas.fill(Qt::transparent);
    QPainter painter(&canvas);
    const QVector<bool> shown = psdLayersShown(document);
    for (int i = 0; i < document.layers().size(); ++i) {
        const PsdLayer& layer = document.layers().at(i);
        if (!shown.at(i) || layer.kind == PsdLayer::Kind::Adjustment
            || layer.kind == PsdLayer::Kind::GroupStart || layer.kind == PsdLayer::Kind::GroupEnd)
            continue;
        const QImage image = document.layerImage(i);
        if (image.isNull()) continue;
        painter.setOpacity(layer.opacity / 255.0);
        painter.drawImage(layer.rect.topLeft(), image);
    }
    return canvas;
}

} // namespace

QImage readStillImage(const QString& path)
{
    if (isPsdMarkerPath(path)) {
        const MarkerTarget target = resolveMarker(path);
        if (!target.document) return {};
        if (!target.sourceId.isEmpty()) return sourceImage(path, *target.document, target.sourceId);
        QImage image = target.document->layerImage(target.layer);
        if (image.isNull() || target.clipBase < 0) return image;
        // Clipped: only where the base layer is.
        const PsdLayer& own = target.document->layers().at(target.layer);
        const PsdLayer& base = target.document->layers().at(target.clipBase);
        const QImage baseImage = target.document->layerImage(target.clipBase);
        for (int y = 0; y < image.height(); ++y) {
            uchar* row = image.scanLine(y);
            for (int x = 0; x < image.width(); ++x) {
                const QPoint at = own.rect.topLeft() + QPoint(x, y) - base.rect.topLeft();
                const int cover = baseImage.valid(at) ? uchar(baseImage.constScanLine(at.y())[at.x() * 4 + 3]) : 0;
                row[x * 4 + 3] = uchar(row[x * 4 + 3] * cover / 255);
            }
        }
        return image;
    }
    if (isPsdFile(path)) {
        const auto document = cachedDocument(path);
        if (!document) return {};
        QImage composite = document->compositeImage();
        return composite.isNull() ? flattenLayers(*document) : composite;
    }
    return QImage(path);
}

QSize stillImageSize(const QString& path)
{
    if (isPsdMarkerPath(path)) {
        const MarkerTarget target = resolveMarker(path);
        if (!target.document) return {};
        if (!target.sourceId.isEmpty()) {
            QByteArray data = target.document->linkedFileView(target.sourceId);
            if (data.startsWith("8BPS")) {
                const auto inner = openPsdDocument(path);
                return inner ? inner->size() : QSize();
            }
            QBuffer buffer(&data);
            return QImageReader(&buffer).size();
        }
        return target.layer >= 0 ? target.document->layers().at(target.layer).rect.size() : QSize();
    }
    if (isPsdFile(path)) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return {};
        const QByteArray header = file.read(26);
        if (header.size() < 26 || !header.startsWith("8BPS")) return {};
        return QSize(int(qFromBigEndian<quint32>(header.constData() + 18)),
                     int(qFromBigEndian<quint32>(header.constData() + 14)));
    }
    return QImageReader(path).size();
}

std::shared_ptr<const PsdDocument> openPsdDocument(const QString& path)
{
    const qsizetype sourceAt = path.lastIndexOf(QLatin1String(kPsdSourceMarker));
    if (sourceAt < 0) return cachedDocument(path);
    const auto parent = openPsdDocument(path.left(sourceAt));
    if (!parent) return nullptr;
    const QString uniqueId = path.mid(sourceAt + qsizetype(qstrlen(kPsdSourceMarker)));
    // Embedded documents, by their path; one whose parent was read again
    // (its file changed) is read again too.
    struct Entry
    {
        QString path;
        const PsdDocument* parent = nullptr;
        std::shared_ptr<const PsdDocument> document;
    };
    static QMutex mutex;
    static QList<Entry> entries;
    QMutexLocker lock(&mutex);
    for (int i = 0; i < entries.size(); ++i) {
        if (entries.at(i).path != path) continue;
        if (entries.at(i).parent == parent.get()) {
            entries.move(i, 0);
            return entries.first().document;
        }
        entries.removeAt(i);
        break;
    }
    const QByteArray data = parent->linkedFileView(uniqueId);
    if (!data.startsWith("8BPS")) return nullptr;
    auto* inner = new PsdDocument;
    if (!inner->loadData(data)) {
        delete inner;
        return nullptr;
    }
    // The bytes are the parent's: it lives as long as the embedded one.
    std::shared_ptr<const PsdDocument> document(inner, [parent](const PsdDocument* d) { delete d; });
    entries.prepend({path, parent.get(), document});
    while (entries.size() > 4) entries.removeLast();
    return document;
}

QString psdLayerName(const QString& path)
{
    if (!isPsdMarkerPath(path)) return {};
    const MarkerTarget target = resolveMarker(path);
    if (!target.document) return {};
    if (!target.sourceId.isEmpty()) return target.document->linkedFiles().value(target.sourceId).name;
    return target.layer >= 0 ? target.document->layers().at(target.layer).name : QString();
}

} // namespace media
} // namespace openvegas
