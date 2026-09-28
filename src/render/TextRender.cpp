#include "render/TextRender.h"

#include <QTextLayout>
#include <QGlyphRun>
#include <QRawFont>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QTextOption>
#include <utility>

namespace openvegas {
namespace render {

namespace {

using composition::TextStyle;

// The case buttons rewrite the string rather than ask the font, except for
// small caps, which QFont can do properly.
QString applyCaps(const QString& text, TextStyle::Caps caps)
{
    switch (caps) {
    case TextStyle::Caps::AllCaps:
        return text.toUpper();
    case TextStyle::Caps::LowerCase:
        return text.toLower();
    case TextStyle::Caps::StartCase: {
        QString out = text.toLower();
        bool atWordStart = true;
        for (int i = 0; i < out.size(); ++i) {
            if (atWordStart && out.at(i).isLetter()) {
                out[i] = out.at(i).toUpper();
                atWordStart = false;
            } else if (!out.at(i).isLetterOrNumber()) {
                atWordStart = true;
            }
        }
        return out;
    }
    case TextStyle::Caps::SmallCaps:   // handled by the font
    case TextStyle::Caps::None:
        break;
    }
    return text;
}

QFont fontFor(const TextStyle& style)
{
    const QString family = style.fontFamily.isEmpty() ? QFont().family() : style.fontFamily;
    QFont font = QFontDatabase::font(family, style.fontStyle, 12);
    font.setPixelSize(qMax(1, style.fontSize));

    // Keep the actual face (Light, Semibold, Condensed...), not just two flags.
    // Legacy/imported faces missing from the font database still get a fallback.
    if (!QFontDatabase::styles(family).contains(style.fontStyle, Qt::CaseInsensitive)) {
        const QString name = style.fontStyle.toLower();
        font.setBold(name.contains(QLatin1String("bold")));
        font.setItalic(name.contains(QLatin1String("italic"))
                       || name.contains(QLatin1String("oblique")));
    }

    font.setUnderline(style.underline);
    font.setStrikeOut(style.strikethrough);
    if (style.caps == TextStyle::Caps::SmallCaps) {
        font.setCapitalization(QFont::SmallCaps);
    }
    // The reference shows character spacing in per mille of the em, which is
    // what a typographer means by tracking: a fixed fraction of the em, not
    // a percentage of each individual glyph advance.
    if (!qFuzzyIsNull(style.letterSpacing)) {
        font.setLetterSpacing(QFont::AbsoluteSpacing, style.fontSize * style.letterSpacing / 1000.0);
    }
    return font;
}

Qt::Alignment horizontalAlignment(TextStyle::AlignH align)
{
    switch (align) {
    case TextStyle::AlignH::Left: return Qt::AlignLeft;
    case TextStyle::AlignH::Center: return Qt::AlignHCenter;
    case TextStyle::AlignH::Right: return Qt::AlignRight;
    case TextStyle::AlignH::LeftJustify:
    case TextStyle::AlignH::CenterJustify:
    case TextStyle::AlignH::RightJustify:
    case TextStyle::AlignH::Justify: return Qt::AlignJustify;
    }
    return Qt::AlignLeft;
}

struct TextGeometry
{
    QPainterPath glyphs;
    QVector<QPainterPath> glyphPaths;
    QVector<QPointF> glyphOrigins;
    QPainterPath decorations;
    QRectF bounds;
};

QRectF effectiveTextBox(const QRectF& canvas, const TextStyle& style)
{
    if (style.textMode != TextStyle::TextMode::Paragraph) return canvas;
    const QSizeF size(qMin(canvas.width(), qMax(1.0, style.paragraphSize.width())),
                      qMin(canvas.height(), qMax(1.0, style.paragraphSize.height())));
    return QRectF(canvas.center() - QPointF(size.width() / 2.0, size.height() / 2.0), size);
}

// Both fill and outlines use the same shaped, wrapped glyph runs. Rebuilding a
// path from paragraph strings loses wrapping, kerning, script and justification.
TextGeometry layoutText(const QRectF& box, const TextStyle& style)
{
    TextGeometry result;
    result.glyphs.setFillRule(Qt::WindingFill);
    const QFont baseFont = fontFor(style);
    QFont font = baseFont;
    const bool script = style.script != TextStyle::Script::None;
    if (script) font.setPixelSize(qMax(1, qRound(style.fontSize * 0.65)));
    const QFontMetricsF metrics(font);
    const double scriptShift = style.script == TextStyle::Script::Superscript ? -style.fontSize * 0.35
                             : style.script == TextStyle::Script::Subscript ? style.fontSize * 0.2 : 0.0;
    const double width = qMax(1.0, box.width() - style.indentLeft - style.indentRight);
    const double leading = metrics.height() * qMax(1.0, style.lineSpacing) / 100.0;
    double y = 0;
    const QStringList paragraphs = applyCaps(style.text, style.caps).split(QLatin1Char('\n'));
    for (const QString& paragraph : paragraphs) {
        y += style.spaceBeforeParagraph;
        QTextLayout layout(paragraph, font);
        QTextOption option(horizontalAlignment(style.alignH));
        option.setWrapMode(style.textMode == TextStyle::TextMode::Paragraph
                               ? QTextOption::WrapAtWordBoundaryOrAnywhere
                               : QTextOption::NoWrap);
        layout.setTextOption(option);
        if (style.alignH == TextStyle::AlignH::Justify)
            layout.setFlags(Qt::TextJustificationForced);
        layout.beginLayout();
        int lineNumber = 0;
        while (true) {
            QTextLine line = layout.createLine();
            if (!line.isValid()) break;
            const double indent = lineNumber == 0 ? style.indentFirstLine : 0.0;
            line.setLineWidth(qMax(1.0, width - indent));
            line.setPosition(QPointF(indent, y));
            y += leading;
            ++lineNumber;
        }
        layout.endLayout();
        // Alignment flags are alternatives in QTextLayout. OR-ing Center/Right
        // into Justify disables justification instead of aligning the last line.
        if (lineNumber > 0 && (style.alignH == TextStyle::AlignH::CenterJustify
                              || style.alignH == TextStyle::AlignH::RightJustify)) {
            QTextLine last = layout.lineAt(lineNumber - 1);
            const double remaining = qMax(0.0, last.width() - last.naturalTextWidth());
            last.setPosition(last.position() + QPointF(
                remaining * (style.alignH == TextStyle::AlignH::CenterJustify ? 0.5 : 1.0), 0));
        }
        if (lineNumber == 0) y += leading;
        for (const QGlyphRun& run : layout.glyphRuns()) {
            const QRawFont raw = run.rawFont();
            const auto indexes = run.glyphIndexes();
            const auto positions = run.positions();
            for (qsizetype i = 0; i < indexes.size(); ++i) {
                const QPointF pos = positions[i] + QPointF(0, scriptShift);
                QPainterPath glyph = QTransform::fromTranslate(pos.x(), pos.y())
                                         .map(raw.pathForGlyph(indexes[i]));
                result.glyphs.addPath(glyph);
                result.glyphPaths.append(std::move(glyph));
                result.glyphOrigins.append(pos);
            }
        }
        if (style.underline || style.strikethrough) {
            for (int i = 0; i < layout.lineCount(); ++i) {
                const QTextLine line = layout.lineAt(i);
                const QRectF rect = line.naturalTextRect();
                const double baseline = line.y() + line.ascent() + scriptShift;
                const double thickness = qMax(1.0, metrics.lineWidth());
                if (style.underline)
                    result.decorations.addRect(QRectF(rect.x(), baseline + metrics.underlinePos(),
                                                      rect.width(), thickness));
                if (style.strikethrough)
                    result.decorations.addRect(QRectF(rect.x(), baseline - metrics.strikeOutPos(),
                                                      rect.width(), thickness));
            }
        }
        y += style.spaceAfterParagraph;
    }
    const double available = box.height() - style.indentTop - style.indentBottom;
    double top = box.top() + style.indentTop;
    if (style.alignV == TextStyle::AlignV::Middle) top += (available - y) / 2.0;
    else if (style.alignV == TextStyle::AlignV::Bottom) top += available - y;
    top -= style.fontSize * style.baselineShift / 100.0;
    const QTransform origin = QTransform::fromTranslate(box.left() + style.indentLeft, top);
    result.glyphs = origin.map(result.glyphs);
    for (QPainterPath& glyph : result.glyphPaths) glyph = origin.map(glyph);
    for (QPointF& glyphOrigin : result.glyphOrigins) glyphOrigin = origin.map(glyphOrigin);
    result.decorations = origin.map(result.decorations);
    result.bounds = result.glyphs.boundingRect().united(result.decorations.boundingRect());
    return result;
}

} // namespace

QRectF styledTextBounds(const QRectF& box, const TextStyle& style)
{
    return style.text.isEmpty() ? QRectF() : layoutText(effectiveTextBox(box, style), style).bounds;
}

void drawStyledText(QPainter& painter, const QRectF& box,
                    const TextStyle& style,
                    const GlyphModifier& glyphModifier)
{
    if (style.text.isEmpty()) return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate(box.center());
    painter.scale(qMax(0.01, style.horizontalScale / 100.0),
                  qMax(0.01, style.verticalScale / 100.0));
    painter.translate(-box.center());
    const TextGeometry geometry = layoutText(effectiveTextBox(box, style), style);
    QVector<GlyphRenderState> glyphStates;
    QVector<QPainterPath> transformedGlyphs;
    QVector<QPainterPath> glyphClips;
    if (glyphModifier && !geometry.glyphPaths.isEmpty()) {
        glyphStates.resize(geometry.glyphPaths.size());
        glyphModifier(glyphStates);
        if (glyphStates.size() != geometry.glyphPaths.size()) {
            glyphStates.clear();
        } else {
            transformedGlyphs.reserve(glyphStates.size());
            glyphClips.reserve(glyphStates.size());
            for (qsizetype i = 0; i < glyphStates.size(); ++i) {
                const QPointF anchor = geometry.glyphOrigins.at(i);
                const QTransform aroundAnchor =
                    QTransform::fromTranslate(anchor.x(), anchor.y())
                    * glyphStates.at(i).transformation
                    * QTransform::fromTranslate(-anchor.x(), -anchor.y());
                transformedGlyphs.append(aroundAnchor.map(geometry.glyphPaths.at(i)));
                QPainterPath clip;
                if (glyphStates.at(i).clipEnabled) {
                    // The native shader evaluates ecLocalPos before cursorMatrix.
                    // Move the Y-up rectangle into Qt's Y-down glyph coordinates
                    // before applying the same cursor transform as the outline.
                    const QRectF native = glyphStates.at(i).clipRect;
                    if (native.width() > 0.0 && native.height() > 0.0) {
                        clip.addRect(QRectF(anchor.x() + native.left(),
                                            anchor.y() - native.bottom(),
                                            native.width(), native.height()));
                        clip = aroundAnchor.map(clip);
                    }
                }
                glyphClips.append(clip);
            }
        }
    }
    if (style.backgroundEnabled && style.backgroundOpacity > 0 && !geometry.bounds.isEmpty()) {
        const double ex = style.backgroundExpansionX * style.fontSize / 100.0;
        const double ey = style.expansionLinked ? ex : style.backgroundExpansionY * style.fontSize / 100.0;
        const QRectF plate = geometry.bounds.adjusted(-ex, -ey, ex, ey);
        QColor color = style.backgroundColor;
        color.setAlphaF(qBound(0.0, style.backgroundOpacity / 100.0, 1.0));
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        // Corner radius in percent is clamped by the available rectangle.
        const double radius = qMin(qMin(plate.width(), plate.height()) / 2.0,
                                   style.fontSize * qMax(0.0, style.backgroundRoundness) / 100.0);
        painter.drawRoundedRect(plate, radius, radius);
    }
    const auto fill = [&] {
        if (glyphStates.isEmpty()) {
            painter.fillPath(geometry.glyphs, style.fontColor);
        } else {
            const qreal originalOpacity = painter.opacity();
            for (qsizetype i = 0; i < transformedGlyphs.size(); ++i) {
                if (glyphStates[i].clipEnabled && glyphClips[i].isEmpty()) continue;
                if (glyphStates[i].clipEnabled) {
                    painter.save();
                    painter.setClipPath(glyphClips[i], Qt::IntersectClip);
                }
                painter.setOpacity(originalOpacity * qBound(0.0f, glyphStates[i].opacity, 1.0f));
                painter.fillPath(transformedGlyphs[i], style.fontColor);
                if (glyphStates[i].clipEnabled) painter.restore();
            }
            painter.setOpacity(originalOpacity);
        }
        painter.fillPath(geometry.decorations, style.fontColor);
    };
    const auto outlines = [&] {
        QVector<TextStyle::Outline> strokes = style.additionalOutlines;
        strokes.prepend({style.outlineSize, style.outlineColor});
        // The first outline is closest to the fill; wider additional outlines
        // sit behind it. The order remains the order shown by the list.
        for (auto it = strokes.crbegin(); it != strokes.crend(); ++it) {
            if (it->size <= 0) continue;
            QPen pen(it->color);
            pen.setWidthF(it->size * (style.strokeOrder == TextStyle::StrokeOrder::Centered ? 1.0 : 2.0));
            pen.setJoinStyle(Qt::RoundJoin);
            if (glyphStates.isEmpty()) {
                painter.strokePath(geometry.glyphs, pen);
            } else {
                const qreal originalOpacity = painter.opacity();
                for (qsizetype i = 0; i < transformedGlyphs.size(); ++i) {
                    if (glyphStates[i].clipEnabled && glyphClips[i].isEmpty()) continue;
                    if (glyphStates[i].clipEnabled) {
                        painter.save();
                        painter.setClipPath(glyphClips[i], Qt::IntersectClip);
                    }
                    painter.setOpacity(originalOpacity * qBound(0.0f, glyphStates[i].opacity, 1.0f));
                    painter.strokePath(transformedGlyphs[i], pen);
                    if (glyphStates[i].clipEnabled) painter.restore();
                }
                painter.setOpacity(originalOpacity);
            }
            painter.strokePath(geometry.decorations, pen);
        }
    };
    if (style.strokeOrder == TextStyle::StrokeOrder::UnderFill) {
        outlines(); fill();
    } else {
        fill(); outlines();
    }
    painter.restore();
}

} // namespace render
} // namespace openvegas
