#pragma once

// Writes small Photoshop documents for the import tests: layers of any of
// the channel compressions, masks, vector masks, groups, adjustment layers,
// layer styles and smart objects with smart filters and embedded sources,
// laid out as Adobe's file format specification describes - the same
// specification media/PsdDocument reads by, so a test documents a reading
// of it rather than checking the reader against itself; the sample PSDs in
// SAMPLES (made by Photoshop) are read by the tests too.

#include <QByteArray>
#include <QColor>
#include <QImage>
#include <QRect>
#include <QString>
#include <QVariant>
#include <QVector>
#include <QtEndian>

#include <cstring>

namespace psdtest {

class Bytes
{
public:
    QByteArray data;
    void u8(int v) { data.append(char(v)); }
    void u16(int v) { char b[2]; qToBigEndian<quint16>(quint16(v), b); data.append(b, 2); }
    void u32(quint32 v) { char b[4]; qToBigEndian<quint32>(v, b); data.append(b, 4); }
    void i32(qint32 v) { u32(quint32(v)); }
    void f32(float v) { quint32 bits; std::memcpy(&bits, &v, 4); u32(bits); }
    void f64(double v) { quint64 bits; std::memcpy(&bits, &v, 8); char b[8]; qToBigEndian<quint64>(bits, b); data.append(b, 8); }
    void raw(const QByteArray& bytes) { data.append(bytes); }
    void key(const char* k) { data.append(k, 4); }
    void unicode(const QString& text)
    {
        u32(quint32(text.size() + 1));
        for (QChar c : text) u16(c.unicode());
        u16(0);
    }
    void id(const QString& name)
    {
        if (name.size() == 4) { u32(0); data.append(name.toLatin1()); }
        else { u32(quint32(name.size())); data.append(name.toLatin1()); }
    }
};

// Descriptor values: a QVariantMap with "_class" is an object, one with
// "_unit" a unit value, one with "_enum" an enumeration; lists, doubles,
// ints, bools and strings are themselves.
inline void writeItem(Bytes& b, const QVariant& value);

inline void writeDescriptor(Bytes& b, const QVariantMap& map)
{
    b.unicode(QString());
    b.id(map.value(QStringLiteral("_class")).toString());
    int count = 0;
    for (auto it = map.cbegin(); it != map.cend(); ++it) if (it.key() != QLatin1String("_class")) ++count;
    b.u32(quint32(count));
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
        if (it.key() == QLatin1String("_class")) continue;
        b.id(it.key());
        writeItem(b, it.value());
    }
}

inline void writeItem(Bytes& b, const QVariant& value)
{
    switch (value.typeId()) {
    case QMetaType::QVariantMap: {
        const QVariantMap map = value.toMap();
        if (map.contains(QStringLiteral("_unit"))) {
            b.key("UntF");
            b.raw(map.value(QStringLiteral("_unit")).toString().toLatin1());
            b.f64(map.value(QStringLiteral("value")).toDouble());
        } else if (map.contains(QStringLiteral("_enum"))) {
            b.key("enum");
            b.id(map.value(QStringLiteral("_enum")).toString());
            b.id(map.value(QStringLiteral("value")).toString());
        } else {
            b.key("Objc");
            writeDescriptor(b, map);
        }
        break;
    }
    case QMetaType::QVariantList: {
        b.key("VlLs");
        const QVariantList list = value.toList();
        b.u32(quint32(list.size()));
        for (const QVariant& item : list) writeItem(b, item);
        break;
    }
    case QMetaType::Double: b.key("doub"); b.f64(value.toDouble()); break;
    case QMetaType::Int: b.key("long"); b.i32(value.toInt()); break;
    case QMetaType::Bool: b.key("bool"); b.u8(value.toBool() ? 1 : 0); break;
    default: b.key("TEXT"); b.unicode(value.toString()); break;
    }
}

inline QVariantMap unit(const char* u, double v) { return {{"_unit", QString::fromLatin1(u)}, {"value", v}}; }
inline QVariantMap enumValue(const char* type, const char* v)
{
    return {{"_enum", QString::fromLatin1(type)}, {"value", QString::fromLatin1(v)}};
}
inline QVariantMap rgb(const QColor& c)
{
    return {{"_class", "RGBC"}, {"Rd  ", double(c.red())}, {"Grn ", double(c.green())}, {"Bl  ", double(c.blue())}};
}

struct Layer
{
    QString name;
    QRect rect;
    QImage pixels;                // rect-sized, RGBA
    QByteArray blend = "norm";
    int opacity = 255;
    bool clipping = false;
    bool hidden = false;
    int compression = 0;          // 0 raw, 1 RLE, 2 ZIP, 3 ZIP with prediction
    quint32 id = 0;
    int section = 0;              // 'lsct': 1 folder, 3 divider
    QRect maskRect;               // a raster mask (channel -2), when not empty
    QImage mask;                  // maskRect-sized, Grayscale8
    int maskDefault = 0;
    QByteArray vectorMask;        // 'vmsk' data, when not empty
    QVector<QPair<QByteArray, QByteArray>> blocks;   // further tagged blocks
};

inline QByteArray packBitsRow(const QByteArray& row)
{
    // Literal runs only: simple, and still a valid PackBits stream.
    QByteArray out;
    for (int i = 0; i < row.size(); i += 128) {
        const int n = qMin(128, int(row.size()) - i);
        out.append(char(n - 1));
        out.append(row.mid(i, n));
    }
    return out;
}

inline QByteArray encodePlane(const QByteArray& plane, int width, int height, int compression)
{
    Bytes b;
    b.u16(compression);
    if (compression == 0) {
        b.raw(plane);
    } else if (compression == 1) {
        QVector<QByteArray> rows;
        for (int y = 0; y < height; ++y) rows.append(packBitsRow(plane.mid(y * width, width)));
        for (const QByteArray& row : rows) b.u16(int(row.size()));
        for (const QByteArray& row : rows) b.raw(row);
    } else {
        QByteArray source = plane;
        if (compression == 3) {
            for (int y = 0; y < height; ++y)
                for (int x = width - 1; x > 0; --x)
                    source[y * width + x] = char(uchar(source[y * width + x]) - uchar(source[y * width + x - 1]));
        }
        // qCompress writes a zlib stream after a 4-byte length.
        b.raw(qCompress(source).mid(4));
    }
    return b.data;
}

inline QByteArray channelPlane(const QImage& image, int channel)
{
    const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
    QByteArray plane(rgba.width() * rgba.height(), '\0');
    for (int y = 0; y < rgba.height(); ++y)
        for (int x = 0; x < rgba.width(); ++x)
            plane[y * rgba.width() + x] = char(rgba.constScanLine(y)[x * 4 + (channel < 0 ? 3 : channel)]);
    return plane;
}

// A vector mask of one closed rectangle, in document pixels.
inline QByteArray rectangleVectorMask(const QRectF& r, const QSize& document)
{
    Bytes b;
    b.u32(3);
    b.u32(0);
    const auto record = [&b](int selector, auto body) {
        const int start = int(b.data.size());
        b.u16(selector);
        body();
        while (b.data.size() < start + 26) b.u8(0);
    };
    record(6, [] {});
    record(0, [&b] { b.u16(4); });
    const QPointF corners[] = {r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft()};
    for (const QPointF& p : corners) {
        record(1, [&] {
            for (int i = 0; i < 3; ++i) {
                b.i32(qint32(p.y() / document.height() * (1 << 24)));
                b.i32(qint32(p.x() / document.width() * (1 << 24)));
            }
        });
    }
    return b.data;
}

inline QByteArray descriptorBlock(const QVariantMap& descriptor, const char* prefix = nullptr)
{
    Bytes b;
    if (prefix) b.key(prefix);
    b.u32(prefix ? 4 : 0);
    b.u32(16);
    writeDescriptor(b, descriptor);
    return b.data;
}

// An embedded file for smart objects ('lnk2' item).
inline QByteArray linkedFiles(const QString& uniqueId, const QString& name, const char* type, const QByteArray& file)
{
    Bytes item;
    item.key("liFD");
    item.u32(2);
    item.u8(uniqueId.size());
    item.raw(uniqueId.toLatin1());
    item.unicode(name);
    item.key(type);
    item.key("    ");
    item.u32(0);
    item.u32(quint32(file.size()));
    item.u8(0);
    item.raw(file);
    Bytes b;
    b.u32(0);
    b.u32(quint32(item.data.size()));
    b.raw(item.data);
    while (b.data.size() % 4) b.u8(0);
    return b.data;
}

inline QByteArray document(const QSize& size, const QVector<Layer>& layers, const QImage& composite,
                           const QVector<QPair<QByteArray, QByteArray>>& documentBlocks = {})
{
    Bytes b;
    b.key("8BPS");
    b.u16(1);
    b.raw(QByteArray(6, '\0'));
    b.u16(3);
    b.u32(quint32(size.height()));
    b.u32(quint32(size.width()));
    b.u16(8);
    b.u16(3);
    b.u32(0);                                  // colour mode data
    Bytes resources;                           // global light angle 90
    resources.key("8BIM");
    resources.u16(1037);
    resources.u16(0);
    resources.u32(4);
    resources.i32(90);
    b.u32(quint32(resources.data.size()));
    b.raw(resources.data);

    Bytes info;
    info.u16(quint16(-qint16(layers.size())));
    QVector<QVector<QByteArray>> channelData;
    for (const Layer& layer : layers) {
        info.i32(layer.rect.top());
        info.i32(layer.rect.left());
        info.i32(layer.rect.top() + layer.rect.height());
        info.i32(layer.rect.left() + layer.rect.width());
        QVector<QPair<int, QByteArray>> channels;
        const int w = layer.rect.width(), h = layer.rect.height();
        if (w > 0 && h > 0) {
            for (int c : {-1, 0, 1, 2})
                channels.append({c, encodePlane(channelPlane(layer.pixels, c), w, h, layer.compression)});
        } else {
            for (int c : {-1, 0, 1, 2}) channels.append({c, QByteArray(2, '\0')});
        }
        if (!layer.maskRect.isEmpty()) {
            QByteArray plane(layer.maskRect.width() * layer.maskRect.height(), '\0');
            const QImage gray = layer.mask.convertToFormat(QImage::Format_Grayscale8);
            for (int y = 0; y < layer.maskRect.height(); ++y)
                for (int x = 0; x < layer.maskRect.width(); ++x)
                    plane[y * layer.maskRect.width() + x] = char(gray.constScanLine(y)[x]);
            channels.append({-2, encodePlane(plane, layer.maskRect.width(), layer.maskRect.height(), 1)});
        }
        info.u16(int(channels.size()));
        QVector<QByteArray> data;
        for (const auto& channel : channels) {
            info.u16(quint16(qint16(channel.first)));
            info.u32(quint32(channel.second.size()));
            data.append(channel.second);
        }
        channelData.append(data);
        info.key("8BIM");
        info.raw(layer.blend.leftJustified(4, ' ').left(4));
        info.u8(layer.opacity);
        info.u8(layer.clipping ? 1 : 0);
        info.u8(layer.hidden ? 0x0a : 0x08);
        info.u8(0);
        Bytes extra;
        if (!layer.maskRect.isEmpty()) {
            extra.u32(20);
            extra.i32(layer.maskRect.top());
            extra.i32(layer.maskRect.left());
            extra.i32(layer.maskRect.top() + layer.maskRect.height());
            extra.i32(layer.maskRect.left() + layer.maskRect.width());
            extra.u8(layer.maskDefault);
            extra.u8(0);
            extra.u16(0);
        } else {
            extra.u32(0);
        }
        extra.u32(0);                          // blending ranges
        const QByteArray ascii = layer.name.toLatin1().left(31);
        const int nameStart = int(extra.data.size());
        extra.u8(ascii.size());
        extra.raw(ascii);
        while ((extra.data.size() - nameStart) % 4) extra.u8(0);
        QVector<QPair<QByteArray, QByteArray>> blocks = layer.blocks;
        Bytes luni;
        luni.unicode(layer.name);
        blocks.prepend({"luni", luni.data});
        if (layer.id) { Bytes id; id.u32(layer.id); blocks.append({"lyid", id.data}); }
        if (layer.section) { Bytes s; s.u32(quint32(layer.section)); blocks.append({"lsct", s.data}); }
        if (!layer.vectorMask.isEmpty()) blocks.append({"vmsk", layer.vectorMask});
        for (const auto& block : blocks) {
            QByteArray payload = block.second;
            while (payload.size() % 2) payload.append('\0');
            extra.key("8BIM");
            extra.raw(block.first);
            extra.u32(quint32(payload.size()));
            extra.raw(payload);
        }
        info.u32(quint32(extra.data.size()));
        info.raw(extra.data);
    }
    for (const QVector<QByteArray>& data : channelData)
        for (const QByteArray& channel : data) info.raw(channel);
    while (info.data.size() % 2) info.u8(0);

    Bytes section;
    section.u32(quint32(info.data.size()));
    section.raw(info.data);
    section.u32(0);                            // global layer mask info
    for (const auto& block : documentBlocks) {
        section.key("8BIM");
        section.raw(block.first);
        section.u32(quint32(block.second.size()));
        section.raw(block.second);
    }
    b.u32(quint32(section.data.size()));
    b.raw(section.data);

    // The flattened image, raw.
    b.u16(0);
    const QImage flat = composite.convertToFormat(QImage::Format_RGBA8888);
    for (int c : {0, 1, 2, -1}) b.raw(channelPlane(flat, c));
    return b.data;
}

inline QImage filled(const QSize& size, const QColor& colour)
{
    QImage image(size, QImage::Format_RGBA8888);
    image.fill(colour);
    return image;
}

} // namespace psdtest
