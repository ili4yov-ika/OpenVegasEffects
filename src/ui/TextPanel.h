#pragma once

#include <QDockWidget>
#include <QList>

#include "composition/TextStyle.h"

class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QListWidget;
class QCheckBox;
class QToolButton;

namespace openvegas {
namespace ui {

class ColorSwatchButton;

// Mirrors the reference "Text" panel (type 128, biff::ui::common::TextPanelWidget,
// setupUi FUN_1402f4410, retranslateUi FUN_1402fcd30).
//
// It is a formatting panel for the selected text layer, not a place to type: the
// reference has no text field here at all - the string is edited on the canvas
// with the Text tool - and the panel is three group boxes:
//
//   groupBoxCharacter  font family and style, size, colour and its pipette, the
//                      outline (size, colour, stroke order, the outline list),
//                      line spacing, vertical and horizontal scale, character
//                      spacing, baseline shift, and eight toggles: All Caps,
//                      Small Caps, Start Case, Lower Case, Underline,
//                      Strikethrough, Superscript, Subscript.
//   groupBoxParagraph  seven horizontal alignments (three plain, three justify
//                      variants and full justify), three vertical alignments,
//                      the four indents, the first-line indent, and the gaps
//                      before and after a paragraph.
//   groupBoxBackground colour and its pipette, opacity, corner roundness, and
//                      the X and Y expansions with the link that separates them.
//
// Text layers are created from the timeline's New menu.
class TextPanel : public QDockWidget
{
    Q_OBJECT

public:
    explicit TextPanel(QWidget* parent = nullptr);

    // Style of the selected text layer. Passing no selection greys the panel,
    // which is what a formatting panel with nothing to format should do.
    void setStyle(const composition::TextStyle& style);
    void clearSelection();
    composition::TextStyle style() const { return m_style; }
    bool hasSelection() const { return m_hasSelection; }

    // Programmatic defaults; these do not create a layer or emit an edit.
    void setText(const QString& text);
    QString text() const;
    void setFontSize(int size);
    int fontSize() const;

signals:
    // The user changed something; the window applies it to the selected layer.
    void styleEdited(const composition::TextStyle& style);

private:
    void rebuildOutlines();
    void refreshFontStyles(const QString& preferredStyle);
    void pickScreenColor(ColorSwatchButton* swatch);

    // Pushes m_style into the controls; the guard stops that from looking like
    // an edit.
    void refresh();
    // Reads the controls back into m_style and reports it.
    void commit();
    void updateEnabled();

    composition::TextStyle m_style;
    bool m_hasSelection = false;
    bool m_updating = false;

    // Character
    QComboBox* m_fontFamily = nullptr;
    QComboBox* m_fontStyle = nullptr;
    QSpinBox* m_fontSize = nullptr;
    ColorSwatchButton* m_fontColor = nullptr;
    QDoubleSpinBox* m_outlineSize = nullptr;
    ColorSwatchButton* m_outlineColor = nullptr;
    QComboBox* m_strokeOrder = nullptr;
    QListWidget* m_outlines = nullptr;
    QDoubleSpinBox* m_lineSpacing = nullptr;
    QDoubleSpinBox* m_verticalScale = nullptr;
    QDoubleSpinBox* m_horizontalScale = nullptr;
    QDoubleSpinBox* m_letterSpacing = nullptr;
    QDoubleSpinBox* m_baselineShift = nullptr;
    QList<QToolButton*> m_capsButtons;     // AllCaps, SmallCaps, StartCase, LowerCase
    QToolButton* m_underline = nullptr;
    QToolButton* m_strikethrough = nullptr;
    QList<QToolButton*> m_scriptButtons;   // Superscript, Subscript

    // Paragraph
    QList<QToolButton*> m_alignH;          // seven, in the reference's order
    QList<QToolButton*> m_alignV;          // three
    QDoubleSpinBox* m_indentLeft = nullptr;
    QDoubleSpinBox* m_indentRight = nullptr;
    QDoubleSpinBox* m_indentTop = nullptr;
    QDoubleSpinBox* m_indentBottom = nullptr;
    QDoubleSpinBox* m_indentFirstLine = nullptr;
    QDoubleSpinBox* m_spaceBefore = nullptr;
    QDoubleSpinBox* m_spaceAfter = nullptr;

    // Background
    QCheckBox* m_backgroundEnabled = nullptr;
    ColorSwatchButton* m_backgroundColor = nullptr;
    QDoubleSpinBox* m_backgroundOpacity = nullptr;
    QDoubleSpinBox* m_backgroundRoundness = nullptr;
    QDoubleSpinBox* m_backgroundExpansionX = nullptr;
    QDoubleSpinBox* m_backgroundExpansionY = nullptr;
    QToolButton* m_expansionLinked = nullptr;

    QList<QWidget*> m_styleWidgets;        // everything greyed with no selection
};

} // namespace ui
} // namespace openvegas
