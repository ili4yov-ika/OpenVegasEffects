#pragma once

#include <QByteArray>
#include <QColor>
#include <QHash>
#include <QImage>
#include <QPainterPath>
#include <QPointF>
#include <QRect>
#include <QSize>
#include <QString>
#include <QVariant>
#include <QVector>

#include <memory>

// Photoshop documents (.psd, and .psb, the large document format): what the
// file says about its layers - names, bounds, blend modes, opacity,
// visibility, groups, clipping, raster and vector masks, layer styles,
// smart objects with their smart filters, adjustment layers - and the
// layers' pixels, decoded from any of the format's compressions at 8, 16 or
// 32 bits in RGB, grayscale or CMYK. Written from Adobe's published file
// format specification; the reference application imports a .psd only as
// one flattened image (through FreeImage), never by layers.

namespace openvegas {
namespace media {

// The path of a media item that is one layer of a Photoshop document:
// "<document>#psdlayer:<layer id>", optionally followed by ":clip:<base
// layer id>" when the layer is clipped to the one below it (its alpha is
// then multiplied by the base layer's). A smart object's own embedded
// source, before its smart filters, is "<document>#psdsource:<unique id>".
// An embedded source that is a Photoshop document is a document itself:
// "<document>#psdsource:<id>#psdlayer:<layer id>" is one of its layers, and
// sources nest the same way to any depth. "<document>#psdmask:<layer id>"
// (optionally ":clip:<base layer id>") is what shows of a layer as a mask:
// white, its alpha its raster and vector masks and its groups' (and the
// base's alpha when clipped), over the layer's rectangle - the document's
// for a layer without pixels.
inline constexpr char kPsdLayerMarker[] = "#psdlayer:";
inline constexpr char kPsdSourceMarker[] = "#psdsource:";
inline constexpr char kPsdMaskMarker[] = "#psdmask:";
inline constexpr char kPsdClipMarker[] = ":clip:";

// Whether a path names a layer or a smart object source of a document.
bool isPsdMarkerPath(const QString& path);
// The document file of such a path (the path itself for any other).
QString psdFileOfPath(const QString& path);
// The document a path's layer is in: the file, or the embedded document
// its last source marker names ("<document>#psdsource:<id>"); the path of
// a whole document for any other.
QString psdDocumentOfPath(const QString& path);
// Whether a file is a Photoshop document, by its extension.
bool isPsdFile(const QString& path);

// A layer's raster mask: its own rectangle in document pixels, the value
// outside it and whether it is switched off.
struct PsdRasterMask
{
    QRect rect;
    quint8 defaultColor = 255;
    bool disabled = false;
    qint16 channel = -2;    // -2 the user mask, -3 the real one beside a vector mask
};

struct PsdChannel
{
    qint16 id = 0;           // 0.. colour channels, -1 transparency, -2/-3 masks
    qint64 offset = 0;       // of its compression field in the file
    qint64 length = 0;       // with that field
};

struct PsdLayer
{
    enum class Kind
    {
        Pixel,        // pixels of its own (also shape, fill and text layers)
        GroupStart,   // the folder record at the top of a group
        GroupEnd,     // the hidden divider at the bottom of a group
        Adjustment,   // no pixels: changes everything beneath it
        SmartObject,  // a placed document or image, possibly with smart filters
        Text,         // type, kept with its rendered pixels
    };

    Kind kind = Kind::Pixel;
    QString name;
    quint32 id = 0;              // 'lyid', stable while the document is edited
    QRect rect;                  // in document pixels
    QString blendKey;            // 'norm', 'mul ', 'pass', ...
    quint8 opacity = 255;
    quint8 fillOpacity = 255;    // 'iOpa': the pixels' own, effects excluded
    bool clipping = false;       // clipped to the nearest unclipped layer below
    bool visible = true;
    QVector<PsdChannel> channels;
    bool hasRasterMask = false;
    PsdRasterMask rasterMask;
    bool hasVectorMask = false;
    bool vectorMaskInverted = false;
    bool vectorMaskDisabled = false;
    QPainterPath vectorMask;     // in document pixels
    QVariantMap effects;         // 'lfx2', the layer style
    // A Solid Color fill layer's colour ('SoCo'); such a layer may come
    // without pixels of its own, its colour filling the document.
    QColor fillColor;
    QVariantMap smartObject;     // 'SoLd' / 'SoLE'
    QString adjustmentKey;       // 'expA', 'levl', 'hue2', 'CgEd', ...
    QByteArray adjustmentData;
};

// An embedded file of the document (smart object sources).
struct PsdLinkedFile
{
    QString uniqueId;
    QString name;
    QString fileType;            // 'JPEG', 'png ', 'TIFF', '8BPS', ...
    qint64 offset = 0;           // of its bytes in the document
    qint64 length = 0;
};

class PsdDocument
{
public:
    // Reads the document's structure; pixels are decoded on request.
    bool load(const QString& path);
    bool loadData(const QByteArray& data);
    QString errorString() const { return m_error; }

    QSize size() const { return m_size; }
    int depth() const { return m_depth; }
    int colorMode() const { return m_colorMode; }     // 1 grayscale, 3 RGB, 4 CMYK, ...
    bool isLargeDocument() const { return m_version == 2; }
    int globalLightAngle() const { return m_globalAngle; }
    // The layers bottom to top, as the file stores them, group records included.
    const QVector<PsdLayer>& layers() const { return m_layers; }
    int indexOfLayerId(quint32 id) const;
    const QHash<QString, PsdLinkedFile>& linkedFiles() const { return m_linked; }

    // A layer's pixels over its own rectangle with straight alpha, its raster
    // and vector masks and those of its groups applied; null for a layer
    // without pixels.
    QImage layerImage(int index) const;
    // The folder records of the groups a layer is in, innermost first.
    QVector<int> enclosingGroups(int index) const;
    // Whether a layer's raster mask hides nothing: none, switched off, or
    // white everywhere - the mask Photoshop gives every new adjustment layer.
    bool rasterMaskHidesNothing(int index) const;
    // A layer's vector mask as a path in document pixels; empty when it has
    // none (or it is switched off).
    QPainterPath vectorMaskPath(int index) const;
    // Where a layer's mask lies: its rectangle, or the whole document for a
    // layer without pixels (an adjustment).
    QRect maskArea(int index) const;
    // What of a layer its masks and its groups' let show, over maskArea:
    // white, the alpha the masks.
    QImage maskImage(int index) const;
    // The flattened document as Photoshop saved it ("maximize compatibility").
    QImage compositeImage() const;
    // Whether that is the document's picture: without "maximize
    // compatibility" Photoshop saves a blank one (one colour all over) - the
    // smart objects' embedded documents among them.
    bool hasCompositeImage() const;
    QByteArray linkedFileData(const QString& uniqueId) const;
    // The same bytes without a copy, valid while this document lives.
    QByteArray linkedFileView(const QString& uniqueId) const;

private:
    bool parse();
    bool parseLayerInfo(qint64 begin, qint64 end, bool wide);
    void parseDocumentBlocks(qint64 begin, qint64 end);
    void parseLinkedFiles(qint64 begin, qint64 end);
    QByteArray channelPlane(const PsdChannel& channel, int width, int height) const;
    void applyMasks(QImage& image, const QRect& area, const PsdLayer& owner) const;
    QImage planesToImage(const QVector<QByteArray>& colour, const QByteArray& alpha, int width,
                         int height) const;

    QByteArray m_data;
    QString m_error;
    QSize m_size;
    int m_version = 1;
    int m_channels = 0;
    int m_depth = 8;
    int m_colorMode = 3;
    int m_globalAngle = 120;
    qint64 m_imageDataOffset = -1;
    // A negative layer count: the flattened image's first extra channel is
    // its transparency. Otherwise the extra channels are saved selections
    // (alpha and spot channels) and the flattened image is opaque.
    bool m_mergedTransparency = false;
    QVector<PsdLayer> m_layers;
    QHash<QString, PsdLinkedFile> m_linked;
};

// Whether each layer shows, its groups' visibility included - and, for a
// clipped layer, its base's.
QVector<bool> psdLayersShown(const PsdDocument& document);

// A document by its path - a file, or a document embedded in one as a
// smart object's source ("<file>#psdsource:<id>", nested to any depth) -
// read once and shared: the layers of one document share one parse, and an
// embedded document reads its parent's bytes in place. Null when it cannot
// be read or the source is not a Photoshop document.
std::shared_ptr<const PsdDocument> openPsdDocument(const QString& path);

// Action descriptors as the format stores them, read into variants: an
// object is a QVariantMap with its class under "_class"; unit values are
// maps of "_unit" and "value"; enumerations their value's identifier.
QVariantMap readPsdDescriptor(const QByteArray& data, qint64 offset = 0, bool* ok = nullptr);
// A number of a descriptor value, unit or not.
double psdNumber(const QVariant& value, double fallback = 0.0);
// A colour descriptor (RGB, HSB, CMYK, grayscale) as a colour.
QColor psdColor(const QVariant& value, const QColor& fallback = Qt::black);

// A still image from a path: a Photoshop layer or smart object source (by
// their markers), a .psd/.psb flattened, or anything QImage reads. The
// documents read are cached, so the layers of one file share one parse.
QImage readStillImage(const QString& path);
// Its size without decoding pixels where the format allows; empty if unknown.
QSize stillImageSize(const QString& path);
// The name a marked path's layer has in its document ("" for other paths).
QString psdLayerName(const QString& path);

} // namespace media
} // namespace openvegas
