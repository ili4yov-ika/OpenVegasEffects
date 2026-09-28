#pragma once

#include <QRectF>
#include <QTransform>
#include <QVector>
#include <functional>

#include "composition/TextStyle.h"

class QPainter;

namespace openvegas {
namespace render {

// Draws a styled text layer.
//
// Laid out through QTextLayout rather than QPainter::drawText, because the
// paragraph half of the reference's panel - the seven alignments, the four
// indents, the first-line indent, the gaps before and after - is what a text
// shaped layout does and what a single drawText call cannot. The character half is
// applied to the font and glyph paths, since the reference's
// panel edits the layer, not a selection inside it.
//
// `box` is the text box in the painter's current coordinates; the caller has
// already applied the layer transform. The background, if the style asks for
// one, is drawn behind the text and expanded by the style's own margins.
struct GlyphRenderState
{
    QTransform transformation;
    float opacity = 1.0f;
};

// Called once after shaping, with one state per rendered glyph. The transform
// is relative to that glyph's baseline origin in the text box's coordinates.
using GlyphModifier = std::function<void(QVector<GlyphRenderState>&)>;
void drawStyledText(QPainter& painter, const QRectF& box,
                    const composition::TextStyle& style,
                    const GlyphModifier& glyphModifier = {});

// Bounding box the text actually occupies inside `box`, in the same
// coordinates. Used for the background rectangle and worth having on its own so
// a caller can measure without drawing.
QRectF styledTextBounds(const QRectF& box, const composition::TextStyle& style);

} // namespace render
} // namespace openvegas
