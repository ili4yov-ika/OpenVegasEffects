#include <cmath>
#include "project/VegfxSerializer.h"

#include <QColor>
#include <QDomDocument>
#include <QDomElement>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetricsF>

#include "model3d/ModelImportSettings.h"
#include <QRegularExpression>

#include "composition/KeyFrame.h"
#include "composition/TextStyle.h"
#include "media/PsdDocument.h"
#include "project/NativeEffectCatalog.h"
#include "project/VegfxMerge.h"
#include <QTextStream>
#include <optional>
#include <QFile>
#include <QSaveFile>
#include <QFileInfo>
#include <QDir>
#include <QDirIterator>
#include <QHash>
#include <QSet>
#include <QCryptographicHash>
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

QDomElement addText(QDomDocument& doc, QDomElement& parent, const QString& tag, const QString& text);

QColor colorFromAttributes(const QDomElement& element, const QColor& fallback)
{
    if (element.isNull()) return fallback;
    return QColor::fromRgbF(float(element.attribute(QStringLiteral("R")).toDouble()),
                            float(element.attribute(QStringLiteral("G")).toDouble()),
                            float(element.attribute(QStringLiteral("B")).toDouble()),
                            float(element.attribute(QStringLiteral("A"), QStringLiteral("1")).toDouble()));
}

void setColorAttributes(QDomElement& element, const QColor& color)
{
    element.setAttribute(QStringLiteral("A"), QString::number(color.alphaF(), 'g', 6));
    element.setAttribute(QStringLiteral("R"), QString::number(color.redF(), 'g', 6));
    element.setAttribute(QStringLiteral("G"), QString::number(color.greenF(), 'g', 6));
    element.setAttribute(QStringLiteral("B"), QString::number(color.blueF(), 'g', 6));
}

// The reference's <TextBox>: tokens (Tp 1 opens a paragraph and carries its
// alignment, indents and gaps; Tp 0 is a character in format Ft) and the
// <Formats> they use. The port keeps one style per layer, so the format of
// the first character speaks for the whole layer.
composition::TextStyle textStyleFromTextBox(const QDomElement& wrapper, int fallbackSize)
{
    composition::TextStyle style;
    style.fontSize = fallbackSize;
    const QDomElement box = wrapper.firstChildElement(QStringLiteral("TextBox"));
    if (box.isNull()) return style;
    const auto number = [&box](const char* tag, double fallback) {
        bool ok = false;
        const double value = elemText(box, QString::fromLatin1(tag)).toDouble(&ok);
        return ok ? value : fallback;
    };
    style.textMode = number("Mode", 0) != 0 ? composition::TextStyle::TextMode::Paragraph
                                             : composition::TextStyle::TextMode::Point;
    // The extents are layer space, Y up. A paragraph box keeps where it is; a
    // point-text box is only the text's own extent around the origin (Flux
    // recomputes it, FUN_180523fc0), so it says nothing to keep.
    style.paragraphSize = QSizeF(number("MaxX", 0) - number("MinX", 0),
                                 number("MaxY", 0) - number("MinY", 0));
    if (style.paragraphSize.width() <= 0 || style.paragraphSize.height() <= 0)
        style.paragraphSize = QSizeF(640.0, 360.0);
    else if (style.textMode == composition::TextStyle::TextMode::Paragraph)
        style.paragraphOffset = QPointF((number("MinX", 0) + number("MaxX", 0)) / 2.0,
                                        (number("MinY", 0) + number("MaxY", 0)) / 2.0);
    style.alignV = composition::TextStyle::AlignV(qBound(0, int(number("VerticalAlignment", 1)), 2));
    style.indentTop = number("TopIndentation", 0);
    style.indentBottom = number("BottomIndentation", 0);
    style.backgroundEnabled = number("BackgroundEnable", 0) != 0;
    style.backgroundOpacity = number("BackgroundOpacity", 0);
    style.backgroundExpansionX = number("BackgroundExpansionX", 0);
    style.backgroundExpansionY = number("BackgroundExpansionY", 0);
    style.expansionLinked = number("BackgroundExpandLink", 0) == 0;
    style.backgroundRoundness = number("BackgroundRoundness", 0);
    style.backgroundColor = colorFromAttributes(box.firstChildElement(QStringLiteral("BackgroundColor")),
                                                style.backgroundColor);

    QString formatId;
    bool paragraphRead = false;
    const QDomElement tokens = box.firstChildElement(QStringLiteral("Tokens"));
    for (QDomElement tk = tokens.firstChildElement(QStringLiteral("Tk")); !tk.isNull();
         tk = tk.nextSiblingElement(QStringLiteral("Tk"))) {
        if (tk.attribute(QStringLiteral("Tp")) == QLatin1String("1")) {
            if (!paragraphRead) {
                paragraphRead = true;
                style.alignH = composition::TextStyle::AlignH(
                    qBound(0, tk.attribute(QStringLiteral("Jf")).toInt(), 6));
                style.indentLeft = tk.attribute(QStringLiteral("Li")).toDouble();
                style.indentRight = tk.attribute(QStringLiteral("Ri")).toDouble();
                style.indentFirstLine = tk.attribute(QStringLiteral("Fi")).toDouble();
                style.spaceBeforeParagraph = tk.attribute(QStringLiteral("Sb")).toDouble();
                style.spaceAfterParagraph = tk.attribute(QStringLiteral("Sa")).toDouble();
            } else {
                style.text.append(QLatin1Char('\n'));
            }
            continue;
        }
        bool ok = false;
        const uint code = tk.attribute(QStringLiteral("Ch")).toUInt(&ok);
        if (!ok || code == 0) continue;
        if (formatId.isEmpty()) formatId = tk.attribute(QStringLiteral("Ft"));
        style.text.append(QChar(static_cast<char16_t>(code)));
    }

    const QDomElement formats = box.firstChildElement(QStringLiteral("Formats"));
    QDomElement format = formats.firstChildElement(QStringLiteral("Format"));
    for (QDomElement f = format; !f.isNull(); f = f.nextSiblingElement(QStringLiteral("Format"))) {
        if (f.attribute(QStringLiteral("ID")) == formatId) { format = f; break; }
    }
    if (format.isNull()) return style;
    const auto attribute = [&format](const char* name, double fallback) {
        bool ok = false;
        const double value = format.attribute(QString::fromLatin1(name)).toDouble(&ok);
        return ok ? value : fallback;
    };
    style.fontSize = qMax(1, int(std::lround(attribute("Size", fallbackSize))));
    style.fontFamily = elemText(format, QStringLiteral("Family"));
    // The reference names the face in full ("Ubuntu Medium"); the port keeps
    // the style part Qt lists for the family ("Medium").
    QString face = elemText(format, QStringLiteral("Style")).trimmed();
    if (!style.fontFamily.isEmpty() && face.startsWith(style.fontFamily, Qt::CaseInsensitive))
        face = face.mid(style.fontFamily.size()).trimmed();
    style.fontStyle = face.isEmpty() ? QStringLiteral("Regular") : face;
    style.fontColor = colorFromAttributes(format.firstChildElement(QStringLiteral("FillColor")),
                                          style.fontColor);
    style.underline = attribute("Underline", 0) != 0;
    style.strikethrough = attribute("Strikethrough", 0) != 0;
    style.horizontalScale = attribute("HorizontalScale", 1) * 100.0;
    style.verticalScale = attribute("VerticalScale", 1) * 100.0;
    style.lineSpacing = attribute("Leading", 1) * 100.0;
    style.letterSpacing = attribute("Tracking", 0);
    style.baselineShift = attribute("BaselineShift", 0);
    style.caps = composition::TextStyle::Caps(qBound(0, int(attribute("Capitalization", 0)), 4));
    style.script = composition::TextStyle::Script(qBound(0, int(attribute("Script", 0)), 2));
    return style;
}

// The same box written back: one format for the layer, a paragraph token per
// line. The extents follow the reference's layer space - centred on the
// layer, Y up - from the font's own metrics for point text.
QDomElement textBoxElement(QDomDocument& doc, const composition::TextStyle& style,
                           const core::Identifier& layerId)
{
    QDomElement box = doc.createElement(QStringLiteral("TextBox"));
    box.setAttribute(QStringLiteral("Version"), 1);
    const QString boxId = QUuid::createUuidV5(QUuid(layerId.value()), QStringLiteral("TextBox"))
                              .toString(QUuid::WithoutBraces);
    const QString formatId = QUuid::createUuidV5(QUuid(layerId.value()), QStringLiteral("Format"))
                                 .toString(QUuid::WithoutBraces);
    addText(doc, box, QStringLiteral("ID"), boxId);

    const QStringList lines = style.text.split(QLatin1Char('\n'));
    double minX = 0, maxX = 0, minY = 0, maxY = 0;
    if (style.textMode == composition::TextStyle::TextMode::Paragraph) {
        minX = style.paragraphOffset.x() - style.paragraphSize.width() / 2.0;
        maxX = style.paragraphOffset.x() + style.paragraphSize.width() / 2.0;
        minY = style.paragraphOffset.y() - style.paragraphSize.height() / 2.0;
        maxY = style.paragraphOffset.y() + style.paragraphSize.height() / 2.0;
    } else {
        // Point text hangs off the origin: first baseline on it, lines start
        // at it, centre on it or end on it by their alignment.
        QFont font = QFontDatabase::font(style.fontFamily, style.fontStyle, 12);
        font.setPixelSize(qMax(1, style.fontSize));
        const QFontMetricsF metrics(font);
        double width = 0;
        for (QString line : lines) {
            while (!line.isEmpty() && line.back().isSpace()) line.chop(1);   // not aligned on
            width = qMax(width, metrics.horizontalAdvance(line));
        }
        width *= style.horizontalScale / 100.0;
        using AlignH = composition::TextStyle::AlignH;
        const double anchor = style.alignH == AlignH::Center || style.alignH == AlignH::CenterJustify
                                  ? 0.5
                              : style.alignH == AlignH::Right || style.alignH == AlignH::RightJustify
                                  ? 1.0 : 0.0;
        minX = -width * anchor;
        maxX = minX + width;
        maxY = metrics.ascent();
        minY = -(metrics.descent() + (lines.size() - 1) * metrics.lineSpacing()
                                         * style.lineSpacing / 100.0);
    }
    const auto real = [](double value) { return QString::number(value, 'g', 6); };
    addText(doc, box, QStringLiteral("MinX"), real(minX));
    addText(doc, box, QStringLiteral("MaxX"), real(maxX));
    addText(doc, box, QStringLiteral("MinY"), real(minY));
    addText(doc, box, QStringLiteral("MaxY"), real(maxY));
    addText(doc, box, QStringLiteral("Mode"),
            style.textMode == composition::TextStyle::TextMode::Paragraph ? QStringLiteral("1")
                                                                           : QStringLiteral("0"));
    addText(doc, box, QStringLiteral("VerticalAlignment"), QString::number(int(style.alignV)));
    addText(doc, box, QStringLiteral("TopIndentation"), real(style.indentTop));
    addText(doc, box, QStringLiteral("BottomIndentation"), real(style.indentBottom));
    addText(doc, box, QStringLiteral("BackgroundEnable"),
            style.backgroundEnabled ? QStringLiteral("1") : QStringLiteral("0"));
    addText(doc, box, QStringLiteral("BackgroundOpacity"), real(style.backgroundOpacity));
    addText(doc, box, QStringLiteral("BackgroundExpansionX"), real(style.backgroundExpansionX));
    addText(doc, box, QStringLiteral("BackgroundExpansionY"), real(style.backgroundExpansionY));
    addText(doc, box, QStringLiteral("BackgroundExpandLink"),
            style.expansionLinked ? QStringLiteral("0") : QStringLiteral("1"));
    addText(doc, box, QStringLiteral("BackgroundRoundness"), real(style.backgroundRoundness));
    QDomElement background = doc.createElement(QStringLiteral("BackgroundColor"));
    setColorAttributes(background, style.backgroundColor);
    box.appendChild(background);

    QDomElement tokens = doc.createElement(QStringLiteral("Tokens"));
    for (const QString& line : lines) {
        QDomElement paragraph = doc.createElement(QStringLiteral("Tk"));
        paragraph.setAttribute(QStringLiteral("Fi"), real(style.indentFirstLine));
        paragraph.setAttribute(QStringLiteral("Tp"), 1);
        paragraph.setAttribute(QStringLiteral("Jf"), int(style.alignH));
        paragraph.setAttribute(QStringLiteral("Sb"), real(style.spaceBeforeParagraph));
        paragraph.setAttribute(QStringLiteral("Li"), real(style.indentLeft));
        paragraph.setAttribute(QStringLiteral("V"), 2);
        paragraph.setAttribute(QStringLiteral("Sa"), real(style.spaceAfterParagraph));
        paragraph.setAttribute(QStringLiteral("Ri"), real(style.indentRight));
        tokens.appendChild(paragraph);
        for (const QChar character : line) {
            QDomElement token = doc.createElement(QStringLiteral("Tk"));
            token.setAttribute(QStringLiteral("Tp"), 0);
            token.setAttribute(QStringLiteral("Ft"), formatId);
            token.setAttribute(QStringLiteral("V"), 2);
            token.setAttribute(QStringLiteral("Ch"), character.unicode());
            tokens.appendChild(token);
        }
    }
    box.appendChild(tokens);

    QDomElement formats = doc.createElement(QStringLiteral("Formats"));
    QDomElement format = doc.createElement(QStringLiteral("Format"));
    format.setAttribute(QStringLiteral("Size"), style.fontSize);
    format.setAttribute(QStringLiteral("Underline"), style.underline ? 1 : 0);
    format.setAttribute(QStringLiteral("HorizontalScale"), real(style.horizontalScale / 100.0));
    format.setAttribute(QStringLiteral("Script"), int(style.script));
    format.setAttribute(QStringLiteral("BaselineShift"), real(style.baselineShift));
    format.setAttribute(QStringLiteral("Version"), 4);
    format.setAttribute(QStringLiteral("Tracking"), real(style.letterSpacing));
    format.setAttribute(QStringLiteral("Strikethrough"), style.strikethrough ? 1 : 0);
    format.setAttribute(QStringLiteral("Capitalization"), int(style.caps));
    format.setAttribute(QStringLiteral("ID"), formatId);
    format.setAttribute(QStringLiteral("Leading"), real(style.lineSpacing / 100.0));
    format.setAttribute(QStringLiteral("StrokeLocation"), int(style.strokeOrder));
    format.setAttribute(QStringLiteral("VerticalScale"), real(style.verticalScale / 100.0));
    const QString family = style.fontFamily.isEmpty() ? QFont().family() : style.fontFamily;
    addText(doc, format, QStringLiteral("Family"), family);
    addText(doc, format, QStringLiteral("Style"),
            style.fontStyle.compare(QLatin1String("Regular"), Qt::CaseInsensitive) == 0
                ? family : family + QLatin1Char(' ') + style.fontStyle);
    QDomElement fill = doc.createElement(QStringLiteral("FillColor"));
    setColorAttributes(fill, style.fontColor);
    format.appendChild(fill);
    formats.appendChild(format);
    box.appendChild(formats);
    return box;
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

// One axis ("X", "Y" or "Z") of a point property's constant value.
double propAxis(const QDomElement& propertyManager, const QString& name, const QString& axis,
                double fallback)
{
    const QDomElement holder = propValueHolder(propElement(propertyManager, name));
    for (const char* tag : {"p3", "sc", "or"}) {
        const QDomElement v = holder.firstChildElement(QString::fromLatin1(tag));
        if (!v.isNull() && v.hasAttribute(axis)) {
            bool ok = false;
            const double value = v.attribute(axis).toDouble(&ok);
            return ok ? value : fallback;
        }
    }
    return fallback;
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

// A <b>, <i> or <cl> property's constant value (<cl> holds A R G B, 0..1).
bool propBool(const QDomElement& propertyManager, const QString& name, bool fallback)
{
    const QDomElement b = propValueHolder(propElement(propertyManager, name)).firstChildElement(QStringLiteral("b"));
    if (b.isNull()) return fallback;
    const QString text = b.text().trimmed();
    return text == QLatin1String("1") || text.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;
}

int propInt(const QDomElement& propertyManager, const QString& name, int fallback)
{
    const QDomElement i = propValueHolder(propElement(propertyManager, name)).firstChildElement(QStringLiteral("i"));
    bool ok = false;
    const int value = i.text().trimmed().toInt(&ok);
    return ok ? value : fallback;
}

QColor propColor(const QDomElement& propertyManager, const QString& name, const QColor& fallback)
{
    const QDomElement cl = propValueHolder(propElement(propertyManager, name)).firstChildElement(QStringLiteral("cl"));
    if (cl.isNull()) return fallback;
    const auto channel = [&](const char* key, double otherwise) {
        bool ok = false;
        const double value = cl.attribute(QString::fromLatin1(key)).toDouble(&ok);
        return ok ? qBound(0.0, value, 1.0) : otherwise;
    };
    return QColor::fromRgbF(float(channel("R", fallback.redF())), float(channel("G", fallback.greenF())),
                            float(channel("B", fallback.blueF())), float(channel("A", fallback.alphaF())));
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
// One axis of an animated point property (<p3>/<sc>/<or> inside <Vl>), one
// channel of a colour (<cl>, "R", "G", "B" or "A"), or the scalar itself when
// `attribute` is empty (<fl>).
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
            for (const char* tag : {"p3", "sc", "or", "cl"}) {
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

// <Animation> of a colour property: one key per <Key>, its whole <cl> as a
// QColor, timed and eased as the red channel reads.
composition::KeyFrameList propColorAnimation(const QDomElement& prop, double fps)
{
    const composition::KeyFrameList red = propAnimation(prop, fps, QStringLiteral("R"));
    const composition::KeyFrameList green = propAnimation(prop, fps, QStringLiteral("G"));
    const composition::KeyFrameList blue = propAnimation(prop, fps, QStringLiteral("B"));
    const composition::KeyFrameList alpha = propAnimation(prop, fps, QStringLiteral("A"));
    composition::KeyFrameList curve;
    for (composition::KeyFrame key : red.all()) {
        const auto channel = [&](const composition::KeyFrameList& list, double fallback) {
            const composition::KeyFrame* at = list.at(key.frame);
            return float(qBound(0.0, at ? at->value.toDouble() : fallback, 1.0));
        };
        key.value = QVariant(QColor::fromRgbF(channel(red, 0.0), channel(green, 0.0), channel(blue, 0.0),
                                              channel(alpha, 1.0)));
        curve.add(key);
    }
    return curve;
}

// A model's import settings. The reference keeps them on its Model3DAsset
// (Model3DImportOptions::Serialize, Project.dll 1802a5970) as ImportFlipZY,
// ImportCenterAnchor, ImportAutoNormalize, ImportModelScale,
// ImportAutoSmoothingAngle, ImportFlipNormals, ImportUnifyNormals and
// ImportFlipUVs, with the unit as ImportModelUnits (LinearType 3001 mm ...
// 3010 pixel) and the normal method as ImportNormalGen (NormalGenerationType
// 0..3); this port writes no Model3DAsset, so the same elements sit under its
// own OpenVegasModelImport on the layer. Projects written before the codes
// were known carry the unit and method by name (OpenVegasUnit,
// OpenVegasNormalMethod); those are still read, "FromFile" as a pixel.
const char* const kLegacyUnitNames[] = {"Millimeter", "Centimeter", "Decimeter", "Meter", "Kilometer", "Inch"};
const char* const kLegacyNormalMethodNames[] = {"FromFile", "FromFileAutoSmoothing", "GenerateFaceted",
                                                "GenerateFacetedAutoSmoothing"};

QDomElement modelImportSettingsElement(QDomDocument& doc, const model3d::ImportSettings& settings)
{
    QDomElement element = doc.createElement(QStringLiteral("OpenVegasModelImport"));
    const auto flag = [&](const char* name, bool value) {
        addText(doc, element, QString::fromLatin1(name), value ? QStringLiteral("1") : QStringLiteral("0"));
    };
    const auto number = [&](const char* name, double value) {
        addText(doc, element, QString::fromLatin1(name), QString::number(value, 'g', 10));
    };
    flag("ImportFlipZY", settings.flipYZAxis);
    flag("ImportCenterAnchor", settings.centerAnchorPoint);
    flag("ImportAutoNormalize", settings.autoNormalize);
    number("ImportModelScale", settings.singleUnitScale);
    number("ImportAutoSmoothingAngle", settings.autoSmoothingAngle);
    flag("ImportFlipNormals", settings.flipNormals);
    flag("ImportUnifyNormals", settings.unifyNormals);
    flag("ImportFlipUVs", settings.flipUVCoordinates);
    addText(doc, element, QStringLiteral("ImportModelUnits"), QString::number(model3d::unitCode(settings.unit)));
    addText(doc, element, QStringLiteral("ImportNormalGen"), QString::number(int(settings.normalMethod)));
    return element;
}

// What modelImportSettingsElement wrote; the defaults when it is absent.
model3d::ImportSettings readModelImportSettings(const QDomElement& element)
{
    model3d::ImportSettings settings;
    if (element.isNull()) return settings;
    const auto flag = [&](const char* name, bool fallback) {
        const QDomElement child = element.firstChildElement(QString::fromLatin1(name));
        return child.isNull() ? fallback : child.text().trimmed() != QLatin1String("0");
    };
    const auto number = [&](const char* name, double fallback) {
        bool ok = false;
        const double value = element.firstChildElement(QString::fromLatin1(name)).text().toDouble(&ok);
        return ok ? value : fallback;
    };
    settings.flipYZAxis = flag("ImportFlipZY", settings.flipYZAxis);
    settings.centerAnchorPoint = flag("ImportCenterAnchor", settings.centerAnchorPoint);
    settings.autoNormalize = flag("ImportAutoNormalize", settings.autoNormalize);
    settings.singleUnitScale = number("ImportModelScale", settings.singleUnitScale);
    settings.autoSmoothingAngle = number("ImportAutoSmoothingAngle", settings.autoSmoothingAngle);
    settings.flipNormals = flag("ImportFlipNormals", settings.flipNormals);
    settings.unifyNormals = flag("ImportUnifyNormals", settings.unifyNormals);
    settings.flipUVCoordinates = flag("ImportFlipUVs", settings.flipUVCoordinates);
    using Unit = model3d::ImportSettings::Unit;
    using NormalMethod = model3d::ImportSettings::NormalMethod;
    bool ok = false;
    const int code = element.firstChildElement(QStringLiteral("ImportModelUnits")).text().trimmed().toInt(&ok);
    if (!ok || !model3d::unitFromCode(code, &settings.unit)) {
        const QString unit = element.firstChildElement(QStringLiteral("OpenVegasUnit")).text().trimmed();
        if (unit == QLatin1String("FromFile") || unit == QLatin1String("Pixel")) settings.unit = Unit::Pixel;
        for (int i = 0; i < 6; ++i)
            if (unit == QLatin1String(kLegacyUnitNames[i])) settings.unit = Unit(i);
    }
    const int method = element.firstChildElement(QStringLiteral("ImportNormalGen")).text().trimmed().toInt(&ok);
    if (ok && method >= 0 && method <= 3) {
        settings.normalMethod = NormalMethod(method);
    } else {
        const QString normals = element.firstChildElement(QStringLiteral("OpenVegasNormalMethod")).text().trimmed();
        const NormalMethod legacy[] = {NormalMethod::FromFile, NormalMethod::FromFileAutoSmoothing,
                                       NormalMethod::GenerateFaceted, NormalMethod::GenerateFacetedAutoSmoothing};
        for (int i = 0; i < 4; ++i)
            if (normals == QLatin1String(kLegacyNormalMethodNames[i])) settings.normalMethod = legacy[i];
    }
    return settings;
}

// A model's material as the reference writes it in its Model3DAsset's
// <Materials> (Model3DMaterial, Project.dll 0x1802bf450, Version 7):
// percentages for Opacity, the reflectivities, Refraction Amount, the
// Cook-Torrance, Metallic-Roughness and Specular-Glossiness factors, Alpha
// Cut Off and Occlusion Strength; colours as <… R G B A/>, the Fresnel term
// as <CK_Fresnel X Y Z/>, the rest as text (%g). The port keeps it under the
// model layer, beside its OpenVegasModelImport.
QDomElement modelMaterialElement(QDomDocument& doc, const model3d::Material& material, const QString& id)
{
    QDomElement element = doc.createElement(QStringLiteral("Material"));
    element.setAttribute(QStringLiteral("Version"), 7);
    const auto text = [&](const char* name, const QString& value) {
        addText(doc, element, QString::fromLatin1(name), value);
    };
    const auto number = [&](const char* name, double value) { text(name, QString::number(value)); };
    const auto color = [&](const char* name, const QColor& value) {
        QDomElement c = doc.createElement(QString::fromLatin1(name));
        c.setAttribute(QStringLiteral("R"), QString::number(value.redF()));
        c.setAttribute(QStringLiteral("G"), QString::number(value.greenF()));
        c.setAttribute(QStringLiteral("B"), QString::number(value.blueF()));
        c.setAttribute(QStringLiteral("A"), QString::number(value.alphaF()));
        element.appendChild(c);
    };
    text("ID", id);
    text("Name", material.name);
    number("IlluminationModel", int(material.illuminationModel));
    text("DiffuseTextureFilename", material.diffuseMap);
    number("DiffuseTextureHasAlphaChannel", material.diffuseImage.hasAlphaChannel() ? 1 : 0);
    text("SpecularTextureFilename", material.specularMap);
    text("BumpTextureFilename", material.bumpMap);
    text("NormalTextureFilename", material.normalMap);
    number("MaxBumpDepth", material.bumpScale);
    color("DiffuseColor", material.diffuseColor);
    color("SpecularColor", material.specularColor);
    color("AmbientColor", material.ambientColor);
    color("EmissiveColor", material.emissiveColor);
    number("Shininess", material.shininess);
    number("DiffuseReflectivity", material.diffuseReflectivity * 100.0);
    number("SpecularReflectivity", material.specularReflectivity * 100.0);
    number("Opacity", material.opacity * 100.0);
    number("RefractionIndex", material.indexOfRefraction);
    number("RefractionAmount", material.refractionAmount * 100.0);
    number("CK_SpecularReflectance", material.cookTorranceSpecularReflectance * 100.0);
    {
        QDomElement f = doc.createElement(QStringLiteral("CK_Fresnel"));
        f.setAttribute(QStringLiteral("X"), QString::number(material.cookTorranceFresnel.x()));
        f.setAttribute(QStringLiteral("Y"), QString::number(material.cookTorranceFresnel.y()));
        f.setAttribute(QStringLiteral("Z"), QString::number(material.cookTorranceFresnel.z()));
        element.appendChild(f);
    }
    number("CK_Roughness", material.cookTorranceRoughness * 100.0);
    number("ReflectionType", int(material.reflectionMode));
    number("OverrideMaterial", 0);
    text("EmissiveTextureFilename", material.emissiveMap);
    text("OcclusionTextureFilename", material.occlusionMap);
    number("OcclusionStrength", material.occlusionStrength * 100.0);
    text("MetallicTextureFilename", material.metallicMap);
    text("RoughnessTextureFilename", material.roughnessMap);
    text("ClearCoatIntensityTextureFilename", QString());
    text("ClearCoatRoughnessTextureFilename", QString());
    text("ClearCoatNormalTextureFilename", QString());
    number("MR_MetallicFactor", material.metallic * 100.0);
    number("MR_RoughnessFactor", material.roughness * 100.0);
    number("MR_ClearCoatFactor", material.clearCoat * 100.0);
    number("MR_ClearCoatRoughnessFactor", material.clearCoatRoughness * 100.0);
    text("SG_SpecularTextureFilename", material.specularGlossinessMap);
    text("SG_GlossinessTextureFilename", material.specularGlossinessMap);
    number("SG_GlossinessFactor", material.glossiness * 100.0);
    number("AlphaCutOff", material.alphaCutOff * 100.0);
    number("AlphaMode", int(material.alphaMode));
    return element;
}

// What modelMaterialElement wrote, over `material` (the file's own).
void readModelMaterial(const QDomElement& element, model3d::Material* material)
{
    const auto number = [&](const char* name, double fallback) {
        bool ok = false;
        const double value = element.firstChildElement(QString::fromLatin1(name)).text().trimmed().toDouble(&ok);
        return ok ? value : fallback;
    };
    const auto color = [&](const char* name, const QColor& fallback) {
        const QDomElement c = element.firstChildElement(QString::fromLatin1(name));
        if (c.isNull()) return fallback;
        const auto channel = [&](const char* key, double otherwise) {
            bool ok = false;
            const double value = c.attribute(QString::fromLatin1(key)).toDouble(&ok);
            return float(ok ? qBound(0.0, value, 1.0) : otherwise);
        };
        return QColor::fromRgbF(channel("R", fallback.redF()), channel("G", fallback.greenF()),
                                channel("B", fallback.blueF()), channel("A", fallback.alphaF()));
    };
    const int model = int(number("IlluminationModel", int(material->illuminationModel)));
    if (model >= 0 && model <= 3) material->illuminationModel = model3d::IlluminationModel(model);
    material->bumpScale = number("MaxBumpDepth", material->bumpScale);
    material->diffuseColor = color("DiffuseColor", material->diffuseColor);
    material->specularColor = color("SpecularColor", material->specularColor);
    material->ambientColor = color("AmbientColor", material->ambientColor);
    material->emissiveColor = color("EmissiveColor", material->emissiveColor);
    material->shininess = number("Shininess", material->shininess);
    material->diffuseReflectivity = number("DiffuseReflectivity", material->diffuseReflectivity * 100.0) / 100.0;
    material->specularReflectivity = number("SpecularReflectivity", material->specularReflectivity * 100.0) / 100.0;
    material->opacity = qBound(0.0, number("Opacity", material->opacity * 100.0) / 100.0, 1.0);
    material->indexOfRefraction = number("RefractionIndex", material->indexOfRefraction);
    material->refractionAmount = number("RefractionAmount", material->refractionAmount * 100.0) / 100.0;
    material->reflectionMode = int(number("ReflectionType", int(material->reflectionMode))) == 0
        ? model3d::ReflectionMode::Planar : model3d::ReflectionMode::Spherical;
    material->cookTorranceSpecularReflectance =
        number("CK_SpecularReflectance", material->cookTorranceSpecularReflectance * 100.0) / 100.0;
    const QDomElement fresnel = element.firstChildElement(QStringLiteral("CK_Fresnel"));
    if (!fresnel.isNull())
        material->cookTorranceFresnel = QVector3D(fresnel.attribute(QStringLiteral("X")).toFloat(),
                                                  fresnel.attribute(QStringLiteral("Y")).toFloat(),
                                                  fresnel.attribute(QStringLiteral("Z")).toFloat());
    material->cookTorranceRoughness = number("CK_Roughness", material->cookTorranceRoughness * 100.0) / 100.0;
    material->occlusionStrength = number("OcclusionStrength", material->occlusionStrength * 100.0) / 100.0;
    material->metallic = number("MR_MetallicFactor", material->metallic * 100.0) / 100.0;
    material->roughness = number("MR_RoughnessFactor", material->roughness * 100.0) / 100.0;
    material->clearCoat = number("MR_ClearCoatFactor", material->clearCoat * 100.0) / 100.0;
    material->clearCoatRoughness = number("MR_ClearCoatRoughnessFactor", material->clearCoatRoughness * 100.0) / 100.0;
    material->glossiness = number("SG_GlossinessFactor", material->glossiness * 100.0) / 100.0;
    material->alphaCutOff = number("AlphaCutOff", material->alphaCutOff * 100.0) / 100.0;
    const int alpha = int(number("AlphaMode", int(material->alphaMode)));
    if (alpha >= 0 && alpha <= 3) material->alphaMode = model3d::AlphaMode(alpha);
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

    // The third axis and the 3D rotations sit in the same properties: Z of
    // position/anchorPoint/scale, the three angles of "orientation" and the
    // scalars rotationX/rotationY. A 2D layer simply carries zeros there.
    const QString Z = QStringLiteral("Z");
    t.positionZ = propAxis(propertyManager, QStringLiteral("position"), Z, 0.0);
    // A camera's, a light's or a point layer's "target".
    t.targetX = propAxis(propertyManager, QStringLiteral("target"), X, 0.0);
    t.targetY = propAxis(propertyManager, QStringLiteral("target"), Y, 0.0);
    t.targetZ = propAxis(propertyManager, QStringLiteral("target"), Z, 0.0);
    const QDomElement target = propElement(propertyManager, QStringLiteral("target"));
    t.targetXCurve = propAnimation(target, fps, X);
    t.targetYCurve = propAnimation(target, fps, Y);
    t.targetZCurve = propAnimation(target, fps, Z);
    t.positionZCurve = propAnimation(pos, fps, Z);
    t.anchorPointZ = propAxis(propertyManager, QStringLiteral("anchorPoint"), Z, 0.0);
    t.scaleZPercent = propAxis(propertyManager, QStringLiteral("scale"), Z, 100.0);
    t.scaleZCurve = propAnimation(sc, fps, Z);
    const QDomElement orientation = propElement(propertyManager, QStringLiteral("orientation"));
    t.orientationX = propAxis(propertyManager, QStringLiteral("orientation"), X, 0.0);
    t.orientationY = propAxis(propertyManager, QStringLiteral("orientation"), Y, 0.0);
    t.orientationZ = propAxis(propertyManager, QStringLiteral("orientation"), Z, 0.0);
    t.orientationXCurve = propAnimation(orientation, fps, X);
    t.orientationYCurve = propAnimation(orientation, fps, Y);
    t.orientationZCurve = propAnimation(orientation, fps, Z);
    t.rotationXDegrees = propScalar(propertyManager, QStringLiteral("rotationX"), 0.0);
    t.rotationYDegrees = propScalar(propertyManager, QStringLiteral("rotationY"), 0.0);
    t.rotationXCurve =
        propAnimation(propElement(propertyManager, QStringLiteral("rotationX")), fps, QString());
    t.rotationYCurve =
        propAnimation(propElement(propertyManager, QStringLiteral("rotationY")), fps, QString());
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

// CompositionAsset/<RenderSettings Version="2">, in the reference's own order.
// An absent element (older port files) leaves the defaults.
void readRenderSettings(const QDomElement& node, composition::CompositionRenderSettings* settings)
{
    if (node.isNull() || !settings) return;
    const auto number = [&node](const char* tag, double fallback) {
        bool ok = false;
        const double value = elemText(node, QString::fromLatin1(tag)).toDouble(&ok);
        return ok ? value : fallback;
    };
    const auto flag = [&node](const char* tag, bool fallback) {
        const QString text = elemText(node, QString::fromLatin1(tag));
        return text.isEmpty() ? fallback : text != QStringLiteral("0");
    };
    composition::CompositionRenderSettings& s = *settings;
    s.fogEnabled = flag("FogEnabled", s.fogEnabled);
    s.fogNearDistance = number("FogNearDistance", s.fogNearDistance);
    s.fogFarDistance = number("FogFarDistance", s.fogFarDistance);
    s.fogDensity = number("FogDensity", s.fogDensity);
    const QDomElement color = node.firstChildElement(QStringLiteral("FogColor"));
    if (!color.isNull()) {
        s.fogColor = QColor::fromRgbF(float(color.attribute(QStringLiteral("R")).toDouble()),
                                      float(color.attribute(QStringLiteral("G")).toDouble()),
                                      float(color.attribute(QStringLiteral("B")).toDouble()),
                                      float(color.attribute(QStringLiteral("A"), QStringLiteral("1")).toDouble()));
    }
    s.fogFalloff = int(number("FogFalloff", s.fogFalloff));
    s.motionBlurEnabled = flag("MotionBlurEnabled", s.motionBlurEnabled);
    s.shutterAngle = number("ShutterAngle", s.shutterAngle);
    s.shutterPhase = number("ShutterPhase", s.shutterPhase);
    s.maxNumOfSamples = qMax(1, int(number("MaxNumOfSamples", s.maxNumOfSamples)));
    s.useAdaptiveSamples = flag("UseAdaptiveSamples", s.useAdaptiveSamples);
}

// Project/<ProjectSettings Version="9">, in the reference's own order. Out of
// range values fall back the way its Options readers do.
void readProjectSettings(const QDomElement& node, composition::ProjectRenderSettings* settings)
{
    if (node.isNull() || !settings) return;
    composition::ProjectRenderSettings& s = *settings;
    const auto integer = [&node](const char* tag, int fallback) {
        bool ok = false;
        const int value = elemText(node, QString::fromLatin1(tag)).toInt(&ok);
        return ok ? value : fallback;
    };
    s.bitDepth = integer("BPC", s.bitDepth);
    if (s.bitDepth < 1000 || s.bitDepth > 1002) s.bitDepth = 1000;
    s.antialiasingMode = integer("AntialiasingMode", s.antialiasingMode);
    if (s.antialiasingMode < 1 || s.antialiasingMode > 9) s.antialiasingMode = 1;
    s.reflectionMapSize = qMax(1, integer("ReflectionMapSize", s.reflectionMapSize));
    s.modelTextureMaxSize = qMax(1, integer("ModelTextureMaxSize", s.modelTextureMaxSize));
    s.shadowMapSize = qMax(1, integer("ShadowMapSize", s.shadowMapSize));
    s.limitVideoDecodingTo8bit = integer("LimitVideoDecodingTo8bit", 0) != 0;
    s.useLinearColor = integer("UseLinearColor", 0) != 0;
}

QDomElement projectSettingsElement(QDomDocument& doc, const composition::ProjectRenderSettings& s)
{
    QDomElement node = doc.createElement(QStringLiteral("ProjectSettings"));
    node.setAttribute(QStringLiteral("Version"), QStringLiteral("9"));
    addText(doc, node, QStringLiteral("BPC"), QString::number(s.bitDepth));
    addText(doc, node, QStringLiteral("AntialiasingMode"), QString::number(s.antialiasingMode));
    addText(doc, node, QStringLiteral("ReflectionMapSize"), QString::number(s.reflectionMapSize));
    addText(doc, node, QStringLiteral("ModelTextureMaxSize"), QString::number(s.modelTextureMaxSize));
    addText(doc, node, QStringLiteral("ShadowMapSize"), QString::number(s.shadowMapSize));
    addText(doc, node, QStringLiteral("LimitVideoDecodingTo8bit"),
            s.limitVideoDecodingTo8bit ? QStringLiteral("1") : QStringLiteral("0"));
    addText(doc, node, QStringLiteral("UseLinearColor"),
            s.useLinearColor ? QStringLiteral("1") : QStringLiteral("0"));
    return node;
}

QDomElement renderSettingsElement(QDomDocument& doc, const composition::CompositionRenderSettings& s)
{
    QDomElement node = doc.createElement(QStringLiteral("RenderSettings"));
    node.setAttribute(QStringLiteral("Version"), QStringLiteral("2"));
    const auto flag = [](bool on) { return on ? QStringLiteral("1") : QStringLiteral("0"); };
    addText(doc, node, QStringLiteral("FogEnabled"), flag(s.fogEnabled));
    addText(doc, node, QStringLiteral("FogNearDistance"), QString::number(s.fogNearDistance));
    addText(doc, node, QStringLiteral("FogFarDistance"), QString::number(s.fogFarDistance));
    addText(doc, node, QStringLiteral("FogDensity"), QString::number(s.fogDensity));
    QDomElement color = doc.createElement(QStringLiteral("FogColor"));
    color.setAttribute(QStringLiteral("A"), QString::number(s.fogColor.alphaF()));
    color.setAttribute(QStringLiteral("R"), QString::number(s.fogColor.redF()));
    color.setAttribute(QStringLiteral("G"), QString::number(s.fogColor.greenF()));
    color.setAttribute(QStringLiteral("B"), QString::number(s.fogColor.blueF()));
    node.appendChild(color);
    addText(doc, node, QStringLiteral("FogFalloff"), QString::number(s.fogFalloff));
    addText(doc, node, QStringLiteral("MotionBlurEnabled"), flag(s.motionBlurEnabled));
    addText(doc, node, QStringLiteral("ShutterAngle"), QString::number(s.shutterAngle));
    addText(doc, node, QStringLiteral("ShutterPhase"), QString::number(s.shutterPhase));
    addText(doc, node, QStringLiteral("MaxNumOfSamples"), QString::number(s.maxNumOfSamples));
    addText(doc, node, QStringLiteral("UseAdaptiveSamples"), flag(s.useAdaptiveSamples));
    return node;
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

// A constant <b>, <i> or <cl> property, as the reference writes them
// (Type 1, its value under <Static>).
void appendStaticProp(QDomDocument& doc, QDomElement& propertyManager, const QString& name,
                      const std::function<void(QDomElement&)>& value)
{
    QDomElement prop = doc.createElement(QStringLiteral("Prop"));
    prop.setAttribute(QStringLiteral("Type"), 1);
    addText(doc, prop, QStringLiteral("Name"), name);
    QDomElement holder = doc.createElement(QStringLiteral("Static"));
    value(holder);
    prop.appendChild(holder);
    propertyManager.appendChild(prop);
}

void appendBoolProp(QDomDocument& doc, QDomElement& propertyManager, const QString& name, bool value)
{
    appendStaticProp(doc, propertyManager, name, [&](QDomElement& holder) {
        addText(doc, holder, QStringLiteral("b"), value ? QStringLiteral("1") : QStringLiteral("0"));
    });
}

void appendIntProp(QDomDocument& doc, QDomElement& propertyManager, const QString& name, int value)
{
    appendStaticProp(doc, propertyManager, name, [&](QDomElement& holder) {
        addText(doc, holder, QStringLiteral("i"), QString::number(value));
    });
}

QDomElement colorElement(QDomDocument& doc, const QColor& value)
{
    QDomElement cl = doc.createElement(QStringLiteral("cl"));
    cl.setAttribute(QStringLiteral("A"), QString::number(value.alphaF(), 'g', 6));
    cl.setAttribute(QStringLiteral("R"), QString::number(value.redF(), 'g', 6));
    cl.setAttribute(QStringLiteral("G"), QString::number(value.greenF(), 'g', 6));
    cl.setAttribute(QStringLiteral("B"), QString::number(value.blueF(), 'g', 6));
    return cl;
}

// A colour property - one that animates (Type 0, as the reference marks its
// colours): its own value, and its keys when it has any, each a whole <cl>.
void appendColorProp(QDomDocument& doc, QDomElement& propertyManager, const QString& name, const QColor& value,
                     const composition::KeyFrameList& curve = composition::KeyFrameList(), double fps = 30.0)
{
    QDomElement prop = doc.createElement(QStringLiteral("Prop"));
    prop.setAttribute(QStringLiteral("Type"), 0);
    addText(doc, prop, QStringLiteral("Name"), name);
    QDomElement holder = doc.createElement(QStringLiteral("Static"));
    holder.appendChild(colorElement(doc, value));
    prop.appendChild(holder);
    const QVector<composition::KeyFrame> keys = curve.all();
    if (!keys.isEmpty()) {
        QDomElement animation = doc.createElement(QStringLiteral("Animation"));
        for (int i = 0; i < keys.size(); ++i) {
            const composition::KeyFrame& key = keys.at(i);
            const bool hasPrevious = i > 0;
            const bool hasNext = i + 1 < keys.size();
            appendKey(doc, animation, key, fps, hasPrevious ? keyTimeMs(keys.at(i - 1).frame, fps) : 0.0,
                      hasNext ? keyTimeMs(keys.at(i + 1).frame, fps) : 0.0, 0.0, 0.0, hasPrevious, hasNext,
                      [&doc, &key](QDomElement& holder) {
                          holder.appendChild(colorElement(doc, key.value.value<QColor>()));
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
                     const composition::KeyFrameList& yCurve, double fps,
                     double staticZ = 0.0,
                     const composition::KeyFrameList& zCurve = composition::KeyFrameList())
{
    QDomElement prop = doc.createElement(QStringLiteral("Prop"));
    addText(doc, prop, QStringLiteral("Name"), name);

    const auto writeHolder = [&doc, &holderTag](QDomElement& parent, double x, double y,
                                                double z) {
        QDomElement holder = doc.createElement(holderTag);
        holder.setAttribute(QStringLiteral("X"), QString::number(x, 'f', 4));
        holder.setAttribute(QStringLiteral("Y"), QString::number(y, 'f', 4));
        holder.setAttribute(QStringLiteral("Z"), QString::number(z, 'f', 4));
        parent.appendChild(holder);
    };

    QDomElement stat = doc.createElement(QStringLiteral("Static"));
    writeHolder(stat, staticValue.x(), staticValue.y(), staticZ);
    prop.appendChild(stat);

    QVector<int> frames = xCurve.locations();
    const QVector<int> yFrames = yCurve.locations();
    for (int frame : yFrames) {
        if (!frames.contains(frame)) {
            frames.append(frame);
        }
    }
    for (int frame : zCurve.locations()) {
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
            if (const composition::KeyFrame* k = zCurve.at(frame)) {
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
                                      sample(yCurve, frame, staticValue.y()),
                                      sample(zCurve, frame, staticZ));
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

// <OpenCompositeShots TimelineType>: 1404 names a composite shot timeline,
// 1000 the editor sequence (FUN_140202ab0 accepts only these two).
constexpr int kShotTimeline = 1404;
// Root of a composite shot file (the reference's DAT_141542ab0).
constexpr char kShotFileRoot[] = "BiffCompositeShot";

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

// Effect instances and their parameter curves, every one of them - built-in
// effects and transitions included - in an ignorable extension that makes an
// OpenVegas save/load lossless and that VEGAS Effects ignores. Native effects
// go into the reference's own lists as well (nativefx below), which is what
// a file of the reference is read from.
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
        if (effect.isTransition()) {
            fx.setAttribute(QStringLiteral("Transition"),
                            effect.transitionEdge == composition::TransitionEdge::In
                                ? QStringLiteral("In") : QStringLiteral("Out"));
            fx.setAttribute(QStringLiteral("TransitionLength"),
                            QString::number(effect.transitionSeconds, 'g', 17));
        }
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
        if (!effect.instanceData.isEmpty()) {
            QDomElement bytes = doc.createElement(QStringLiteral("InstanceBytes"));
            bytes.appendChild(doc.createTextNode(QString::fromLatin1(effect.instanceData.toBase64())));
            fx.appendChild(bytes);
        }
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
        const QString edge = fx.attribute(QStringLiteral("Transition"));
        if (edge.compare(QStringLiteral("In"), Qt::CaseInsensitive) == 0) {
            effect.transitionEdge = composition::TransitionEdge::In;
        } else if (edge.compare(QStringLiteral("Out"), Qt::CaseInsensitive) == 0) {
            effect.transitionEdge = composition::TransitionEdge::Out;
        }
        bool lengthOk = false;
        const double length = fx.attribute(QStringLiteral("TransitionLength")).toDouble(&lengthOk);
        if (lengthOk && qIsFinite(length) && length >= 0.0) effect.transitionSeconds = length;
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
        const QDomElement bytes = fx.firstChildElement(QStringLiteral("InstanceBytes"));
        if (!bytes.isNull()) effect.instanceData = QByteArray::fromBase64(bytes.text().toLatin1());
        clip->effects.append(effect);
    }
    return true;
}

// The reference's own effect lists (EffectsHelper::SerializeEffects and
// friends): each native effect an <Instance Version="8"> as
// EffectInstance::Serialize writes it (Project.dll 0x1803f4310) - ID, Name,
// PluginID, PluginName, PluginVersion (Ma/Mi/Re/Bu), ParentID,
// ParentTimelineID, Enabled, CreatedInComp, InitializedUIState,
// ParentLayerLastSize (X/Y), the PropertyManager of the keys the module
// registered (a float slider or an angle <fl>, an int slider or a combo box
// <i>, a check box or a button <b>, a colour <cl>, a point <p2>/<p3>, a layer
// picker <id>, text <st> - PluginHostAPI::Create* in Tannen.dll), UIState,
// then the module's own data (<InstanceBytes>, SerializeInstanceDataBytes).
// The reader takes a property by its name (SynchronizeSourceWithDest), so
// what the module did not register is left out.
namespace nativefx {

const QString kZeroId = QStringLiteral("00000000-0000-0000-0000-000000000000");

QString containerName(project::NativeEffectEntry::Container container)
{
    switch (container) {
    case project::NativeEffectEntry::Container::Effects: return QStringLiteral("Effects");
    case project::NativeEffectEntry::Container::GeometryEffects: return QStringLiteral("GeometryEffects");
    case project::NativeEffectEntry::Container::BehaviorEffects: return QStringLiteral("BehaviorEffects");
    case project::NativeEffectEntry::Container::None: break;
    }
    return QString();
}

QString number(double value)
{
    return QString::number(value, 'g', 9);
}

bool isText(const QString& type)
{
    return type == QLatin1String("string") || type == QLatin1String("multiline")
           || type == QLatin1String("save-file") || type == QLatin1String("open-file")
           || type == QLatin1String("directory");
}

QDomElement prop(QDomDocument& doc, QDomElement& propertyManager, const QString& name, int type, bool spatial,
                 bool interpolates)
{
    QDomElement element = doc.createElement(QStringLiteral("Prop"));
    element.setAttribute(QStringLiteral("Type"), type);
    element.setAttribute(QStringLiteral("Spatial"), spatial ? 1 : 0);
    element.setAttribute(QStringLiteral("CanInterpT"), interpolates ? 1 : 0);
    addText(doc, element, QStringLiteral("Name"), name);
    propertyManager.appendChild(element);
    return element;
}

QDomElement holder(QDomDocument& doc, QDomElement& parent)
{
    QDomElement element = doc.createElement(QStringLiteral("Static"));
    element.setAttribute(QStringLiteral("V"), 4);
    parent.appendChild(element);
    return element;
}

QStringList components(const QString& value, int count)
{
    QStringList parts = value.split(QLatin1Char(','));
    while (parts.size() < count) parts.append(QStringLiteral("0"));
    for (QString& part : parts) part = part.trimmed();
    return parts;
}

int choiceIndex(const QString& value, const QStringList& choices)
{
    bool ok = false;
    const int numeric = value.trimmed().toInt(&ok);
    if (ok) return numeric;
    for (int i = 0; i < choices.size(); ++i)
        if (choices.at(i).compare(value.trimmed(), Qt::CaseInsensitive) == 0) return i;
    return 0;
}

bool isScalar(const QString& type)
{
    return type == QLatin1String("double") || type == QLatin1String("angle");
}

// A value of `p`'s type, as this port keeps it, into `parent`.
void writeValue(QDomDocument& doc, QDomElement& parent, const project::NativeEffectEntry::Parameter& p,
                const QString& value)
{
    const QString& type = p.type;
    if (isScalar(type)) {
        addText(doc, parent, QStringLiteral("fl"), number(value.toDouble()));
    } else if (type == QLatin1String("int") || type == QLatin1String("enum")) {
        addText(doc, parent, QStringLiteral("i"), QString::number(choiceIndex(value, p.choices)));
    } else if (type == QLatin1String("bool") || type == QLatin1String("button")) {
        const QString v = value.trimmed();
        addText(doc, parent, QStringLiteral("b"),
                v == QLatin1String("1") || v.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0
                    ? QStringLiteral("1") : QStringLiteral("0"));
    } else if (type == QLatin1String("color")) {
        const QColor color(value.trimmed());
        parent.appendChild(colorElement(doc, color.isValid() ? color : QColor(Qt::black)));
    } else if (type == QLatin1String("point2d") || type == QLatin1String("point3d")) {
        const bool three = type == QLatin1String("point3d");
        const QStringList parts = components(value, three ? 3 : 2);
        QDomElement point = doc.createElement(three ? QStringLiteral("p3") : QStringLiteral("p2"));
        point.setAttribute(QStringLiteral("X"), number(parts.at(0).toDouble()));
        point.setAttribute(QStringLiteral("Y"), number(parts.at(1).toDouble()));
        if (three) point.setAttribute(QStringLiteral("Z"), number(parts.at(2).toDouble()));
        parent.appendChild(point);
    } else if (type == QLatin1String("layer") || type == QLatin1String("mask")) {
        addText(doc, parent, QStringLiteral("id"), value.trimmed().isEmpty() ? kZeroId : value.trimmed());
    } else {
        addText(doc, parent, QStringLiteral("st"), value);
    }
}

void appendProperty(QDomDocument& doc, QDomElement& propertyManager, const project::NativeEffectEntry::Parameter& p,
                    const composition::Effect& effect, int index, double fps)
{
    const QString value = index < effect.parameterValues.size() && !effect.parameterValues.at(index).isEmpty()
        ? effect.parameterValues.at(index) : p.defaultValue;
    const QString& type = p.type;
    if (type == QLatin1String("label")) return;
    // Type 0 for what animates smoothly (floats, colours, points - spatial),
    // 1 for discrete values; a combo box does not interpolate in time.
    const bool smooth = isScalar(type) || type == QLatin1String("color") || type == QLatin1String("point2d")
                        || type == QLatin1String("point3d");
    const bool spatial = type == QLatin1String("point2d") || type == QLatin1String("point3d");
    const bool interpolates = smooth || (type == QLatin1String("int") && p.choices.isEmpty());
    QDomElement element = prop(doc, propertyManager, p.name, smooth ? 0 : 1, spatial, interpolates);
    QDomElement stat = holder(doc, element);
    writeValue(doc, stat, p, value);
    const auto curve = effect.animation.constFind(index);
    if (curve == effect.animation.cend() || curve->isEmpty()) return;
    const QVector<composition::KeyFrame> keys = curve->all();
    const auto numeric = [&](const QVariant& v) { return isScalar(type) ? v.toDouble() : 0.0; };
    QDomElement animation = doc.createElement(QStringLiteral("Animation"));
    for (int k = 0; k < keys.size(); ++k) {
        const composition::KeyFrame& key = keys.at(k);
        const bool hasPrevious = k > 0, hasNext = k + 1 < keys.size();
        appendKey(doc, animation, key, fps, hasPrevious ? keyTimeMs(keys.at(k - 1).frame, fps) : 0.0,
                  hasNext ? keyTimeMs(keys.at(k + 1).frame, fps) : 0.0,
                  hasPrevious ? numeric(keys.at(k - 1).value) : 0.0,
                  hasNext ? numeric(keys.at(k + 1).value) : 0.0, hasPrevious, hasNext,
                  [&](QDomElement& holderElement) {
                      const QString text = key.value.typeId() == QMetaType::Bool
                          ? (key.value.toBool() ? QStringLiteral("true") : QStringLiteral("false"))
                          : key.value.toString();
                      writeValue(doc, holderElement, p, text);
                  });
    }
    element.appendChild(animation);
}

// The effect's <Instance> in `container`.
void appendInstance(QDomDocument& doc, QDomElement& container, const project::NativeEffectEntry& entry,
                    const composition::Effect& effect, const QString& instanceId, const QString& layerId,
                    const QString& shotId, const QSizeF& layerSize, double fps)
{
    QDomElement instance = doc.createElement(QStringLiteral("Instance"));
    instance.setAttribute(QStringLiteral("Version"), 8);
    addText(doc, instance, QStringLiteral("ID"), instanceId);
    addText(doc, instance, QStringLiteral("Name"), effect.name.isEmpty() ? entry.name : effect.name);
    addText(doc, instance, QStringLiteral("PluginID"), entry.guid);
    addText(doc, instance, QStringLiteral("PluginName"), entry.name);
    QDomElement version = doc.createElement(QStringLiteral("PluginVersion"));
    version.setAttribute(QStringLiteral("Ma"), entry.versionMajor);
    version.setAttribute(QStringLiteral("Mi"), entry.versionMinor);
    version.setAttribute(QStringLiteral("Re"), 0);
    version.setAttribute(QStringLiteral("Bu"), 0);
    instance.appendChild(version);
    addText(doc, instance, QStringLiteral("ParentID"), layerId);
    addText(doc, instance, QStringLiteral("ParentTimelineID"), shotId.isEmpty() ? kZeroId : shotId);
    addText(doc, instance, QStringLiteral("Enabled"), effect.enabled ? QStringLiteral("1") : QStringLiteral("0"));
    addText(doc, instance, QStringLiteral("CreatedInComp"), QStringLiteral("1"));
    addText(doc, instance, QStringLiteral("InitializedUIState"), QStringLiteral("1"));
    QDomElement size = doc.createElement(QStringLiteral("ParentLayerLastSize"));
    size.setAttribute(QStringLiteral("X"), number(layerSize.width()));
    size.setAttribute(QStringLiteral("Y"), number(layerSize.height()));
    instance.appendChild(size);
    QDomElement properties = doc.createElement(QStringLiteral("PropertyManager"));
    properties.setAttribute(QStringLiteral("Version"), QStringLiteral("7"));
    for (int i = 0; i < entry.parameters.size(); ++i) appendProperty(doc, properties, entry.parameters.at(i), effect, i, fps);
    instance.appendChild(properties);
    instance.appendChild(doc.createElement(QStringLiteral("UIState")));
    if (!effect.instanceData.isEmpty())
        addText(doc, instance, QStringLiteral("InstanceBytes"), QString::fromLatin1(effect.instanceData.toBase64()));
    container.appendChild(instance);
}

// An <Instance> written back as it was read.
void appendVerbatim(QDomDocument& doc, QDomElement& container, const QString& xml)
{
    QDomDocument fragment;
    if (!fragment.setContent(xml)) return;
    container.appendChild(doc.importNode(fragment.documentElement(), true));
}

QString textOf(const QDomElement& element)
{
    QString text;
    QTextStream stream(&text);
    element.save(stream, 0);
    return text;
}

// The value a property of `type` holds, as this port keeps it.
QString readHolderValue(const QDomElement& value, const project::NativeEffectEntry::Parameter& p)
{
    const QString& type = p.type;
    if (type == QLatin1String("double") || type == QLatin1String("angle")) {
        const QDomElement fl = value.firstChildElement(QStringLiteral("fl"));
        const QDomElement db = value.firstChildElement(QStringLiteral("db"));
        if (!fl.isNull()) return number(fl.text().toDouble());
        if (!db.isNull()) return number(db.text().toDouble());
    } else if (type == QLatin1String("int") || type == QLatin1String("enum")) {
        const QDomElement i = value.firstChildElement(QStringLiteral("i"));
        if (!i.isNull()) return QString::number(i.text().trimmed().toInt());
    } else if (type == QLatin1String("bool") || type == QLatin1String("button")) {
        const QDomElement b = value.firstChildElement(QStringLiteral("b"));
        if (!b.isNull()) {
            const QString text = b.text().trimmed();
            return text == QLatin1String("1") || text.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0
                ? QStringLiteral("true") : QStringLiteral("false");
        }
    } else if (type == QLatin1String("color")) {
        const QDomElement cl = value.firstChildElement(QStringLiteral("cl"));
        if (!cl.isNull()) {
            const auto channel = [&](const char* key) {
                return qBound(0.0, cl.attribute(QString::fromLatin1(key), QStringLiteral("1")).toDouble(), 1.0);
            };
            const QColor color = QColor::fromRgbF(float(channel("R")), float(channel("G")), float(channel("B")),
                                                  float(channel("A")));
            return color.alpha() == 255 ? color.name() : color.name(QColor::HexArgb);
        }
    } else if (type == QLatin1String("point2d") || type == QLatin1String("point3d")) {
        for (const char* tag : {"p2", "p3", "v2", "v3"}) {
            const QDomElement point = value.firstChildElement(QString::fromLatin1(tag));
            if (point.isNull()) continue;
            QStringList parts {number(point.attribute(QStringLiteral("X")).toDouble()),
                               number(point.attribute(QStringLiteral("Y")).toDouble())};
            if (type == QLatin1String("point3d")) parts << number(point.attribute(QStringLiteral("Z")).toDouble());
            return parts.join(QLatin1Char(','));
        }
    } else if (type == QLatin1String("layer") || type == QLatin1String("mask")) {
        const QDomElement id = value.firstChildElement(QStringLiteral("id"));
        if (!id.isNull()) {
            const QString text = id.text().trimmed();
            return text == kZeroId ? QString() : text;
        }
    } else {
        const QDomElement st = value.firstChildElement(QStringLiteral("st"));
        if (!st.isNull()) return st.text();
    }
    return p.defaultValue;
}

QString readValue(const QDomElement& property, const project::NativeEffectEntry::Parameter& p)
{
    return readHolderValue(propValueHolder(property), p);
}

// The keys of a property that is not a float: each value whole, as this
// port keys it (a colour "#rrggbb", a point "x,y"), at the key's time.
composition::KeyFrameList readKeys(const QDomElement& property, const project::NativeEffectEntry::Parameter& p,
                                   double fps)
{
    composition::KeyFrameList curve;
    const QDomElement animation = property.firstChildElement(QStringLiteral("Animation"));
    if (animation.isNull() || fps <= 0.0) return curve;
    const bool discrete = !(p.type == QLatin1String("color") || p.type == QLatin1String("point2d")
                            || p.type == QLatin1String("point3d")
                            || (p.type == QLatin1String("int") && p.choices.isEmpty()));
    curve.setCanInterpolate(!discrete);
    for (QDomElement key = animation.firstChildElement(QStringLiteral("Key")); !key.isNull();
         key = key.nextSiblingElement(QStringLiteral("Key"))) {
        composition::KeyFrame frame;
        frame.frame = int(qRound(key.attribute(QStringLiteral("Ti")).toDouble() / 1000.0 * fps));
        frame.temporal = discrete ? composition::TemporalType::Hold
                                  : temporalTypeFromId(key.attribute(QStringLiteral("Tp")).toInt());
        const QString text = readHolderValue(key.firstChildElement(QStringLiteral("Vl")), p);
        if (p.type == QLatin1String("int") || p.type == QLatin1String("enum")) frame.value = text.toInt();
        else if (p.type == QLatin1String("bool") || p.type == QLatin1String("button"))
            frame.value = text == QLatin1String("true");
        else frame.value = text;
        curve.add(frame);
    }
    return curve;
}

QVector<composition::Effect> readInstanceList(const QDomElement& list, const QString& tag, double fps);

// The effects of the reference's own lists under `layerWrapper` (Effects,
// GeometryEffects) and `named` (BehaviorEffects), in that order.
QVector<composition::Effect> readInstances(const QDomElement& layerWrapper, const QDomElement& named, double fps)
{
    QVector<composition::Effect> effects;
    const auto readList = [&](const QDomElement& parent, const QString& tag) {
        effects += readInstanceList(parent.firstChildElement(tag), tag, fps);
    };
    readList(layerWrapper, QStringLiteral("Effects"));
    readList(layerWrapper, QStringLiteral("GeometryEffects"));
    readList(named, QStringLiteral("BehaviorEffects"));
    return effects;
}

// Writes `effects` - native ones as <Instance>, unknown ones as they were
// read - into `list`, whatever their kind (a preset's EffectsPreset holds all
// of them).
void appendInstanceList(QDomDocument& doc, QDomElement& list, const QVector<composition::Effect>& effects,
                        const QString& layerId, const QString& shotId, const QSizeF& layerSize, double fps)
{
    for (int e = 0; e < effects.size(); ++e) {
        const composition::Effect& effect = effects.at(e);
        if (effect.isTransition()) continue;
        if (!effect.nativeXml.isEmpty()) {
            appendVerbatim(doc, list, effect.nativeXml);
            continue;
        }
        const std::optional<project::NativeEffectEntry> entry = project::findNativeEffect(effect.pluginId.value());
        if (!entry || entry->guid.isEmpty()) continue;
        const QString instanceId = effect.instanceId.isValid()
            ? effect.instanceId.value() : QUuid::createUuid().toString(QUuid::WithoutBraces);
        appendInstance(doc, list, *entry, effect, instanceId, layerId, shotId, layerSize, fps);
    }
}

QVector<composition::Effect> readInstanceList(const QDomElement& list, const QString& tag, double fps)
{
    QVector<composition::Effect> effects;
    {
        for (QDomElement instance = list.firstChildElement(QStringLiteral("Instance")); !instance.isNull();
             instance = instance.nextSiblingElement(QStringLiteral("Instance"))) {
            composition::Effect effect;
            const QString guid = elemText(instance, QStringLiteral("PluginID")).trimmed();
            effect.instanceId = core::Identifier(elemText(instance, QStringLiteral("ID")).trimmed());
            effect.name = elemText(instance, QStringLiteral("Name"));
            effect.enabled = elemText(instance, QStringLiteral("Enabled")).trimmed() != QLatin1String("0");
            const QDomElement bytes = instance.firstChildElement(QStringLiteral("InstanceBytes"));
            if (!bytes.isNull()) effect.instanceData = QByteArray::fromBase64(bytes.text().toLatin1());
            const std::optional<project::NativeEffectEntry> entry = project::findNativeEffect(guid);
            if (!entry) {
                // Kept as the file had it, to go back out unchanged.
                effect.pluginId = core::Identifier(guid);
                if (effect.name.isEmpty()) effect.name = elemText(instance, QStringLiteral("PluginName"));
                effect.nativeXml = textOf(instance);
                effect.nativeContainer = tag;
                effects.append(effect);
                continue;
            }
            effect.pluginId = core::Identifier(entry->pluginId);
            if (effect.name.isEmpty()) effect.name = entry->name;
            const QDomElement properties = instance.firstChildElement(QStringLiteral("PropertyManager"));
            for (int i = 0; i < entry->parameters.size(); ++i) {
                const project::NativeEffectEntry::Parameter& p = entry->parameters.at(i);
                const QDomElement property = propElement(properties, p.name);
                effect.parameterValues.append(property.isNull() ? p.defaultValue : readValue(property, p));
                if (property.isNull()) continue;
                composition::KeyFrameList curve = isScalar(p.type) ? propAnimation(property, fps, QString())
                                                                   : readKeys(property, p, fps);
                if (curve.isEmpty()) continue;
                curve.setDefaultValue(effect.parameterValues.last());
                effect.animation.insert(i, curve);
            }
            effects.append(effect);
        }
    }
    return effects;
}

} // namespace nativefx

} // namespace

QVector<composition::Effect> VegfxSerializer::effectsFromInstances(const QDomElement& list, double fps)
{
    return nativefx::readInstanceList(list, list.tagName(), fps);
}

void VegfxSerializer::appendEffectInstances(QDomDocument& doc, QDomElement& list,
                                            const QVector<composition::Effect>& effects, double fps)
{
    nativefx::appendInstanceList(doc, list, effects, nativefx::kZeroId, nativefx::kZeroId, QSizeF(), fps);
}

namespace {

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

// Shots older saves of this port kept in an <OpenVegasCompositions> block of
// the root CompositionAsset, before every shot became a CompositionAsset of
// its own. Read for those files only; nothing writes the block any more.
void readEmbeddedCompositions(
    const QDomElement& compositionAsset, const core::Identifier& rootId,
    QHash<QString, std::shared_ptr<composition::Composition>>* byId,
    QVector<std::shared_ptr<composition::Composition>>* order)
{
    const QDomElement root = compositionAsset.firstChildElement(
        QStringLiteral("OpenVegasCompositions"));
    if (root.isNull()) return;
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
        readRenderSettings(node.firstChildElement(QStringLiteral("RenderSettings")),
                           &comp->renderSettings());
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
            if (validFov && std::isfinite(cameraFov))
                layer.camera.zoom = composition::CameraSettings::zoomForVerticalFieldOfView(
                    qBound(1.0, cameraFov, 179.0), comp->displaySize().height());
            const QColor plane(layerNode.attribute(QStringLiteral("PlaneColor")));
            if (plane.isValid()) layer.planeColor = plane;
            layer.modelAssetId = core::Identifier(layerNode.attribute(QStringLiteral("ModelAssetID")));
            layer.blendMode = layerNode.attribute(QStringLiteral("BlendMode"), QStringLiteral("None"));
            layer.opacity = layerNode.attribute(QStringLiteral("Opacity"), QStringLiteral("1")).toDouble();
            layer.visible = layerNode.attribute(QStringLiteral("Visible"), QStringLiteral("1")).toInt() != 0;
            layer.muted = layerNode.attribute(QStringLiteral("Muted")).toInt() != 0;
            layer.locked = layerNode.attribute(QStringLiteral("Locked")).toInt() != 0;
            layer.motionBlur = layerNode.attribute(QStringLiteral("MotionBlur")).toInt() != 0;
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
            layer.transform.audioLevel = scalar(QStringLiteral("audioLevel"), 0.0, &layer.transform.audioLevelCurve);
            readClipsExtension(layerNode, &layer);
            readMasksExtension(layerNode, &layer);
            readMotionTracksExtension(layerNode, &layer);
        }
        if (byId->contains(comp->id().value()) || comp->id() == rootId) continue;
        byId->insert(comp->id().value(), comp);
        order->append(comp);
    }
}

// Whether `from` shows `target` somewhere down its nested layers.
bool nestsShot(const composition::Composition* from, const composition::Composition* target,
               QSet<const composition::Composition*>* seen)
{
    if (!from || seen->contains(from)) return false;
    seen->insert(from);
    for (const composition::Layer& layer : from->layers()) {
        for (const composition::Clip& clip : layer.clips) {
            if (clip.nestedComposition.get() == target
                || nestsShot(clip.nestedComposition.get(), target, seen))
                return true;
        }
    }
    return false;
}

// Points every nested layer at its shot. A link that would put a shot inside
// itself - or inside the root, which the caller owns by value - is left
// unresolved, so a damaged file cannot make rendering recurse.
void linkNestedShots(composition::Composition* root,
                     const QHash<QString, std::shared_ptr<composition::Composition>>& byId)
{
    QVector<composition::Composition*> shots{root};
    for (const auto& shot : byId) shots.append(shot.get());
    for (composition::Composition* shot : shots) {
        for (int li = 0; li < shot->layers().size(); ++li) {
            for (composition::Clip& clip : shot->layerRef(li).clips) {
                if (!clip.nestedCompositionId.isValid()) continue;
                const auto target = byId.value(clip.nestedCompositionId.value());
                QSet<const composition::Composition*> seen;
                if (!target || target.get() == shot || nestsShot(target.get(), shot, &seen)) {
                    clip.nestedComposition.reset();
                    OV_LOG_WARN(QStringLiteral("VEGFX: layer nests a shot it cannot show (%1)")
                                    .arg(clip.nestedCompositionId.value()));
                    continue;
                }
                clip.nestedComposition = target;
            }
        }
    }
}

// One <CompositionAsset>: the shot's own settings and its layers. An
// AssetLayer whose <AssetID> is one of `shotIds` nests that shot.
// CompositionAsset/<ViewerState Version="5"> as the reference writes it
// (PanelRenderState::Serialize, Project.dll 1803c0b00): CurrentLayout,
// Layout0..4 with the State IDs in their places (PosN) and the splitters
// (SpN), and the Views' States. A shot the Viewer never showed gets the
// reference's own default, one 2D view.
QDomElement viewerStateElement(QDomDocument& doc, const composition::ViewerSetup& setup)
{
    QDomElement element = doc.createElement(QStringLiteral("ViewerState"));
    element.setAttribute(QStringLiteral("Version"), QStringLiteral("5"));
    QVector<composition::ViewerViewSetup> views = setup.views;
    if (!setup.present || views.isEmpty()) {
        composition::ViewerViewSetup only;
        only.type = 0;
        views = {only};
    }
    for (composition::ViewerViewSetup& view : views)
        if (view.id.isEmpty()) view.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const int current = setup.present ? qBound(0, setup.layout, 4) : 0;
    addText(doc, element, QStringLiteral("CurrentLayout"), QString::number(current));
    // The reference's splitter defaults per layout.
    const char* const splitters[5][3] = {{nullptr, nullptr, nullptr}, {nullptr, nullptr, nullptr},
                                         {"0.5", "0", "0"}, {"0.5", "0", "0.5"}, {"0.5", "0.5", "0.5"}};
    for (int layout = 0; layout < 5; ++layout) {
        QDomElement node = doc.createElement(QStringLiteral("Layout%1").arg(layout));
        if (layout == current) {
            for (int place = 0; place < views.size(); ++place)
                node.setAttribute(QStringLiteral("Pos%1").arg(place), views.at(place).id);
        } else if (layout == 0) {
            node.setAttribute(QStringLiteral("Pos0"), views.first().id);
        }
        for (int s = 0; s < 3; ++s)
            if (splitters[layout][s]) node.setAttribute(QStringLiteral("Sp%1").arg(s), QString::fromLatin1(splitters[layout][s]));
        element.appendChild(node);
    }
    QDomElement list = doc.createElement(QStringLiteral("Views"));
    const auto vector = [&](const QString& name, const QVector3D& v) {
        QDomElement e = doc.createElement(name);
        e.setAttribute(QStringLiteral("X"), QString::number(v.x(), 'g', 9));
        e.setAttribute(QStringLiteral("Y"), QString::number(v.y(), 'g', 9));
        e.setAttribute(QStringLiteral("Z"), QString::number(v.z(), 'g', 9));
        return e;
    };
    for (int place = 0; place < views.size(); ++place) {
        const composition::ViewerViewSetup& view = views.at(place);
        QDomElement state = doc.createElement(QStringLiteral("State"));
        state.setAttribute(QStringLiteral("CameraScale"), QString::number(view.cameraScale, 'g', 9));
        state.setAttribute(QStringLiteral("Active"), place == setup.activeView ? QStringLiteral("1") : QStringLiteral("0"));
        state.setAttribute(QStringLiteral("TextureScale"), QStringLiteral("1"));
        state.setAttribute(QStringLiteral("Type"), QString::number(view.type));
        state.setAttribute(QStringLiteral("Layout"), QString::number(current));
        state.setAttribute(QStringLiteral("ID"), view.id);
        QDomElement texturePosition = doc.createElement(QStringLiteral("TexturePosition"));
        texturePosition.setAttribute(QStringLiteral("X"), QStringLiteral("0"));
        texturePosition.setAttribute(QStringLiteral("Y"), QStringLiteral("0"));
        state.appendChild(texturePosition);
        QDomElement anchor = doc.createElement(QStringLiteral("TextureAnchorPoint"));
        anchor.setAttribute(QStringLiteral("X"), QStringLiteral("0"));
        anchor.setAttribute(QStringLiteral("Y"), QStringLiteral("0"));
        state.appendChild(anchor);
        state.appendChild(vector(QStringLiteral("CameraPosition"), view.cameraPosition));
        state.appendChild(vector(QStringLiteral("CameraRotation"), view.cameraRotation));
        // The reference's view options as a new view has them.
        QDomElement options = doc.createElement(QStringLiteral("Options"));
        for (const auto& [key, value] : {std::pair<const char*, const char*>{"Gz", "0"}, {"Vol", "1"}, {"BiTo", "1"},
                                         {"MoPa", "1"}, {"TrCb", "0"}, {"TrPa", "1"}, {"Ch", "0"}, {"WoAx", "1"}})
            options.setAttribute(QString::fromLatin1(key), QString::fromLatin1(value));
        QDomElement colour = doc.createElement(QStringLiteral("BGColor"));
        colour.setAttribute(QStringLiteral("A"), QStringLiteral("1"));
        colour.setAttribute(QStringLiteral("R"), QStringLiteral("0"));
        colour.setAttribute(QStringLiteral("G"), QStringLiteral("0"));
        colour.setAttribute(QStringLiteral("B"), QStringLiteral("0"));
        options.appendChild(colour);
        state.appendChild(options);
        list.appendChild(state);
    }
    element.appendChild(list);
    return element;
}

// What viewerStateElement wrote, or the reference: the current layout's
// views in their places.
composition::ViewerSetup readViewerState(const QDomElement& element)
{
    composition::ViewerSetup setup;
    if (element.isNull()) return setup;
    setup.layout = qBound(0, elemText(element, QStringLiteral("CurrentLayout")).toInt(), 4);
    QHash<QString, QDomElement> states;
    const QDomElement list = element.firstChildElement(QStringLiteral("Views"));
    for (QDomElement state = list.firstChildElement(QStringLiteral("State")); !state.isNull();
         state = state.nextSiblingElement(QStringLiteral("State")))
        states.insert(state.attribute(QStringLiteral("ID")), state);
    const QDomElement layout = element.firstChildElement(QStringLiteral("Layout%1").arg(setup.layout));
    const auto vector = [](const QDomElement& e) {
        return QVector3D(e.attribute(QStringLiteral("X")).toFloat(), e.attribute(QStringLiteral("Y")).toFloat(),
                         e.attribute(QStringLiteral("Z")).toFloat());
    };
    for (int place = 0; place < 4; ++place) {
        const QString id = layout.attribute(QStringLiteral("Pos%1").arg(place));
        if (id.isEmpty() || !states.contains(id)) break;
        const QDomElement state = states.value(id);
        composition::ViewerViewSetup view;
        view.id = id;
        view.type = qBound(0, state.attribute(QStringLiteral("Type")).toInt(), 8);
        bool ok = false;
        const double scale = state.attribute(QStringLiteral("CameraScale")).toDouble(&ok);
        view.cameraScale = ok && scale > 0.0 ? scale : 1.0;
        view.cameraPosition = vector(state.firstChildElement(QStringLiteral("CameraPosition")));
        view.cameraRotation = vector(state.firstChildElement(QStringLiteral("CameraRotation")));
        if (state.attribute(QStringLiteral("Active")) == QLatin1String("1")) setup.activeView = place;
        setup.views.append(view);
    }
    setup.present = !setup.views.isEmpty();
    return setup;
}

void readCompositionAsset(const QDomElement& compositionAsset,
                          composition::Composition* composition,
                          const QMap<QString, QString>& assetPathByGuid,
                          const QSet<QString>& shotIds, const QDir& projectDir,
                          media::MediaManager* media)
{
    const QString compositionId = elemText(compositionAsset, QStringLiteral("ID"));
    if (!compositionId.isEmpty()) composition->setId(core::Identifier(compositionId));
    composition->setCurrentFrame(elemText(compositionAsset, QStringLiteral("CTI")).toLongLong());
    composition->setViewerSetup(readViewerState(compositionAsset.firstChildElement(QStringLiteral("ViewerState"))));
    {
        // <InPoint>/<OutPoint>; older saves of this port spelled them In/Out.
        const bool native = !compositionAsset.firstChildElement(QStringLiteral("OutPoint")).isNull();
        bool hasOut = false;
        const long long out = elemText(compositionAsset, native ? QStringLiteral("OutPoint")
                                                                : QStringLiteral("Out"))
                                  .toLongLong(&hasOut);
        composition->setWorkArea(elemText(compositionAsset, native ? QStringLiteral("InPoint")
                                                                   : QStringLiteral("In"))
                                     .toLongLong(),
                                 hasOut ? out : -1);
    }

    const QString compName = elemText(compositionAsset, QStringLiteral("Name"));
    if (!compName.isEmpty()) {
        composition->setName(compName);
    }

    double fps = 30.0;
    // The shot's own length, when the file states one. Layers are allowed to
    // run past it - project_1's music layer is 256 s in a 35 s shot - and
    // addClip() below would otherwise stretch the shot over them, so a mere
    // open-and-save changed the project.
    double statedDuration = -1.0;
    QDomElement avs = compositionAsset.firstChildElement(QStringLiteral("AudioVideoSettings"));
    if (!avs.isNull()) {
        const QString fr = elemText(avs, QStringLiteral("FrameRate"));
        if (!fr.isEmpty()) {
            bool ok = false;
            const double v = fr.toDouble(&ok);
            if (ok && v > 0.0) {
                fps = v;
                // 29.97 stays NTSC rather than becoming 30.
                int numerator = 30, denominator = 1;
                composition::Composition::frameRateFraction(fps, &numerator, &denominator);
                composition->setFrameRate(numerator, denominator);
            }
        }
        // <FrameCount> is not always written; this project instead carries the
        // range on the composition itself, and without the fallback the timeline
        // kept its default length while the layers ran far past it.
        const QString frameCount = elemText(avs, QStringLiteral("FrameCount"));
        if (!frameCount.isEmpty()) {
            statedDuration = frameToSeconds(frameCount.toLongLong(), fps);
            composition->setDurationSeconds(statedDuration);
        } else {
            const long long inPoint = elemText(compositionAsset, QStringLiteral("InPoint")).toLongLong();
            const long long outPoint =
                elemText(compositionAsset, QStringLiteral("OutPoint")).toLongLong();
            if (outPoint > inPoint) {
                statedDuration = frameToSeconds(outPoint - inPoint, fps);
                composition->setDurationSeconds(statedDuration);
            }
        }
        const QString width = elemText(avs, QStringLiteral("Width"));
        const QString height = elemText(avs, QStringLiteral("Height"));
        if (!width.isEmpty() && !height.isEmpty()) {
            composition->setSize(width.toInt(), height.toInt());
        }
        // <PAR> is the reference's PAR enum, <PARCustom> the value of a
        // Custom one; <AudioSampleRate> in Hz.
        bool parOk = false, customOk = false;
        const int par = elemText(avs, QStringLiteral("PAR")).toInt(&parOk);
        const double custom = elemText(avs, QStringLiteral("PARCustom")).toDouble(&customOk);
        if (parOk) composition->setPixelAspect(par, customOk ? custom : 1.0);
        composition->setAudioSampleRate(elemText(avs, QStringLiteral("AudioSampleRate")).toInt());
    }
    readRenderSettings(compositionAsset.firstChildElement(QStringLiteral("RenderSettings")),
                       &composition->renderSettings());

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
        // A later clip of a layer of this port, written as a layer of its own
        // for the reference: the layer's OpenVegasClips already holds it.
        if (lw.hasAttribute(QStringLiteral("OpenVegasSliceOf"))) continue;
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
        // LayerBase carries ParentLayerID, Locked, Muted and MotionBlurOn in the
        // reference's own files; the OpenVegas* attributes are what older
        // saves of this port wrote and still win when present.
        static const QString kNullId = QStringLiteral("00000000-0000-0000-0000-000000000000");
        QString parentLayerId = lw.attribute(QStringLiteral("OpenVegasParentLayerID"));
        if (parentLayerId.isEmpty()) parentLayerId = elemText(named, QStringLiteral("ParentLayerID"));
        if (!parentLayerId.isEmpty() && parentLayerId != kNullId) {
            layer.parentLayerId = core::Identifier(parentLayerId);
        }
        layer.kind = layerKind;
        layer.zIndex = z++;
        layer.visible = elemText(named, QStringLiteral("Visible")) != QStringLiteral("0");
        layer.locked = named.hasAttribute(QStringLiteral("OpenVegasLocked"))
            ? named.attribute(QStringLiteral("OpenVegasLocked")) == QStringLiteral("1")
            : elemText(named, QStringLiteral("Locked")) == QStringLiteral("1");
        layer.muted = lw.hasAttribute(QStringLiteral("OpenVegasMuted"))
            ? lw.attribute(QStringLiteral("OpenVegasMuted")) == QStringLiteral("1")
            : elemText(named, QStringLiteral("Muted")) == QStringLiteral("1");
        layer.motionBlur = elemText(named, QStringLiteral("MotionBlurOn")) == QStringLiteral("1");
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
        // <AutoOrient>, <AutoOrientPathAxis> and <AutoOrientLayer>, as
        // AssetLayer, CameraLayer, LightLayer and PointLayer keep them.
        {
            bool modeOk = false;
            const int mode = elemText(lw, QStringLiteral("AutoOrient")).toInt(&modeOk);
            if (modeOk && (mode == 0 || mode == 1 || mode == 2 || mode == 4 || mode == 5))
                layer.autoOrient = composition::AutoOrient(mode);
            bool axisOk = false;
            const int axis = elemText(lw, QStringLiteral("AutoOrientPathAxis")).toInt(&axisOk);
            if (axisOk && axis >= 1 && axis <= 3) layer.autoOrientAxis = axis;
            const QUuid towards(elemText(lw, QStringLiteral("AutoOrientLayer")));
            if (!towards.isNull()) layer.autoOrientLayer = core::Identifier(towards.toString(QUuid::WithoutBraces));
        }
        if (layerKind == composition::LayerKind::Camera) {
            // CameraLayer: its lens properties and the elements beside them.
            composition::CameraSettings& camera = layer.camera;
            camera.zoom = propScalar(pm, QStringLiteral("zoom"), camera.zoom);
            camera.zoomCurve = propAnimation(propElement(pm, QStringLiteral("zoom")), fps, QString());
            camera.focusDistance = propScalar(pm, QStringLiteral("focusDistance"), camera.focusDistance);
            camera.focusDistanceCurve = propAnimation(propElement(pm, QStringLiteral("focusDistance")), fps, QString());
            camera.aperture = propScalar(pm, QStringLiteral("aperture"), camera.aperture);
            camera.apertureCurve = propAnimation(propElement(pm, QStringLiteral("aperture")), fps, QString());
            camera.blur = propScalar(pm, QStringLiteral("blurDoF"), camera.blur);
            camera.blurCurve = propAnimation(propElement(pm, QStringLiteral("blurDoF")), fps, QString());
            const auto number = [&](const char* name, double fallback) {
                bool ok = false;
                const double value = elemText(lw, QString::fromLatin1(name)).toDouble(&ok);
                return ok && std::isfinite(value) ? value : fallback;
            };
            camera.depthOfField = number("HasDoF", camera.depthOfField ? 1 : 0) != 0.0;
            camera.nearClip = number("NearClip", camera.nearClip);
            camera.farClip = number("FarClip", camera.farClip);
            camera.filmWidth = number("HorizontalFilmSize", camera.filmWidth);
            camera.filmHeight = number("VerticalFilmSize", camera.filmHeight);
        }
        if (layerKind == composition::LayerKind::Light) {
            // LightLayer: its <LightType> and its own properties.
            composition::LightSettings& light = layer.light;
            bool typeOk = false;
            const int type = elemText(lw, QStringLiteral("LightType")).toInt(&typeOk);
            if (typeOk && type >= 1 && type <= 4) light.type = composition::LightSettings::Type(type);
            light.color = propColor(pm, QStringLiteral("color"), light.color);
            light.intensity = propScalar(pm, QStringLiteral("intensity"), light.intensity);
            light.intensityCurve = propAnimation(propElement(pm, QStringLiteral("intensity")), fps, QString());
            light.castShadows = propBool(pm, QStringLiteral("castShadows"), light.castShadows);
            const int falloff = propInt(pm, QStringLiteral("falloff"), int(light.falloff));
            if (falloff >= 1 && falloff <= 3) light.falloff = composition::LightSettings::Falloff(falloff);
            light.reach = propScalar(pm, QStringLiteral("reach"), light.reach);
            light.coneAngle = propScalar(pm, QStringLiteral("spotlightAngle"), light.coneAngle);
            light.feather = propScalar(pm, QStringLiteral("spotlightFeather"), light.feather);
            light.shadowOpacity = propScalar(pm, QStringLiteral("shadowOpacity"), light.shadowOpacity);
            light.shadowDiffusion = propScalar(pm, QStringLiteral("shadowDiffusion"), light.shadowDiffusion);
            light.shadowColor = propColor(pm, QStringLiteral("shadowColor"), light.shadowColor);
            const auto keys = [&](const char* name) {
                return propAnimation(propElement(pm, QString::fromLatin1(name)), fps, QString());
            };
            light.colorCurve = propColorAnimation(propElement(pm, QStringLiteral("color")), fps);
            light.reachCurve = keys("reach");
            light.coneAngleCurve = keys("spotlightAngle");
            light.featherCurve = keys("spotlightFeather");
            light.shadowOpacityCurve = keys("shadowOpacity");
            light.shadowDiffusionCurve = keys("shadowDiffusion");
            light.shadowColorCurve = propColorAnimation(propElement(pm, QStringLiteral("shadowColor")), fps);
        } else {
            layer.surface.illuminated = propBool(pm, QStringLiteral("illuminated"), layer.surface.illuminated);
            layer.surface.castShadows = propBool(pm, QStringLiteral("castShadows"), layer.surface.castShadows);
            layer.surface.receiveShadows = propBool(pm, QStringLiteral("receiveShadows"), layer.surface.receiveShadows);
            layer.surface.shadowColor = propColor(pm, QStringLiteral("shadowColor"), layer.surface.shadowColor);
            layer.surface.ambient = propScalar(pm, QStringLiteral("matAmbient"), layer.surface.ambient);
            layer.surface.diffuse = propScalar(pm, QStringLiteral("matDiffuse"), layer.surface.diffuse);
            layer.surface.specular = propScalar(pm, QStringLiteral("matSpecular"), layer.surface.specular);
            layer.surface.shininess = propScalar(pm, QStringLiteral("matShininess"), layer.surface.shininess);
            layer.surface.emissive = propColor(pm, QStringLiteral("matEmissiveColor"), layer.surface.emissive);
            composition::SurfaceShading& surface = layer.surface;
            surface.castShadowsIfInvisible =
                propBool(pm, QStringLiteral("castShadowsIfLayerDisabled"), surface.castShadowsIfInvisible);
            surface.billboardedLights = propBool(pm, QStringLiteral("billboardedLights"), surface.billboardedLights);
            surface.billboardedShadows = propBool(pm, QStringLiteral("billboardedShadows"), surface.billboardedShadows);
            surface.selfShadowBias = qBound(1.0, propScalar(pm, QStringLiteral("selfShadowBias"), surface.selfShadowBias),
                                            1000.0);
            surface.castAmbientOcclusion =
                propBool(pm, QStringLiteral("castAmbientOcclusion"), surface.castAmbientOcclusion);
            surface.receiveAmbientOcclusion =
                propBool(pm, QStringLiteral("receiveAmbientOcclusion"), surface.receiveAmbientOcclusion);
            surface.aoSamples = qBound(4, propInt(pm, QStringLiteral("aoSamples"), surface.aoSamples), 10);
            surface.aoSampleRadius = qBound(0, propInt(pm, QStringLiteral("aoSampleRadius"), surface.aoSampleRadius), 20);
            surface.aoIntensity = qBound(0.0, propScalar(pm, QStringLiteral("aoIntensity"), surface.aoIntensity), 100.0);
            surface.aoDepthScale = qBound(0.0, propScalar(pm, QStringLiteral("aoDepthScale"), surface.aoDepthScale), 100.0);
            surface.aoBias = qBound(0.0, propScalar(pm, QStringLiteral("aoBias"), surface.aoBias), 10.0);
            surface.aoBlurRadius = qBound(1, propInt(pm, QStringLiteral("aoBlurRadius"), surface.aoBlurRadius), 20);
            surface.aoDepthFalloff =
                qBound(0.0, propScalar(pm, QStringLiteral("aoDepthFallOff"), surface.aoDepthFalloff), 100.0);
            surface.includeSemiTransparentTriangles = propBool(pm, QStringLiteral("includeSemiTransparentTriangles"),
                                                               surface.includeSemiTransparentTriangles);
            surface.castReflections = propBool(pm, QStringLiteral("castReflections"), surface.castReflections);
            surface.castReflectionsIfInvisible =
                propBool(pm, QStringLiteral("castReflectionsIfLayerDisabled"), surface.castReflectionsIfInvisible);
            surface.receiveReflections = propBool(pm, QStringLiteral("receiveReflections"), surface.receiveReflections);
            surface.specularReflectivity =
                propScalar(pm, QStringLiteral("specularReflectivity"), surface.specularReflectivity);
            surface.diffuseReflectivity = propScalar(pm, QStringLiteral("diffuseReflectivity"), surface.diffuseReflectivity);
            const int environment = propInt(pm, QStringLiteral("environmentMapType"), int(surface.environmentMap));
            if (environment >= 1000 && environment <= 1003)
                surface.environmentMap = composition::SurfaceShading::EnvironmentMap(environment);
            surface.environmentMapLayer = core::Identifier(
                propValueHolder(propElement(pm, QStringLiteral("environmentMapLayerID"))).firstChildElement(QStringLiteral("id")).text());
            const auto keys = [&](const char* name) {
                return propAnimation(propElement(pm, QString::fromLatin1(name)), fps, QString());
            };
            surface.ambientCurve = keys("matAmbient");
            surface.diffuseCurve = keys("matDiffuse");
            surface.specularCurve = keys("matSpecular");
            surface.shininessCurve = keys("matShininess");
            surface.emissiveCurve = propColorAnimation(propElement(pm, QStringLiteral("matEmissiveColor")), fps);
            surface.specularReflectivityCurve = keys("specularReflectivity");
            surface.diffuseReflectivityCurve = keys("diffuseReflectivity");
        }
        // Audio > Level. Its keys are composition milliseconds like every
        // other layer property - project_1's music layer fades out over
        // 29.70-30.05 s of its 30 s shot - not times inside the media.
        const QDomElement levelProp = propElement(pm, QStringLiteral("audioLevel"));
        if (!levelProp.isNull()) {
            layer.transform.audioLevel = propScalar(pm, QStringLiteral("audioLevel"), 0.0);
            layer.transform.audioLevelCurve = propAnimation(levelProp, fps, QString());
        }
        // Written by this port before cameras kept their zoom: the vertical
        // angle, turned into the zoom that gives it in this shot.
        bool validCameraFov = false;
        const double cameraFov = lw.attribute(QStringLiteral("OpenVegasCameraFieldOfView")).toDouble(&validCameraFov);
        if (validCameraFov && std::isfinite(cameraFov) && layer.kind == composition::LayerKind::Camera
            && propElement(pm, QStringLiteral("zoom")).isNull())
            layer.camera.zoom = composition::CameraSettings::zoomForVerticalFieldOfView(
                qBound(1.0, cameraFov, 179.0), composition->displaySize().height());
        readMasksExtension(lw, &layer);
        readMotionTracksExtension(lw, &layer);

        // The 3D side, from the attributes the writer above put on the wrapper.
        // A file written by the reference says it in <Dimensions> instead.
        if (lw.attribute(QStringLiteral("OpenVegasDimension")) == QLatin1String("3D")) {
            layer.dimension = composition::LayerDimension::ThreeD;
            composition::LayerTransform& t = layer.transform;
            const auto attr = [&lw](const char* name, double fallback) {
                const QString value = lw.attribute(QString::fromLatin1(name));
                bool ok = false;
                const double parsed = value.toDouble(&ok);
                return ok ? parsed : fallback;
            };
            // Older saves of this port held the 3D values only here; the
            // native properties read above stand when an attribute is absent.
            t.positionZ = attr("OpenVegasPositionZ", t.positionZ);
            t.anchorPointZ = attr("OpenVegasAnchorZ", t.anchorPointZ);
            t.scaleZPercent = attr("OpenVegasScaleZ", t.scaleZPercent);
            t.rotationXDegrees = attr("OpenVegasRotationX", t.rotationXDegrees);
            t.rotationYDegrees = attr("OpenVegasRotationY", t.rotationYDegrees);
            t.orientationX = attr("OpenVegasOrientationX", t.orientationX);
            t.orientationY = attr("OpenVegasOrientationY", t.orientationY);
            t.orientationZ = attr("OpenVegasOrientationZ", t.orientationZ);
        } else if (!lw.hasAttribute(QStringLiteral("OpenVegasDimension"))
                   && elemText(lw, QStringLiteral("Dimensions")).toInt() != 0) {
            // The reference's own <Dimensions>: 0 is 2D, 1 3D, 2 "3D Unrolled".
            layer.dimension = composition::LayerDimension::ThreeD;
        }

        // A model layer needs its geometry back, not just the reference to it:
        // the mesh lives beside the asset and nothing else would reload it.
        const QString modelPath = resolveAssetPath(
            lw.attribute(QStringLiteral("OpenVegasModelPath")), projectDir, nullptr);
        if (layerKind == composition::LayerKind::Model3D && !modelPath.isEmpty() && media) {
            // Imported again as it was: the size and axes the import gave it
            // are what the layer's and its nodes' values are in.
            const core::Result imported = media->importModel(
                modelPath, readModelImportSettings(lw.firstChildElement(QStringLiteral("OpenVegasModelImport"))));
            if (imported.isFailure()) {
                OV_LOG_WARN(QStringLiteral("Could not reload 3D model %1: %2")
                                .arg(modelPath, imported.message()));
            } else {
                layer.modelAssetId = media->assetByFilePath(modelPath).id();
                // Its materials as they were edited.
                const QDomElement materials = lw.firstChildElement(QStringLiteral("Materials"));
                if (!materials.isNull()) {
                    const model3d::Mesh mesh = media->modelMesh(layer.modelAssetId);
                    QVector<model3d::Material> edited;
                    for (QDomElement element = materials.firstChildElement(QStringLiteral("Material"));
                         !element.isNull(); element = element.nextSiblingElement(QStringLiteral("Material"))) {
                        model3d::Material material;
                        material.name = elemText(element, QStringLiteral("Name"));
                        for (const model3d::Material& own : mesh.materials)
                            if (own.name == material.name) material = own;
                        readModelMaterial(element, &material);
                        edited.append(material);
                    }
                    if (!edited.isEmpty()) media->setModelMaterials(layer.modelAssetId, edited);
                }
            }
        }
        // Nodes given a transform of their own, written below.
        const QDomElement nodes = lw.firstChildElement(QStringLiteral("OpenVegasModelNodes"));
        for (QDomElement node = nodes.firstChildElement(QStringLiteral("Model3DTransformNode"));
             !node.isNull(); node = node.nextSiblingElement(QStringLiteral("Model3DTransformNode"))) {
            composition::ModelNodeTransform state;
            state.path = elemText(node, QStringLiteral("GroupName"));
            if (state.path.isEmpty()) continue;
            state.transform = readTransform(node.firstChildElement(QStringLiteral("PropertyManager")), fps);
            layer.modelNodes.append(state);
        }
        const QDomElement overrides = lw.firstChildElement(QStringLiteral("OverriddenModel3DMaterialList"));
        for (QDomElement element = overrides.firstChildElement(QStringLiteral("Model3DMaterialOverride"));
             !element.isNull();
             element = element.nextSiblingElement(QStringLiteral("Model3DMaterialOverride"))) {
            composition::ModelMaterialOverride entry;
            entry.material = elemText(element, QStringLiteral("Name"));
            if (entry.material.isEmpty()) continue;
            const QDomElement properties = element.firstChildElement(QStringLiteral("PropertyManager"));
            const auto value = [&](const QString& name, const QString& tag) {
                return propValueHolder(propElement(properties, name)).firstChildElement(tag).text();
            };
            for (int m = 0; m < composition::ModelMaterialOverride::kMapCount; ++m) {
                const auto which = composition::ModelMaterialOverride::Map(m);
                const QString key = QString::fromLatin1(composition::ModelMaterialOverride::mapKey(which));
                bool ok = false;
                const int source = value(key + QStringLiteral("MapSource"), QStringLiteral("i")).toInt(&ok);
                if (ok && source >= 0 && source <= 2)
                    entry.map(which).source = composition::ModelMaterialOverride::MapSource(source);
                entry.map(which).layer = core::Identifier(value(key + QStringLiteral("MapLayerID"), QStringLiteral("id")));
            }
            entry.bumpMapScale = propScalar(properties, QStringLiteral("bumpMapScale"), 1.0);
            layer.materialOverrides.append(entry);
        }
        // Animations playing on the model (written below).
        const QDomElement animations = lw.firstChildElement(QStringLiteral("OpenVegasModelAnimations"));
        for (QDomElement element = animations.firstChildElement(QStringLiteral("Model3DAnimationAssetInstance"));
             !element.isNull();
             element = element.nextSiblingElement(QStringLiteral("Model3DAnimationAssetInstance"))) {
            composition::ModelAnimationInstance instance;
            const QString id = elemText(element, QStringLiteral("ID"));
            if (!id.isEmpty()) instance.id = core::Identifier(id);
            instance.name = elemText(element, QStringLiteral("Name"));
            const QDomElement properties = element.firstChildElement(QStringLiteral("PropertyManager"));
            const auto flag = [&](const QString& name) {
                const QString text = propValueHolder(propElement(properties, name))
                                         .firstChildElement(QStringLiteral("b")).text().trimmed();
                return text == QLatin1String("1") || text.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;
            };
            instance.strength = propScalar(properties, QStringLiteral("strength"), 100.0);
            instance.strengthCurve =
                propAnimation(propElement(properties, QStringLiteral("strength")), fps, QString());
            instance.loop = flag(QStringLiteral("loop"));
            instance.customPlayback = flag(QStringLiteral("customPlayback"));
            instance.playback = propScalar(properties, QStringLiteral("playback"), 0.0);
            instance.playbackCurve =
                propAnimation(propElement(properties, QStringLiteral("playback")), fps, QString());
            const QDomElement asset = element.firstChildElement(QStringLiteral("Model3DAnimationAsset"));
            const QString stored = elemText(asset, QStringLiteral("Filename"));
            if (!stored.isEmpty()) instance.filePath = resolveAssetPath(stored, projectDir, nullptr);
            instance.animationIndex = qMax(0, elemText(asset, QStringLiteral("AnimationIndex")).toInt());
            instance.frameRateOverridden = elemText(asset, QStringLiteral("OverrideFrameRate")).toInt() != 0;
            bool rateOk = false;
            const double rate = elemText(asset, QStringLiteral("FrameRate")).toDouble(&rateOk);
            if (rateOk && rate > 0.0) instance.frameRate = rate;
            layer.modelAnimations.append(instance);
        }

        const long long start = elemText(named, QStringLiteral("StartFrame")).toLongLong();
        const long long end = elemText(named, QStringLiteral("EndFrame")).toLongLong();
        const long long len = (end > start) ? (end - start) : 0;
        // A layer without clips keeps its span. One from the start to the
        // shot's end stays open, following the shot's length; older saves of
        // this port wrote an empty one (EndFrame = StartFrame), the whole
        // shot.
        if (composition::layerHasSpan(layer.kind)) {
            layer.spanStartSeconds = frameToSeconds(qMax(0LL, start), fps);
            const bool toShotEnd = statedDuration > 0.0 && start <= 0
                                   && end >= qRound64(statedDuration * fps);
            layer.spanEndSeconds = end > start && !toShotEnd ? frameToSeconds(end, fps) : -1.0;
        }

        if (kind == QStringLiteral("AssetLayer")) {
            const QString assetGuid = elemText(lw, QStringLiteral("AssetID"));
            const QString path = assetPathByGuid.value(assetGuid);
            if (shotIds.contains(assetGuid)) {
                // An AssetLayer on another CompositionAsset: a nested shot,
                // tied to its object once every shot has been read.
                composition::Clip* clip = composition->addClip(
                    layerName, core::Identifier(QStringLiteral("composition:") + assetGuid),
                    frameToSeconds(start, fps), len > 0 ? frameToSeconds(len, fps) : 1.0);
                if (clip) clip->nestedCompositionId = core::Identifier(assetGuid);
            } else if (!path.isEmpty()) {
                const core::Identifier mediaId(QStringLiteral("media:") + path);
                composition::Clip* clip =
                    composition->addClip(layerName, mediaId, frameToSeconds(start, fps),
                                         len > 0 ? frameToSeconds(len, fps) : 1.0);
                if (clip) {
                    clip->startSeconds = frameToSeconds(start, fps);
                    clip->durationSeconds = len > 0 ? frameToSeconds(len, fps) : 1.0;
                }
            } else if (layerKind == composition::LayerKind::Plane) {
                // A plane makes its own picture; a clip with no media gives
                // it its range, as a new one gets in the timeline.
                composition->addClip(layerName, core::Identifier(), frameToSeconds(start, fps),
                                     len > 0 ? frameToSeconds(len, fps) : 1.0);
            }
        } else if (kind == QStringLiteral("GradeLayer")) {
            // Like a plane: no media, a range all the same. Point, Light,
            // Camera and Model3D layers carry no clip at all - they used to
            // come back with a text clip and a Text effect.
            composition->addClip(layerName, core::Identifier(), frameToSeconds(start, fps),
                                 len > 0 ? frameToSeconds(len, fps) : 1.0);
        } else if (kind == QStringLiteral("TextLayer")) {
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
                // The box height only stands in for the size when the file
                // has no <Formats> to say it.
                textFromTextBox(lw, &pixelSize);
                fx.parameterValues = composition::textStyleToParameters(
                    textStyleFromTextBox(lw, pixelSize));
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
            if (!readEffectsExtension(lw, &first)) {
                // A file of the reference: its own effect lists.
                for (const composition::Effect& effect : nativefx::readInstances(lw, named, fps))
                    first.effects.append(effect);
            }
            bool ok = false;
            const double source =
                lw.attribute(QStringLiteral("OpenVegasSourceStart")).toDouble(&ok);
            if (ok) {
                first.sourceStartSeconds = source;
            } else {
                // The reference's own in-point: the media frame (at the shot's
                // rate) shown at the layer's StartFrame. project_1's music layer
                // starts 1853 frames - 30.9 s - into the song; with its 1.0146
                // speed its 256 s layer then ends at 290.9 s, by the asset's
                // 291.3 s out-point.
                const QDomElement instanceStart =
                    lw.firstChildElement(QStringLiteral("AssetInstanceStart"));
                const long long frames = instanceStart.text().toLongLong(&ok);
                if (!instanceStart.isNull() && ok && frames > 0) {
                    first.sourceStartSeconds = frameToSeconds(frames, fps);
                }
            }
            double speed = lw.attribute(QStringLiteral("OpenVegasSpeed")).toDouble(&ok);
            if (!ok && !propElement(pm, QStringLiteral("speed")).isNull()) {
                // LayerBase's "speed" property, a <db> in the reference.
                speed = propValue(pm, QStringLiteral("speed"));
                ok = true;
            }
            if (ok && speed > 0.0) {
                first.speed = speed;
            }
            const double audioLevel = lw.attribute(QStringLiteral("OpenVegasAudioLevel")).toDouble(&ok);
            if (ok) first.audioLevel = audioLevel;
        }
        readClipsExtension(lw, &layer);
        // Key times are relative to the layer's start in the reference (Flux
        // evaluates a layer at AbstractLayer::GlobalFrameToRelativeTime: frame
        // - StartFrame); here they are on the shot's timeline. Saves of this
        // port without OpenVegasKeyTimes wrote them there already.
        const bool shotTimes = lw.hasAttribute(QStringLiteral("OpenVegasKind"))
                               && lw.attribute(QStringLiteral("OpenVegasKeyTimes")) != QLatin1String("layer");
        if (!shotTimes) composition::shiftLayerKeys(layer, int(start));
    }
    if (statedDuration > 0.0) composition->setDurationSeconds(statedDuration);
}


// The XML of a project, or of a composite shot file laid out as a one-shot
// project: a .vegfxcs holds <BiffCompositeShot> with the media the shot uses
// in <Assets>, then its own CompositionAsset (FUN_1402c8b10), and the
// project reader takes that as it is once both sit in Project/AssetList.
core::Result readProjectDocument(const QString& filePath, QDomDocument* doc, QByteArray* bytes)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return core::Result::fail(core::ResultStatus::MissingResource,
                                  QStringLiteral("Cannot open file for reading: %1").arg(filePath));
    }
    *bytes = file.readAll();
    file.close();
    const QDomDocument::ParseResult result =
        doc->setContent(QAnyStringView(QString::fromUtf8(*bytes)));
    if (!result) {
        return core::Result::fail(
            core::ResultStatus::OperationFailed,
            QStringLiteral("Invalid .vegfx (XML) at line %1: %2")
                .arg(result.errorLine)
                .arg(result.errorMessage));
    }
    const QDomElement shotRoot = doc->documentElement();
    if (shotRoot.tagName() != QLatin1String(kShotFileRoot)) return core::Result::ok();
    QDomDocument project;
    QDomElement root = project.createElement(QStringLiteral("VegasEffectsProject"));
    project.appendChild(root);
    QDomElement projectNode = project.createElement(QStringLiteral("Project"));
    root.appendChild(projectNode);
    QDomElement assetList = project.createElement(QStringLiteral("AssetList"));
    projectNode.appendChild(assetList);
    QDomElement assets = project.createElement(QStringLiteral("Assets"));
    assetList.appendChild(assets);
    const QDomElement media = shotRoot.firstChildElement(QStringLiteral("Assets"));
    for (QDomElement asset = media.firstChildElement(); !asset.isNull(); asset = asset.nextSiblingElement())
        assets.appendChild(project.importNode(asset, true));
    for (QDomElement shot = shotRoot.firstChildElement(QStringLiteral("CompositionAsset")); !shot.isNull();
         shot = shot.nextSiblingElement(QStringLiteral("CompositionAsset")))
        assets.appendChild(project.importNode(shot, true));
    *doc = project;
    return core::Result::ok();
}

} // namespace

static core::Result loadProjectImpl(const QString& filePath,
                                           composition::Composition* composition,
                                           media::MediaManager* media, QByteArray* screenLayout)
{
    if (screenLayout) screenLayout->clear();
    if (!composition || !media) {
        return core::Result::fail(core::ResultStatus::InvalidArgument,
                                  QStringLiteral("Null model pointers"));
    }

    QByteArray bytes;
    QDomDocument doc;
    if (const core::Result read = readProjectDocument(filePath, &doc, &bytes); read.isFailure())
        return read;

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
    auto source = std::make_shared<composition::NativeProjectSource>();
    source->document = bytes;

    const QString projectName = elemText(project, QStringLiteral("Name"));
    if (!projectName.isEmpty()) {
        composition->setName(projectName);
    }
    composition->setProjectId(core::Identifier(elemText(project, QStringLiteral("ID"))));
    readProjectSettings(project.firstChildElement(QStringLiteral("ProjectSettings")),
                        &composition->projectSettings());

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
        // A layer (or smart object source) of a Photoshop document: the
        // document is the Filename, the part of it this port's own element.
        const QString psdPart = elemText(a, QStringLiteral("OpenVegasPsdPart"));
        if (!filename.isEmpty()) {
            const QString stored = QDir::cleanPath(filename);
            bool relinked = false;
            filename = resolveAssetPath(stored, projectDir, &relinked) + psdPart;
            if (relinked) {
                OV_LOG_INFO(QStringLiteral("VEGFX media relinked beside the project: %1 -> %2")
                                .arg(stored, filename));
            }
            core::Result r = core::Result::ok();
            if (tag == QStringLiteral("Model3DAnimationAsset")) {
                // A file's animations in Media, apart from the same file as a model.
                QString animationPath;
                r = media->importModelAnimation(filename, &animationPath);
                filename = r.isSuccess() ? animationPath : filename + QLatin1String(media::kModelAnimationMarker);
                assetPathByGuid.insert(id, filename);
                source->assetIdByPath.insert(QDir::cleanPath(filename), id);
                if (r.isFailure()) media->registerMissingFile(filename);
                continue;
            }
            if (tag == QStringLiteral("MediaAsset")
                && elemText(a, QStringLiteral("IsImageSequence")).toInt() == 1) {
                // Rebuilt from the first still and the files now beside it.
                QString sequencePath;
                const double rate = elemText(a, QStringLiteral("FrameRate")).toDouble();
                r = media->importImageSequence(media::findImageSequence(filename),
                                               rate > 0.0 ? rate : 30.0, &sequencePath);
                filename = r.isSuccess() ? sequencePath
                                         : filename + QLatin1String(media::kImageSequenceMarker);
            } else {
                r = media->importFile(filename);
            }
            assetPathByGuid.insert(id, filename);
            source->assetIdByPath.insert(QDir::cleanPath(filename), id);
            if (filename.endsWith(QLatin1String(media::kImageSequenceMarker))) {
                // Clips name the sequence by its marked path; the first still
                // alone stays the single image it is.
                resolvedMediaPaths.insert(stored + QLatin1String(media::kImageSequenceMarker),
                                          filename);
            } else if (!psdPart.isEmpty()) {
                resolvedMediaPaths.insert(stored + psdPart, filename);
            } else {
                resolvedMediaPaths.insert(stored, filename);
            }
            if (r.isFailure()) {
                media->registerMissingFile(filename);
                OV_LOG_WARN(
                    QStringLiteral("VEGFX media not found on this system: %1").arg(filename));
            }
            if (media::MediaAsset* asset = media->assetByFilePathForEdit(filename)) {
                asset->setLabelColor(QColor(a.attribute(QStringLiteral("OpenVegasLabelColor"))));
                const QString proxy = a.attribute(QStringLiteral("OpenVegasProxy"));
                asset->setProxyMode(proxy == QLatin1String("Quality") ? media::ProxyMode::Quality
                                    : proxy == QLatin1String("Performance")
                                        ? media::ProxyMode::Performance : media::ProxyMode::None);
            }
            if (tag == QStringLiteral("MediaAsset")) {
                // Trimmer in/out points are serialized on MediaAsset as
                // <InPoint>/<OutPoint> (frames).
                if (media::MediaAsset* asset = media->assetByFilePathForEdit(filename)) {
                    asset->setTrimInPoint(elemText(a, QStringLiteral("InPoint")).toInt());
                    asset->setTrimOutPoint(elemText(a, QStringLiteral("OutPoint")).toInt());
                    // MediaOverrideOptions: each <OverrideX> and its value.
                    asset->setPixelAspectOverride(
                        elemText(a, QStringLiteral("OverridePAR")).toInt() != 0,
                        elemText(a, QStringLiteral("PAR")).toInt());
                    if (!asset->isImageSequence()) {
                        asset->setFrameRateOverride(
                            elemText(a, QStringLiteral("OverrideFrameRate")).toInt() != 0,
                            elemText(a, QStringLiteral("FrameRate")).toDouble());
                    }
                    asset->setAlphaOverride(elemText(a, QStringLiteral("OverrideAlpha")).toInt() != 0,
                                            elemText(a, QStringLiteral("AlphaMode")).toInt());
                    asset->setColorLevels(elemText(a, QStringLiteral("OverrideColorLevels")).toInt() != 0
                                              ? elemText(a, QStringLiteral("ColorLevels")).toInt() : 0);
                    asset->setColorSpace(elemText(a, QStringLiteral("OverrideColorSpace")).toInt() != 0
                                             ? elemText(a, QStringLiteral("ColorSpace")).toInt() : 0);
                    const QString hardware = elemText(a, QStringLiteral("HWAccelerate"));
                    asset->setHardwareDecoding(hardware.isEmpty() || hardware.toInt() != 0);
                    const QString audioStream = elemText(a, QStringLiteral("OpenVegasAudioStream"));
                    asset->setAudioStreamIndex(audioStream.isEmpty() ? -1 : audioStream.toInt());
                }
            } else if (media::MediaAsset* asset = media->assetByFilePathForEdit(filename)) {
                // An image's own <PAR> (ImageAsset::PixelAspectRatio) and its
                // alpha override (ImageAsset::OverriddenAlpha).
                const int par = elemText(a, QStringLiteral("PAR")).toInt();
                if (par > 0) asset->setPixelAspectOverride(true, par);
                asset->setAlphaOverride(elemText(a, QStringLiteral("OverrideAlpha")).toInt() != 0,
                                        elemText(a, QStringLiteral("AlphaMode")).toInt());
            }
        }
    }

    // ---- Composite shots ----
    // Every CompositionAsset of the asset list. The primary one (<IsPrimary>,
    // else the first) is read into `composition`, which also holds the
    // project; the others become the project's further shots.
    QVector<QDomElement> shotElements;
    QSet<QString> shotIds;
    int rootIndex = 0;
    for (QDomElement c = assets.firstChildElement(QStringLiteral("CompositionAsset")); !c.isNull();
         c = c.nextSiblingElement(QStringLiteral("CompositionAsset"))) {
        if (elemText(c, QStringLiteral("IsPrimary")) == QLatin1String("1")
            && elemText(shotElements.value(rootIndex), QStringLiteral("IsPrimary")) != QLatin1String("1"))
            rootIndex = shotElements.size();
        shotElements.append(c);
        shotIds.insert(elemText(c, QStringLiteral("ID")));
    }
    // Kept from here on, whatever else the file holds, so saving keeps it.
    composition->setNativeSource(source);
    if (shotElements.isEmpty()) {
        return core::Result::ok();
    }
    const QDomElement compositionAsset = shotElements.at(rootIndex);

    readCompositionAsset(compositionAsset, composition, assetPathByGuid, shotIds, projectDir,
                         media);
    composition->setPrimary(elemText(compositionAsset, QStringLiteral("IsPrimary")) == QLatin1String("1"));
    QVector<std::shared_ptr<composition::Composition>> shots;
    QHash<QString, std::shared_ptr<composition::Composition>> shotsById;
    for (int i = 0; i < shotElements.size(); ++i) {
        if (i == rootIndex) continue;
        auto shot = std::make_shared<composition::Composition>();
        readCompositionAsset(shotElements.at(i), shot.get(), assetPathByGuid, shotIds, projectDir,
                             media);
        shot->setPrimary(elemText(shotElements.at(i), QStringLiteral("IsPrimary"))
                         == QLatin1String("1"));
        if (shotsById.contains(shot->id().value()) || shot->id() == composition->id()) continue;
        shotsById.insert(shot->id().value(), shot);
        shots.append(shot);
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

    // Shots older saves of this port kept in their own block.
    readEmbeddedCompositions(compositionAsset, composition->id(), &shotsById, &shots);
    linkNestedShots(composition, shotsById);
    composition->setCompositeShots(shots);

    // <OpenCompositeShots>, beside <Project>: the Editor tabs and the one in
    // front (TimelineType 1404 is a composite shot timeline).
    {
        const QDomElement open = docRoot.firstChildElement(QStringLiteral("OpenCompositeShots"));
        QStringList openIds;
        for (QDomElement shot = open.firstChildElement(QStringLiteral("CompositeShot")); !shot.isNull();
             shot = shot.nextSiblingElement(QStringLiteral("CompositeShot"))) {
            const QString id = shot.attribute(QStringLiteral("CompositionId"));
            if (!id.isEmpty() && !openIds.contains(id)) openIds.append(id);
        }
        const QString active = open.attribute(QStringLiteral("TimelineType")).toInt() == kShotTimeline
            ? open.attribute(QStringLiteral("TimelineId")) : QString();
        composition->setOpenShots(openIds, active);
    }

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
    for (const auto& shot : shots) resolveClipPaths(shot.get());
    return core::Result::ok();
}

core::Result VegfxSerializer::loadFromFile(const QString& filePath,
                                          composition::Composition* composition,
                                          media::MediaManager* media, QByteArray* screenLayout)
{
    if (!composition || !media)
        return core::Result::fail(core::ResultStatus::InvalidArgument, QStringLiteral("Null model pointers"));
    composition::Composition stagedComposition;
    media::MediaManager stagedMedia;
    QByteArray stagedLayout;
    const core::Result result = loadProjectImpl(filePath, &stagedComposition, &stagedMedia, &stagedLayout);
    if (result.isFailure()) return result;
    media->replaceProjectAssets(std::move(stagedMedia));
    *composition = std::move(stagedComposition);
    if (screenLayout) *screenLayout = std::move(stagedLayout);
    return result;
}

// The project as this port models it, before it is laid over the document it
// was read from.
static QDomDocument projectDocument(const QString& filePath,
                                   const composition::Composition& composition,
                                   const media::MediaManager& media,
                                   const ProjectSaveOptions& options)
{
    const QDir projectDirectory(QFileInfo(filePath).absolutePath());
    // Absolute paths in the platform's own spelling, as the reference writes
    // them ("D:\Media\clip.mp4" on Windows); relative ones with '/', so a
    // project moved to another system still finds its media.
    const auto storedPath = [&](const QString& path) {
        return options.useRelativePaths && !path.isEmpty()
            ? projectDirectory.relativeFilePath(QFileInfo(path).absoluteFilePath())
            : QDir::toNativeSeparators(path);
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
    if (options.isAutoSave)
        addText(doc, project, QStringLiteral("OpenVegasAutoSaveOf"),
                QDir::toNativeSeparators(options.autoSaveOf));
    if (!options.screenLayout.isEmpty())
        addText(doc, project, QStringLiteral("OpenVegasScreenLayout"),
                QString::fromLatin1(options.screenLayout.toBase64()));

    // The project keeps its identity across saves, and like the reference
    // names itself after its file ("Project.vegfx").
    addText(doc, project, QStringLiteral("ID"), composition.projectId().value());
    addText(doc, project, QStringLiteral("Name"), QFileInfo(filePath).fileName());
    project.appendChild(projectSettingsElement(doc, composition.projectSettings()));

    const double fps =
        composition.fpsDenominator() > 0
            ? double(composition.fpsNumerator()) / double(composition.fpsDenominator())
            : 30.0;
    const long long frameCount =
        qMax(1LL, qRound64(composition.durationSeconds() * fps));

    QHash<QString, QString> guidByPath;
    // Media read from a file keeps the <ID> it had there.
    if (const auto source = composition.nativeSource()) {
        for (auto it = source->assetIdByPath.cbegin(); it != source->assetIdByPath.cend(); ++it)
            guidByPath.insert(it.key(), it.value());
    }
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

    // One <CompositionAsset> per composite shot, in the reference's own form.
    const auto compositionAssetElement = [&](const composition::Composition& shot) {
        const double shotFps = shot.fpsDenominator() > 0
            ? double(shot.fpsNumerator()) / double(shot.fpsDenominator()) : 30.0;
        const long long shotFrameCount = qMax(1LL, qRound64(shot.durationSeconds() * shotFps));
        QDomElement comp = doc.createElement(QStringLiteral("CompositionAsset"));
        comp.setAttribute(QStringLiteral("Version"), QStringLiteral("19"));
        addText(doc, comp, QStringLiteral("ID"), shot.id().value());
        addText(doc, comp, QStringLiteral("Name"),
                shot.name().isEmpty() ? QStringLiteral("Untitled") : shot.name());
        addText(doc, comp, QStringLiteral("CTI"), QString::number(shot.currentFrame()));
        addText(doc, comp, QStringLiteral("InPoint"), QString::number(shot.workIn()));
        addText(doc, comp, QStringLiteral("OutPoint"),
                QString::number(shot.workOut() >= 0 ? shot.workOut() : shotFrameCount));
        addText(doc, comp, QStringLiteral("IsPrimary"),
                shot.isPrimary() ? QStringLiteral("1") : QStringLiteral("0"));

        QDomElement avs = doc.createElement(QStringLiteral("AudioVideoSettings"));
        avs.setAttribute(QStringLiteral("Version"), QStringLiteral("1"));
        addText(doc, avs, QStringLiteral("FrameCount"), QStringLiteral("%1").arg(shotFrameCount));
        addText(doc, avs, QStringLiteral("AudioSampleRate"), QString::number(shot.audioSampleRate()));
        addText(doc, avs, QStringLiteral("Width"), QStringLiteral("%1").arg(shot.width()));
        addText(doc, avs, QStringLiteral("Height"), QStringLiteral("%1").arg(shot.height()));
        addText(doc, avs, QStringLiteral("PAR"), QString::number(shot.pixelAspect()));
        addText(doc, avs, QStringLiteral("PARCustom"),
                QString::number(shot.customPixelAspect(), 'g', 10));
        // As the reference writes it: "60", "29.97".
        addText(doc, avs, QStringLiteral("FrameRate"),
                QString::number(std::round(shotFps * 1000.0) / 1000.0, 'g', 10));
        comp.appendChild(avs);
        comp.appendChild(renderSettingsElement(doc, shot.renderSettings()));

        QDomElement layers = doc.createElement(QStringLiteral("Layers"));
        comp.appendChild(layers);

        const auto frameRound = [shotFps](double seconds) {
            return qMax(0LL, qRound64(seconds * shotFps));
        };

        // A layer of this port may hold several clips; a reference layer has
        // one range. Its second and later clips go out as layers of their own
        // as well - what the reference's Slice Layer makes (a copy of the
        // layer, its keys kept) - marked OpenVegasSliceOf so this port leaves
        // them out on reading: the first one's OpenVegasClips has them all.
        struct WrittenLayer
        {
            composition::Layer layer;
            QString sliceOf;
        };
        QVector<WrittenLayer> writtenLayers;
        for (const composition::Layer& shotLayer : shot.layers()) {
            writtenLayers.append({shotLayer, QString()});
            for (int c = 1; c < shotLayer.clips.size(); ++c) {
                composition::Layer slice = shotLayer;
                slice.clips = {shotLayer.clips.at(c)};
                // The same identity on every save of the same layer.
                slice.id = core::Identifier(
                    QUuid::createUuidV5(QUuid(shotLayer.id.value()),
                                        QStringLiteral("slice:%1").arg(c)).toString(QUuid::WithoutBraces));
                writtenLayers.append({slice, shotLayer.id.value()});
            }
        }
        for (const WrittenLayer& written : std::as_const(writtenLayers)) {
            const composition::Layer& shotLayer = written.layer;
            // Keys go out relative to the layer's start (its StartFrame below),
            // as the reference keeps them.
            composition::Layer layer = shotLayer;
            // A layer without clips lies by its span, open to the shot's end
            // when it has none of its own; one with clips by its first.
            const bool spanned = composition::layerHasSpan(layer);
            const long long layerStartFrame = spanned
                ? frameRound(layer.spanStartSeconds)
                : frameRound(layer.clips.isEmpty() ? 0.0 : layer.clips.first().startSeconds);
            const long long layerEndFrame = spanned
                ? qMax(layerStartFrame + 1,
                       layer.spanEndSeconds < 0.0 ? shotFrameCount : frameRound(layer.spanEndSeconds))
                : frameRound(layer.clips.isEmpty()
                                 ? 0.0 : layer.clips.first().startSeconds + layer.clips.first().durationSeconds);
            composition::shiftLayerKeys(layer, -int(layerStartFrame));
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
            if (!written.sliceOf.isEmpty())
                wrapper.setAttribute(QStringLiteral("OpenVegasSliceOf"), written.sliceOf);
            wrapper.setAttribute(QStringLiteral("OpenVegasKind"),
                                 composition::layerKindToken(layer.kind));
            wrapper.setAttribute(QStringLiteral("OpenVegasKeyTimes"), QStringLiteral("layer"));
            wrapper.setAttribute(QStringLiteral("OpenVegasMuted"), layer.muted ? 1 : 0);
            if (layer.kind == composition::LayerKind::Camera) {
                // CameraLayer's own elements: depth of field, clipping, film.
                addText(doc, wrapper, QStringLiteral("HasDoF"), layer.camera.depthOfField ? QStringLiteral("1") : QStringLiteral("0"));
                addText(doc, wrapper, QStringLiteral("NearClip"), QString::number(layer.camera.nearClip, 'g', 9));
                addText(doc, wrapper, QStringLiteral("FarClip"), QString::number(layer.camera.farClip, 'g', 9));
                addText(doc, wrapper, QStringLiteral("HorizontalFilmSize"), QString::number(layer.camera.filmWidth, 'g', 9));
                addText(doc, wrapper, QStringLiteral("VerticalFilmSize"), QString::number(layer.camera.filmHeight, 'g', 9));
            }
            if (layer.kind == composition::LayerKind::Light)
                addText(doc, wrapper, QStringLiteral("LightType"), QString::number(int(layer.light.type)));
            if (layer.kind == composition::LayerKind::Media || layer.kind == composition::LayerKind::Plane || text
                || layer.kind == composition::LayerKind::Camera || layer.kind == composition::LayerKind::Light
                || layer.kind == composition::LayerKind::Point) {
                // The layer's Alignment, as its own element keeps it.
                addText(doc, wrapper, QStringLiteral("AutoOrient"), QString::number(int(layer.autoOrient)));
                if (layer.kind != composition::LayerKind::Point)
                    addText(doc, wrapper, QStringLiteral("AutoOrientPathAxis"), QString::number(layer.autoOrientAxis));
                addText(doc, wrapper, QStringLiteral("AutoOrientLayer"),
                        layer.autoOrientLayer.isValid() ? layer.autoOrientLayer.value()
                                                        : QStringLiteral("00000000-0000-0000-0000-000000000000"));
            }
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
                if (media.hasModel(layer.modelAssetId)) {
                    wrapper.appendChild(modelImportSettingsElement(doc, media.modelSettings(layer.modelAssetId)));
                    // The materials as the Materials tab left them.
                    QDomElement materials = doc.createElement(QStringLiteral("Materials"));
                    const QString path = mediaPathFromId(layer.modelAssetId);
                    for (const model3d::Material& material : media.modelMesh(layer.modelAssetId).materials) {
                        const QUuid id = QUuid::createUuidV5(QUuid(QStringLiteral("{6ba7b811-9dad-11d1-80b4-00c04fd430c8}")),
                                                             path + QLatin1Char('/') + material.name);
                        materials.appendChild(modelMaterialElement(doc, material, id.toString(QUuid::WithoutBraces)));
                    }
                    wrapper.appendChild(materials);
                }
            }
            if (layer.kind == composition::LayerKind::Model3D && !layer.modelNodes.isEmpty()) {
                // The nodes given a transform of their own, each as the
                // reference writes a Model3DTransformNode (Version 3, its
                // group's name, a PropertyManager with the node's 3D
                // transform). The reference nests them in a
                // Model3DAssetInstance this port does not write, so they
                // travel under its own prefix, keyed by the node's path.
                QDomElement nodes = doc.createElement(QStringLiteral("OpenVegasModelNodes"));
                for (const composition::ModelNodeTransform& node : layer.modelNodes) {
                    QDomElement nodeElement = doc.createElement(QStringLiteral("Model3DTransformNode"));
                    nodeElement.setAttribute(QStringLiteral("Version"), QStringLiteral("3"));
                    addText(doc, nodeElement, QStringLiteral("GroupName"), node.path);
                    const composition::LayerTransform& nt = node.transform;
                    QDomElement properties = doc.createElement(QStringLiteral("PropertyManager"));
                    properties.setAttribute(QStringLiteral("Version"), QStringLiteral("7"));
                    appendPointProp(doc, properties, QStringLiteral("anchorPoint"), QStringLiteral("p3"),
                                    nt.anchorPoint, composition::KeyFrameList(),
                                    composition::KeyFrameList(), shotFps, nt.anchorPointZ);
                    appendPointProp(doc, properties, QStringLiteral("position"), QStringLiteral("p3"),
                                    nt.position, nt.positionXCurve, nt.positionYCurve, shotFps,
                                    nt.positionZ, nt.positionZCurve);
                    appendPointProp(doc, properties, QStringLiteral("scale"), QStringLiteral("sc"),
                                    nt.scalePercent, nt.scaleXCurve, nt.scaleYCurve, shotFps,
                                    nt.scaleZPercent, nt.scaleZCurve);
                    appendPointProp(doc, properties, QStringLiteral("orientation"), QStringLiteral("or"),
                                    QPointF(nt.orientationX, nt.orientationY), nt.orientationXCurve,
                                    nt.orientationYCurve, shotFps, nt.orientationZ, nt.orientationZCurve);
                    appendScalarProp(doc, properties, QStringLiteral("rotationX"), nt.rotationXDegrees,
                                     nt.rotationXCurve, shotFps);
                    appendScalarProp(doc, properties, QStringLiteral("rotationY"), nt.rotationYDegrees,
                                     nt.rotationYCurve, shotFps);
                    appendScalarProp(doc, properties, QStringLiteral("rotationZ"), nt.rotationDegrees,
                                     nt.rotationCurve, shotFps);
                    nodeElement.appendChild(properties);
                    nodes.appendChild(nodeElement);
                }
                wrapper.appendChild(nodes);
            }
            if (layer.kind == composition::LayerKind::Model3D && !layer.materialOverrides.isEmpty()) {
                // Override Materials, as the reference writes them: an
                // OverriddenModel3DMaterialList of Model3DMaterialOverride
                // (Version 6: ID, Model3DMaterialID, Model3DAssetID, Name and a
                // PropertyManager with <map>MapSource / <map>MapLayerID). The
                // material is named; its ID in the reference is the asset's.
                QDomElement list = doc.createElement(QStringLiteral("OverriddenModel3DMaterialList"));
                for (const composition::ModelMaterialOverride& entry : layer.materialOverrides) {
                    QDomElement overrideElement = doc.createElement(QStringLiteral("Model3DMaterialOverride"));
                    overrideElement.setAttribute(QStringLiteral("Version"), QStringLiteral("6"));
                    addText(doc, overrideElement, QStringLiteral("Name"), entry.material);
                    QDomElement properties = doc.createElement(QStringLiteral("PropertyManager"));
                    properties.setAttribute(QStringLiteral("Version"), QStringLiteral("7"));
                    const auto prop = [&](const QString& name, const QString& tag, const QString& value) {
                        QDomElement p = doc.createElement(QStringLiteral("Prop"));
                        addText(doc, p, QStringLiteral("Name"), name);
                        QDomElement stat = doc.createElement(QStringLiteral("Static"));
                        addText(doc, stat, tag, value);
                        p.appendChild(stat);
                        properties.appendChild(p);
                    };
                    for (int m = 0; m < composition::ModelMaterialOverride::kMapCount; ++m) {
                        const auto which = composition::ModelMaterialOverride::Map(m);
                        const QString key = QString::fromLatin1(composition::ModelMaterialOverride::mapKey(which));
                        prop(key + QStringLiteral("MapSource"), QStringLiteral("i"),
                             QString::number(int(entry.map(which).source)));
                        prop(key + QStringLiteral("MapLayerID"), QStringLiteral("id"),
                             entry.map(which).layer.value());
                    }
                    appendScalarProp(doc, properties, QStringLiteral("bumpMapScale"), entry.bumpMapScale,
                                     composition::KeyFrameList(), shotFps);
                    overrideElement.appendChild(properties);
                    list.appendChild(overrideElement);
                }
                wrapper.appendChild(list);
            }
            if (layer.kind == composition::LayerKind::Model3D && !layer.modelAnimations.isEmpty()) {
                // Animations, each as the reference writes a
                // Model3DAnimationAssetInstance (Version 1: ID, Name, a
                // PropertyManager with strength, loop, playback and
                // customPlayback) with what its Model3DAnimationAsset keeps
                // (Version 3: Name, Filename, OverrideFrameRate, FrameRate,
                // AnimationIndex) inside it. The reference keeps the asset in
                // the project's asset list and the instance in the layer's
                // Model3DAssetInstance, which this port does not write.
                QDomElement list = doc.createElement(QStringLiteral("OpenVegasModelAnimations"));
                for (const composition::ModelAnimationInstance& instance : layer.modelAnimations) {
                    QDomElement instanceElement = doc.createElement(QStringLiteral("Model3DAnimationAssetInstance"));
                    instanceElement.setAttribute(QStringLiteral("Version"), QStringLiteral("1"));
                    addText(doc, instanceElement, QStringLiteral("ID"), instance.id.value());
                    addText(doc, instanceElement, QStringLiteral("Name"), instance.name);
                    QDomElement properties = doc.createElement(QStringLiteral("PropertyManager"));
                    properties.setAttribute(QStringLiteral("Version"), QStringLiteral("7"));
                    const auto flag = [&](const QString& name, bool value) {
                        QDomElement p = doc.createElement(QStringLiteral("Prop"));
                        p.setAttribute(QStringLiteral("Type"), 1);
                        addText(doc, p, QStringLiteral("Name"), name);
                        QDomElement stat = doc.createElement(QStringLiteral("Static"));
                        addText(doc, stat, QStringLiteral("b"), value ? QStringLiteral("1") : QStringLiteral("0"));
                        p.appendChild(stat);
                        properties.appendChild(p);
                    };
                    flag(QStringLiteral("loop"), instance.loop);
                    appendScalarProp(doc, properties, QStringLiteral("strength"), instance.strength,
                                     instance.strengthCurve, shotFps);
                    appendScalarProp(doc, properties, QStringLiteral("playback"), instance.playback,
                                     instance.playbackCurve, shotFps);
                    flag(QStringLiteral("customPlayback"), instance.customPlayback);
                    instanceElement.appendChild(properties);
                    QDomElement asset = doc.createElement(QStringLiteral("Model3DAnimationAsset"));
                    asset.setAttribute(QStringLiteral("Version"), QStringLiteral("3"));
                    addText(doc, asset, QStringLiteral("Name"), instance.name);
                    if (!instance.filePath.isEmpty())
                        addText(doc, asset, QStringLiteral("Filename"), storedPath(instance.filePath));
                    addText(doc, asset, QStringLiteral("OverrideFrameRate"),
                            QString::number(instance.frameRateOverridden ? 1 : 0));
                    addText(doc, asset, QStringLiteral("FrameRate"), QString::number(instance.frameRate, 'g', 6));
                    addText(doc, asset, QStringLiteral("AnimationIndex"), QString::number(instance.animationIndex));
                    instanceElement.appendChild(asset);
                    list.appendChild(instanceElement);
                }
                wrapper.appendChild(list);
            }

            // Only a media layer references an asset. Point/Light/Camera have no
            // clip at all and Grade/Plane produce their own picture, so writing an
            // AssetID for them would mint a GUID for an empty path.
            QString assetGuid;
            const core::Identifier nestedShotId =
                clip.nestedComposition ? clip.nestedComposition->id() : clip.nestedCompositionId;
            if (!text && layer.kind == composition::LayerKind::Media && nestedShotId.isValid()) {
                // A nested shot is an AssetLayer on the CompositionAsset itself.
                addText(doc, wrapper, QStringLiteral("AssetID"), nestedShotId.value());
                addText(doc, wrapper, QStringLiteral("AssetInstanceStart"),
                        QString::number(qMax(0LL, qRound64(clip.sourceStartSeconds * shotFps))));
            } else if (!text && layer.kind == composition::LayerKind::Media
                && !mediaPathFromId(clip.mediaId).isEmpty()) {
                const QString path = mediaPathFromId(clip.mediaId);
                assetGuid = guidByPath.value(QDir::cleanPath(path));
                if (assetGuid.isEmpty()) {
                    assetGuid = guidForPath(path);
                    guidByPath.insert(QDir::cleanPath(path), assetGuid);
                }
                addText(doc, wrapper, QStringLiteral("AssetID"), assetGuid);
                addText(doc, wrapper, QStringLiteral("AssetInstanceStart"),
                        QString::number(qMax(0LL, qRound64(clip.sourceStartSeconds * shotFps))));
            }
            if (text || layer.kind == composition::LayerKind::Media
                || layer.kind == composition::LayerKind::Plane
                || layer.kind == composition::LayerKind::Grade) {
                addText(doc, wrapper, QStringLiteral("Dimensions"),
                        layer.dimension == composition::LayerDimension::ThreeD ? QStringLiteral("1")
                                                                               : QStringLiteral("0"));
            }

            if (text) {
                composition::TextStyle style;
                for (const composition::Effect& effect : clip.effects) {
                    if (effect.pluginId.value() == QLatin1String("text")) {
                        style = composition::textStyleFromParameters(effect.parameterValues);
                        break;
                    }
                }
                // The reference's own TextBox and Formats; what has no native
                // home (outlines, extra strokes) travels in the extension below.
                wrapper.appendChild(textBoxElement(doc, style, layer.id));
                QDomElement savedStyle = doc.createElement(QStringLiteral("OpenVegasTextStyle"));
                savedStyle.setAttribute(QStringLiteral("Version"), 1);
                for (const QString& value : composition::textStyleToParameters(style))
                    addText(doc, savedStyle, QStringLiteral("Value"), value);
                wrapper.appendChild(savedStyle);
            }

            // The reference's own effect lists; the extension below stays the
            // port's lossless copy (and the only one of its built-in effects).
            QDomElement effectsList = doc.createElement(QStringLiteral("Effects"));
            QDomElement geometryList = doc.createElement(QStringLiteral("GeometryEffects"));
            QDomElement behaviorList = doc.createElement(QStringLiteral("BehaviorEffects"));
            {
                QSizeF layerSize(shot.width(), shot.height());
                if (layer.kind == composition::LayerKind::Media && clip.mediaId.isValid()) {
                    const QSize frame = media.assetById(clip.mediaId).frameSize();
                    if (frame.isValid() && !frame.isEmpty()) layerSize = QSizeF(frame);
                }
                for (int e = 0; e < clip.effects.size(); ++e) {
                    const composition::Effect& effect = clip.effects.at(e);
                    if (effect.isTransition()) continue;
                    if (!effect.nativeXml.isEmpty()) {
                        QDomElement& list = effect.nativeContainer == QLatin1String("GeometryEffects") ? geometryList
                            : effect.nativeContainer == QLatin1String("BehaviorEffects") ? behaviorList : effectsList;
                        nativefx::appendVerbatim(doc, list, effect.nativeXml);
                        continue;
                    }
                    const std::optional<project::NativeEffectEntry> entry =
                        project::findNativeEffect(effect.pluginId.value());
                    if (!entry || entry->guid.isEmpty()) continue;
                    QDomElement* list = nullptr;
                    switch (entry->container) {
                    case project::NativeEffectEntry::Container::Effects: list = &effectsList; break;
                    case project::NativeEffectEntry::Container::GeometryEffects: list = &geometryList; break;
                    case project::NativeEffectEntry::Container::BehaviorEffects: list = &behaviorList; break;
                    case project::NativeEffectEntry::Container::None: break;
                    }
                    if (!list) continue;
                    // The same identity on every save of the same effect.
                    const QString instanceId = effect.instanceId.isValid()
                        ? effect.instanceId.value()
                        : QUuid::createUuidV5(QUuid(layer.id.value()), QStringLiteral("effect:%1").arg(e))
                              .toString(QUuid::WithoutBraces);
                    nativefx::appendInstance(doc, *list, *entry, effect, instanceId, layer.id.value(),
                                             shot.id().value(), layerSize, shotFps);
                }
            }
            wrapper.appendChild(effectsList);
            if (text || !geometryList.firstChildElement().isNull()) wrapper.appendChild(geometryList);
            appendEffectsExtension(doc, wrapper, clip);
            const bool hasNestedClip = std::any_of(
                layer.clips.cbegin(), layer.clips.cend(), [](const composition::Clip& c) {
                    return bool(c.nestedComposition) || c.nestedCompositionId.isValid();
                });
            if (written.sliceOf.isEmpty()) appendClipsExtension(doc, wrapper, layer.clips, hasNestedClip);
            appendMasksExtension(doc, wrapper, layer.masks);
            appendMotionTracksExtension(doc, wrapper, layer.motionTracks);

            QDomElement named = doc.createElement(QStringLiteral("LayerBase"));
            named.setAttribute(QStringLiteral("Version"), QStringLiteral("2"));
            addText(doc, named, QStringLiteral("ID"), layer.id.value());
            addText(doc, named, QStringLiteral("Name"), layer.name);
            addText(doc, named, QStringLiteral("ParentLayerID"),
                    layer.parentLayerId.isValid() ? layer.parentLayerId.value()
                                                  : QStringLiteral("00000000-0000-0000-0000-000000000000"));
            addText(doc, named, QStringLiteral("StartFrame"), QStringLiteral("%1").arg(layerStartFrame));
            addText(doc, named, QStringLiteral("EndFrame"), QStringLiteral("%1").arg(layerEndFrame));
            // Mirrors the read above: the default goes back out as 0.
            addText(doc, named, QStringLiteral("BlendMode"),
                    layer.blendMode == QStringLiteral("None") ? QStringLiteral("0")
                                                              : layer.blendMode);
            addText(doc, named, QStringLiteral("Visible"), layer.visible ? QStringLiteral("1") : QStringLiteral("0"));
            addText(doc, named, QStringLiteral("Muted"), layer.muted ? QStringLiteral("1") : QStringLiteral("0"));
            addText(doc, named, QStringLiteral("Locked"), layer.locked ? QStringLiteral("1") : QStringLiteral("0"));
            addText(doc, named, QStringLiteral("MotionBlurOn"),
                    layer.motionBlur ? QStringLiteral("1") : QStringLiteral("0"));
            named.setAttribute(QStringLiteral("OpenVegasLocked"), layer.locked ? QStringLiteral("1") : QStringLiteral("0"));
            named.setAttribute(QStringLiteral("OpenVegasLabelColor"), layer.labelColor.name());
            if (layer.parentLayerId.isValid()) {
                // LayerBase/ParentLayerID above is the native holder; the
                // extension stays so older builds of this port still read it.
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
                             qBound(0.0, layer.opacity * 100.0, 100.0), t.opacityCurve, shotFps);
            // Anchor Point carries no curve in this model, so it is written static.
            appendPointProp(doc, pmEl, QStringLiteral("anchorPoint"), QStringLiteral("p3"),
                            t.anchorPoint, composition::KeyFrameList(), composition::KeyFrameList(),
                            shotFps, t.anchorPointZ);
            appendPointProp(doc, pmEl, QStringLiteral("position"), QStringLiteral("p3"), t.position,
                            t.positionXCurve, t.positionYCurve, shotFps, t.positionZ, t.positionZCurve);
            if (layer.kind == composition::LayerKind::Camera || layer.kind == composition::LayerKind::Light
                || layer.kind == composition::LayerKind::Point) {
                appendPointProp(doc, pmEl, QStringLiteral("target"), QStringLiteral("p3"), QPointF(t.targetX, t.targetY),
                                t.targetXCurve, t.targetYCurve, shotFps, t.targetZ, t.targetZCurve);
            }
            appendPointProp(doc, pmEl, QStringLiteral("scale"), QStringLiteral("sc"), t.scalePercent,
                            t.scaleXCurve, t.scaleYCurve, shotFps, t.scaleZPercent, t.scaleZCurve);
            // The 3D side in the reference's own properties; a 2D layer carries
            // zeros there, as the reference's files do.
            appendPointProp(doc, pmEl, QStringLiteral("orientation"), QStringLiteral("or"),
                            QPointF(t.orientationX, t.orientationY), t.orientationXCurve,
                            t.orientationYCurve, shotFps, t.orientationZ, t.orientationZCurve);
            appendScalarProp(doc, pmEl, QStringLiteral("rotationX"), t.rotationXDegrees,
                             t.rotationXCurve, shotFps);
            appendScalarProp(doc, pmEl, QStringLiteral("rotationY"), t.rotationYDegrees,
                             t.rotationYCurve, shotFps);
            appendScalarProp(doc, pmEl, QStringLiteral("rotationZ"), t.rotationDegrees,
                             t.rotationCurve, shotFps);
            if (layer.kind == composition::LayerKind::Camera) {
                // CameraLayer's lens, by the reference's names.
                const composition::CameraSettings& camera = layer.camera;
                appendScalarProp(doc, pmEl, QStringLiteral("aperture"), camera.aperture, camera.apertureCurve, shotFps);
                appendScalarProp(doc, pmEl, QStringLiteral("zoom"), camera.zoom, camera.zoomCurve, shotFps);
                appendScalarProp(doc, pmEl, QStringLiteral("focusDistance"), camera.focusDistance,
                                 camera.focusDistanceCurve, shotFps);
                appendScalarProp(doc, pmEl, QStringLiteral("blurDoF"), camera.blur, camera.blurCurve, shotFps);
            }
            if (layer.kind == composition::LayerKind::Light) {
                // LightLayer's own properties, by the reference's names.
                const composition::LightSettings& light = layer.light;
                appendColorProp(doc, pmEl, QStringLiteral("color"), light.color, light.colorCurve, shotFps);
                appendScalarProp(doc, pmEl, QStringLiteral("intensity"), light.intensity, light.intensityCurve, shotFps);
                appendBoolProp(doc, pmEl, QStringLiteral("castShadows"), light.castShadows);
                appendIntProp(doc, pmEl, QStringLiteral("falloff"), int(light.falloff));
                appendScalarProp(doc, pmEl, QStringLiteral("reach"), light.reach, light.reachCurve, shotFps);
                appendScalarProp(doc, pmEl, QStringLiteral("spotlightAngle"), light.coneAngle, light.coneAngleCurve,
                                 shotFps);
                appendScalarProp(doc, pmEl, QStringLiteral("spotlightFeather"), light.feather, light.featherCurve,
                                 shotFps);
                appendScalarProp(doc, pmEl, QStringLiteral("shadowOpacity"), light.shadowOpacity,
                                 light.shadowOpacityCurve, shotFps);
                appendScalarProp(doc, pmEl, QStringLiteral("shadowDiffusion"), light.shadowDiffusion,
                                 light.shadowDiffusionCurve, shotFps);
                appendColorProp(doc, pmEl, QStringLiteral("shadowColor"), light.shadowColor, light.shadowColorCurve,
                                shotFps);
            } else if (layer.kind != composition::LayerKind::Camera && layer.kind != composition::LayerKind::Point) {
                // How the layer takes lights and shadows in the 3D scene.
                const composition::SurfaceShading& surface = layer.surface;
                appendBoolProp(doc, pmEl, QStringLiteral("illuminated"), surface.illuminated);
                appendBoolProp(doc, pmEl, QStringLiteral("castShadows"), surface.castShadows);
                appendBoolProp(doc, pmEl, QStringLiteral("castShadowsIfLayerDisabled"), surface.castShadowsIfInvisible);
                appendBoolProp(doc, pmEl, QStringLiteral("receiveShadows"), surface.receiveShadows);
                appendBoolProp(doc, pmEl, QStringLiteral("billboardedLights"), surface.billboardedLights);
                appendBoolProp(doc, pmEl, QStringLiteral("billboardedShadows"), surface.billboardedShadows);
                // Static <fl>s (Type 1), as the reference keeps them.
                const auto staticReal = [&](const char* name, double value) {
                    appendStaticProp(doc, pmEl, QString::fromLatin1(name), [&](QDomElement& holder) {
                        addText(doc, holder, QStringLiteral("fl"), QString::number(value, 'g', 9));
                    });
                };
                if (layer.kind == composition::LayerKind::Model3D) staticReal("selfShadowBias", surface.selfShadowBias);
                appendBoolProp(doc, pmEl, QStringLiteral("castAmbientOcclusion"), surface.castAmbientOcclusion);
                appendBoolProp(doc, pmEl, QStringLiteral("receiveAmbientOcclusion"), surface.receiveAmbientOcclusion);
                appendIntProp(doc, pmEl, QStringLiteral("aoSamples"), surface.aoSamples);
                appendIntProp(doc, pmEl, QStringLiteral("aoSampleRadius"), surface.aoSampleRadius);
                staticReal("aoIntensity", surface.aoIntensity);
                staticReal("aoDepthScale", surface.aoDepthScale);
                staticReal("aoBias", surface.aoBias);
                appendIntProp(doc, pmEl, QStringLiteral("aoBlurRadius"), surface.aoBlurRadius);
                staticReal("aoDepthFallOff", surface.aoDepthFalloff);
                if (layer.kind == composition::LayerKind::Model3D)
                    appendBoolProp(doc, pmEl, QStringLiteral("includeSemiTransparentTriangles"),
                                   surface.includeSemiTransparentTriangles);
                appendBoolProp(doc, pmEl, QStringLiteral("castReflections"), surface.castReflections);
                appendBoolProp(doc, pmEl, QStringLiteral("castReflectionsIfLayerDisabled"),
                               surface.castReflectionsIfInvisible);
                appendBoolProp(doc, pmEl, QStringLiteral("receiveReflections"), surface.receiveReflections);
                if (layer.kind != composition::LayerKind::Model3D)
                    appendScalarProp(doc, pmEl, QStringLiteral("specularReflectivity"), surface.specularReflectivity,
                                     surface.specularReflectivityCurve, shotFps);
                if (layer.kind == composition::LayerKind::Text)
                    appendScalarProp(doc, pmEl, QStringLiteral("diffuseReflectivity"), surface.diffuseReflectivity,
                                     surface.diffuseReflectivityCurve, shotFps);
                if (layer.kind == composition::LayerKind::Text || layer.kind == composition::LayerKind::Model3D) {
                    appendIntProp(doc, pmEl, QStringLiteral("environmentMapType"), int(surface.environmentMap));
                    appendStaticProp(doc, pmEl, QStringLiteral("environmentMapLayerID"), [&](QDomElement& holder) {
                        addText(doc, holder, QStringLiteral("id"), surface.environmentMapLayer.value());
                    });
                }
                appendColorProp(doc, pmEl, QStringLiteral("shadowColor"), surface.shadowColor);
                appendScalarProp(doc, pmEl, QStringLiteral("matAmbient"), surface.ambient, surface.ambientCurve, shotFps);
                appendScalarProp(doc, pmEl, QStringLiteral("matDiffuse"), surface.diffuse, surface.diffuseCurve, shotFps);
                appendScalarProp(doc, pmEl, QStringLiteral("matSpecular"), surface.specular, surface.specularCurve,
                                 shotFps);
                appendScalarProp(doc, pmEl, QStringLiteral("matShininess"), surface.shininess, surface.shininessCurve,
                                 shotFps);
                appendColorProp(doc, pmEl, QStringLiteral("matEmissiveColor"), surface.emissive, surface.emissiveCurve,
                                shotFps);
            }
            if (layer.kind == composition::LayerKind::Media) {
                appendScalarProp(doc, pmEl, QStringLiteral("audioLevel"), layer.transform.audioLevel,
                                 layer.transform.audioLevelCurve, shotFps);
                if (!layer.clips.isEmpty()) {
                    // LayerBase "speed", a <db> in the reference (Type 1).
                    QDomElement speed = doc.createElement(QStringLiteral("Prop"));
                    speed.setAttribute(QStringLiteral("Type"), 1);
                    addText(doc, speed, QStringLiteral("Name"), QStringLiteral("speed"));
                    QDomElement holder = doc.createElement(QStringLiteral("Static"));
                    addText(doc, holder, QStringLiteral("db"),
                            QString::number(layer.clips.first().speed, 'g', 10));
                    speed.appendChild(holder);
                    pmEl.appendChild(speed);
                }
            }

            named.appendChild(behaviorList);
            named.appendChild(pmEl);

            wrapper.appendChild(named);
            layers.appendChild(wrapper);
        }

        comp.appendChild(viewerStateElement(doc, shot.viewerSetup()));
        return comp;
    };

    // The root shot first, then the project's other shots, then any shot only
    // a layer still nests - each once.
    QVector<const composition::Composition*> shots;
    QSet<QString> writtenShots;
    std::function<void(const composition::Composition&)> collectShots =
        [&](const composition::Composition& shot) {
            if (writtenShots.contains(shot.id().value())) return;
            writtenShots.insert(shot.id().value());
            shots.append(&shot);
        };
    collectShots(composition);
    for (const auto& shot : composition.compositeShots())
        if (shot) collectShots(*shot);
    for (int i = 0; i < shots.size(); ++i) {
        for (const composition::Layer& layer : shots.at(i)->layers())
            for (const composition::Clip& clip : layer.clips)
                if (clip.nestedComposition) collectShots(*clip.nestedComposition);
    }
    for (const composition::Composition* shot : shots)
        assets.appendChild(compositionAssetElement(*shot));

    for (auto it = guidByPath.constBegin(); it != guidByPath.constEnd(); ++it) {
        const QString path = it.key();
        const QString guid = it.value();
        const media::MediaAsset recorded = media.assetByFilePath(path);
        if (recorded.kind() == media::MediaKind::Model3DAnimation) {
            // The reference's Model3DAnimationAsset (Version 3: ID,
            // ParentFolderID, IsHidden, Name, Filename, OverrideFrameRate,
            // FrameRate, AnimationIndex); the instances on layers keep their
            // own rate and index here.
            QDomElement m = doc.createElement(QStringLiteral("Model3DAnimationAsset"));
            m.setAttribute(QStringLiteral("Version"), QStringLiteral("3"));
            addText(doc, m, QStringLiteral("ID"), guid);
            addText(doc, m, QStringLiteral("ParentFolderID"), QStringLiteral("00000000-0000-0000-0000-000000000000"));
            addText(doc, m, QStringLiteral("IsHidden"), QStringLiteral("0"));
            addText(doc, m, QStringLiteral("Name"), recorded.fileName());
            addText(doc, m, QStringLiteral("Filename"), storedPath(recorded.sourcePath()));
            addText(doc, m, QStringLiteral("OverrideFrameRate"), QStringLiteral("0"));
            addText(doc, m, QStringLiteral("FrameRate"),
                    QString::number(recorded.fileFrameRate() > 0.0 ? recorded.fileFrameRate() : 24.0, 'g', 6));
            addText(doc, m, QStringLiteral("AnimationIndex"), QStringLiteral("0"));
            assets.appendChild(m);
            continue;
        }
        // Image -> ImageAsset; Video/Audio -> MediaAsset (matches reference schema).
        const bool mediaAsset = recorded.kind() != media::MediaKind::Image;
        const QString tagName =
            mediaAsset ? QStringLiteral("MediaAsset") : QStringLiteral("ImageAsset");
        QDomElement m = doc.createElement(tagName);
        if (recorded.labelColor().isValid())
            m.setAttribute(QStringLiteral("OpenVegasLabelColor"), recorded.labelColor().name(QColor::HexArgb));
        if (recorded.proxyMode() != media::ProxyMode::None) {
            m.setAttribute(QStringLiteral("OpenVegasProxy"),
                           recorded.proxyMode() == media::ProxyMode::Quality
                               ? QStringLiteral("Quality") : QStringLiteral("Performance"));
        }
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
        // An image sequence is stored by its first still, as the reference
        // does with IsImageSequence and its own FrameRate.
        const bool sequence = recorded.isImageSequence();
        addText(doc, m, QStringLiteral("Filename"),
                storedPath(recorded.isValid() ? recorded.sourcePath() : path));
        // One layer of a Photoshop document: the reference opens the
        // document itself, flattened; this port the layer again.
        if (media::isPsdMarkerPath(path))
            addText(doc, m, QStringLiteral("OpenVegasPsdPart"), path.mid(media::psdFileOfPath(path).size()));
        if (mediaAsset) {
            addText(doc, m, QStringLiteral("IsImageSequence"),
                    sequence ? QStringLiteral("1") : QStringLiteral("0"));
            addText(doc, m, QStringLiteral("HWAccelerate"),
                    recorded.isValid() && !recorded.hardwareDecoding() ? QStringLiteral("0")
                                                                       : QStringLiteral("1"));
            addText(doc, m, QStringLiteral("OpenVegasAudioStream"),
                    QString::number(recorded.audioStreamIndex()));
            addText(doc, m, QStringLiteral("MergedAudioFilename"), QString());
            addText(doc, m, QStringLiteral("MergedAudioOffset"), QStringLiteral("0"));
            addText(doc, m, QStringLiteral("VideoIdx"), QStringLiteral("0"));
            addText(doc, m, QStringLiteral("AudioIdx"),
                    recorded.kind() == media::MediaKind::Audio ? QStringLiteral("0")
                                                               : QStringLiteral("-1"));
            // MediaOverrideOptions::FrameRate; a sequence's <FrameRate> is its rate.
            addText(doc, m, QStringLiteral("OverrideFrameRate"),
                    !sequence && recorded.overridesFrameRate() ? QStringLiteral("1") : QStringLiteral("0"));
            addText(doc, m, QStringLiteral("FrameRate"),
                    sequence ? QString::number(recorded.sequenceFrameRate(), 'g', 10)
                    : recorded.overridesFrameRate() ? QString::number(recorded.frameRateOverride(), 'g', 10)
                    : recorded.fileFrameRate() > 0.0 ? QString::number(recorded.fileFrameRate(), 'g', 10)
                                                     : QString::number(int(fps + 0.5)));
            // MediaOverrideOptions::PixelAspectRatio.
            addText(doc, m, QStringLiteral("OverridePAR"),
                    recorded.overridesPixelAspect() ? QStringLiteral("1") : QStringLiteral("0"));
            addText(doc, m, QStringLiteral("PAR"), QString::number(recorded.pixelAspectOverride()));
            addText(doc, m, QStringLiteral("OverrideAlpha"),
                    recorded.overridesAlpha() ? QStringLiteral("1") : QStringLiteral("0"));
            addText(doc, m, QStringLiteral("AlphaMode"), QString::number(recorded.alphaOverride()));
            // Automatic levels/space are "not overridden" (MediaVideoStream
            // returns 0 then).
            addText(doc, m, QStringLiteral("OverrideColorLevels"),
                    recorded.colorLevels() != 0 ? QStringLiteral("1") : QStringLiteral("0"));
            addText(doc, m, QStringLiteral("ColorLevels"), QString::number(recorded.colorLevels()));
            addText(doc, m, QStringLiteral("OverrideColorSpace"),
                    recorded.colorSpace() != 0 ? QStringLiteral("1") : QStringLiteral("0"));
            addText(doc, m, QStringLiteral("ColorSpace"), QString::number(recorded.colorSpace()));
            // Trimmer in/out points (frames); mirrors MediaAsset::SetTrimmerInPoint.
            addText(doc, m, QStringLiteral("InPoint"),
                    QStringLiteral("%1").arg(recorded.trimInPoint()));
            addText(doc, m, QStringLiteral("OutPoint"),
                    QStringLiteral("%1").arg(recorded.trimOutPoint()));
        } else {
            // ImageAsset::PixelAspectRatio: an image's PAR is its setting alone.
            addText(doc, m, QStringLiteral("PAR"),
                    QString::number(recorded.overridesPixelAspect() ? recorded.pixelAspectOverride() : 0));
            // ImageAsset::OverriddenAlpha.
            addText(doc, m, QStringLiteral("OverrideAlpha"),
                    recorded.overridesAlpha() ? QStringLiteral("1") : QStringLiteral("0"));
            addText(doc, m, QStringLiteral("AlphaMode"), QString::number(recorded.alphaOverride()));
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

    // <OpenCompositeShots>, as the reference writes it (FUN_140209440): the
    // Editor tabs by id and name, and the timeline in front.
    {
        QHash<QString, const composition::Composition*> shotById;
        for (const composition::Composition* shot : shots) shotById.insert(shot->id().value(), shot);
        QStringList openIds;
        for (const QString& id : composition.openShotIds())
            if (shotById.contains(id) && !openIds.contains(id)) openIds.append(id);
        if (openIds.isEmpty()) openIds.append(composition.id().value());
        const QString active = openIds.contains(composition.activeShotId())
            ? composition.activeShotId() : openIds.first();
        QDomElement open = doc.createElement(QStringLiteral("OpenCompositeShots"));
        open.setAttribute(QStringLiteral("Version"), QStringLiteral("0"));
        open.setAttribute(QStringLiteral("TimelineType"), QString::number(kShotTimeline));
        open.setAttribute(QStringLiteral("TimelineId"), active);
        for (const QString& id : openIds) {
            QDomElement shot = doc.createElement(QStringLiteral("CompositeShot"));
            shot.setAttribute(QStringLiteral("CompositionId"), id);
            shot.setAttribute(QStringLiteral("Name"), shotById.value(id)->name());
            open.appendChild(shot);
        }
        root.appendChild(open);
    }

    if (options.useRelativePaths) {
        std::function<void(QDomElement)> relativiseIds = [&](QDomElement element) {
            for (const QString& attribute : {QStringLiteral("MediaID"), QStringLiteral("ModelAssetID")}) {
                const QString id = element.attribute(attribute);
                const QString path = QDir::cleanPath(mediaPathFromId(core::Identifier(id)));
                if (id.startsWith(QStringLiteral("media:")) && !path.isEmpty() && guidByPath.contains(path))
                    element.setAttribute(attribute, QStringLiteral("media:")
                                                        + QDir::fromNativeSeparators(storedPath(path)));
            }
            for (QDomElement child = element.firstChildElement(); !child.isNull(); child = child.nextSiblingElement())
                relativiseIds(child);
        };
        relativiseIds(root);
    }
    return doc;
}

static core::Result writeDocument(const QString& filePath, const QDomDocument& doc)
{
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return core::Result::fail(core::ResultStatus::OperationFailed,
                                  QStringLiteral("Cannot open file for writing: %1").arg(filePath));
    }
    const QByteArray xml = doc.toByteArray(1);
    if (file.write(xml) != xml.size()) {
        file.cancelWriting();
        return core::Result::fail(core::ResultStatus::OperationFailed,
                                  QStringLiteral("Cannot write project: %1").arg(filePath));
    }
    if (!file.commit())
        return core::Result::fail(core::ResultStatus::OperationFailed,
                                  QStringLiteral("Cannot commit project: %1").arg(filePath));
    return core::Result::ok();
}

core::Result VegfxSerializer::saveToFile(const QString& filePath,
                                         const composition::Composition& composition,
                                         const media::MediaManager& media,
                                         const ProjectSaveOptions& options)
{
    QDomDocument doc = projectDocument(filePath, composition, media, options);
    const QDomElement root = doc.documentElement();
    // A project opened from a file is written over that file's document, so
    // everything the model does not hold survives the save (VegfxMerge).
    if (const auto source = composition.nativeSource(); source && !source->document.isEmpty()) {
        QDomDocument stored;
        if (stored.setContent(source->document)
            && stored.documentElement().tagName() == root.tagName()) {
            doc = mergeIntoSource(stored, doc);
        }
    }
    return writeDocument(filePath, doc);
}

core::Result VegfxSerializer::listCompositeShots(const QString& filePath,
                                                 QVector<CompositeShotInfo>* shots)
{
    if (!shots) return core::Result::fail(core::ResultStatus::InvalidArgument, QStringLiteral("Null list"));
    shots->clear();
    QDomDocument doc;
    QByteArray bytes;
    if (const core::Result read = readProjectDocument(filePath, &doc, &bytes); read.isFailure())
        return read;
    const QDomElement root = doc.documentElement();
    if (root.tagName() != QLatin1String("VegasEffectsProject"))
        return core::Result::fail(core::ResultStatus::Unsupported,
                                  QStringLiteral("Not a project or composite shot file: %1").arg(filePath));
    const QDomElement assets = root.firstChildElement(QStringLiteral("Project"))
                                   .firstChildElement(QStringLiteral("AssetList"))
                                   .firstChildElement(QStringLiteral("Assets"));
    for (QDomElement shot = assets.firstChildElement(QStringLiteral("CompositionAsset")); !shot.isNull();
         shot = shot.nextSiblingElement(QStringLiteral("CompositionAsset"))) {
        CompositeShotInfo info;
        info.id = elemText(shot, QStringLiteral("ID"));
        info.name = elemText(shot, QStringLiteral("Name"));
        const QDomElement avs = shot.firstChildElement(QStringLiteral("AudioVideoSettings"));
        info.size = QSize(elemText(avs, QStringLiteral("Width")).toInt(),
                          elemText(avs, QStringLiteral("Height")).toInt());
        const double fps = elemText(avs, QStringLiteral("FrameRate")).toDouble();
        if (fps > 0.0) info.durationSeconds = elemText(avs, QStringLiteral("FrameCount")).toDouble() / fps;
        if (!info.id.isEmpty()) shots->append(info);
    }
    return core::Result::ok();
}

core::Result VegfxSerializer::importCompositeShots(
    const QString& filePath, const QStringList& ids, const QSet<QString>& takenIds,
    media::MediaManager* media, QVector<std::shared_ptr<composition::Composition>>* shots)
{
    if (!media || !shots)
        return core::Result::fail(core::ResultStatus::InvalidArgument, QStringLiteral("Null model pointers"));
    shots->clear();
    composition::Composition root;
    media::MediaManager staged;
    if (const core::Result loaded = loadProjectImpl(filePath, &root, &staged, nullptr); loaded.isFailure())
        return loaded;

    // Every shot of the file by ID; the root one becomes an object of its
    // own, and the layers that nest it - left unlinked while it was the
    // root - point at it now.
    auto rootShot = std::make_shared<composition::Composition>(root);
    rootShot->setCompositeShots({});
    rootShot->setOpenShots({}, {});
    rootShot->setNativeSource(nullptr);
    QHash<QString, std::shared_ptr<composition::Composition>> byId;
    byId.insert(rootShot->id().value(), rootShot);
    for (const auto& shot : root.compositeShots())
        if (shot) byId.insert(shot->id().value(), shot);
    for (const auto& shot : byId) {
        for (int li = 0; li < shot->layers().size(); ++li) {
            for (composition::Clip& clip : shot->layerRef(li).clips) {
                if (clip.nestedComposition || clip.nestedCompositionId != rootShot->id()) continue;
                QSet<const composition::Composition*> seen;
                if (shot != rootShot && !nestsShot(rootShot.get(), shot.get(), &seen))
                    clip.nestedComposition = rootShot;
            }
        }
    }

    // The shots asked for, then whatever they nest.
    QVector<std::shared_ptr<composition::Composition>> picked;
    std::function<void(const std::shared_ptr<composition::Composition>&)> pick =
        [&](const std::shared_ptr<composition::Composition>& shot) {
            if (!shot || picked.contains(shot)) return;
            picked.append(shot);
            for (const composition::Layer& layer : shot->layers())
                for (const composition::Clip& clip : layer.clips) pick(clip.nestedComposition);
        };
    for (const QString& id : ids) pick(byId.value(id));
    if (picked.isEmpty())
        return core::Result::fail(core::ResultStatus::MissingResource,
                                  QStringLiteral("No composite shots can be imported from this file."));

    // A shot the project already has keeps its own ID there; the copy gets a
    // new one, and the layers nesting it follow.
    for (const auto& shot : picked) {
        shot->setPrimary(false);
        if (takenIds.contains(shot->id().value()))
            shot->setId(core::Identifier(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    }
    for (const auto& shot : picked) {
        for (int li = 0; li < shot->layers().size(); ++li) {
            composition::Layer& layer = shot->layerRef(li);
            if (layer.modelAssetId.isValid()) media->adoptAsset(staged, layer.modelAssetId);
            for (composition::Clip& clip : layer.clips) {
                if (clip.nestedComposition) {
                    clip.nestedCompositionId = clip.nestedComposition->id();
                    clip.mediaId = core::Identifier(QStringLiteral("composition:")
                                                    + clip.nestedComposition->id().value());
                } else if (clip.mediaId.isValid()) {
                    media->adoptAsset(staged, clip.mediaId);
                }
            }
        }
    }
    *shots = picked;
    return core::Result::ok();
}

core::Result VegfxSerializer::saveCompositeShot(const QString& filePath,
                                                const composition::Composition& shot,
                                                const media::MediaManager& media,
                                                const ProjectSaveOptions& options)
{
    for (const composition::Layer& layer : shot.layers()) {
        for (const composition::Clip& clip : layer.clips) {
            if (clip.nestedComposition || clip.nestedCompositionId.isValid())
                return core::Result::fail(
                    core::ResultStatus::Unsupported,
                    QStringLiteral("This composite shot cannot be saved because it contains one or "
                                   "more embedded composite shots."));
        }
    }
    // The shot alone, written as a project would be; its CompositionAsset and
    // the media its layers name then move under <BiffCompositeShot>.
    composition::Composition single = shot;
    single.setCompositeShots({});
    single.setOpenShots({}, {});
    single.setNativeSource(nullptr);
    ProjectSaveOptions shotOptions = options;
    shotOptions.isAutoSave = false;
    shotOptions.screenLayout.clear();
    const QDomDocument project = projectDocument(filePath, single, media, shotOptions);
    const QDomElement assets = project.documentElement().firstChildElement(QStringLiteral("Project"))
                                   .firstChildElement(QStringLiteral("AssetList"))
                                   .firstChildElement(QStringLiteral("Assets"));
    const QDomElement shotAsset = assets.firstChildElement(QStringLiteral("CompositionAsset"));
    QSet<QString> used;
    const QDomNodeList references = shotAsset.elementsByTagName(QStringLiteral("AssetID"));
    for (int i = 0; i < references.size(); ++i) used.insert(references.at(i).toElement().text());

    QDomDocument doc;
    doc.appendChild(doc.createProcessingInstruction(QStringLiteral("xml"),
                                                    QStringLiteral("version=\"1.0\" encoding=\"UTF-8\"")));
    QDomElement root = doc.createElement(QLatin1String(kShotFileRoot));
    root.setAttribute(QStringLiteral("Version"), QStringLiteral("1"));
    root.setAttribute(QStringLiteral("AppEdition"), QStringLiteral("5000"));
    root.setAttribute(QStringLiteral("AppVersion"), QStringLiteral("1.0.0.0"));
    doc.appendChild(root);
    QDomElement mediaAssets = doc.createElement(QStringLiteral("Assets"));
    for (QDomElement asset = assets.firstChildElement(); !asset.isNull(); asset = asset.nextSiblingElement()) {
        if (asset.tagName() != QLatin1String("CompositionAsset")
            && used.contains(elemText(asset, QStringLiteral("ID"))))
            mediaAssets.appendChild(doc.importNode(asset, true));
    }
    if (mediaAssets.hasChildNodes()) root.appendChild(mediaAssets);
    root.appendChild(doc.importNode(shotAsset, true));
    return writeDocument(filePath, doc);
}

} // namespace project
} // namespace openvegas
