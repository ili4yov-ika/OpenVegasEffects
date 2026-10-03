#pragma once

#include <QColor>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QPointF>
#include <QSizeF>

namespace openvegas {
namespace composition {

// Everything the reference's Text panel edits about a text layer.
//
// The panel (biff::ui::common::TextPanelWidget, setupUi FUN_1402f4410,
// retranslateUi FUN_1402fcd30) is three group boxes - Character, Paragraph and
// Background - and this is their contents, one field per control, named after
// the control. It is deliberately not a QFont: half of these are things a QFont
// has no room for (outline, background expansion, per-mille tracking), and the
// renderer needs them all in one place anyway.
//
// The values live on the layer's "text" effect as its parameter list, so the
// order below is a file format: appending is safe, reordering is not. Indices 0
// and 1 are the two the port already had, which is why they lead.
struct TextStyle
{
    enum class TextMode { Point, Paragraph };
    // --- what the layer says ------------------------------------------------
    QString text;
    int fontSize = 48;
    // Point text hangs off the layer origin: the first line's baseline sits on
    // it and each line is aligned to it (left from it, centred on it, right up
    // to it). Paragraph text fills a box of paragraphSize whose centre is
    // paragraphOffset from the origin, in layer space (Y up, like a position).
    TextMode textMode = TextMode::Point;
    QSizeF paragraphSize{640.0, 360.0};
    QPointF paragraphOffset;

    // --- Character ----------------------------------------------------------
    QString fontFamily;                    // empty = the application font
    QString fontStyle = QStringLiteral("Regular");   // Regular / Bold / Italic / Bold Italic
    QColor fontColor = QColor(255, 255, 255);

    struct Outline { double size = 1.0; QColor color = QColor(0, 0, 0); };
    QVector<Outline> additionalOutlines;    // first outline stays in legacy fields below
    double outlineSize = 0.0;              // px
    QColor outlineColor = QColor(0, 0, 0);
    // comboBoxStrokeOrder. The reference's three items, in its own order.
    enum class StrokeOrder
    {
        OverFill,
        UnderFill,
        Centered,
    };
    StrokeOrder strokeOrder = StrokeOrder::OverFill;

    double lineSpacing = 100.0;            // "Line Spacing", per cent
    double verticalScale = 100.0;          // "Vertical Scale", per cent
    double horizontalScale = 100.0;        // "Horizontal Scale", per cent
    // "Character Spacing". The reference shows it in per mille, which is what
    // its own field reads ("0%o"), so that is the unit kept here.
    double letterSpacing = 0.0;
    double baselineShift = 0.0;            // "Baseline Shift", per cent of the size

    // toolButtonAllCaps / SmallCaps / TitleCase / LowerCase - one at a time.
    enum class Caps
    {
        None,
        AllCaps,
        SmallCaps,
        StartCase,
        LowerCase,
    };
    Caps caps = Caps::None;

    bool underline = false;
    bool strikethrough = false;

    // toolButtonSuperscript / toolButtonSubscript - one at a time.
    enum class Script
    {
        None,
        Superscript,
        Subscript,
    };
    Script script = Script::None;

    // --- Paragraph ----------------------------------------------------------
    // The reference's seven horizontal buttons, in its build order.
    enum class AlignH
    {
        Left,
        Center,
        Right,
        LeftJustify,
        CenterJustify,
        RightJustify,
        Justify,
    };
    AlignH alignH = AlignH::Center;

    enum class AlignV
    {
        Top,
        Middle,
        Bottom,
    };
    AlignV alignV = AlignV::Middle;

    double indentLeft = 0.0;
    double indentRight = 0.0;
    double indentTop = 0.0;
    double indentBottom = 0.0;
    double indentFirstLine = 0.0;
    double spaceBeforeParagraph = 0.0;     // "Gap Before Paragraph"
    double spaceAfterParagraph = 0.0;      // "Gap After Paragraph"

    // --- Background ---------------------------------------------------------
    bool backgroundEnabled = false;
    QColor backgroundColor = QColor(0, 0, 0);
    double backgroundOpacity = 0.0;        // per cent; 0 means no background
    double backgroundRoundness = 0.0;      // "Background Corners", per cent
    double backgroundExpansionX = 0.0;     // percent of font size
    double backgroundExpansionY = 0.0;     // percent of font size
    // toolButtonExpansionLinked, "Enable Individual Expansion". The reference
    // words it the other way round: the button turns the two axes into separate
    // numbers, so linked is the default.
    bool expansionLinked = true;
};

// The effect's parameter list is the storage. Reading tolerates a short list -
// a project written before a field existed simply keeps that field's default -
// and writing always emits the full list.
TextStyle textStyleFromParameters(const QStringList& values);
QStringList textStyleToParameters(const TextStyle& style);

// Number of parameters textStyleToParameters() writes; the effect spec declares
// exactly this many so the inspector and the panel agree.
int textStyleParameterCount();

} // namespace composition
} // namespace openvegas
