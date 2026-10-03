#include "project/VegfxMerge.h"

#include <QHash>
#include <QSet>
#include <QStringList>
#include <QVector>

namespace openvegas {
namespace project {
namespace {

bool isLayerWrapper(const QString& tag)
{
    static const QSet<QString> wrappers = {
        QStringLiteral("AssetLayer"), QStringLiteral("TextLayer"), QStringLiteral("PointLayer"),
        QStringLiteral("GradeLayer"), QStringLiteral("LightLayer"), QStringLiteral("CameraLayer"),
        QStringLiteral("Model3DLayer")};
    return wrappers.contains(tag);
}

// Containers, with the children the writer is the authority for. A listed
// child replaces the stored one; an unlisted leaf the stored document already
// has stays as it is (the writer's copy only fills a gap). Containers met in
// both documents are merged child by child whatever the table says.
const QHash<QString, QSet<QString>>& containers()
{
    static const QHash<QString, QSet<QString>> table = {
        {QStringLiteral("VegasEffectsProject"), {QStringLiteral("OpenCompositeShots")}},
        {QStringLiteral("Project"), {}},
        {QStringLiteral("ProjectSettings"),
         {QStringLiteral("BPC"), QStringLiteral("AntialiasingMode"),
          QStringLiteral("ReflectionMapSize"), QStringLiteral("ModelTextureMaxSize"),
          QStringLiteral("ShadowMapSize"), QStringLiteral("LimitVideoDecodingTo8bit"),
          QStringLiteral("UseLinearColor"), QStringLiteral("UseMipMapping")}},
        {QStringLiteral("AssetList"), {}},
        {QStringLiteral("CompositionAsset"),
         {QStringLiteral("Name"), QStringLiteral("CTI"), QStringLiteral("InPoint"),
          QStringLiteral("OutPoint"), QStringLiteral("IsPrimary")}},
        {QStringLiteral("AudioVideoSettings"),
         {QStringLiteral("FrameCount"), QStringLiteral("FrameRate"), QStringLiteral("Width"),
          QStringLiteral("Height"), QStringLiteral("AudioSampleRate"), QStringLiteral("PAR"),
          QStringLiteral("PARCustom")}},
        {QStringLiteral("RenderSettings"),
         {QStringLiteral("FogEnabled"), QStringLiteral("FogNearDistance"),
          QStringLiteral("FogFarDistance"), QStringLiteral("FogDensity"),
          QStringLiteral("FogColor"), QStringLiteral("FogFalloff"),
          QStringLiteral("MotionBlurEnabled"), QStringLiteral("ShutterAngle"),
          QStringLiteral("ShutterPhase"), QStringLiteral("MaxNumOfSamples"),
          QStringLiteral("UseAdaptiveSamples")}},
        {QStringLiteral("LayerBase"),
         {QStringLiteral("ID"), QStringLiteral("Name"), QStringLiteral("ParentLayerID"),
          QStringLiteral("StartFrame"), QStringLiteral("EndFrame"), QStringLiteral("BlendMode"),
          QStringLiteral("Visible"), QStringLiteral("Muted"), QStringLiteral("Locked"),
          QStringLiteral("MotionBlurOn")}},
        {QStringLiteral("TextBox"),
         {QStringLiteral("MinX"), QStringLiteral("MaxX"), QStringLiteral("MinY"),
          QStringLiteral("MaxY"), QStringLiteral("Mode"), QStringLiteral("VerticalAlignment"),
          QStringLiteral("TopIndentation"), QStringLiteral("BottomIndentation"),
          QStringLiteral("BackgroundEnable"), QStringLiteral("BackgroundOpacity"),
          QStringLiteral("BackgroundExpansionX"), QStringLiteral("BackgroundExpansionY"),
          QStringLiteral("BackgroundExpandLink"), QStringLiteral("BackgroundRoundness"),
          QStringLiteral("BackgroundColor"), QStringLiteral("Tokens"),
          QStringLiteral("Formats")}},
        {QStringLiteral("MediaAsset"),
         {QStringLiteral("Name"), QStringLiteral("Filename"), QStringLiteral("IsImageSequence"),
          QStringLiteral("InPoint"), QStringLiteral("OutPoint"), QStringLiteral("OverridePAR"),
          QStringLiteral("PAR"), QStringLiteral("OverrideFrameRate"), QStringLiteral("FrameRate"),
          QStringLiteral("OverrideAlpha"), QStringLiteral("AlphaMode"),
          QStringLiteral("OverrideColorLevels"), QStringLiteral("ColorLevels"),
          QStringLiteral("OverrideColorSpace"), QStringLiteral("ColorSpace"),
          QStringLiteral("HWAccelerate"), QStringLiteral("OpenVegasAudioStream")}},
        {QStringLiteral("ImageAsset"),
         {QStringLiteral("Name"), QStringLiteral("Filename"), QStringLiteral("PAR"),
          QStringLiteral("OverrideAlpha"), QStringLiteral("AlphaMode")}},
        {QStringLiteral("EditorSequence"),
         {QStringLiteral("Name"), QStringLiteral("CTI"), QStringLiteral("InPoint"),
          QStringLiteral("OutPoint"), QStringLiteral("TimelineZoom"),
          QStringLiteral("TimelineTimeFormat"), QStringLiteral("TimelineSnapMode"),
          QStringLiteral("TimelineScrollSyncMode"), QStringLiteral("TimelineValueGraph"),
          QStringLiteral("TimelineGraphAutoZoom"), QStringLiteral("VideoPreviewSize"),
          QStringLiteral("AudioPreviewSize"), QStringLiteral("PreviewMode")}},
        {QStringLiteral("VideoTrack"),
         {QStringLiteral("Name"), QStringLiteral("Visible"), QStringLiteral("Locked")}},
        {QStringLiteral("AudioTrack"),
         {QStringLiteral("Name"), QStringLiteral("Muted"), QStringLiteral("Solo"),
          QStringLiteral("Locked")}},
    };
    return table;
}

QSet<QString> ownedChildren(const QString& tag)
{
    if (isLayerWrapper(tag)) {
        return {QStringLiteral("AssetID"), QStringLiteral("AssetInstanceStart"),
                QStringLiteral("Dimensions")};
    }
    return containers().value(tag);
}

bool isContainer(const QString& tag)
{
    return isLayerWrapper(tag) || containers().contains(tag);
}

// Lists that follow the model: what the model has comes out of the merge,
// matched to its stored copy by key; a stored entry the model does not have
// is dropped - every composite shot included, since all of them are loaded.
bool isKeyedList(const QString& tag)
{
    static const QSet<QString> lists = {
        QStringLiteral("Assets"), QStringLiteral("Layers"), QStringLiteral("Video"),
        QStringLiteral("Audio"), QStringLiteral("AudioMaster")};
    return lists.contains(tag);
}

QString layerKey(const QDomElement& wrapper)
{
    for (QDomElement child = wrapper.firstChildElement(); !child.isNull();
         child = child.nextSiblingElement()) {
        if (!child.firstChildElement(QStringLiteral("StartFrame")).isNull()) {
            return child.firstChildElement(QStringLiteral("ID")).text();
        }
    }
    return QString();
}

QString itemKey(const QString& listTag, const QDomElement& item)
{
    if (listTag == QLatin1String("Layers")) return layerKey(item);
    return item.firstChildElement(QStringLiteral("ID")).text();
}

bool isPortExtension(const QString& name)
{
    return name.startsWith(QLatin1String("OpenVegas"));
}

void stripPortExtensions(QDomElement& base)
{
    QStringList attributes;
    const QDomNamedNodeMap map = base.attributes();
    for (int i = 0; i < map.count(); ++i) {
        const QString name = map.item(i).nodeName();
        if (isPortExtension(name)) attributes.append(name);
    }
    for (const QString& name : attributes) base.removeAttribute(name);
    QVector<QDomElement> stale;
    for (QDomElement child = base.firstChildElement(); !child.isNull();
         child = child.nextSiblingElement()) {
        if (isPortExtension(child.tagName())) stale.append(child);
    }
    for (QDomElement& child : stale) base.removeChild(child);
}

void copyAttributes(QDomElement& base, const QDomElement& generated)
{
    const QDomNamedNodeMap map = generated.attributes();
    for (int i = 0; i < map.count(); ++i) {
        const QDomNode attribute = map.item(i);
        base.setAttribute(attribute.nodeName(), attribute.nodeValue());
    }
}

QDomElement nthChild(const QDomElement& parent, const QString& tag, int index)
{
    int seen = 0;
    for (QDomElement child = parent.firstChildElement(tag); !child.isNull();
         child = child.nextSiblingElement(tag)) {
        if (seen++ == index) return child;
    }
    return QDomElement();
}

// Places `node` after `after`, or first when nothing has been placed yet, so
// gaps are filled in the writer's order rather than piled up at the end.
QDomElement placeAfter(QDomElement& parent, const QDomNode& node, const QDomElement& after)
{
    QDomNode placed;
    if (after.isNull()) {
        placed = parent.firstChild().isNull() ? parent.appendChild(node)
                                              : parent.insertBefore(node, parent.firstChild());
    } else {
        placed = parent.insertAfter(node, after);
    }
    return placed.toElement();
}

QDomElement propByName(const QDomElement& propertyManager, const QString& name)
{
    for (QDomElement prop = propertyManager.firstChildElement(QStringLiteral("Prop"));
         !prop.isNull(); prop = prop.nextSiblingElement(QStringLiteral("Prop"))) {
        if (prop.firstChildElement(QStringLiteral("Name")).text() == name) return prop;
    }
    return QDomElement();
}

void mergeElement(QDomElement base, const QDomElement& generated);

// A text format still on the same face keeps what the port cannot know about
// it, such as the face's PostScript name.
void keepFormatDetails(QDomElement& formats, const QDomElement& stored)
{
    const auto face = [](const QDomElement& format) {
        return format.firstChildElement(QStringLiteral("Family")).text() + QLatin1Char('\n')
               + format.firstChildElement(QStringLiteral("Style")).text();
    };
    for (QDomElement format = formats.firstChildElement(QStringLiteral("Format"));
         !format.isNull(); format = format.nextSiblingElement(QStringLiteral("Format"))) {
        for (QDomElement old = stored.firstChildElement(QStringLiteral("Format")); !old.isNull();
             old = old.nextSiblingElement(QStringLiteral("Format"))) {
            if (face(old) != face(format)) continue;
            for (QDomElement part = old.firstChildElement(); !part.isNull();
                 part = part.nextSiblingElement()) {
                if (format.firstChildElement(part.tagName()).isNull())
                    format.insertBefore(part.cloneNode(true),
                                        format.firstChildElement(QStringLiteral("FillColor")));
            }
            break;
        }
    }
}

// A property the writer emits wins as a whole - its static value and every
// key - but keeps what only the stored copy says: its <Default> and the
// Type/Spatial/CanInterpT attributes the reference puts on it.
void mergeProperty(QDomElement& propertyManager, QDomElement stored, const QDomElement& generated)
{
    QDomDocument document = propertyManager.ownerDocument();
    QDomElement result = document.importNode(generated, true).toElement();
    const QDomNamedNodeMap map = stored.attributes();
    for (int i = 0; i < map.count(); ++i) {
        const QDomNode attribute = map.item(i);
        if (!result.hasAttribute(attribute.nodeName()))
            result.setAttribute(attribute.nodeName(), attribute.nodeValue());
    }
    const QDomElement storedDefault = stored.firstChildElement(QStringLiteral("Default"));
    if (!storedDefault.isNull() && result.firstChildElement(QStringLiteral("Default")).isNull()) {
        result.insertAfter(storedDefault.cloneNode(true),
                           result.firstChildElement(QStringLiteral("Name")));
    }
    // A key still at the time it was read keeps what only the file knew about
    // it - the spatial tangents (SInPt/SOuPt) of a moving point, for one.
    const QDomElement storedAnimation = stored.firstChildElement(QStringLiteral("Animation"));
    QDomElement animation = result.firstChildElement(QStringLiteral("Animation"));
    for (QDomElement key = animation.firstChildElement(QStringLiteral("Key")); !key.isNull();
         key = key.nextSiblingElement(QStringLiteral("Key"))) {
        const double time = key.attribute(QStringLiteral("Ti")).toDouble();
        for (QDomElement old = storedAnimation.firstChildElement(QStringLiteral("Key"));
             !old.isNull(); old = old.nextSiblingElement(QStringLiteral("Key"))) {
            // The port keys frames, so a time comes back within a frame's
            // rounding of what the file said (34366 ms is 34366.67 at 60 fps).
            if (qAbs(old.attribute(QStringLiteral("Ti")).toDouble() - time) > 5.0) continue;
            for (QDomElement part = old.firstChildElement(); !part.isNull();
                 part = part.nextSiblingElement()) {
                if (key.firstChildElement(part.tagName()).isNull()) {
                    key.insertBefore(part.cloneNode(true),
                                     key.firstChildElement(QStringLiteral("Vl")));
                }
            }
            break;
        }
    }
    propertyManager.replaceChild(result, stored);
}

void mergeProperties(QDomElement base, const QDomElement& generated)
{
    copyAttributes(base, generated);
    QDomDocument document = base.ownerDocument();
    for (QDomElement prop = generated.firstChildElement(QStringLiteral("Prop")); !prop.isNull();
         prop = prop.nextSiblingElement(QStringLiteral("Prop"))) {
        const QString name = prop.firstChildElement(QStringLiteral("Name")).text();
        const QDomElement stored = propByName(base, name);
        if (stored.isNull()) {
            base.appendChild(document.importNode(prop, true));
        } else {
            mergeProperty(base, stored, prop);
        }
    }
}

void mergeKeyedList(QDomElement base, const QDomElement& generated)
{
    copyAttributes(base, generated);
    QDomDocument document = base.ownerDocument();
    const QString tag = base.tagName();
    QHash<QString, QDomElement> stored;
    for (QDomElement item = base.firstChildElement(); !item.isNull();
         item = item.nextSiblingElement()) {
        const QString key = itemKey(tag, item);
        if (!key.isEmpty() && !stored.contains(key)) stored.insert(key, item);
    }
    QVector<QDomElement> result;
    QSet<QString> used;
    for (QDomElement item = generated.firstChildElement(); !item.isNull();
         item = item.nextSiblingElement()) {
        const QString key = itemKey(tag, item);
        const auto match = stored.constFind(key);
        if (key.isEmpty() || match == stored.cend() || used.contains(key)
            || match->tagName() != item.tagName()) {
            result.append(document.importNode(item, true).toElement());
            continue;
        }
        used.insert(key);
        mergeElement(*match, item);
        result.append(*match);
    }
    while (!base.firstChild().isNull()) base.removeChild(base.firstChild());
    for (const QDomElement& item : result) base.appendChild(item);
}

void mergeElement(QDomElement base, const QDomElement& generated)
{
    const QString tag = generated.tagName();
    if (tag == QLatin1String("PropertyManager")) {
        mergeProperties(base, generated);
        return;
    }
    if (isKeyedList(tag)) {
        mergeKeyedList(base, generated);
        return;
    }
    stripPortExtensions(base);
    if (tag != QLatin1String("VegasEffectsProject")) copyAttributes(base, generated);
    const QSet<QString> owned = ownedChildren(tag);
    QDomDocument document = base.ownerDocument();
    QHash<QString, int> occurrences;
    QDomElement lastPlaced;
    for (QDomElement child = generated.firstChildElement(); !child.isNull();
         child = child.nextSiblingElement()) {
        const QString childTag = child.tagName();
        const int occurrence = occurrences[childTag]++;
        if (isPortExtension(childTag)) {
            lastPlaced = placeAfter(base, document.importNode(child, true), lastPlaced);
            continue;
        }
        QDomElement stored = nthChild(base, childTag, occurrence);
        if (stored.isNull()) {
            lastPlaced = placeAfter(base, document.importNode(child, true), lastPlaced);
        } else if (isContainer(childTag) || isKeyedList(childTag)
                   || childTag == QLatin1String("PropertyManager")) {
            mergeElement(stored, child);
            lastPlaced = stored;
        } else if (owned.contains(childTag)) {
            QDomElement replacement = document.importNode(child, true).toElement();
            if (childTag == QLatin1String("Formats")) keepFormatDetails(replacement, stored);
            base.replaceChild(replacement, stored);
            lastPlaced = replacement;
        } else {
            lastPlaced = stored;
        }
    }
}

} // namespace

QDomDocument mergeIntoSource(QDomDocument source, const QDomDocument& generated)
{
    QDomElement root = source.documentElement();
    const QDomElement generatedRoot = generated.documentElement();
    if (root.isNull() || generatedRoot.isNull() || root.tagName() != generatedRoot.tagName()) {
        return generated;
    }
    mergeElement(root, generatedRoot);
    return source;
}

} // namespace project
} // namespace openvegas
