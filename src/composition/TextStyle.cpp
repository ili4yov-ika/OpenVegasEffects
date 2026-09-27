#include "composition/TextStyle.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>

namespace openvegas {
namespace composition {

namespace {

// Order of the effect's parameter list. Appending is safe; reordering would
// change what an existing project means.
enum Index
{
    IdxText = 0,
    IdxFontSize,
    IdxFontFamily,
    IdxFontStyle,
    IdxFontColor,
    IdxOutlineSize,
    IdxOutlineColor,
    IdxStrokeOrder,
    IdxLineSpacing,
    IdxVerticalScale,
    IdxHorizontalScale,
    IdxLetterSpacing,
    IdxBaselineShift,
    IdxCaps,
    IdxUnderline,
    IdxStrikethrough,
    IdxScript,
    IdxAlignH,
    IdxAlignV,
    IdxIndentLeft,
    IdxIndentRight,
    IdxIndentTop,
    IdxIndentBottom,
    IdxIndentFirstLine,
    IdxSpaceBefore,
    IdxSpaceAfter,
    IdxBackgroundColor,
    IdxBackgroundOpacity,
    IdxBackgroundRoundness,
    IdxBackgroundExpansionX,
    IdxBackgroundExpansionY,
    IdxExpansionLinked,
    IdxAdditionalOutlines,
    IdxBackgroundEnabled,
    IdxTextMode,
    IdxParagraphWidth,
    IdxParagraphHeight,
    IdxCount,
};

QString at(const QStringList& values, int index)
{
    return index < values.size() ? values.at(index) : QString();
}

double readDouble(const QStringList& values, int index, double fallback)
{
    bool ok = false;
    const double value = at(values, index).toDouble(&ok);
    return ok ? value : fallback;
}

int readInt(const QStringList& values, int index, int fallback)
{
    bool ok = false;
    const int value = at(values, index).toInt(&ok);
    return ok ? value : fallback;
}

bool readBool(const QStringList& values, int index, bool fallback)
{
    const QString text = at(values, index);
    if (text.isEmpty()) {
        return fallback;
    }
    return text.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0
           || text == QLatin1String("1");
}

QColor readColor(const QStringList& values, int index, const QColor& fallback)
{
    const QColor color(at(values, index));
    return color.isValid() ? color : fallback;
}

// The enums travel as their own words rather than as numbers, so a project file
// stays readable and a reordered enum cannot silently change a saved layer.
const char* kStrokeOrder[] = {"Over Fill", "Under Fill", "Centered"};
const char* kCaps[] = {"None", "All Caps", "Small Caps", "Start Case", "Lower Case"};
const char* kScript[] = {"None", "Superscript", "Subscript"};
const char* kAlignH[] = {"Left",          "Center",        "Right",  "Left Justify",
                         "Center Justify", "Right Justify", "Justify"};
const char* kAlignV[] = {"Top", "Middle", "Bottom"};
const char* kTextMode[] = {"Point", "Paragraph"};

template <typename Enum, size_t N>
Enum readEnum(const QStringList& values, int index, const char* const (&names)[N], Enum fallback)
{
    const QString text = at(values, index);
    if (text.isEmpty()) {
        return fallback;
    }
    for (size_t i = 0; i < N; ++i) {
        if (text.compare(QLatin1String(names[i]), Qt::CaseInsensitive) == 0) {
            return static_cast<Enum>(i);
        }
    }
    return fallback;
}

template <typename Enum, size_t N>
QString writeEnum(Enum value, const char* const (&names)[N])
{
    const size_t index = static_cast<size_t>(value);
    return QLatin1String(index < N ? names[index] : names[0]);
}

QString number(double value)
{
    return QString::number(value, 'f', 2);
}

} // namespace

int textStyleParameterCount()
{
    return int(IdxCount);
}

TextStyle textStyleFromParameters(const QStringList& values)
{
    TextStyle style;
    style.text = at(values, IdxText);
    style.fontSize = qMax(1, readInt(values, IdxFontSize, style.fontSize));
    style.fontFamily = at(values, IdxFontFamily);
    const QString fontStyle = at(values, IdxFontStyle);
    if (!fontStyle.isEmpty()) {
        style.fontStyle = fontStyle;
    }
    style.fontColor = readColor(values, IdxFontColor, style.fontColor);
    style.outlineSize = readDouble(values, IdxOutlineSize, style.outlineSize);
    style.outlineColor = readColor(values, IdxOutlineColor, style.outlineColor);
    style.strokeOrder =
        readEnum(values, IdxStrokeOrder, kStrokeOrder, TextStyle::StrokeOrder::OverFill);
    style.lineSpacing = readDouble(values, IdxLineSpacing, style.lineSpacing);
    style.verticalScale = readDouble(values, IdxVerticalScale, style.verticalScale);
    style.horizontalScale = readDouble(values, IdxHorizontalScale, style.horizontalScale);
    style.letterSpacing = readDouble(values, IdxLetterSpacing, style.letterSpacing);
    style.baselineShift = readDouble(values, IdxBaselineShift, style.baselineShift);
    style.caps = readEnum(values, IdxCaps, kCaps, TextStyle::Caps::None);
    style.underline = readBool(values, IdxUnderline, style.underline);
    style.strikethrough = readBool(values, IdxStrikethrough, style.strikethrough);
    style.script = readEnum(values, IdxScript, kScript, TextStyle::Script::None);
    style.alignH = readEnum(values, IdxAlignH, kAlignH, TextStyle::AlignH::Center);
    style.alignV = readEnum(values, IdxAlignV, kAlignV, TextStyle::AlignV::Middle);
    style.indentLeft = readDouble(values, IdxIndentLeft, style.indentLeft);
    style.indentRight = readDouble(values, IdxIndentRight, style.indentRight);
    style.indentTop = readDouble(values, IdxIndentTop, style.indentTop);
    style.indentBottom = readDouble(values, IdxIndentBottom, style.indentBottom);
    style.indentFirstLine = readDouble(values, IdxIndentFirstLine, style.indentFirstLine);
    style.spaceBeforeParagraph = readDouble(values, IdxSpaceBefore, style.spaceBeforeParagraph);
    style.spaceAfterParagraph = readDouble(values, IdxSpaceAfter, style.spaceAfterParagraph);
    style.backgroundColor = readColor(values, IdxBackgroundColor, style.backgroundColor);
    style.backgroundOpacity = readDouble(values, IdxBackgroundOpacity, style.backgroundOpacity);
    style.backgroundRoundness =
        readDouble(values, IdxBackgroundRoundness, style.backgroundRoundness);
    style.backgroundExpansionX =
        readDouble(values, IdxBackgroundExpansionX, style.backgroundExpansionX);
    style.backgroundExpansionY =
        readDouble(values, IdxBackgroundExpansionY, style.backgroundExpansionY);
    style.expansionLinked = readBool(values, IdxExpansionLinked, style.expansionLinked);
    style.backgroundEnabled = readBool(values, IdxBackgroundEnabled, style.backgroundOpacity > 0);
    style.textMode = readEnum(values, IdxTextMode, kTextMode, TextStyle::TextMode::Point);
    style.paragraphSize.setWidth(qMax(1.0, readDouble(values, IdxParagraphWidth,
                                                      style.paragraphSize.width())));
    style.paragraphSize.setHeight(qMax(1.0, readDouble(values, IdxParagraphHeight,
                                                       style.paragraphSize.height())));
    // Pre-v2 styles stored expansion in pixels. Keep their visual size.
    if (values.size() <= IdxAdditionalOutlines) {
        style.backgroundExpansionX *= 100.0 / style.fontSize;
        style.backgroundExpansionY *= 100.0 / style.fontSize;
    }
    const QJsonArray outlines = QJsonDocument::fromJson(at(values, IdxAdditionalOutlines).toUtf8()).array();
    for (const QJsonValue& item : outlines) {
        const QJsonObject entry = item.toObject();
        const QColor color(entry.value(QStringLiteral("color")).toString());
        const double size = entry.value(QStringLiteral("size")).toDouble(-1);
        if (color.isValid() && std::isfinite(size) && size >= 0 && size <= 400)
            style.additionalOutlines.append({size, color});
    }
    return style;
}

QStringList textStyleToParameters(const TextStyle& style)
{
    QStringList values;
    values.resize(IdxCount);
    values[IdxText] = style.text;
    values[IdxFontSize] = QString::number(style.fontSize);
    values[IdxFontFamily] = style.fontFamily;
    values[IdxFontStyle] = style.fontStyle;
    values[IdxFontColor] = style.fontColor.name(QColor::HexRgb);
    values[IdxOutlineSize] = number(style.outlineSize);
    values[IdxOutlineColor] = style.outlineColor.name(QColor::HexRgb);
    values[IdxStrokeOrder] = writeEnum(style.strokeOrder, kStrokeOrder);
    values[IdxLineSpacing] = number(style.lineSpacing);
    values[IdxVerticalScale] = number(style.verticalScale);
    values[IdxHorizontalScale] = number(style.horizontalScale);
    values[IdxLetterSpacing] = number(style.letterSpacing);
    values[IdxBaselineShift] = number(style.baselineShift);
    values[IdxCaps] = writeEnum(style.caps, kCaps);
    values[IdxUnderline] = style.underline ? QStringLiteral("true") : QStringLiteral("false");
    values[IdxStrikethrough] =
        style.strikethrough ? QStringLiteral("true") : QStringLiteral("false");
    values[IdxScript] = writeEnum(style.script, kScript);
    values[IdxAlignH] = writeEnum(style.alignH, kAlignH);
    values[IdxAlignV] = writeEnum(style.alignV, kAlignV);
    values[IdxIndentLeft] = number(style.indentLeft);
    values[IdxIndentRight] = number(style.indentRight);
    values[IdxIndentTop] = number(style.indentTop);
    values[IdxIndentBottom] = number(style.indentBottom);
    values[IdxIndentFirstLine] = number(style.indentFirstLine);
    values[IdxSpaceBefore] = number(style.spaceBeforeParagraph);
    values[IdxSpaceAfter] = number(style.spaceAfterParagraph);
    values[IdxBackgroundColor] = style.backgroundColor.name(QColor::HexRgb);
    values[IdxBackgroundOpacity] = number(style.backgroundOpacity);
    values[IdxBackgroundRoundness] = number(style.backgroundRoundness);
    values[IdxBackgroundExpansionX] = number(style.backgroundExpansionX);
    values[IdxBackgroundExpansionY] = number(style.backgroundExpansionY);
    values[IdxExpansionLinked] =
        style.expansionLinked ? QStringLiteral("true") : QStringLiteral("false");
    QJsonArray outlines;
    for (const TextStyle::Outline& outline : style.additionalOutlines) {
        QJsonObject entry;
        entry.insert(QStringLiteral("size"), outline.size);
        entry.insert(QStringLiteral("color"), outline.color.name(QColor::HexArgb));
        outlines.append(entry);
    }
    values[IdxAdditionalOutlines] = QString::fromUtf8(QJsonDocument(outlines).toJson(QJsonDocument::Compact));
    values[IdxBackgroundEnabled] = style.backgroundEnabled ? QStringLiteral("true") : QStringLiteral("false");
    values[IdxTextMode] = style.textMode == TextStyle::TextMode::Paragraph
                              ? QStringLiteral("Paragraph") : QStringLiteral("Point");
    values[IdxParagraphWidth] = number(style.paragraphSize.width());
    values[IdxParagraphHeight] = number(style.paragraphSize.height());
    return values;
}

} // namespace composition
} // namespace openvegas
