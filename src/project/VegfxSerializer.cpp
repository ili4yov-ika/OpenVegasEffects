#include <cmath>
#include "project/VegfxSerializer.h"

#include <QDomDocument>
#include <QDomElement>

#include "model3d/ModelImportSettings.h"
#include <QRegularExpression>

#include "composition/KeyFrame.h"
#include "composition/TextStyle.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDirIterator>
#include <QHash>
#include <QSet>
#include <QCryptographicHash>
#include <QTextStream>
#include <QUuid>
#include <algorithm>
#include <functional>

#include "core/Log.h"

namespace openvegas {
namespace project {

namespace {

QString elemText(const QDomElement& parent, const QString& tag)
{
    const QDomElement child = parent.firstChildElement(tag);
    if (child.isNull()) {
        return QString();
    }
    // The reference writer wraps long values across lines and indents the
    // continuation, so a path can come back as
    //   "...\ika copyleft -
    //                        Very D.I.S.C.O.mp3".
    // Undo just that: collapse a line break together with the indentation that
    // follows it back to the single space it stood for. Runs of plain spaces are
    // left alone, since a file name may legitimately contain them.
    static const QRegularExpression wrapped(QStringLiteral("[ \\t]*\\r?\\n[ \\t]*"));
    return child.text().replace(wrapped, QStringLiteral(" ")).trimmed();
}

// A .vegfx stores absolute media paths, so a project that has been moved - or
// whose media folder was renamed - would lose every asset even when the files
// sit right beside it. The reference relinks in that situation; this does the
// same by looking for the tail of the stored path under the project directory,
// longest tail first so a specific sub-path wins over a bare file name.
//
//   stored : C:\...\РѕР±Р»РѕРіР° РіСЂСѓРїРїС‹\РћР±Р»РѕРіРё\2_РѕР±Р»РѕРіР°-РєСЂСѓРі.png
//   project: <dir>/Project.vegfx
//   found  : <dir>/РћР±Р»РѕРіРё/2_РѕР±Р»РѕРіР°-РєСЂСѓРі.png
//
// Returns the stored path unchanged when nothing matches, so the warning still
// names what the project actually asked for.
QString resolveAssetPath(const QString& storedPath, const QDir& projectDir, bool* relinked)
{
    if (relinked) {
        *relinked = false;
    }
    if (storedPath.isEmpty()) {
        return storedPath;
    }

    // Normalise separators first: the paths are written with backslashes.
    QString normalised = storedPath;
    normalised.replace(QLatin1Char('\\'), QLatin1Char('/'));
    const QString exact = QDir::isRelativePath(normalised)
        ? QDir::cleanPath(projectDir.absoluteFilePath(normalised)) : normalised;
    if (QFileInfo::exists(exact)) return exact;
    const QStringList parts = normalised.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.isEmpty()) return exact;

    for (int take = parts.size() - 1; take >= 1; --take) {
        const QString tail = QStringList(parts.mid(parts.size() - take)).join(QLatin1Char('/'));
        if (tail.contains(QLatin1Char(':'))) {
            continue; // still carries the drive letter
        }
        const QString candidate = projectDir.filePath(tail);
        if (QFileInfo::exists(candidate)) {
            if (relinked) {
                *relinked = true;
            }
            return QDir::cleanPath(candidate);
        }
    }
    // A moved media folder can have a new name. Only adopt a unique basename
    // below this project's directory; never guess between duplicate files.
    QString match;
    QDirIterator files(projectDir.absolutePath(), QDir::Files,
                       QDirIterator::Subdirectories);
    int visited = 0;
    while (files.hasNext() && visited++ < 10000) {
        const QString candidate = files.next();
        if (QFileInfo(candidate).fileName() != parts.last()) continue;
        if (!match.isEmpty()) return exact;
        match = candidate;
    }
    if (!files.hasNext() && !match.isEmpty()) {
        if (relinked) *relinked = true;
        return QDir::cleanPath(match);
    }
    return exact;
}

// A text layer stores its string one character per token: <Tk Tp="0" Ch="68"/>
// is 'D'. Tp="1" marks a paragraph, which becomes a line break. Without reading
// this the loader dropped the text entirely and the layer rendered blank.
QString textFromTextBox(const QDomElement& named, int* pixelSize)
{
    const QDomElement box = named.firstChildElement(QStringLiteral("TextBox"));
    if (box.isNull()) {
        return QString();
    }
    if (pixelSize) {
        // The token box height is the closest thing to a font size the element
        // offers without resolving the font table.
        const double minY = elemText(box, QStringLiteral("MinY")).toDouble();
        const double maxY = elemText(box, QStringLiteral("MaxY")).toDouble();
        const int h = static_cast<int>(qRound(maxY - minY));
        *pixelSize = (h >= 8 && h <= 512) ? h : 48;
    }

    QString out;
    const QDomElement tokens = box.firstChildElement(QStringLiteral("Tokens"));
    for (QDomElement tk = tokens.firstChildElement(QStringLiteral("Tk")); !tk.isNull();
         tk = tk.nextSiblingElement(QStringLiteral("Tk"))) {
        const QString type = tk.attribute(QStringLiteral("Tp"));
        if (type == QLatin1String("1")) {
            if (!out.isEmpty()) {
                out.append(QLatin1Char('\n'));
            }
            continue;
        }
        if (!tk.hasAttribute(QStringLiteral("Ch"))) {
            continue;
        }
        bool ok = false;
        const uint code = tk.attribute(QStringLiteral("Ch")).toUInt(&ok);
        if (ok && code > 0) {
            out.append(QChar(static_cast<char16_t>(code)));
        }
    }
    return out;
}

// One <Prop> of the layer's <PropertyManager>, by its <Name>.
QDomElement propElement(const QDomElement& propertyManager, const QString& name)
{
    for (QDomElement p = propertyManager.firstChildElement(QStringLiteral("Prop")); !p.isNull();
         p = p.nextSiblingElement(QStringLiteral("Prop"))) {
        if (elemText(p, QStringLiteral("Name")) == name) {
            return p;
        }
    }
    return QDomElement();
}

// A property holds its constant value in <Static>, falling back to <Default>.
// Both wrap the value in a typed element: <p3> for a point, <sc> for a scale,
// <fl> for a scalar.
QDomElement propValueHolder(const QDomElement& prop)
{
    const QDomElement stat = prop.firstChildElement(QStringLiteral("Static"));
    return stat.isNull() ? prop.firstChildElement(QStringLiteral("Default")) : stat;
}

QPointF propPoint(const QDomElement& propertyManager, const QString& name, const QPointF& fallback)
{
    const QDomElement prop = propElement(propertyManager, name);
    if (prop.isNull()) {
        return fallback;
    }
    const QDomElement holder = propValueHolder(prop);
    for (const char* tag : {"p3", "sc", "or"}) {
        const QDomElement v = holder.firstChildElement(QString::fromLatin1(tag));
        if (!v.isNull()) {
            return QPointF(v.attribute(QStringLiteral("X")).toDouble(),
                           v.attribute(QStringLiteral("Y")).toDouble());
        }
    }
    return fallback;
}

double propScalar(const QDomElement& propertyManager, const QString& name, double fallback)
{
    const QDomElement prop = propElement(propertyManager, name);
    if (prop.isNull()) {
        return fallback;
    }
    const QDomElement fl = propValueHolder(prop).firstChildElement(QStringLiteral("fl"));
    return fl.isNull() ? fallback : fl.text().toDouble();
}

// Reference temporal type ids, as recovered from VegasEffects.exe
// ("Set Keyframe Temporal Type" and the toolButtonKeyFrameType* group).
composition::TemporalType temporalTypeFromId(int id)
{
    switch (id) {
    case 0:  return composition::TemporalType::Hold;         // Constant
    case 1:  return composition::TemporalType::Linear;
    case 6:  return composition::TemporalType::ManualBezier;
    default: return composition::TemporalType::Linear;
    }
}

// <Animation> of a scalar property. Key times are milliseconds - calibrated
// against this project, whose rotation runs 0..29933 over a 1800-frame, 60 fps
// composition - while <StartFrame>/<EndFrame> elsewhere are frames.
// One axis of an animated point property (<p3>/<sc>/<or> inside <Vl>), or the
// scalar itself when `attribute` is empty (<fl>).
composition::KeyFrameList propAnimation(const QDomElement& prop, double fps,
                                        const QString& attribute)
{
    composition::KeyFrameList curve;
    // Raw handle data as the file states it, kept beside each key until the
    // whole curve is known: converting a handle needs the segment it shapes.
    struct RawHandles
    {
        double milliseconds = 0.0;
        double value = 0.0;
        QPointF incoming;
        QPointF outgoing;
        bool hasIncoming = false;
        bool hasOutgoing = false;
    };
    QVector<QPair<composition::KeyFrame, RawHandles>> pending;

    const QDomElement anim = prop.firstChildElement(QStringLiteral("Animation"));
    if (anim.isNull() || fps <= 0.0) {
        return curve;
    }
    for (QDomElement key = anim.firstChildElement(QStringLiteral("Key")); !key.isNull();
         key = key.nextSiblingElement(QStringLiteral("Key"))) {
        const QDomElement value = key.firstChildElement(QStringLiteral("Vl"));
        double numeric = 0.0;
        if (attribute.isEmpty()) {
            const QDomElement fl = value.firstChildElement(QStringLiteral("fl"));
            if (fl.isNull()) {
                continue;
            }
            numeric = fl.text().toDouble();
        } else {
            QDomElement holder;
            for (const char* tag : {"p3", "sc", "or"}) {
                holder = value.firstChildElement(QString::fromLatin1(tag));
                if (!holder.isNull()) {
                    break;
                }
            }
            if (holder.isNull() || !holder.hasAttribute(attribute)) {
                continue;
            }
            numeric = holder.attribute(attribute).toDouble();
        }
        const double ms = key.attribute(QStringLiteral("Ti")).toDouble();
        const int frame = static_cast<int>(qRound(ms / 1000.0 * fps));
        composition::KeyFrame kf;
        kf.frame = frame;
        kf.value = QVariant(numeric);
        kf.temporal = temporalTypeFromId(key.attribute(QStringLiteral("Tp")).toInt());
        kf.incomingInfluence = key.firstChildElement(QStringLiteral("InInf")).text().toDouble();
        kf.outgoingInfluence = key.firstChildElement(QStringLiteral("OuInf")).text().toDouble();
        kf.handlesLocked = key.firstChildElement(QStringLiteral("TLk")).text().toInt() != 0;

        RawHandles raw;
        raw.milliseconds = ms;
        raw.value = numeric;
        const QDomElement tin = key.firstChildElement(QStringLiteral("TInPt"));
        if (!tin.isNull()) {
            raw.hasIncoming = true;
            raw.incoming = QPointF(tin.attribute(QStringLiteral("X")).toDouble(),
                                   tin.attribute(QStringLiteral("Y")).toDouble());
        }
        const QDomElement tout = key.firstChildElement(QStringLiteral("TOuPt"));
        if (!tout.isNull()) {
            raw.hasOutgoing = true;
            raw.outgoing = QPointF(tout.attribute(QStringLiteral("X")).toDouble(),
                                   tout.attribute(QStringLiteral("Y")).toDouble());
        }
        pending.append(qMakePair(kf, raw));
    }

    // Handles come out of the file as offsets from the keyframe in the file's
    // own units - milliseconds on X, property units on Y - while the model
    // keeps them as fractions of the segment they shape, running (0,0) to
    // (1,1). Converting needs the neighbouring keys, so it happens once the
    // whole curve is read rather than key by key. Read literally, a handle of
    // "-16866.5" landed sixteen thousand pixels off the value graph and gave
    // the interpolator a control point far outside its segment.
    for (int i = 0; i < pending.size(); ++i) {
        composition::KeyFrame kf = pending.at(i).first;
        const RawHandles& raw = pending.at(i).second;

        if (raw.hasOutgoing && i + 1 < pending.size()) {
            const RawHandles& next = pending.at(i + 1).second;
            const double timeSpan = next.milliseconds - raw.milliseconds;
            const double valueSpan = next.value - raw.value;
            if (timeSpan > 0.0) {
                // Y is an offset from this key's own value, so a stored 0 means
                // the handle sits level with it - which is exactly the eased
                // preset the model spells (1/3, 0).
                const double y = qFuzzyIsNull(valueSpan) ? 0.0 : raw.outgoing.y() / valueSpan;
                kf.outgoingHandle = QPointF(qBound(0.0, raw.outgoing.x() / timeSpan, 1.0), y);
            }
        }
        if (raw.hasIncoming && i > 0) {
            const RawHandles& previous = pending.at(i - 1).second;
            const double timeSpan = raw.milliseconds - previous.milliseconds;
            const double valueSpan = raw.value - previous.value;
            if (timeSpan > 0.0) {
                // The incoming offset is negative: it reaches back from this
                // key towards the previous one, so 1 + offset/span lands it in
                // the segment's unit space.
                const double y =
                    qFuzzyIsNull(valueSpan) ? 1.0 : 1.0 + raw.incoming.y() / valueSpan;
                kf.incomingHandle =
                    QPointF(qBound(0.0, 1.0 + raw.incoming.x() / timeSpan, 1.0), y);
            }
        }
        curve.add(kf);
    }
    return curve;
}

composition::LayerTransform readTransform(const QDomElement& propertyManager, double fps)
{
    composition::LayerTransform t;
    if (propertyManager.isNull()) {
        return t;
    }
    t.anchorPoint = propPoint(propertyManager, QStringLiteral("anchorPoint"), QPointF(0.0, 0.0));
    t.position = propPoint(propertyManager, QStringLiteral("position"), QPointF(0.0, 0.0));
    t.scalePercent = propPoint(propertyManager, QStringLiteral("scale"), QPointF(100.0, 100.0));
    t.rotationDegrees = propScalar(propertyManager, QStringLiteral("rotationZ"), 0.0);

    const QString X = QStringLiteral("X");
    const QString Y = QStringLiteral("Y");
    const QDomElement pos = propElement(propertyManager, QStringLiteral("position"));
    t.positionXCurve = propAnimation(pos, fps, X);
    t.positionYCurve = propAnimation(pos, fps, Y);
    const QDomElement sc = propElement(propertyManager, QStringLiteral("scale"));
    t.scaleXCurve = propAnimation(sc, fps, X);
    t.scaleYCurve = propAnimation(sc, fps, Y);
    t.rotationCurve =
        propAnimation(propElement(propertyManager, QStringLiteral("rotationZ")), fps, QString());
    t.opacityCurve =
        propAnimation(propElement(propertyManager, QStringLiteral("opacity")), fps, QString());
    return t;
}

// Layers wrap their real fields inside an element named after the layer itself
// (e.g. <AssetLayer> -> <1_foo.png> -> <StartFrame>, ...). Find that inner
// element: the direct child that exposes a <StartFrame> sibling.
QDomElement namedLayerElement(const QDomElement& layerWrapper)
{
    for (QDomElement child = layerWrapper.firstChildElement(); !child.isNull();
         child = child.nextSiblingElement()) {
        if (!child.firstChildElement(QStringLiteral("StartFrame")).isNull()) {
            return child;
        }
    }
    return layerWrapper;
}

double frameToSeconds(long long frames, double fps)
{
    return fps > 0.0 ? double(frames) / fps : 0.0;
}

// Read a named property's <fl>/<db>/<i> value from a PropertyManager. Prefers
// the <Static> value, falls back to <Default>. Returns 0.0 when absent.
double propValue(const QDomElement& propertyManager, const QString& propName)
{
    for (QDomElement prop = propertyManager.firstChildElement(QStringLiteral("Prop"));
         !prop.isNull(); prop = prop.nextSiblingElement(QStringLiteral("Prop"))) {
        if (elemText(prop, QStringLiteral("Name")) != propName) {
            continue;
        }
        const QDomElement staticNode = prop.firstChildElement(QStringLiteral("Static"));
        const QDomElement valNode =
            staticNode.isNull() ? prop.firstChildElement(QStringLiteral("Default")) : staticNode;
        if (valNode.isNull()) {
            return 0.0;
        }
        const QDomElement fl = valNode.firstChildElement();
        if (fl.isNull()) {
            return 0.0;
        }
        bool ok = false;
        const double v = fl.text().toDouble(&ok);
        return ok ? v : 0.0;
    }
    return 0.0;
}

QDomElement addText(QDomDocument& doc, QDomElement& parent, const QString& tag, const QString& text)
{
    QDomElement e = doc.createElement(tag);
    const QDomText t = doc.createTextNode(text);
    e.appendChild(t);
    parent.appendChild(e);
    return e;
}

// Reference temporal type id for a keyframe. The reader knows 0/1/6; the three
// eased types have no recovered id, so they go out as Manual Bezier with their
// preset handles written explicitly - the curve shape survives exactly - and
// the port's own type travels beside it in OpenVegasTemporal so a reload gets
// the label back too.
int temporalIdFor(composition::TemporalType type)
{
    switch (type) {
    case composition::TemporalType::Hold:   return 0;
    case composition::TemporalType::Linear: return 1;
    default: break;
    }
    return 6;   // Manual Bezier
}

// Milliseconds, the unit the reference keys its animation in.
double keyTimeMs(int frame, double fps)
{
    return fps > 0.0 ? (frame * 1000.0 / fps) : 0.0;
}

// One <Key>, in the reference's own child order: TLk, InInf, OuInf, the
// temporal handles, SLk, then <Vl>. A Linear key carries no handles at all,
// exactly as the reference writes it.
//
// `valueWriter` fills <Vl> - a scalar puts an <fl> there, a point a <p3>/<sc> -
// because that is the only part of a key that differs between the two.
void appendKey(QDomDocument& doc, QDomElement& animation, const composition::KeyFrame& key,
               double fps, double previousMs, double nextMs, double previousValue,
               double nextValue, bool hasPrevious, bool hasNext,
               const std::function<void(QDomElement&)>& valueWriter)
{
    QDomElement el = doc.createElement(QStringLiteral("Key"));
    el.setAttribute(QStringLiteral("STp"), QStringLiteral("2"));
    el.setAttribute(QStringLiteral("Ti"), QString::number(keyTimeMs(key.frame, fps), 'f', 4));
    el.setAttribute(QStringLiteral("Tp"), temporalIdFor(key.temporal));
    el.setAttribute(QStringLiteral("V"), QStringLiteral("5"));
    // The port's own six-way type, so a reload keeps "Smooth In" as Smooth In
    // rather than as the Manual Bezier it is written as.
    el.setAttribute(QStringLiteral("OpenVegasTemporal"),
                    QString::fromLatin1(composition::temporalTypeName(key.temporal)));

    addText(doc, el, QStringLiteral("TLk"), key.handlesLocked ? QStringLiteral("1")
                                                             : QStringLiteral("0"));

    const bool eased = key.temporal != composition::TemporalType::Linear
                       && key.temporal != composition::TemporalType::Hold;
    if (eased) {
        addText(doc, el, QStringLiteral("InInf"), QString::number(key.incomingInfluence, 'f', 4));
        addText(doc, el, QStringLiteral("OuInf"), QString::number(key.outgoingInfluence, 'f', 4));

        // Handles go back out as offsets from the key in milliseconds and
        // property units - the inverse of what propAnimation() reads - so a
        // file written here means the same thing to the reference as to us.
        const double thisMs = keyTimeMs(key.frame, fps);
        if (hasNext) {
            const QPointF unit = composition::outgoingHandleFor(key);
            const double timeSpan = nextMs - thisMs;
            const double valueSpan = nextValue - key.value.toDouble();
            QDomElement out = doc.createElement(QStringLiteral("TOuPt"));
            out.setAttribute(QStringLiteral("X"), QString::number(unit.x() * timeSpan, 'f', 4));
            out.setAttribute(QStringLiteral("Y"), QString::number(unit.y() * valueSpan, 'f', 4));
            el.appendChild(out);
        }
        if (hasPrevious) {
            const QPointF unit = composition::incomingHandleFor(key);
            const double timeSpan = thisMs - previousMs;
            const double valueSpan = key.value.toDouble() - previousValue;
            QDomElement in = doc.createElement(QStringLiteral("TInPt"));
            in.setAttribute(QStringLiteral("X"),
                            QString::number((unit.x() - 1.0) * timeSpan, 'f', 4));
            in.setAttribute(QStringLiteral("Y"),
                            QString::number((unit.y() - 1.0) * valueSpan, 'f', 4));
            el.appendChild(in);
        }
    }
    addText(doc, el, QStringLiteral("SLk"), QStringLiteral("0"));

    QDomElement value = doc.createElement(QStringLiteral("Vl"));
    value.setAttribute(QStringLiteral("V"), QStringLiteral("4"));
    valueWriter(value);
    el.appendChild(value);

    animation.appendChild(el);
}

// <Prop> for a scalar property: its constant value, plus an <Animation> when
// the curve carries keys. Written even for a static property, because the
// reader falls back to <Static> and a missing Prop loses the value entirely -
// which is what used to happen to every transform but opacity.
void appendScalarProp(QDomDocument& doc, QDomElement& propertyManager, const QString& name,
                      double staticValue, const composition::KeyFrameList& curve, double fps)
{
    QDomElement prop = doc.createElement(QStringLiteral("Prop"));
    addText(doc, prop, QStringLiteral("Name"), name);

    QDomElement stat = doc.createElement(QStringLiteral("Static"));
    QDomElement fl = doc.createElement(QStringLiteral("fl"));
    fl.appendChild(doc.createTextNode(QString::number(staticValue, 'f', 4)));
    stat.appendChild(fl);
    prop.appendChild(stat);

    const QVector<composition::KeyFrame> keys = curve.all();
    if (!keys.isEmpty()) {
        QDomElement animation = doc.createElement(QStringLiteral("Animation"));
        for (int i = 0; i < keys.size(); ++i) {
            const composition::KeyFrame& key = keys.at(i);
            const bool hasPrevious = i > 0;
            const bool hasNext = i + 1 < keys.size();
            appendKey(doc, animation, key, fps,
                      hasPrevious ? keyTimeMs(keys.at(i - 1).frame, fps) : 0.0,
                      hasNext ? keyTimeMs(keys.at(i + 1).frame, fps) : 0.0,
                      hasPrevious ? keys.at(i - 1).value.toDouble() : 0.0,
                      hasNext ? keys.at(i + 1).value.toDouble() : 0.0, hasPrevious, hasNext,
                      [&doc, &key](QDomElement& value) {
                          QDomElement inner = doc.createElement(QStringLiteral("fl"));
                          inner.appendChild(
                              doc.createTextNode(QString::number(key.value.toDouble(), 'f', 4)));
                          value.appendChild(inner);
                      });
        }
        prop.appendChild(animation);
    }
    propertyManager.appendChild(prop);
}

// <Prop> for a point property. The two axes are separate curves in the model
// but one <Animation> in the file, so the keys are the union of both axes and
// each axis is sampled at every one of them - which is also how the reference
// stores a point: one key, both numbers.
void appendPointProp(QDomDocument& doc, QDomElement& propertyManager, const QString& name,
                     const QString& holderTag, const QPointF& staticValue,
                     const composition::KeyFrameList& xCurve,
                     const composition::KeyFrameList& yCurve, double fps)
{
    QDomElement prop = doc.createElement(QStringLiteral("Prop"));
    addText(doc, prop, QStringLiteral("Name"), name);

    const auto writeHolder = [&doc, &holderTag](QDomElement& parent, double x, double y) {
        QDomElement holder = doc.createElement(holderTag);
        holder.setAttribute(QStringLiteral("X"), QString::number(x, 'f', 4));
        holder.setAttribute(QStringLiteral("Y"), QString::number(y, 'f', 4));
        holder.setAttribute(QStringLiteral("Z"), QStringLiteral("0"));
        parent.appendChild(holder);
    };

    QDomElement stat = doc.createElement(QStringLiteral("Static"));
    writeHolder(stat, staticValue.x(), staticValue.y());
    prop.appendChild(stat);

    QVector<int> frames = xCurve.locations();
    const QVector<int> yFrames = yCurve.locations();
    for (int frame : yFrames) {
        if (!frames.contains(frame)) {
            frames.append(frame);
        }
    }
    std::sort(frames.begin(), frames.end());

    if (!frames.isEmpty()) {
        const auto sample = [](const composition::KeyFrameList& curve, int frame,
                               double fallback) {
            if (curve.isEmpty()) {
                return fallback;
            }
            bool ok = false;
            const double v = curve.valueAt(frame).toDouble(&ok);
            return ok ? v : fallback;
        };
        // Handle and type metadata come from whichever axis actually holds a
        // key at this frame; the reference keeps one set per point key too.
        const auto metadata = [&](int frame) {
            if (const composition::KeyFrame* k = xCurve.at(frame)) {
                return *k;
            }
            if (const composition::KeyFrame* k = yCurve.at(frame)) {
                return *k;
            }
            composition::KeyFrame fallback;
            fallback.frame = frame;
            return fallback;
        };

        QDomElement animation = doc.createElement(QStringLiteral("Animation"));
        for (int i = 0; i < frames.size(); ++i) {
            const int frame = frames.at(i);
            composition::KeyFrame key = metadata(frame);
            key.frame = frame;
            // The X axis stands for the point when spans are measured, which is
            // the axis the handles were stored against.
            key.value = QVariant(sample(xCurve, frame, staticValue.x()));
            const bool hasPrevious = i > 0;
            const bool hasNext = i + 1 < frames.size();
            appendKey(doc, animation, key, fps,
                      hasPrevious ? keyTimeMs(frames.at(i - 1), fps) : 0.0,
                      hasNext ? keyTimeMs(frames.at(i + 1), fps) : 0.0,
                      hasPrevious ? sample(xCurve, frames.at(i - 1), staticValue.x()) : 0.0,
                      hasNext ? sample(xCurve, frames.at(i + 1), staticValue.x()) : 0.0,
                      hasPrevious, hasNext,
                      [&](QDomElement& value) {
                          writeHolder(value, sample(xCurve, frame, staticValue.x()),
                                      sample(yCurve, frame, staticValue.y()));
                      });
        }
        prop.appendChild(animation);
    }
    propertyManager.appendChild(prop);
}

// Stable pseudo-GUID for a media path so repeated saves stay consistent.
QString guidForPath(const QString& path)
{
    const QByteArray md5 = QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Md5);
    const auto hex = QString::fromLatin1(md5.toHex());
    return QStringLiteral("%1-%2-%3-%4-%5")
        .arg(hex.mid(0, 8), hex.mid(8, 4), hex.mid(12, 4), hex.mid(16, 4), hex.mid(20, 12));
}

bool isTextClip(const composition::Clip& clip)
{
    if (clip.mediaId.value().startsWith(QStringLiteral("media:text"))) {
        return true;
    }
    for (const composition::Effect& fx : clip.effects) {
        if (fx.pluginId.value() == QStringLiteral("text")) {
            return true;
        }
    }
    return false;
}

QString mediaPathFromId(const core::Identifier& id)
{
    const QString v = id.value();
    if (v.startsWith(QStringLiteral("media:"))) {
        return v.mid(6);
    }
    return QString();
}

QString variantTypeName(const QVariant& value)
{
    switch (value.typeId()) {
    case QMetaType::Bool: return QStringLiteral("bool");
    case QMetaType::Int:
    case QMetaType::LongLong: return QStringLiteral("int");
    case QMetaType::Double:
    case QMetaType::Float: return QStringLiteral("double");
    default: return QStringLiteral("string");
    }
}

QVariant variantFromText(const QString& text, const QString& type)
{
    if (type == QLatin1String("bool")) return QVariant(text == QLatin1String("1")
                                                        || text.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0);
    if (type == QLatin1String("int")) return QVariant(text.toLongLong());
    if (type == QLatin1String("double")) return QVariant(text.toDouble());
    return QVariant(text);
}

void appendVariant(QDomDocument& doc, QDomElement& parent, const QString& tag,
                   const QVariant& value)
{
    QDomElement element = doc.createElement(tag);
    element.setAttribute(QStringLiteral("Type"), variantTypeName(value));
    element.appendChild(doc.createTextNode(value.typeId() == QMetaType::Bool
        ? (value.toBool() ? QStringLiteral("1") : QStringLiteral("0")) : value.toString()));
    parent.appendChild(element);
}

QVariant readVariant(const QDomElement& element)
{
    return variantFromText(element.text(), element.attribute(QStringLiteral("Type")));
}

// Effect instances and their parameter curves are kept in an ignorable
// extension. The native PropertyManager schema is plugin-specific; writing a
// guessed native block would make reference compatibility worse, while this
// block makes an OpenVegas save/load lossless and is ignored by VEGAS Effects.
void appendEffectsExtension(QDomDocument& doc, QDomElement& wrapper,
                            const composition::Clip& clip)
{
    if (clip.effects.isEmpty()) return;
    QDomElement root = doc.createElement(QStringLiteral("OpenVegasEffects"));
    root.setAttribute(QStringLiteral("Version"), 1);
    for (const composition::Effect& effect : clip.effects) {
        QDomElement fx = doc.createElement(QStringLiteral("Effect"));
        fx.setAttribute(QStringLiteral("PluginID"), effect.pluginId.value());
        fx.setAttribute(QStringLiteral("Name"), effect.name);
        fx.setAttribute(QStringLiteral("Enabled"), effect.enabled ? 1 : 0);
        QDomElement parameters = doc.createElement(QStringLiteral("Parameters"));
        for (int i = 0; i < effect.parameterValues.size(); ++i) {
            QDomElement value = doc.createElement(QStringLiteral("Value"));
            value.setAttribute(QStringLiteral("Index"), i);
            value.appendChild(doc.createTextNode(effect.parameterValues.at(i)));
            parameters.appendChild(value);
        }
        fx.appendChild(parameters);
        QDomElement animations = doc.createElement(QStringLiteral("Animations"));
        for (auto it = effect.animation.constBegin(); it != effect.animation.constEnd(); ++it) {
            QDomElement curve = doc.createElement(QStringLiteral("Curve"));
            curve.setAttribute(QStringLiteral("Parameter"), it.key());
            curve.setAttribute(QStringLiteral("CanInterpolate"), it->canInterpolate() ? 1 : 0);
            appendVariant(doc, curve, QStringLiteral("Default"), it->defaultValue());
            for (const composition::KeyFrame& key : it->all()) {
                QDomElement node = doc.createElement(QStringLiteral("Key"));
                node.setAttribute(QStringLiteral("Frame"), key.frame);
                node.setAttribute(QStringLiteral("ID"), key.id);
                node.setAttribute(QStringLiteral("Temporal"), static_cast<int>(key.temporal));
                node.setAttribute(QStringLiteral("IncomingX"), key.incomingHandle.x());
                node.setAttribute(QStringLiteral("IncomingY"), key.incomingHandle.y());
                node.setAttribute(QStringLiteral("OutgoingX"), key.outgoingHandle.x());
                node.setAttribute(QStringLiteral("OutgoingY"), key.outgoingHandle.y());
                node.setAttribute(QStringLiteral("IncomingInfluence"), key.incomingInfluence);
                node.setAttribute(QStringLiteral("OutgoingInfluence"), key.outgoingInfluence);
                node.setAttribute(QStringLiteral("HandlesLocked"), key.handlesLocked ? 1 : 0);
                appendVariant(doc, node, QStringLiteral("Value"), key.value);
                curve.appendChild(node);
            }
            animations.appendChild(curve);
        }
        fx.appendChild(animations);
        root.appendChild(fx);
    }
    wrapper.appendChild(root);
}

bool readEffectsExtension(const QDomElement& wrapper, composition::Clip* clip)
{
    const QDomElement root = wrapper.firstChildElement(QStringLiteral("OpenVegasEffects"));
    if (root.isNull() || !clip) return false;
    clip->effects.clear();
    for (QDomElement fx = root.firstChildElement(QStringLiteral("Effect")); !fx.isNull();
         fx = fx.nextSiblingElement(QStringLiteral("Effect"))) {
        composition::Effect effect;
        effect.pluginId = core::Identifier(fx.attribute(QStringLiteral("PluginID")));
        effect.name = fx.attribute(QStringLiteral("Name"));
        effect.enabled = fx.attribute(QStringLiteral("Enabled"), QStringLiteral("1")).toInt() != 0;
        const QDomElement parameters = fx.firstChildElement(QStringLiteral("Parameters"));
        for (QDomElement value = parameters.firstChildElement(QStringLiteral("Value")); !value.isNull();
             value = value.nextSiblingElement(QStringLiteral("Value"))) {
            const int index = value.attribute(QStringLiteral("Index")).toInt();
            if (index >= effect.parameterValues.size()) effect.parameterValues.resize(index + 1);
            if (index >= 0) effect.parameterValues[index] = value.text();
        }
        const QDomElement animations = fx.firstChildElement(QStringLiteral("Animations"));
        for (QDomElement curveNode = animations.firstChildElement(QStringLiteral("Curve"));
             !curveNode.isNull(); curveNode = curveNode.nextSiblingElement(QStringLiteral("Curve"))) {
            const int parameter = curveNode.attribute(QStringLiteral("Parameter")).toInt();
            composition::KeyFrameList curve(readVariant(
                curveNode.firstChildElement(QStringLiteral("Default"))));
            curve.setCanInterpolate(curveNode.attribute(
                QStringLiteral("CanInterpolate"), QStringLiteral("1")).toInt() != 0);
            for (QDomElement node = curveNode.firstChildElement(QStringLiteral("Key")); !node.isNull();
                 node = node.nextSiblingElement(QStringLiteral("Key"))) {
                composition::KeyFrame key;
                key.frame = node.attribute(QStringLiteral("Frame")).toInt();
                key.id = node.attribute(QStringLiteral("ID")).toInt();
                key.temporal = static_cast<composition::TemporalType>(qBound(
                    0, node.attribute(QStringLiteral("Temporal")).toInt(),
                    static_cast<int>(composition::TemporalType::ManualBezier)));
                key.value = readVariant(node.firstChildElement(QStringLiteral("Value")));
                key.incomingHandle = QPointF(node.attribute(QStringLiteral("IncomingX")).toDouble(),
                                             node.attribute(QStringLiteral("IncomingY")).toDouble());
                key.outgoingHandle = QPointF(node.attribute(QStringLiteral("OutgoingX")).toDouble(),
                                             node.attribute(QStringLiteral("OutgoingY")).toDouble());
                key.incomingInfluence = node.attribute(QStringLiteral("IncomingInfluence")).toDouble();
                key.outgoingInfluence = node.attribute(QStringLiteral("OutgoingInfluence")).toDouble();
                key.handlesLocked = node.attribute(QStringLiteral("HandlesLocked")).toInt() != 0;
                curve.add(key);
            }
            if (!curve.isEmpty()) effect.animation.insert(parameter, curve);
        }
        clip->effects.append(effect);
    }
    return true;
}

void appendClipsExtension(QDomDocument& doc, QDomElement& wrapper,
                          const QVector<composition::Clip>& clips, bool force = false)
{
    if (!force && clips.size() <= 1) return;
    if (clips.isEmpty()) return;
    QDomElement root = doc.createElement(QStringLiteral("OpenVegasClips"));
    root.setAttribute(QStringLiteral("Version"), 1);
    for (const auto& clip : clips) {
        QDomElement node = doc.createElement(QStringLiteral("Clip"));
        node.setAttribute(QStringLiteral("MediaID"), clip.mediaId.value());
        node.setAttribute(QStringLiteral("Start"), QString::number(clip.startSeconds, 'g', 17));
        node.setAttribute(QStringLiteral("Duration"), QString::number(clip.durationSeconds, 'g', 17));
        node.setAttribute(QStringLiteral("SourceStart"), QString::number(clip.sourceStartSeconds, 'g', 17));
        node.setAttribute(QStringLiteral("Speed"), QString::number(clip.speed, 'g', 17));
        node.setAttribute(QStringLiteral("AudioLevel"), QString::number(clip.audioLevel, 'g', 17));
        const core::Identifier nestedId = clip.nestedComposition
            ? clip.nestedComposition->id() : clip.nestedCompositionId;
        if (nestedId.isValid())
            node.setAttribute(QStringLiteral("NestedCompositionID"), nestedId.value());
        appendEffectsExtension(doc, node, clip);
        root.appendChild(node);
    }
    wrapper.appendChild(root);
}

bool readClipsExtension(const QDomElement& wrapper, composition::Layer* layer)
{
    if (!layer) return false;
    const QDomElement root = wrapper.firstChildElement(QStringLiteral("OpenVegasClips"));
    if (root.isNull()) return false;
    QVector<composition::Clip> clips;
    for (QDomElement node = root.firstChildElement(QStringLiteral("Clip")); !node.isNull();
         node = node.nextSiblingElement(QStringLiteral("Clip"))) {
        composition::Clip clip;
        clip.mediaId = core::Identifier(node.attribute(QStringLiteral("MediaID")));
        clip.startSeconds = node.attribute(QStringLiteral("Start")).toDouble();
        clip.durationSeconds = node.attribute(QStringLiteral("Duration")).toDouble();
        clip.sourceStartSeconds = node.attribute(QStringLiteral("SourceStart")).toDouble();
        clip.speed = node.attribute(QStringLiteral("Speed"), QStringLiteral("1")).toDouble();
        if (clip.speed <= 0.0) clip.speed = 1.0;
        clip.audioLevel = node.attribute(QStringLiteral("AudioLevel")).toDouble();
        clip.nestedCompositionId = core::Identifier(
            node.attribute(QStringLiteral("NestedCompositionID")));
        readEffectsExtension(node, &clip);
        clips.append(clip);
    }
    if (clips.isEmpty()) return false;
    layer->clips = clips;
    return true;
}

void appendMasksExtension(QDomDocument& doc, QDomElement& wrapper,
                          const QVector<composition::LayerMask>& masks)
{
    if (masks.isEmpty()) return;
    QDomElement root = doc.createElement(QStringLiteral("OpenVegasMasks"));
    root.setAttribute(QStringLiteral("Version"), 1);
    for (const auto& mask : masks) {
        QDomElement node = doc.createElement(QStringLiteral("Mask"));
        node.setAttribute(QStringLiteral("ID"), mask.id.value());
        node.setAttribute(QStringLiteral("Name"), mask.name);
        node.setAttribute(QStringLiteral("Shape"), int(mask.shape));
        node.setAttribute(QStringLiteral("X"), mask.bounds.x());
        node.setAttribute(QStringLiteral("Y"), mask.bounds.y());
        node.setAttribute(QStringLiteral("Width"), mask.bounds.width());
        node.setAttribute(QStringLiteral("Height"), mask.bounds.height());
        node.setAttribute(QStringLiteral("Enabled"), mask.enabled ? 1 : 0);
        node.setAttribute(QStringLiteral("Inverted"), mask.inverted ? 1 : 0);
        node.setAttribute(QStringLiteral("Opacity"), mask.opacity);
        node.setAttribute(QStringLiteral("Feather"), mask.feather);
        node.setAttribute(QStringLiteral("Expansion"), mask.expansion);
        for (const QPointF& point : mask.points) {
            QDomElement pointNode = doc.createElement(QStringLiteral("Point"));
            pointNode.setAttribute(QStringLiteral("X"), point.x());
            pointNode.setAttribute(QStringLiteral("Y"), point.y());
            node.appendChild(pointNode);
        }
        root.appendChild(node);
    }
    wrapper.appendChild(root);
}

void readMasksExtension(const QDomElement& wrapper, composition::Layer* layer)
{
    if (!layer) return;
    const QDomElement root = wrapper.firstChildElement(QStringLiteral("OpenVegasMasks"));
    if (root.isNull()) return;
    layer->masks.clear();
    for (QDomElement node = root.firstChildElement(QStringLiteral("Mask")); !node.isNull();
         node = node.nextSiblingElement(QStringLiteral("Mask"))) {
        composition::LayerMask mask;
        const QString id = node.attribute(QStringLiteral("ID"));
        if (!id.isEmpty()) mask.id = core::Identifier(id);
        mask.name = node.attribute(QStringLiteral("Name"), QStringLiteral("Mask"));
        mask.shape = static_cast<composition::MaskShape>(qBound(0,
            node.attribute(QStringLiteral("Shape")).toInt(),
            int(composition::MaskShape::Freehand)));
        mask.bounds = QRectF(node.attribute(QStringLiteral("X")).toDouble(),
                             node.attribute(QStringLiteral("Y")).toDouble(),
                             node.attribute(QStringLiteral("Width")).toDouble(),
                             node.attribute(QStringLiteral("Height")).toDouble());
        mask.enabled = node.attribute(QStringLiteral("Enabled"), QStringLiteral("1")).toInt() != 0;
        mask.inverted = node.attribute(QStringLiteral("Inverted")).toInt() != 0;
        mask.opacity = node.attribute(QStringLiteral("Opacity"), QStringLiteral("1")).toDouble();
        mask.feather = node.attribute(QStringLiteral("Feather")).toDouble();
        mask.expansion = node.attribute(QStringLiteral("Expansion")).toDouble();
        for (QDomElement point = node.firstChildElement(QStringLiteral("Point")); !point.isNull();
             point = point.nextSiblingElement(QStringLiteral("Point")))
            mask.points.append(QPointF(point.attribute(QStringLiteral("X")).toDouble(),
                                       point.attribute(QStringLiteral("Y")).toDouble()));
        layer->masks.append(mask);
    }
}

void appendMotionTracksExtension(QDomDocument& doc, QDomElement& wrapper,
                                 const QVector<composition::MotionTrack>& tracks)
{
    if (tracks.isEmpty()) return;
    QDomElement root = doc.createElement(QStringLiteral("OpenVegasMotionTracks"));
    root.setAttribute(QStringLiteral("Version"), 1);
    for (const auto& track : tracks) {
        QDomElement node = doc.createElement(QStringLiteral("Track"));
        node.setAttribute(QStringLiteral("ID"), track.id.value());
        node.setAttribute(QStringLiteral("Name"), track.name);
        node.setAttribute(QStringLiteral("Enabled"), track.enabled ? 1 : 0);
        node.setAttribute(QStringLiteral("X"), track.point.x());
        node.setAttribute(QStringLiteral("Y"), track.point.y());
        node.setAttribute(QStringLiteral("SampleRadius"), track.sampleRadius);
        node.setAttribute(QStringLiteral("SearchRadius"), track.searchRadius);
        QVector<int> frames = track.xCurve.locations();
        for (int frame : track.yCurve.locations()) if (!frames.contains(frame)) frames.append(frame);
        std::sort(frames.begin(), frames.end());
        for (int frame : frames) {
            QDomElement key = doc.createElement(QStringLiteral("Key"));
            const QPointF point = track.pointAt(frame);
            key.setAttribute(QStringLiteral("Frame"), frame);
            key.setAttribute(QStringLiteral("X"), QString::number(point.x(), 'g', 17));
            key.setAttribute(QStringLiteral("Y"), QString::number(point.y(), 'g', 17));
            node.appendChild(key);
        }
        root.appendChild(node);
    }
    wrapper.appendChild(root);
}

void readMotionTracksExtension(const QDomElement& wrapper, composition::Layer* layer)
{
    if (!layer) return;
    const QDomElement root = wrapper.firstChildElement(QStringLiteral("OpenVegasMotionTracks"));
    if (root.isNull()) return;
    layer->motionTracks.clear();
    for (QDomElement node = root.firstChildElement(QStringLiteral("Track")); !node.isNull();
         node = node.nextSiblingElement(QStringLiteral("Track"))) {
        composition::MotionTrack track;
        const QString id = node.attribute(QStringLiteral("ID"));
        if (!id.isEmpty()) track.id = core::Identifier(id);
        track.name = node.attribute(QStringLiteral("Name"), QStringLiteral("Track"));
        track.enabled = node.attribute(QStringLiteral("Enabled"), QStringLiteral("1")).toInt() != 0;
        track.point = QPointF(node.attribute(QStringLiteral("X")).toDouble(),
                              node.attribute(QStringLiteral("Y")).toDouble());
        track.sampleRadius = qBound(2, node.attribute(QStringLiteral("SampleRadius"), QStringLiteral("8")).toInt(), 64);
        track.searchRadius = qBound(track.sampleRadius,
            node.attribute(QStringLiteral("SearchRadius"), QStringLiteral("24")).toInt(), 256);
        track.xCurve = composition::KeyFrameList(track.point.x());
        track.yCurve = composition::KeyFrameList(track.point.y());
        for (QDomElement key = node.firstChildElement(QStringLiteral("Key")); !key.isNull();
             key = key.nextSiblingElement(QStringLiteral("Key"))) {
            composition::KeyFrame x;
            x.frame = key.attribute(QStringLiteral("Frame")).toInt();
            x.value = key.attribute(QStringLiteral("X")).toDouble();
            x.temporal = composition::TemporalType::Linear;
            composition::KeyFrame y = x;
            y.value = key.attribute(QStringLiteral("Y")).toDouble();
            track.xCurve.add(x);
            track.yCurve.add(y);
        }
        layer->motionTracks.append(track);
    }
}

// OpenVEGAS extension for composite shots. The reference project format has a
// graph of CompositionAsset objects; until that whole binary schema is mapped,
// this transparent XML block keeps the same graph losslessly for this port and
// is ignored by VEGAS Effects.
void appendEmbeddedComposition(QDomDocument& doc, QDomElement& root,
                               const composition::Composition& comp)
{
    QDomElement node = doc.createElement(QStringLiteral("Composition"));
    node.setAttribute(QStringLiteral("ID"), comp.id().value());
    node.setAttribute(QStringLiteral("Name"), comp.name());
    node.setAttribute(QStringLiteral("Width"), comp.width());
    node.setAttribute(QStringLiteral("Height"), comp.height());
    node.setAttribute(QStringLiteral("FpsNumerator"), comp.fpsNumerator());
    node.setAttribute(QStringLiteral("FpsDenominator"), comp.fpsDenominator());
    node.setAttribute(QStringLiteral("Duration"),
                      QString::number(comp.durationSeconds(), 'g', 17));
    const double fps = comp.fpsDenominator() > 0
        ? double(comp.fpsNumerator()) / comp.fpsDenominator() : 30.0;

    for (const composition::Layer& layer : comp.layers()) {
        QDomElement layerNode = doc.createElement(QStringLiteral("Layer"));
        layerNode.setAttribute(QStringLiteral("ID"), layer.id.value());
        layerNode.setAttribute(QStringLiteral("ParentID"), layer.parentLayerId.value());
        layerNode.setAttribute(QStringLiteral("Name"), layer.name);
        layerNode.setAttribute(QStringLiteral("Kind"), composition::layerKindToken(layer.kind));
        layerNode.setAttribute(QStringLiteral("Dimension"),
                               layer.dimension == composition::LayerDimension::ThreeD ? 3 : 2);
        layerNode.setAttribute(QStringLiteral("CameraFieldOfView"), QString::number(layer.cameraFieldOfView, 'g', 17));
        layerNode.setAttribute(QStringLiteral("PlaneColor"), layer.planeColor.name(QColor::HexArgb));
        layerNode.setAttribute(QStringLiteral("ModelAssetID"), layer.modelAssetId.value());
        layerNode.setAttribute(QStringLiteral("BlendMode"), layer.blendMode);
        layerNode.setAttribute(QStringLiteral("Opacity"), QString::number(layer.opacity, 'g', 17));
        layerNode.setAttribute(QStringLiteral("Visible"), layer.visible ? 1 : 0);
        layerNode.setAttribute(QStringLiteral("Muted"), layer.muted ? 1 : 0);
        layerNode.setAttribute(QStringLiteral("Locked"), layer.locked ? 1 : 0);
        layerNode.setAttribute(QStringLiteral("LabelColor"), layer.labelColor.name(QColor::HexArgb));

        const composition::LayerTransform& t = layer.transform;
        layerNode.setAttribute(QStringLiteral("AnchorX"), t.anchorPoint.x());
        layerNode.setAttribute(QStringLiteral("AnchorY"), t.anchorPoint.y());
        layerNode.setAttribute(QStringLiteral("AnchorZ"), t.anchorPointZ);
        QDomElement pm = doc.createElement(QStringLiteral("PropertyManager"));
        appendScalarProp(doc, pm, QStringLiteral("opacity"), layer.opacity * 100.0,
                         t.opacityCurve, fps);
        appendPointProp(doc, pm, QStringLiteral("position"), QStringLiteral("p3"), t.position,
                        t.positionXCurve, t.positionYCurve, fps);
        appendPointProp(doc, pm, QStringLiteral("scale"), QStringLiteral("sc"), t.scalePercent,
                        t.scaleXCurve, t.scaleYCurve, fps);
        appendScalarProp(doc, pm, QStringLiteral("rotationZ"), t.rotationDegrees,
                         t.rotationCurve, fps);
        appendScalarProp(doc, pm, QStringLiteral("positionZ"), t.positionZ,
                         t.positionZCurve, fps);
        appendScalarProp(doc, pm, QStringLiteral("scaleZ"), t.scaleZPercent,
                         t.scaleZCurve, fps);
        appendScalarProp(doc, pm, QStringLiteral("rotationX"), t.rotationXDegrees,
                         t.rotationXCurve, fps);
        appendScalarProp(doc, pm, QStringLiteral("rotationY"), t.rotationYDegrees,
                         t.rotationYCurve, fps);
        appendScalarProp(doc, pm, QStringLiteral("orientationX"), t.orientationX,
                         t.orientationXCurve, fps);
        appendScalarProp(doc, pm, QStringLiteral("orientationY"), t.orientationY,
                         t.orientationYCurve, fps);
        appendScalarProp(doc, pm, QStringLiteral("orientationZ"), t.orientationZ,
                         t.orientationZCurve, fps);
        layerNode.appendChild(pm);
        appendClipsExtension(doc, layerNode, layer.clips, true);
        appendMasksExtension(doc, layerNode, layer.masks);
        appendMotionTracksExtension(doc, layerNode, layer.motionTracks);
        node.appendChild(layerNode);
    }
    root.appendChild(node);
}

void appendEmbeddedCompositions(QDomDocument& doc, QDomElement& compositionAsset,
                                const composition::Composition& rootComposition)
{
    QDomElement root = doc.createElement(QStringLiteral("OpenVegasCompositions"));
    root.setAttribute(QStringLiteral("Version"), 1);
    QSet<QString> written;
    std::function<void(const composition::Composition&)> visit =
        [&](const composition::Composition& comp) {
            for (const composition::Layer& layer : comp.layers()) {
                for (const composition::Clip& clip : layer.clips) {
                    if (!clip.nestedComposition
                        || written.contains(clip.nestedComposition->id().value())) continue;
                    written.insert(clip.nestedComposition->id().value());
                    appendEmbeddedComposition(doc, root, *clip.nestedComposition);
                    visit(*clip.nestedComposition);
                }
            }
        };
    visit(rootComposition);
    if (root.hasChildNodes()) compositionAsset.appendChild(root);
}

void readEmbeddedCompositions(const QDomElement& compositionAsset,
                              composition::Composition* rootComposition)
{
    if (!rootComposition) return;
    const QDomElement root = compositionAsset.firstChildElement(
        QStringLiteral("OpenVegasCompositions"));
    if (root.isNull()) return;
    QHash<QString, std::shared_ptr<composition::Composition>> byId;
    for (QDomElement node = root.firstChildElement(QStringLiteral("Composition")); !node.isNull();
         node = node.nextSiblingElement(QStringLiteral("Composition"))) {
        auto comp = std::make_shared<composition::Composition>();
        comp->setId(core::Identifier(node.attribute(QStringLiteral("ID"))));
        comp->setName(node.attribute(QStringLiteral("Name"), QStringLiteral("Composite Shot")));
        comp->setSize(node.attribute(QStringLiteral("Width"), QStringLiteral("1920")).toInt(),
                      node.attribute(QStringLiteral("Height"), QStringLiteral("1080")).toInt());
        comp->setFrameRate(node.attribute(QStringLiteral("FpsNumerator"), QStringLiteral("30")).toInt(),
                           qMax(1, node.attribute(QStringLiteral("FpsDenominator"), QStringLiteral("1")).toInt()));
        comp->setDurationSeconds(node.attribute(QStringLiteral("Duration"), QStringLiteral("10")).toDouble());
        const double fps = double(comp->fpsNumerator()) / qMax(1, comp->fpsDenominator());
        for (QDomElement layerNode = node.firstChildElement(QStringLiteral("Layer")); !layerNode.isNull();
             layerNode = layerNode.nextSiblingElement(QStringLiteral("Layer"))) {
            composition::Layer& layer = comp->addLayer(layerNode.attribute(QStringLiteral("Name")));
            const QString id = layerNode.attribute(QStringLiteral("ID"));
            if (!id.isEmpty()) layer.id = core::Identifier(id);
            layer.parentLayerId = core::Identifier(layerNode.attribute(QStringLiteral("ParentID")));
            layer.kind = composition::layerKindFromToken(layerNode.attribute(QStringLiteral("Kind")));
            layer.dimension = layerNode.attribute(QStringLiteral("Dimension")).toInt() == 3
                ? composition::LayerDimension::ThreeD : composition::LayerDimension::TwoD;
            bool validFov = false;
            const double cameraFov = layerNode.attribute(QStringLiteral("CameraFieldOfView")).toDouble(&validFov);
            if (validFov && std::isfinite(cameraFov)) layer.cameraFieldOfView = qBound(20.0, cameraFov, 140.0);
            const QColor plane(layerNode.attribute(QStringLiteral("PlaneColor")));
            if (plane.isValid()) layer.planeColor = plane;
            layer.modelAssetId = core::Identifier(layerNode.attribute(QStringLiteral("ModelAssetID")));
            layer.blendMode = layerNode.attribute(QStringLiteral("BlendMode"), QStringLiteral("None"));
            layer.opacity = layerNode.attribute(QStringLiteral("Opacity"), QStringLiteral("1")).toDouble();
            layer.visible = layerNode.attribute(QStringLiteral("Visible"), QStringLiteral("1")).toInt() != 0;
            layer.muted = layerNode.attribute(QStringLiteral("Muted")).toInt() != 0;
            layer.locked = layerNode.attribute(QStringLiteral("Locked")).toInt() != 0;
            const QColor label(layerNode.attribute(QStringLiteral("LabelColor")));
            if (label.isValid()) layer.labelColor = label;
            const QDomElement pm = layerNode.firstChildElement(QStringLiteral("PropertyManager"));
            layer.transform = readTransform(pm, fps);
            composition::LayerTransform& t = layer.transform;
            t.anchorPoint = QPointF(layerNode.attribute(QStringLiteral("AnchorX")).toDouble(),
                                    layerNode.attribute(QStringLiteral("AnchorY")).toDouble());
            t.anchorPointZ = layerNode.attribute(QStringLiteral("AnchorZ")).toDouble();
            const auto scalar = [&](const QString& name, double fallback,
                                    composition::KeyFrameList* curve) {
                const QDomElement prop = propElement(pm, name);
                if (curve) *curve = propAnimation(prop, fps, QString());
                return prop.isNull() ? fallback : propScalar(pm, name, fallback);
            };
            t.positionZ = scalar(QStringLiteral("positionZ"), 0.0, &t.positionZCurve);
            t.scaleZPercent = scalar(QStringLiteral("scaleZ"), 100.0, &t.scaleZCurve);
            t.rotationXDegrees = scalar(QStringLiteral("rotationX"), 0.0, &t.rotationXCurve);
            t.rotationYDegrees = scalar(QStringLiteral("rotationY"), 0.0, &t.rotationYCurve);
            t.orientationX = scalar(QStringLiteral("orientationX"), 0.0, &t.orientationXCurve);
            t.orientationY = scalar(QStringLiteral("orientationY"), 0.0, &t.orientationYCurve);
            t.orientationZ = scalar(QStringLiteral("orientationZ"), 0.0, &t.orientationZCurve);
            readClipsExtension(layerNode, &layer);
            readMasksExtension(layerNode, &layer);
            readMotionTracksExtension(layerNode, &layer);
        }
        byId.insert(comp->id().value(), comp);
    }
    std::function<void(composition::Composition*)> resolve = [&](composition::Composition* comp) {
        for (int li = 0; li < comp->layers().size(); ++li) {
            composition::Layer& layer = comp->layerRef(li);
            for (composition::Clip& clip : layer.clips) {
                if (!clip.nestedCompositionId.isValid()) continue;
                clip.nestedComposition = byId.value(clip.nestedCompositionId.value());
            }
        }
    };
    for (const auto& comp : byId) resolve(comp.get());
    resolve(rootComposition);
}

} // namespace

core::Result VegfxSerializer::loadFromFile(const QString& filePath,
                                           composition::Composition* composition,
                                           media::MediaManager* media, QByteArray* screenLayout)
{
    if (screenLayout) screenLayout->clear();
    if (!composition || !media) {
        return core::Result::fail(core::ResultStatus::InvalidArgument,
                                  QStringLiteral("Null model pointers"));
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return core::Result::fail(core::ResultStatus::MissingResource,
                                  QStringLiteral("Cannot open file for reading: %1").arg(filePath));
    }
    const QString xmlText = QString::fromUtf8(file.readAll());
    file.close();

    QDomDocument doc;
    const QDomDocument::ParseResult result = doc.setContent(QAnyStringView(xmlText));
    if (!result) {
        return core::Result::fail(
            core::ResultStatus::OperationFailed,
            QStringLiteral("Invalid .vegfx (XML) at line %1: %2")
                .arg(result.errorLine)
                .arg(result.errorMessage));
    }

    const QDomElement docRoot = doc.documentElement();
    if (docRoot.tagName() != QStringLiteral("VegasEffectsProject")) {
        return core::Result::fail(core::ResultStatus::Unsupported,
                                  QStringLiteral("Not a .vegfx project (root <%1>)")
                                      .arg(docRoot.tagName()));
    }

    const QDomElement project = docRoot.firstChildElement(QStringLiteral("Project"));
    if (project.isNull()) {
        return core::Result::fail(core::ResultStatus::Unsupported,
                                  QStringLiteral(".vegfx has no <Project> element"));
    }

    // Media is relinked against the folder the project lives in.
    if (screenLayout) *screenLayout = QByteArray::fromBase64(
        elemText(project, QStringLiteral("OpenVegasScreenLayout")).toLatin1());
    const QDir projectDir(QFileInfo(filePath).absolutePath());

    composition->clear();
    media->clear();

    const QString projectName = elemText(project, QStringLiteral("Name"));
    if (!projectName.isEmpty()) {
        composition->setName(projectName);
    }

    // ---- Asset library: AssetID -> absolute file path (+ media kind hints) ----
    QMap<QString, QString> assetPathByGuid;
    QMap<QString, QString> resolvedMediaPaths;
    const QDomElement assets = project.firstChildElement(QStringLiteral("AssetList"))
                                   .firstChildElement(QStringLiteral("Assets"));
    for (QDomElement a = assets.firstChildElement(); !a.isNull(); a = a.nextSiblingElement()) {
        const QString tag = a.tagName();
        const QString id = a.firstChildElement(QStringLiteral("ID")).text();
        if (tag == QStringLiteral("CompositionAsset") || tag.isEmpty() || id.isEmpty()) {
            continue;
        }
        QString filename = elemText(a, QStringLiteral("Filename"));
        if (!filename.isEmpty()) {
            const QString stored = QDir::cleanPath(filename);
            bool relinked = false;
            filename = resolveAssetPath(stored, projectDir, &relinked);
            if (relinked) {
                OV_LOG_INFO(QStringLiteral("VEGFX media relinked beside the project: %1 -> %2")
                                .arg(stored, filename));
            }
            assetPathByGuid.insert(id, filename);
            resolvedMediaPaths.insert(stored, filename);
            const core::Result r = media->importFile(filename);
            if (r.isFailure()) {
                media->registerMissingFile(filename);
                OV_LOG_WARN(
                    QStringLiteral("VEGFX media not found on this system: %1").arg(filename));
            } else if (tag == QStringLiteral("MediaAsset")) {
                // Trimmer in/out points are serialized on MediaAsset as
                // <InPoint>/<OutPoint> (frames).
                if (media::MediaAsset* asset = media->assetByFilePathForEdit(filename)) {
                    asset->setTrimInPoint(elemText(a, QStringLiteral("InPoint")).toInt());
                    asset->setTrimOutPoint(elemText(a, QStringLiteral("OutPoint")).toInt());
                }
            }
        }
    }

    // ---- Primary composition ----
    QDomElement compositionAsset;
    for (QDomElement c = assets.firstChildElement(); !c.isNull(); c = c.nextSiblingElement()) {
        if (c.tagName() == QStringLiteral("CompositionAsset")) {
            compositionAsset = c;
            break;
        }
    }
    if (compositionAsset.isNull()) {
        return core::Result::ok();
    }

    const QString compositionId = elemText(compositionAsset, QStringLiteral("ID"));
    if (!compositionId.isEmpty()) composition->setId(core::Identifier(compositionId));

    const QString compName = elemText(compositionAsset, QStringLiteral("Name"));
    if (!compName.isEmpty()) {
        composition->setName(compName);
    }

    double fps = 30.0;
    QDomElement avs = compositionAsset.firstChildElement(QStringLiteral("AudioVideoSettings"));
    if (!avs.isNull()) {
        const QString fr = elemText(avs, QStringLiteral("FrameRate"));
        if (!fr.isEmpty()) {
            bool ok = false;
            const double v = fr.toDouble(&ok);
            if (ok && v > 0.0) {
                fps = v;
                composition->setFrameRate(int(fps + 0.5), 1);
            }
        }
        // <FrameCount> is not always written; this project instead carries the
        // range on the composition itself, and without the fallback the timeline
        // kept its default length while the layers ran far past it.
        const QString frameCount = elemText(avs, QStringLiteral("FrameCount"));
        if (!frameCount.isEmpty()) {
            composition->setDurationSeconds(frameToSeconds(frameCount.toLongLong(), fps));
        } else {
            const long long inPoint = elemText(compositionAsset, QStringLiteral("InPoint")).toLongLong();
            const long long outPoint =
                elemText(compositionAsset, QStringLiteral("OutPoint")).toLongLong();
            if (outPoint > inPoint) {
                composition->setDurationSeconds(frameToSeconds(outPoint - inPoint, fps));
            }
        }
        const QString width = elemText(avs, QStringLiteral("Width"));
        const QString height = elemText(avs, QStringLiteral("Height"));
        if (!width.isEmpty() && !height.isEmpty()) {
            composition->setSize(width.toInt(), height.toInt());
        }
    }

    // ---- Layers ----
    const QDomElement layers = compositionAsset.firstChildElement(QStringLiteral("Layers"));
    int z = 0;
    for (QDomElement lw = layers.firstChildElement(); !lw.isNull();
         lw = lw.nextSiblingElement()) {
        // Element names follow the reference's own layer classes (see
        // MARKDOWN/RE_Project_dll.md: AssetLayer, TextLayer, PointLayer,
        // GradeLayer, LightLayer, CameraLayer). A Plane has no class of its own
        // there - it rides on an AssetLayer - so the exact kind is carried by
        // our own attribute, which the reference ignores.
        const QString kind = lw.tagName();
        static const QHash<QString, composition::LayerKind> kLayerElements = {
            {QStringLiteral("AssetLayer"),  composition::LayerKind::Media},
            {QStringLiteral("TextLayer"),   composition::LayerKind::Text},
            {QStringLiteral("PointLayer"),  composition::LayerKind::Point},
            {QStringLiteral("GradeLayer"),  composition::LayerKind::Grade},
            {QStringLiteral("LightLayer"),  composition::LayerKind::Light},
            {QStringLiteral("CameraLayer"), composition::LayerKind::Camera},
            {QStringLiteral("Model3DLayer"), composition::LayerKind::Model3D},
        };
        const auto kindIt = kLayerElements.constFind(kind);
        if (kindIt == kLayerElements.constEnd()) {
            continue;
        }
        composition::LayerKind layerKind = kindIt.value();
        const QString kindAttr = lw.attribute(QStringLiteral("OpenVegasKind"));
        if (!kindAttr.isEmpty()) {
            layerKind = composition::layerKindFromToken(kindAttr, layerKind);
        }

        const QDomElement named = namedLayerElement(lw);
        QString layerName = elemText(named, QStringLiteral("Name"));
        if (layerName.isEmpty()) {
            layerName = named.tagName();
        }
        // Layer identities in .vegfx are GUID-based; names are not unique (e.g.
        // multiple layers called "New Text"). The model addresses layers by
        // name, so disambiguate collisions with a numeric suffix.
        {
            int suffix = 2;
            QString candidate = layerName;
            while (composition->layer(candidate) != nullptr) {
                candidate = QStringLiteral("%1 %2").arg(layerName).arg(suffix++);
            }
            layerName = candidate;
        }

        composition::Layer& layer = composition->addLayer(layerName);
        const QString layerId = elemText(named, QStringLiteral("ID"));
        if (!layerId.isEmpty()) {
            layer.id = core::Identifier(layerId);
        }
        const QString parentLayerId = lw.attribute(QStringLiteral("OpenVegasParentLayerID"));
        if (!parentLayerId.isEmpty()) {
            layer.parentLayerId = core::Identifier(parentLayerId);
        }
        layer.kind = layerKind;
        layer.zIndex = z++;
        layer.visible = elemText(named, QStringLiteral("Visible")) != QStringLiteral("0");
        layer.locked = named.attribute(QStringLiteral("OpenVegasLocked")) == QStringLiteral("1");
        layer.muted = lw.attribute(QStringLiteral("OpenVegasMuted")) == QStringLiteral("1");
        const QColor planeColor(lw.attribute(QStringLiteral("OpenVegasPlaneColor")));
        if (planeColor.isValid()) layer.planeColor = planeColor;
        const QColor labelColor(named.attribute(QStringLiteral("OpenVegasLabelColor")));
        if (labelColor.isValid()) layer.labelColor = labelColor;

        // BlendMode 0 / absent is the reference's default, and its layer tree
        // shows that as "None" - which is also this model's default. It used to
        // be read back as "Normal", so every loaded layer disagreed with both.
        const QString blend = elemText(named, QStringLiteral("BlendMode"));
        layer.blendMode = (blend == QStringLiteral("0") || blend.isEmpty())
                              ? QStringLiteral("None")
                              : blend;

        // opacity is stored as a percentage (0-100) in the layer PropertyManager.
        const QDomElement pm = named.firstChildElement(QStringLiteral("PropertyManager"));
        const double opacityPct = pm.isNull() ? 100.0 : propValue(pm, QStringLiteral("opacity"));
        if (opacityPct >= 0.0) {
            layer.opacity = opacityPct / 100.0;
        }
        layer.transform = readTransform(pm, fps);
        bool validCameraFov = false;
        const double cameraFov = lw.attribute(QStringLiteral("OpenVegasCameraFieldOfView")).toDouble(&validCameraFov);
        if (validCameraFov && std::isfinite(cameraFov)) layer.cameraFieldOfView = qBound(20.0, cameraFov, 140.0);
        readMasksExtension(lw, &layer);
        readMotionTracksExtension(lw, &layer);

        // The 3D side, from the attributes the writer above put on the wrapper.
        // A file written by the reference carries none of these, so a layer
        // read from one simply stays 2D - which is what it was.
        if (lw.attribute(QStringLiteral("OpenVegasDimension")) == QLatin1String("3D")) {
            layer.dimension = composition::LayerDimension::ThreeD;
            composition::LayerTransform& t = layer.transform;
            const auto attr = [&lw](const char* name, double fallback) {
                const QString value = lw.attribute(QString::fromLatin1(name));
                bool ok = false;
                const double parsed = value.toDouble(&ok);
                return ok ? parsed : fallback;
            };
            t.positionZ = attr("OpenVegasPositionZ", 0.0);
            t.anchorPointZ = attr("OpenVegasAnchorZ", 0.0);
            t.scaleZPercent = attr("OpenVegasScaleZ", 100.0);
            t.rotationXDegrees = attr("OpenVegasRotationX", 0.0);
            t.rotationYDegrees = attr("OpenVegasRotationY", 0.0);
            t.orientationX = attr("OpenVegasOrientationX", 0.0);
            t.orientationY = attr("OpenVegasOrientationY", 0.0);
            t.orientationZ = attr("OpenVegasOrientationZ", 0.0);
        }

        // A model layer needs its geometry back, not just the reference to it:
        // the mesh lives beside the asset and nothing else would reload it.
        const QString modelPath = resolveAssetPath(
            lw.attribute(QStringLiteral("OpenVegasModelPath")), projectDir, nullptr);
        if (layerKind == composition::LayerKind::Model3D && !modelPath.isEmpty() && media) {
            const core::Result imported =
                media->importModel(modelPath, model3d::ImportSettings());
            if (imported.isFailure()) {
                OV_LOG_WARN(QStringLiteral("Could not reload 3D model %1: %2")
                                .arg(modelPath, imported.message()));
            } else {
                layer.modelAssetId = media->assetByFilePath(modelPath).id();
            }
        }

        const long long start = elemText(named, QStringLiteral("StartFrame")).toLongLong();
        const long long end = elemText(named, QStringLiteral("EndFrame")).toLongLong();
        const long long len = (end > start) ? (end - start) : 0;

        if (kind == QStringLiteral("AssetLayer")) {
            const QString assetGuid = elemText(lw, QStringLiteral("AssetID"));
            const QString path = assetPathByGuid.value(assetGuid);
            if (!path.isEmpty()) {
                const core::Identifier mediaId(QStringLiteral("media:") + path);
                composition::Clip* clip =
                    composition->addClip(layerName, mediaId, frameToSeconds(start, fps),
                                         len > 0 ? frameToSeconds(len, fps) : 1.0);
                if (clip) {
                    clip->startSeconds = frameToSeconds(start, fps);
                    clip->durationSeconds = len > 0 ? frameToSeconds(len, fps) : 1.0;
                }
            }
        } else {
            composition::Clip* clip =
                composition->addClip(layerName, core::Identifier(QStringLiteral("media:text")),
                                     frameToSeconds(start, fps),
                                     len > 0 ? frameToSeconds(len, fps) : 1.0);
            if (clip) {
                clip->startSeconds = frameToSeconds(start, fps);
                clip->durationSeconds = len > 0 ? frameToSeconds(len, fps) : 1.0;
                composition::Effect fx;
                fx.pluginId = core::Identifier(QStringLiteral("text"));
                fx.name = QStringLiteral("Text");
                int pixelSize = 48;
                // <TextBox> is a sibling of <LayerBase>, i.e. a child of the
                // layer wrapper - not of the named element the other fields come
                // from. Reading it from `named` silently yielded an empty string
                // and the layer rendered blank.
                const QString content = textFromTextBox(lw, &pixelSize);
                fx.parameterValues = {content, QString::number(pixelSize)};
                const QDomElement savedStyle = lw.firstChildElement(QStringLiteral("OpenVegasTextStyle"));
                if (!savedStyle.isNull()) {
                    QStringList values;
                    for (QDomElement value = savedStyle.firstChildElement(QStringLiteral("Value"));
                         !value.isNull(); value = value.nextSiblingElement(QStringLiteral("Value")))
                        values.append(value.text()); // Preserve whitespace in text.
                    fx.parameterValues = composition::textStyleToParameters(
                        composition::textStyleFromParameters(values));
                }
                clip->effects.push_back(fx);
            }
        }

        // Clip geometry the editor tools changed, written by the block in the
        // saver. Read once the clip exists, which is why it sits at the foot of
        // the loop rather than beside the other attributes.
        if (!layer.clips.isEmpty()) {
            composition::Clip& first = layer.clips.first();
            readEffectsExtension(lw, &first);
            bool ok = false;
            const double source =
                lw.attribute(QStringLiteral("OpenVegasSourceStart")).toDouble(&ok);
            if (ok) {
                first.sourceStartSeconds = source;
            }
            const double speed = lw.attribute(QStringLiteral("OpenVegasSpeed")).toDouble(&ok);
            if (ok && speed > 0.0) {
                first.speed = speed;
            }
            const double audioLevel = lw.attribute(QStringLiteral("OpenVegasAudioLevel")).toDouble(&ok);
            if (ok) first.audioLevel = audioLevel;
        }
        readClipsExtension(lw, &layer);
    }

    // ---- EditorSequence ----
    {
        const QDomElement editorSeq =
            project.firstChildElement(QStringLiteral("EditorSequence"));
        composition::EditorSequence& seq = composition->editorSequence();
        if (!editorSeq.isNull()) {
            const QString seqName = elemText(editorSeq, QStringLiteral("Name"));
            if (!seqName.isEmpty()) {
                seq.name = seqName;
            }
            seq.cti = elemText(editorSeq, QStringLiteral("CTI")).toLongLong();
            seq.inPoint = elemText(editorSeq, QStringLiteral("InPoint")).toLongLong();
            seq.outPoint = elemText(editorSeq, QStringLiteral("OutPoint")).toLongLong();
            seq.timelineZoom = elemText(editorSeq, QStringLiteral("TimelineZoom")).toDouble();
            seq.timelineTimeFormat =
                elemText(editorSeq, QStringLiteral("TimelineTimeFormat")).toInt();
            seq.timelineSnapMode =
                elemText(editorSeq, QStringLiteral("TimelineSnapMode")).toInt();
            seq.timelineScrollSyncMode =
                elemText(editorSeq, QStringLiteral("TimelineScrollSyncMode")).toInt();
            const QString vg = elemText(editorSeq, QStringLiteral("TimelineValueGraph"));
            if (!vg.isEmpty()) {
                seq.timelineValueGraph = (vg == QStringLiteral("1") || vg.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0);
            }
            const QString gaz = elemText(editorSeq, QStringLiteral("TimelineGraphAutoZoom"));
            if (!gaz.isEmpty()) {
                seq.timelineGraphAutoZoom = (gaz == QStringLiteral("1") || gaz.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0);
            }
            seq.videoPreviewSize =
                elemText(editorSeq, QStringLiteral("VideoPreviewSize")).toInt();
            seq.audioPreviewSize =
                elemText(editorSeq, QStringLiteral("AudioPreviewSize")).toInt();
            seq.previewMode = elemText(editorSeq, QStringLiteral("PreviewMode")).toInt();

            const QDomElement seqAvs =
                editorSeq.firstChildElement(QStringLiteral("AudioVideoSettings"));
            if (!seqAvs.isNull()) {
                seq.frameCount = elemText(seqAvs, QStringLiteral("FrameCount")).toLongLong();
                seq.audioSampleRate =
                    elemText(seqAvs, QStringLiteral("AudioSampleRate")).toInt();
                seq.width = elemText(seqAvs, QStringLiteral("Width")).toInt();
                seq.height = elemText(seqAvs, QStringLiteral("Height")).toInt();
                const QString fr = elemText(seqAvs, QStringLiteral("FrameRate"));
                if (!fr.isEmpty()) {
                    seq.fps = fr.toDouble();
                }
            }

            const QDomElement renderSettings =
                editorSeq.firstChildElement(QStringLiteral("RenderSettings"));
            if (!renderSettings.isNull()) {
                seq.motionBlurEnabled =
                    elemText(renderSettings, QStringLiteral("MotionBlurEnabled")) ==
                    QStringLiteral("1");
                seq.shutterAngle =
                    elemText(renderSettings, QStringLiteral("ShutterAngle")).toDouble();
                seq.shutterPhase =
                    elemText(renderSettings, QStringLiteral("ShutterPhase")).toDouble();
                seq.maxNumOfSamples =
                    elemText(renderSettings, QStringLiteral("MaxNumOfSamples")).toInt();
                seq.useAdaptiveSamples =
                    elemText(renderSettings, QStringLiteral("UseAdaptiveSamples")) ==
                    QStringLiteral("1");
            }

            const auto readTrack = [](const QDomElement& t) {
                composition::SequenceTrack track;
                track.id = elemText(t, QStringLiteral("ID"));
                track.name = elemText(t, QStringLiteral("Name"));
                track.visible = elemText(t, QStringLiteral("Visible")) != QStringLiteral("0");
                track.muted = elemText(t, QStringLiteral("Muted")) == QStringLiteral("1");
                track.solo = elemText(t, QStringLiteral("Solo")) == QStringLiteral("1");
                track.locked = elemText(t, QStringLiteral("Locked")) == QStringLiteral("1");
                const QDomElement pm = t.firstChildElement(QStringLiteral("PropertyManager"));
                track.audioLevel =
                    pm.isNull() ? 0.0 : propValue(pm, QStringLiteral("audioLevel"));
                track.stereoBalance =
                    pm.isNull() ? 0.0 : propValue(pm, QStringLiteral("stereoBalance"));
                return track;
            };

            const QDomElement video = editorSeq.firstChildElement(QStringLiteral("Video"));
            for (QDomElement t = video.firstChildElement(QStringLiteral("VideoTrack"));
                 !t.isNull(); t = t.nextSiblingElement(QStringLiteral("VideoTrack"))) {
                seq.videoTracks.push_back(readTrack(t));
            }
            const QDomElement audio = editorSeq.firstChildElement(QStringLiteral("Audio"));
            for (QDomElement t = audio.firstChildElement(QStringLiteral("AudioTrack"));
                 !t.isNull(); t = t.nextSiblingElement(QStringLiteral("AudioTrack"))) {
                seq.audioTracks.push_back(readTrack(t));
            }
            const QDomElement master =
                editorSeq.firstChildElement(QStringLiteral("AudioMaster"))
                    .firstChildElement(QStringLiteral("AudioTrack"));
            if (!master.isNull()) {
                seq.masterTrack = readTrack(master);
            }
        }
    }

    readEmbeddedCompositions(compositionAsset, composition);
    QSet<QString> visited;
    std::function<void(composition::Composition*)> resolveClipPaths = [&](composition::Composition* comp) {
        if (!comp || visited.contains(comp->id().value())) return;
        visited.insert(comp->id().value());
        for (int i = 0; i < comp->layers().size(); ++i) {
            auto& layer = comp->layerRef(i);
            const auto resolveId = [&](core::Identifier& id) {
                const QString path = QDir::cleanPath(mediaPathFromId(id));
                if (resolvedMediaPaths.contains(path))
                    id = core::Identifier(QStringLiteral("media:") + resolvedMediaPaths.value(path));
            };
            resolveId(layer.modelAssetId);
            for (auto& clip : layer.clips) {
                resolveId(clip.mediaId);
                if (clip.nestedComposition) resolveClipPaths(clip.nestedComposition.get());
            }
        }
    };
    resolveClipPaths(composition);
    return core::Result::ok();
}

core::Result VegfxSerializer::saveToFile(const QString& filePath,
                                         const composition::Composition& composition,
                                         const media::MediaManager& media,
                                         const ProjectSaveOptions& options)
{
    const QDir projectDirectory(QFileInfo(filePath).absolutePath());
    const auto storedPath = [&](const QString& path) {
        return options.useRelativePaths && !path.isEmpty()
            ? projectDirectory.relativeFilePath(QFileInfo(path).absoluteFilePath()) : path;
    };
    QDomDocument doc;
    QDomElement root = doc.createElement(QStringLiteral("VegasEffectsProject"));
    root.setAttribute(QStringLiteral("Version"), QStringLiteral("0"));
    root.setAttribute(QStringLiteral("CurrentScreen"), QStringLiteral("2"));
    root.setAttribute(QStringLiteral("AppVersion"), QStringLiteral("1.0.0.0"));
    root.setAttribute(QStringLiteral("AppEdition"), QStringLiteral("5000"));
    doc.appendChild(root);

    QDomElement project = doc.createElement(QStringLiteral("Project"));
    project.setAttribute(QStringLiteral("Version"), QStringLiteral("7"));
    root.appendChild(project);
    if (!options.screenLayout.isEmpty())
        addText(doc, project, QStringLiteral("OpenVegasScreenLayout"),
                QString::fromLatin1(options.screenLayout.toBase64()));

    addText(doc, project, QStringLiteral("ID"),
            QStringLiteral("%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    addText(doc, project, QStringLiteral("Name"),
            composition.name().isEmpty() ? QStringLiteral("Untitled") : composition.name());

    QDomElement settings = doc.createElement(QStringLiteral("ProjectSettings"));
    settings.setAttribute(QStringLiteral("Version"), QStringLiteral("9"));
    addText(doc, settings, QStringLiteral("BPC"), QStringLiteral("1000"));
    addText(doc, settings, QStringLiteral("AntialiasingMode"), QStringLiteral("0"));
    addText(doc, settings, QStringLiteral("ReflectionMapSize"), QStringLiteral("512"));
    addText(doc, settings, QStringLiteral("ModelTextureMaxSize"), QStringLiteral("4096"));
    addText(doc, settings, QStringLiteral("ShadowMapSize"), QStringLiteral("2048"));
    project.appendChild(settings);

    const double fps =
        composition.fpsDenominator() > 0
            ? double(composition.fpsNumerator()) / double(composition.fpsDenominator())
            : 30.0;
    const long long frameCount =
        qMax(1LL, qRound64(composition.durationSeconds() * fps));

    QHash<QString, QString> guidByPath;
    const auto addPath = [&guidByPath](const QString& p) {
        const QString path = QDir::cleanPath(p);
        if (!path.isEmpty() && !guidByPath.contains(path)) {
            guidByPath.insert(path, guidForPath(path));
        }
    };
    for (const media::MediaAsset& asset : media.assets()) {
        addPath(asset.filePath());
    }
    QSet<QString> visitedCompositions;
    std::function<void(const composition::Composition&)> collectPaths =
        [&](const composition::Composition& current) {
            if (visitedCompositions.contains(current.id().value())) return;
            visitedCompositions.insert(current.id().value());
            for (const composition::Layer& layer : current.layers()) {
                for (const composition::Clip& clip : layer.clips) {
                    if (clip.nestedComposition) {
                        collectPaths(*clip.nestedComposition);
                    } else if (!isTextClip(clip)) {
                        addPath(mediaPathFromId(clip.mediaId));
                    }
                }
            }
        };
    collectPaths(composition);

    QDomElement assetList = doc.createElement(QStringLiteral("AssetList"));
    assetList.setAttribute(QStringLiteral("Version"), QStringLiteral("7"));
    QDomElement assets = doc.createElement(QStringLiteral("Assets"));
    assetList.appendChild(assets);
    project.appendChild(assetList);

    QDomElement comp = doc.createElement(QStringLiteral("CompositionAsset"));
    comp.setAttribute(QStringLiteral("Version"), QStringLiteral("19"));
    addText(doc, comp, QStringLiteral("ID"), composition.id().value());
    addText(doc, comp, QStringLiteral("Name"),
            composition.name().isEmpty() ? QStringLiteral("Untitled") : composition.name());
    addText(doc, comp, QStringLiteral("CTI"), QStringLiteral("0"));
    addText(doc, comp, QStringLiteral("In"), QStringLiteral("0"));
    addText(doc, comp, QStringLiteral("Out"), QStringLiteral("%1").arg(frameCount));
    addText(doc, comp, QStringLiteral("IsPrimary"), QStringLiteral("1"));

    QDomElement avs = doc.createElement(QStringLiteral("AudioVideoSettings"));
    addText(doc, avs, QStringLiteral("FrameCount"), QStringLiteral("%1").arg(frameCount));
    addText(doc, avs, QStringLiteral("FrameRate"), QString::number(fps, 'f', 3));
    addText(doc, avs, QStringLiteral("Width"), QStringLiteral("%1").arg(composition.width()));
    addText(doc, avs, QStringLiteral("Height"), QStringLiteral("%1").arg(composition.height()));
    comp.appendChild(avs);

    QDomElement layers = doc.createElement(QStringLiteral("Layers"));
    comp.appendChild(layers);

    const auto frameRound = [fps](double seconds) {
        return qMax(0LL, qRound64(seconds * fps));
    };

    for (const composition::Layer& layer : composition.layers()) {
        // Point, Light and Camera carry no clips at all, so an empty-clip test
        // would drop them from the file entirely.
        const bool clipless = layer.kind == composition::LayerKind::Point
                              || layer.kind == composition::LayerKind::Light
                              || layer.kind == composition::LayerKind::Camera
                              || layer.kind == composition::LayerKind::Model3D;
        if (layer.clips.isEmpty() && !clipless) {
            continue;
        }
        const composition::Clip& clip =
            layer.clips.isEmpty() ? composition::Clip() : layer.clips.first();
        const bool text = layer.kind == composition::LayerKind::Text
                          || (layer.kind == composition::LayerKind::Media && isTextClip(clip));
        QString element = QStringLiteral("AssetLayer");
        switch (layer.kind) {
        case composition::LayerKind::Point:  element = QStringLiteral("PointLayer"); break;
        case composition::LayerKind::Grade:  element = QStringLiteral("GradeLayer"); break;
        case composition::LayerKind::Light:  element = QStringLiteral("LightLayer"); break;
        case composition::LayerKind::Camera: element = QStringLiteral("CameraLayer"); break;
        case composition::LayerKind::Model3D: element = QStringLiteral("Model3DLayer"); break;
        default: if (text) { element = QStringLiteral("TextLayer"); } break;
        }
        QDomElement wrapper = doc.createElement(element);
        wrapper.setAttribute(QStringLiteral("Version"),
                             text ? QStringLiteral("13") : QStringLiteral("17"));
        // Distinguishes Plane from Media, which share AssetLayer.
        wrapper.setAttribute(QStringLiteral("OpenVegasKind"),
                             composition::layerKindToken(layer.kind));
        wrapper.setAttribute(QStringLiteral("OpenVegasMuted"), layer.muted ? 1 : 0);
        wrapper.setAttribute(QStringLiteral("OpenVegasCameraFieldOfView"), QString::number(layer.cameraFieldOfView, 'g', 17));
        if (layer.kind == composition::LayerKind::Plane)
            wrapper.setAttribute(QStringLiteral("OpenVegasPlaneColor"), layer.planeColor.name(QColor::HexArgb));

        // The 3D side of a layer. The reference keeps these as ordinary
        // properties in its own PropertyManager; this port has no writer for
        // that block, so they go on the wrapper under our own prefix - the
        // same escape hatch OpenVegasKind already uses - and a file written
        // here still opens in the reference, minus the depth.
        if (layer.dimension == composition::LayerDimension::ThreeD) {
            const composition::LayerTransform& t = layer.transform;
            wrapper.setAttribute(QStringLiteral("OpenVegasDimension"), QStringLiteral("3D"));
            wrapper.setAttribute(QStringLiteral("OpenVegasPositionZ"), t.positionZ);
            wrapper.setAttribute(QStringLiteral("OpenVegasAnchorZ"), t.anchorPointZ);
            wrapper.setAttribute(QStringLiteral("OpenVegasScaleZ"), t.scaleZPercent);
            wrapper.setAttribute(QStringLiteral("OpenVegasRotationX"), t.rotationXDegrees);
            wrapper.setAttribute(QStringLiteral("OpenVegasRotationY"), t.rotationYDegrees);
            wrapper.setAttribute(QStringLiteral("OpenVegasOrientationX"), t.orientationX);
            wrapper.setAttribute(QStringLiteral("OpenVegasOrientationY"), t.orientationY);
            wrapper.setAttribute(QStringLiteral("OpenVegasOrientationZ"), t.orientationZ);
        }
        // Source in-point and speed: the Slip and Rate Stretch tools change
        // these, and the reference's per-clip schema for them is not recovered
        // here, so they travel under our own prefix like the 3D fields above.
        if (!qFuzzyIsNull(clip.sourceStartSeconds)) {
            wrapper.setAttribute(QStringLiteral("OpenVegasSourceStart"), clip.sourceStartSeconds);
        }
        if (!qFuzzyCompare(clip.speed, 1.0)) {
            wrapper.setAttribute(QStringLiteral("OpenVegasSpeed"), clip.speed);
        }
        if (!qFuzzyIsNull(clip.audioLevel))
            wrapper.setAttribute(QStringLiteral("OpenVegasAudioLevel"), clip.audioLevel);

        if (layer.kind == composition::LayerKind::Model3D && layer.modelAssetId.isValid()) {
            // Path rather than id: ids are minted from the path on import, and
            // a path is what a relink would work from.
            wrapper.setAttribute(QStringLiteral("OpenVegasModelPath"),
                                 storedPath(mediaPathFromId(layer.modelAssetId)));
        }

        // Only a media layer references an asset. Point/Light/Camera have no
        // clip at all and Grade/Plane produce their own picture, so writing an
        // AssetID for them would mint a GUID for an empty path.
        QString assetGuid;
        if (!text && layer.kind == composition::LayerKind::Media
            && !mediaPathFromId(clip.mediaId).isEmpty()) {
            const QString path = mediaPathFromId(clip.mediaId);
            assetGuid = guidByPath.value(QDir::cleanPath(path));
            if (assetGuid.isEmpty()) {
                assetGuid = guidForPath(path);
                guidByPath.insert(QDir::cleanPath(path), assetGuid);
            }
            addText(doc, wrapper, QStringLiteral("AssetID"), assetGuid);
        }

        if (text) {
            composition::TextStyle style;
            for (const composition::Effect& effect : clip.effects) {
                if (effect.pluginId.value() == QLatin1String("text")) {
                    style = composition::textStyleFromParameters(effect.parameterValues);
                    break;
                }
            }
            // Native token text remains readable by the existing reference reader.
            // Full port formatting has an explicit extension, not an invented native schema.
            QDomElement box = doc.createElement(QStringLiteral("TextBox"));
            box.setAttribute(QStringLiteral("Version"), 1);
            addText(doc, box, QStringLiteral("MinX"), QStringLiteral("0"));
            addText(doc, box, QStringLiteral("MinY"), QStringLiteral("0"));
            addText(doc, box, QStringLiteral("MaxX"), QString::number(composition.width()));
            addText(doc, box, QStringLiteral("MaxY"), QString::number(style.fontSize));
            QDomElement tokens = doc.createElement(QStringLiteral("Tokens"));
            for (const QChar character : style.text) {
                QDomElement token = doc.createElement(QStringLiteral("Tk"));
                token.setAttribute(QStringLiteral("Tp"), 0);
                token.setAttribute(QStringLiteral("Ch"), character.unicode());
                tokens.appendChild(token);
            }
            box.appendChild(tokens);
            wrapper.appendChild(box);
            QDomElement savedStyle = doc.createElement(QStringLiteral("OpenVegasTextStyle"));
            savedStyle.setAttribute(QStringLiteral("Version"), 1);
            for (const QString& value : composition::textStyleToParameters(style))
                addText(doc, savedStyle, QStringLiteral("Value"), value);
            wrapper.appendChild(savedStyle);
        }

        appendEffectsExtension(doc, wrapper, clip);
        const bool hasNestedClip = std::any_of(
            layer.clips.cbegin(), layer.clips.cend(), [](const composition::Clip& c) {
                return bool(c.nestedComposition) || c.nestedCompositionId.isValid();
            });
        appendClipsExtension(doc, wrapper, layer.clips, hasNestedClip);
        appendMasksExtension(doc, wrapper, layer.masks);
        appendMotionTracksExtension(doc, wrapper, layer.motionTracks);

        QDomElement named = doc.createElement(QStringLiteral("LayerBase"));
        named.setAttribute(QStringLiteral("Version"), QStringLiteral("2"));
        addText(doc, named, QStringLiteral("ID"), layer.id.value());
        addText(doc, named, QStringLiteral("Name"), layer.name);
        addText(doc, named, QStringLiteral("StartFrame"),
                QStringLiteral("%1").arg(frameRound(clip.startSeconds)));
        addText(doc, named, QStringLiteral("EndFrame"),
                QStringLiteral("%1")
                    .arg(frameRound(clip.startSeconds + clip.durationSeconds)));
        // Mirrors the read above: the default goes back out as 0.
        addText(doc, named, QStringLiteral("BlendMode"),
                layer.blendMode == QStringLiteral("None") ? QStringLiteral("0")
                                                          : layer.blendMode);
        addText(doc, named, QStringLiteral("Visible"), layer.visible ? QStringLiteral("1") : QStringLiteral("0"));
        named.setAttribute(QStringLiteral("OpenVegasLocked"), layer.locked ? QStringLiteral("1") : QStringLiteral("0"));
        named.setAttribute(QStringLiteral("OpenVegasLabelColor"), layer.labelColor.name());
        if (layer.parentLayerId.isValid()) {
            // parentLayerID is an internal reference property in the binary.
            // Keep it as an ignorable extension until its native XML holder is
            // recovered, while preserving the exact stable ID on round-trip.
            wrapper.setAttribute(QStringLiteral("OpenVegasParentLayerID"),
                                 layer.parentLayerId.value());
        }

        // PropertyManager: the layer's whole transform, in the reference's own
        // schema, so what readTransform() reads back is what was written.
        //
        // Only opacity used to be written here, and only its static value: a
        // save dropped every position, scale and rotation the layer had, and
        // every keyframe on any of them. The value graph made that visible -
        // a curve shaped there did not survive reopening the project.
        const composition::LayerTransform& t = layer.transform;
        QDomElement pmEl = doc.createElement(QStringLiteral("PropertyManager"));
        pmEl.setAttribute(QStringLiteral("Version"), QStringLiteral("7"));

        appendScalarProp(doc, pmEl, QStringLiteral("opacity"),
                         qBound(0.0, layer.opacity * 100.0, 100.0), t.opacityCurve, fps);
        // Anchor Point carries no curve in this model, so it is written static.
        appendPointProp(doc, pmEl, QStringLiteral("anchorPoint"), QStringLiteral("p3"),
                        t.anchorPoint, composition::KeyFrameList(), composition::KeyFrameList(),
                        fps);
        appendPointProp(doc, pmEl, QStringLiteral("position"), QStringLiteral("p3"), t.position,
                        t.positionXCurve, t.positionYCurve, fps);
        appendPointProp(doc, pmEl, QStringLiteral("scale"), QStringLiteral("sc"), t.scalePercent,
                        t.scaleXCurve, t.scaleYCurve, fps);
        appendScalarProp(doc, pmEl, QStringLiteral("rotationZ"), t.rotationDegrees,
                         t.rotationCurve, fps);

        named.appendChild(pmEl);

        wrapper.appendChild(named);
        layers.appendChild(wrapper);
    }

    appendEmbeddedCompositions(doc, comp, composition);
    assets.appendChild(comp);

    for (auto it = guidByPath.constBegin(); it != guidByPath.constEnd(); ++it) {
        const QString path = it.key();
        const QString guid = it.value();
        const media::MediaAsset recorded = media.assetByFilePath(path);
        // Image -> ImageAsset; Video/Audio -> MediaAsset (matches reference schema).
        const bool mediaAsset = recorded.kind() != media::MediaKind::Image;
        const QString tagName =
            mediaAsset ? QStringLiteral("MediaAsset") : QStringLiteral("ImageAsset");
        QDomElement m = doc.createElement(tagName);
        m.setAttribute(QStringLiteral("Version"), mediaAsset ? QStringLiteral("10")
                                                             : QStringLiteral("3"));
        const QString name = recorded.isValid() ? recorded.fileName()
                                                : QFileInfo(path).fileName();
        addText(doc, m, QStringLiteral("ID"), guid);
        addText(doc, m, QStringLiteral("Name"), name);
        addText(doc, m, QStringLiteral("ParentFolderID"),
                QStringLiteral("00000000-0000-0000-0000-000000000000"));
        addText(doc, m, QStringLiteral("IsHidden"), QStringLiteral("0"));
        if (mediaAsset) {
            addText(doc, m, QStringLiteral("MediaPathType"), QStringLiteral("0"));
        }
        addText(doc, m, QStringLiteral("Filename"),
                storedPath(recorded.isValid() ? recorded.filePath() : path));
        if (mediaAsset) {
            addText(doc, m, QStringLiteral("IsImageSequence"), QStringLiteral("0"));
            addText(doc, m, QStringLiteral("HWAccelerate"), QStringLiteral("1"));
            addText(doc, m, QStringLiteral("MergedAudioFilename"), QString());
            addText(doc, m, QStringLiteral("MergedAudioOffset"), QStringLiteral("0"));
            addText(doc, m, QStringLiteral("VideoIdx"), QStringLiteral("0"));
            addText(doc, m, QStringLiteral("AudioIdx"),
                    recorded.kind() == media::MediaKind::Audio ? QStringLiteral("0")
                                                               : QStringLiteral("-1"));
            addText(doc, m, QStringLiteral("OverrideFrameRate"), QStringLiteral("0"));
            addText(doc, m, QStringLiteral("FrameRate"),
                    QString::number(int(fps + 0.5)));
            addText(doc, m, QStringLiteral("OverridePAR"), QStringLiteral("0"));
            addText(doc, m, QStringLiteral("PAR"), QStringLiteral("0"));
            addText(doc, m, QStringLiteral("OverrideAlpha"), QStringLiteral("0"));
            addText(doc, m, QStringLiteral("AlphaMode"), QStringLiteral("0"));
            addText(doc, m, QStringLiteral("OverrideColorLevels"), QStringLiteral("0"));
            addText(doc, m, QStringLiteral("ColorLevels"), QStringLiteral("0"));
            addText(doc, m, QStringLiteral("OverrideColorSpace"), QStringLiteral("0"));
            addText(doc, m, QStringLiteral("ColorSpace"), QStringLiteral("0"));
            // Trimmer in/out points (frames); mirrors MediaAsset::SetTrimmerInPoint.
            addText(doc, m, QStringLiteral("InPoint"),
                    QStringLiteral("%1").arg(recorded.trimInPoint()));
            addText(doc, m, QStringLiteral("OutPoint"),
                    QStringLiteral("%1").arg(recorded.trimOutPoint()));
        } else {
            addText(doc, m, QStringLiteral("PAR"), QStringLiteral("0"));
            addText(doc, m, QStringLiteral("OverrideAlpha"), QStringLiteral("0"));
            addText(doc, m, QStringLiteral("AlphaMode"), QStringLiteral("0"));
        }
        QDomElement instances = doc.createElement(QStringLiteral("Instances"));
        m.appendChild(instances);
        assets.appendChild(m);
    }

    // ---- EditorSequence ----
    {
        const composition::EditorSequence& seq = composition.editorSequence();
        const long long seqFrameCount =
            seq.frameCount > 0 ? seq.frameCount : frameCount;
        const double seqFps = seq.fps > 0.0 ? seq.fps : fps;

        QDomElement editorSeq = doc.createElement(QStringLiteral("EditorSequence"));
        editorSeq.setAttribute(QStringLiteral("Version"), QStringLiteral("5"));
        addText(doc, editorSeq, QStringLiteral("ID"),
                QStringLiteral("%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        addText(doc, editorSeq, QStringLiteral("Name"), seq.name);
        addText(doc, editorSeq, QStringLiteral("CTI"), QStringLiteral("%1").arg(seq.cti));
        addText(doc, editorSeq, QStringLiteral("InPoint"), QStringLiteral("%1").arg(seq.inPoint));
        addText(doc, editorSeq, QStringLiteral("OutPoint"),
                QStringLiteral("%1").arg(seq.outPoint > 0 ? seq.outPoint : seqFrameCount));
        addText(doc, editorSeq, QStringLiteral("TimelineZoom"),
                QStringLiteral("%1").arg(seq.timelineZoom));
        addText(doc, editorSeq, QStringLiteral("TimelineTimeFormat"),
                QStringLiteral("%1").arg(seq.timelineTimeFormat));
        addText(doc, editorSeq, QStringLiteral("TimelineSnapMode"),
                QStringLiteral("%1").arg(seq.timelineSnapMode));
        addText(doc, editorSeq, QStringLiteral("TimelineScrollSyncMode"),
                QStringLiteral("%1").arg(seq.timelineScrollSyncMode));
        addText(doc, editorSeq, QStringLiteral("TimelineValueGraph"),
                seq.timelineValueGraph ? QStringLiteral("true") : QStringLiteral("false"));
        addText(doc, editorSeq, QStringLiteral("TimelineGraphAutoZoom"),
                seq.timelineGraphAutoZoom ? QStringLiteral("true") : QStringLiteral("false"));
        addText(doc, editorSeq, QStringLiteral("VideoPreviewSize"),
                QStringLiteral("%1").arg(seq.videoPreviewSize));
        addText(doc, editorSeq, QStringLiteral("AudioPreviewSize"),
                QStringLiteral("%1").arg(seq.audioPreviewSize));
        addText(doc, editorSeq, QStringLiteral("PreviewMode"),
                QStringLiteral("%1").arg(seq.previewMode));

        QDomElement seqAvs = doc.createElement(QStringLiteral("AudioVideoSettings"));
        seqAvs.setAttribute(QStringLiteral("Version"), QStringLiteral("1"));
        addText(doc, seqAvs, QStringLiteral("FrameCount"),
                QStringLiteral("%1").arg(seqFrameCount));
        addText(doc, seqAvs, QStringLiteral("AudioSampleRate"),
                QStringLiteral("%1").arg(seq.audioSampleRate));
        addText(doc, seqAvs, QStringLiteral("Width"),
                QStringLiteral("%1").arg(seq.width > 0 ? seq.width : composition.width()));
        addText(doc, seqAvs, QStringLiteral("Height"),
                QStringLiteral("%1").arg(seq.height > 0 ? seq.height : composition.height()));
        addText(doc, seqAvs, QStringLiteral("PAR"), QStringLiteral("0"));
        addText(doc, seqAvs, QStringLiteral("PARCustom"), QStringLiteral("0"));
        addText(doc, seqAvs, QStringLiteral("FrameRate"), QString::number(seqFps, 'f', 3));
        editorSeq.appendChild(seqAvs);

        QDomElement renderSettings = doc.createElement(QStringLiteral("RenderSettings"));
        renderSettings.setAttribute(QStringLiteral("Version"), QStringLiteral("1"));
        addText(doc, renderSettings, QStringLiteral("MotionBlurEnabled"),
                seq.motionBlurEnabled ? QStringLiteral("1") : QStringLiteral("0"));
        addText(doc, renderSettings, QStringLiteral("ShutterAngle"),
                QString::number(seq.shutterAngle, 'f', 3));
        addText(doc, renderSettings, QStringLiteral("ShutterPhase"),
                QString::number(seq.shutterPhase, 'f', 3));
        addText(doc, renderSettings, QStringLiteral("MaxNumOfSamples"),
                QStringLiteral("%1").arg(seq.maxNumOfSamples));
        addText(doc, renderSettings, QStringLiteral("UseAdaptiveSamples"),
                seq.useAdaptiveSamples ? QStringLiteral("1") : QStringLiteral("0"));
        editorSeq.appendChild(renderSettings);

        const auto writeTrack = [&doc](const composition::SequenceTrack& t, int trkVersion) {
            QDomElement el = doc.createElement(QStringLiteral("AudioTrack"));
            el.setAttribute(QStringLiteral("Version"), QStringLiteral("%1").arg(trkVersion));
            addText(doc, el, QStringLiteral("ID"),
                    t.id.isEmpty()
                        ? QStringLiteral("%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces))
                        : t.id);
            addText(doc, el, QStringLiteral("Name"), t.name);
            addText(doc, el, QStringLiteral("Muted"), t.muted ? QStringLiteral("1") : QStringLiteral("0"));
            addText(doc, el, QStringLiteral("Solo"), t.solo ? QStringLiteral("1") : QStringLiteral("0"));
            addText(doc, el, QStringLiteral("Locked"), t.locked ? QStringLiteral("1") : QStringLiteral("0"));
            QDomElement pmEl = doc.createElement(QStringLiteral("PropertyManager"));
            pmEl.setAttribute(QStringLiteral("Version"), QStringLiteral("7"));
            QDomElement p1 = doc.createElement(QStringLiteral("Prop"));
            addText(doc, p1, QStringLiteral("Name"), QStringLiteral("audioLevel"));
            QDomElement d1 = doc.createElement(QStringLiteral("Default"));
            QDomElement fl1 = doc.createElement(QStringLiteral("fl"));
            fl1.appendChild(doc.createTextNode(QString::number(t.audioLevel, 'f', 3)));
            d1.appendChild(fl1);
            p1.appendChild(d1);
            pmEl.appendChild(p1);
            QDomElement p2 = doc.createElement(QStringLiteral("Prop"));
            addText(doc, p2, QStringLiteral("Name"), QStringLiteral("stereoBalance"));
            QDomElement d2 = doc.createElement(QStringLiteral("Default"));
            QDomElement fl2 = doc.createElement(QStringLiteral("fl"));
            fl2.appendChild(doc.createTextNode(QString::number(t.stereoBalance, 'f', 3)));
            d2.appendChild(fl2);
            p2.appendChild(d2);
            pmEl.appendChild(p2);
            el.appendChild(pmEl);
            QDomElement objects = doc.createElement(QStringLiteral("Objects"));
            el.appendChild(objects);
            return el;
        };

        QDomElement video = doc.createElement(QStringLiteral("Video"));
        for (const composition::SequenceTrack& t : seq.videoTracks) {
            QDomElement vt = doc.createElement(QStringLiteral("VideoTrack"));
            vt.setAttribute(QStringLiteral("Version"), QStringLiteral("3"));
            addText(doc, vt, QStringLiteral("ID"),
                    t.id.isEmpty()
                        ? QStringLiteral("%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces))
                        : t.id);
            addText(doc, vt, QStringLiteral("Name"), t.name);
            addText(doc, vt, QStringLiteral("Visible"), t.visible ? QStringLiteral("1") : QStringLiteral("0"));
            addText(doc, vt, QStringLiteral("Locked"), t.locked ? QStringLiteral("1") : QStringLiteral("0"));
            QDomElement objects = doc.createElement(QStringLiteral("Objects"));
            vt.appendChild(objects);
            video.appendChild(vt);
        }
        editorSeq.appendChild(video);

        QDomElement audio = doc.createElement(QStringLiteral("Audio"));
        for (const composition::SequenceTrack& t : seq.audioTracks) {
            audio.appendChild(writeTrack(t, 3));
        }
        editorSeq.appendChild(audio);

        QDomElement audioMaster = doc.createElement(QStringLiteral("AudioMaster"));
        composition::SequenceTrack master = seq.masterTrack;
        if (master.name.isEmpty()) {
            master.name = QStringLiteral("Master");
        }
        audioMaster.appendChild(writeTrack(master, 3));
        editorSeq.appendChild(audioMaster);

        project.appendChild(editorSeq);
    }

    if (options.useRelativePaths) {
        std::function<void(QDomElement)> relativiseIds = [&](QDomElement element) {
            for (const QString& attribute : {QStringLiteral("MediaID"), QStringLiteral("ModelAssetID")}) {
                const QString id = element.attribute(attribute);
                const QString path = QDir::cleanPath(mediaPathFromId(core::Identifier(id)));
                if (id.startsWith(QStringLiteral("media:")) && !path.isEmpty() && guidByPath.contains(path))
                    element.setAttribute(attribute, QStringLiteral("media:") + storedPath(path));
            }
            for (QDomElement child = element.firstChildElement(); !child.isNull(); child = child.nextSiblingElement())
                relativiseIds(child);
        };
        relativiseIds(root);
    }
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return core::Result::fail(core::ResultStatus::OperationFailed,
                                  QStringLiteral("Cannot open file for writing: %1").arg(filePath));
    }
    QTextStream out(&file);
    out << doc.toString(1);
    file.close();
    return core::Result::ok();
}

} // namespace project
} // namespace openvegas
