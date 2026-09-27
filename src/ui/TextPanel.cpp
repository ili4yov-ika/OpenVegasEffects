#include "ui/Theme.h"
#include "ui/TextPanel.h"
#include "ui_Text.h"
#include "ui/TextPropertyWidgets.h"
#include "ui/ScreenColorPicker.h"
#include <QListWidget>
#include <QCheckBox>
#include <QMenu>
#include <QTimer>

#include <QButtonGroup>
#include <QApplication>
#include <QCursor>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QScreen>
#include <QSignalBlocker>
#include <functional>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontDatabase>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPainter>
#include <QScrollArea>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

namespace openvegas {
namespace ui {

using composition::TextStyle;

// A colour swatch that opens a picker, as the reference draws beside its font
// and background colour labels.
class ColorSwatchButton : public QToolButton
{
    Q_DECLARE_TR_FUNCTIONS(openvegas::ui::ColorSwatchButton)
public:
    explicit ColorSwatchButton(QWidget* parent = nullptr)
        : QToolButton(parent)
    {
        setFixedSize(56, 20);
        setProperty("colorSwatch", true);
        // An unqualified rule also applies to the child QColorDialog and all
        // its controls. Scope the size to this swatch instead.
        setStyleSheet(QStringLiteral("QToolButton[colorSwatch=\"true\"] { "
            "min-width: 56px; max-width: 56px; min-height: 20px; max-height: 20px; }"));
        setAutoRaise(false);
        // Report only an accepted change; cancelling must not dirty the project.
        connect(this, &QToolButton::clicked, this, [this] {
            const QColor picked =
                interfaceColor(m_color, window(), tr("Pick a color"));
            if (picked.isValid() && picked != m_color) {
                setColor(picked);
                if (colorPicked) colorPicked();
            }
        });
    }

    std::function<void()> colorPicked;
    QColor color() const { return m_color; }

    void setColor(const QColor& color)
    {
        if (!color.isValid() || color == m_color) {
            return;
        }
        m_color = color;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setOpacity(isEnabled() ? 1.0 : 0.35);
        painter.fillRect(rect().adjusted(1, 1, -1, -1), m_color);
        painter.setPen(QColor(90, 90, 95));
        painter.drawRect(rect().adjusted(0, 0, -1, -1));
    }

private:
    QColor m_color = QColor(255, 255, 255);
};

namespace {

QIcon textIcon(const QString& name)
{
    return QIcon(QStringLiteral(":/text-icons/%1.svg").arg(name));
}

QIcon pipetteIcon() { return textIcon(QStringLiteral("pipette")); }

void setToggleIcon(QToolButton* button)
{
    if (!button) return;
    const QHash<QString, QString> icons = {
        {"toolButtonAllCaps", "allcaps"}, {"toolButtonSmallCaps", "smallcaps"},
        {"toolButtonTitleCase", "startcase"}, {"toolButtonLowerCase", "lowercase"},
        {"toolButtonUnderline", "underline"}, {"toolButtonStrikethrough", "strikethrough"},
        {"toolButtonSuperscript", "superscript"}, {"toolButtonSubscript", "subscript"},
        {"toolButtonExpansionLinked", "expansion-linked"}
    };
    const auto icon = icons.constFind(button->objectName());
    if (icon != icons.constEnd()) button->setIcon(textIcon(icon.value()));
    button->setIconSize(QSize(16, 16));
}

// Designer cannot instantiate the TextScrubber<T> template used by controls
// created in code. Install the same drag gesture on the ordinary spin boxes
// loaded from Text.ui so moving over the editor changes the value consistently.
class DesignerSpinScrubber final : public QObject
{
public:
    explicit DesignerSpinScrubber(QDoubleSpinBox* spin)
        : QObject(spin), m_spin(spin)
    {
    }

protected:
    bool eventFilter(QObject*, QEvent* event) override
    {
        if (event->type() == QEvent::MouseButtonPress) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton) {
                m_pressed = true;
                m_start = mouse->globalPosition();
                m_value = m_spin->value();
            }
        } else if (event->type() == QEvent::MouseMove && m_pressed) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            const double delta = mouse->globalPosition().x() - m_start.x();
            if (!m_dragging && std::abs(delta) >= QApplication::startDragDistance()) {
                m_dragging = true;
                QApplication::setOverrideCursor(Qt::SizeHorCursor);
            }
            if (m_dragging) {
                const double speed = mouse->modifiers().testFlag(Qt::ShiftModifier) ? 0.1
                                   : mouse->modifiers().testFlag(Qt::ControlModifier) ? 10.0
                                                                                     : 1.0;
                m_spin->setValue(m_value + std::round(delta / 2.0)
                                               * m_spin->singleStep() * speed);
                return true;
            }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            const bool dragged = m_dragging;
            endDrag();
            if (dragged) return true;
        } else if (event->type() == QEvent::KeyPress
                   && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape
                   && m_dragging) {
            m_spin->setValue(m_value);
            endDrag();
            return true;
        } else if (event->type() == QEvent::FocusOut || event->type() == QEvent::Hide) {
            endDrag();
        }
        return false;
    }

private:
    void endDrag()
    {
        if (m_dragging) QApplication::restoreOverrideCursor();
        m_pressed = false;
        m_dragging = false;
    }

    QDoubleSpinBox* m_spin = nullptr;
    QPointF m_start;
    double m_value = 0.0;
    bool m_pressed = false;
    bool m_dragging = false;
};

void installDesignerScrubber(QDoubleSpinBox* spin)
{
    if (!spin) return;
    if (auto* editor = spin->findChild<QLineEdit*>()) {
        editor->installEventFilter(new DesignerSpinScrubber(spin));
    }
    spin->setKeyboardTracking(false);
}

QDoubleSpinBox* makeSpin(QWidget* parent, const QString& objectName, double minimum,
                         double maximum, const QString& suffix = QString())
{
    auto* spin = new TextScrubber<QDoubleSpinBox>(parent);
    spin->setObjectName(objectName);
    spin->setRange(minimum, maximum);
    spin->setDecimals(1);
    spin->setMinimumWidth(48);
    spin->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    spin->setSuffix(suffix);
    return spin;
}

QIcon alignmentIcon(int index, bool vertical = false)
{
    static const QStringList horizontal = {"text-left-align", "text-center-align", "text-right-align",
        "text-left-justified", "text-center-justified", "text-right-justified", "text-justified"};
    static const QStringList verticals = {"text-top-align", "text-middle-align", "text-bottom-align"};
    return textIcon((vertical ? verticals : horizontal).value(index));
}

QLabel* makeLabel(QWidget* parent, const QString& objectName, const QString& text)
{
    static const QHash<QString, QString> icons = {
        {"labelFontFamily", "font"}, {"labelFontSize", "font-size"},
        {"labelFontColor", "font-color"}, {"labelFontOutlineSize1", "font-outline-size"},
        {"labelFontOutlineSize2", "font-outline-size"}, {"labelFontOutlineColor", "font-outline-color"},
        {"labelLineHeight", "line-height"}, {"labelVerticalScale", "letter-scaling-vertical"},
        {"labelHorizontalScale", "letter-scaling-horizontal"}, {"labelLetterSpacing", "letter-spacing"},
        {"labelBaselineShift", "baseline-shift"}, {"labelIndentLeft", "text-left-indent"},
        {"labelIndentRight", "text-right-indent"}, {"labelIndentTop", "text-top-indent"},
        {"labelIndentBottom", "text-bottom-indent"}, {"labelIndentFirstLine", "text-first-line-indent"},
        {"labelSpaceBeforeParagraph", "text-gap-before-paragraph"},
        {"labelSpaceAfterParagraph", "text-gap-after-paragraph"},
        {"labelBackgroundColor", "text-background-color"},
        {"labelBackgroundOpacity", "text-background-opacity"},
        {"labelBackgroundRoundness", "text-background-radius"},
        {"labelBackgroundExpansionX", "text-background-expansion-x"},
        {"labelBackgroundExpansionY", "text-background-expansion-y"}
    };
    auto* label = new QLabel(parent);
    label->setObjectName(objectName);
    label->setToolTip(text);
    label->setAccessibleName(text);
    label->setPixmap(textIcon(icons.value(objectName)).pixmap(QSize(20, 20), parent->devicePixelRatioF()));
    label->setFixedSize(20, 22);
    return label;
}

QWidget* sectionBody(QGroupBox* group, const QString& name, const QString& title)
{
    group->setTitle(QString());
    auto* layout = new QVBoxLayout(group);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    auto* heading = new QLabel(title, group);
    heading->setObjectName(QStringLiteral("label") + name);
    heading->setProperty("textSectionHeading", true);
    heading->setFixedHeight(28);
    layout->addWidget(heading);
    auto* content = new QWidget(group);
    content->setObjectName(QStringLiteral("widget") + name);
    layout->addWidget(content);
    return content;
}


} // namespace

TextPanel::TextPanel(QWidget* parent)
    : QDockWidget(parent)
{
    setWindowTitle(tr("Text"));
    setObjectName(QStringLiteral("textPanel"));
    setStyleSheet(QStringLiteral(R"(
        QWidget#TextPanelWidget { background: #303030; }
        QWidget#TextPanelWidget QWidget { background: transparent; }
        QWidget#TextPanelWidget QGroupBox { border: 1px solid #292929; margin: 0; }
        QWidget#TextPanelWidget QLabel[textSectionHeading="true"] { background: #292929; padding-left: 12px; color: #d0d0d0; }
        QWidget#TextPanelWidget QToolButton { padding: 0; min-width: 0; min-height: 0;
            border-radius: 0; background: transparent; border: 0; }
        QWidget#TextPanelWidget QToolButton:hover { background: #444444; }
        QWidget#TextPanelWidget QToolButton:checked { background: #009fdf; }
        QWidget#TextPanelWidget QSpinBox, QWidget#TextPanelWidget QDoubleSpinBox,
        QWidget#TextPanelWidget QComboBox { background: #282828; border: 0; border-radius: 0;
            padding: 2px 16px 2px 5px; min-height: 18px; color: #bdbdbd; }
        QWidget#TextPanelWidget QSpinBox:disabled, QWidget#TextPanelWidget QDoubleSpinBox:disabled,
        QWidget#TextPanelWidget QComboBox:disabled { color: #696969; }
        QWidget#TextPanelWidget QAbstractSpinBox::up-button { subcontrol-origin: border;
            subcontrol-position: top right; width: 12px; border: 0; background: transparent; }
        QWidget#TextPanelWidget QAbstractSpinBox::down-button { subcontrol-origin: border;
            subcontrol-position: bottom right; width: 12px; border: 0; background: transparent; }
        QWidget#TextPanelWidget QAbstractSpinBox::up-arrow { image: url(:/text-icons/arrow-up.svg); width: 9px; height: 9px; }
        QWidget#TextPanelWidget QAbstractSpinBox::down-arrow { image: url(:/text-icons/arrow-down.svg); width: 9px; height: 9px; }
        QWidget#TextPanelWidget QComboBox::drop-down { border: 0; width: 18px; }
        QWidget#TextPanelWidget QComboBox::down-arrow { image: url(:/text-icons/arrow-down.svg); width: 14px; height: 14px; }
        QWidget#TextPanelWidget QListWidget { background: transparent; border: 0; }
    )"));

    Ui::TextPanel form;
    form.setupUi(this);
    form.textLayout->setSizeConstraint(QLayout::SetNoConstraint);
    form.TextPanelWidget->setMinimumWidth(0);
    form.TextPanelWidget->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    form.characterGrid->setColumnStretch(1, 4);
    form.characterGrid->setColumnStretch(5, 2);
    m_fontFamily = form.comboBoxFontFamily;
    m_fontFamily->clear();
    m_fontFamily->addItems(QFontDatabase::families());
    m_fontFamily->setEditable(true);
    m_fontFamily->setInsertPolicy(QComboBox::NoInsert);
    m_fontStyle = form.comboBoxFontStyle;
    m_fontSize = form.spinBoxFontSize;
    m_outlineSize = form.doubleSpinBoxFontOutlineSize;
    m_strokeOrder = form.comboBoxStrokeOrder;
    m_outlines = form.listViewOutlines;
    m_lineSpacing = form.doubleSpinBoxLineHeight;
    m_verticalScale = form.doubleSpinBoxVerticalScale;
    m_horizontalScale = form.doubleSpinBoxHorizontalScale;
    m_letterSpacing = form.doubleSpinBoxLetterSpacing;
    m_baselineShift = form.doubleSpinBoxBaselineShift;
    m_capsButtons = {form.toolButtonAllCaps, form.toolButtonSmallCaps,
                     form.toolButtonTitleCase, form.toolButtonLowerCase};
    m_underline = form.toolButtonUnderline;
    m_strikethrough = form.toolButtonStrikethrough;
    m_scriptButtons = {form.toolButtonSuperscript, form.toolButtonSubscript};
    m_alignH = {form.toolButtonAlignLeft, form.toolButtonAlignCenter,
                form.toolButtonAlignRight, form.toolButtonAlignLeftJustify,
                form.toolButtonAlignCenterJustify, form.toolButtonAlignRightJustify,
                form.toolButtonAlignJustify};
    m_alignV = {form.toolButtonAlignTop, form.toolButtonAlignMiddle,
                form.toolButtonAlignBottom};
    m_indentLeft = form.doubleSpinBoxIndentLeft;
    m_indentRight = form.doubleSpinBoxIndentRight;
    m_indentTop = form.doubleSpinBoxIndentTop;
    m_indentBottom = form.doubleSpinBoxIndentBottom;
    m_indentFirstLine = form.doubleSpinBoxIndentFirstLine;
    m_spaceBefore = form.doubleSpinBoxSpaceBeforeParagraph;
    m_spaceAfter = form.doubleSpinBoxSpaceAfterParagraph;
    m_backgroundEnabled = form.checkBoxBackground;
    m_backgroundOpacity = form.doubleSpinBoxBackgroundOpacity;
    m_backgroundRoundness = form.doubleSpinBoxBackgroundRoundness;
    m_backgroundExpansionX = form.doubleSpinBoxBackgroundExpansionX;
    m_backgroundExpansionY = form.doubleSpinBoxBackgroundExpansionY;
    m_expansionLinked = form.toolButtonExpansionLinked;

    for (QToolButton* button : {form.toolButtonAllCaps, form.toolButtonSmallCaps,
                                form.toolButtonTitleCase, form.toolButtonLowerCase,
                                form.toolButtonUnderline, form.toolButtonStrikethrough,
                                form.toolButtonSuperscript, form.toolButtonSubscript,
                                form.toolButtonExpansionLinked}) {
        setToggleIcon(button);
    }
    for (QDoubleSpinBox* spin : form.TextPanelWidget->findChildren<QDoubleSpinBox*>()) {
        installDesignerScrubber(spin);
    }

    const auto replaceBoxSwatch = [](QBoxLayout* layout, QToolButton* oldButton) {
        const int index = layout->indexOf(oldButton);
        QWidget* parent = oldButton->parentWidget();
        layout->removeWidget(oldButton);
        delete oldButton;
        auto* swatch = new ColorSwatchButton(parent);
        layout->insertWidget(index, swatch);
        return swatch;
    };
    m_fontColor = replaceBoxSwatch(form.fontColorRow, form.toolButtonFontColor);
    m_fontColor->setObjectName(QStringLiteral("toolButtonFontColor"));
    m_outlineColor = replaceBoxSwatch(form.outlineColors, form.toolButtonFontOutlineColor);
    m_outlineColor->setObjectName(QStringLiteral("toolButtonFontOutlineColor"));
    int row = 0, column = 0, rowSpan = 1, columnSpan = 1;
    const int backgroundIndex = form.backgroundGrid->indexOf(form.toolButtonBackgroundColor);
    form.backgroundGrid->getItemPosition(backgroundIndex, &row, &column, &rowSpan, &columnSpan);
    QWidget* backgroundParent = form.toolButtonBackgroundColor->parentWidget();
    form.backgroundGrid->removeWidget(form.toolButtonBackgroundColor);
    delete form.toolButtonBackgroundColor;
    m_backgroundColor = new ColorSwatchButton(backgroundParent);
    m_backgroundColor->setObjectName(QStringLiteral("toolButtonBackgroundColor"));
    form.backgroundGrid->addWidget(m_backgroundColor, row, column, rowSpan, columnSpan, Qt::AlignLeft);

    form.toolButtonFontColorPipette->setIcon(pipetteIcon());
    form.toolButtonFontOutlineColorPipette->setIcon(pipetteIcon());
    form.toolButtonBackgroundColorPipette->setIcon(pipetteIcon());
    form.toolButtonAddOutline->setIcon(textIcon(QStringLiteral("add")));
    connect(form.toolButtonFontColorPipette, &QToolButton::clicked, this,
            [this] { pickScreenColor(m_fontColor); });
    connect(form.toolButtonFontOutlineColorPipette, &QToolButton::clicked, this,
            [this] { pickScreenColor(m_outlineColor); });
    connect(form.toolButtonBackgroundColorPipette, &QToolButton::clicked, this,
            [this] { pickScreenColor(m_backgroundColor); });
    connect(form.toolButtonAddOutline, &QToolButton::clicked, this, [this] {
        m_style.additionalOutlines.append({qMax(1.0, m_outlineSize->value() + 2.0), Qt::black});
        rebuildOutlines();
        commit();
    });
    for (int i = 0; i < m_alignH.size(); ++i) {
        m_alignH[i]->setIcon(alignmentIcon(i));
        connect(m_alignH[i], &QToolButton::clicked, this, [this, button = m_alignH[i]] {
            for (QToolButton* other : m_alignH) other->setChecked(other == button);
            commit();
        });
    }
    for (int i = 0; i < m_alignV.size(); ++i) {
        m_alignV[i]->setIcon(alignmentIcon(i, true));
        connect(m_alignV[i], &QToolButton::clicked, this, [this, button = m_alignV[i]] {
            for (QToolButton* other : m_alignV) other->setChecked(other == button);
            commit();
        });
    }
    for (QToolButton* button : m_capsButtons) {
        connect(button, &QToolButton::clicked, this, [this, button] {
            for (QToolButton* other : m_capsButtons) if (other != button) other->setChecked(false);
            commit();
        });
    }
    for (QToolButton* button : m_scriptButtons) {
        connect(button, &QToolButton::clicked, this, [this, button] {
            for (QToolButton* other : m_scriptButtons) if (other != button) other->setChecked(false);
            commit();
        });
    }
    QFont underlineFont = m_underline->font(); underlineFont.setUnderline(true);
    m_underline->setFont(underlineFont);
    QFont strikeFont = m_strikethrough->font(); strikeFont.setStrikeOut(true);
    m_strikethrough->setFont(strikeFont);
    connect(m_underline, &QToolButton::clicked, this, [this] { commit(); });
    connect(m_strikethrough, &QToolButton::clicked, this, [this] { commit(); });
    connect(m_fontFamily, &QComboBox::currentTextChanged, this, [this](const QString&) {
        if (m_updating || m_fontFamily->findText(m_fontFamily->currentText()) < 0) return;
        refreshFontStyles(m_fontStyle->currentText());
        commit();
    });
    connect(m_fontFamily->lineEdit(), &QLineEdit::editingFinished, this, [this] {
        if (m_updating) return;
        const int index = m_fontFamily->findText(m_fontFamily->currentText(), Qt::MatchFixedString);
        if (index >= 0) { m_fontFamily->setCurrentIndex(index); refreshFontStyles(m_fontStyle->currentText()); commit(); }
        else refresh();
    });
    connect(m_fontStyle, &QComboBox::currentIndexChanged, this, [this](int) { commit(); });
    connect(m_fontSize, &QSpinBox::valueChanged, this, [this](int) { commit(); });
    connect(m_strokeOrder, &QComboBox::currentIndexChanged, this, [this](int) { commit(); });
    for (QDoubleSpinBox* spin : {m_outlineSize, m_lineSpacing, m_verticalScale,
         m_horizontalScale, m_letterSpacing, m_baselineShift, m_indentLeft, m_indentRight,
         m_indentTop, m_indentBottom, m_indentFirstLine, m_spaceBefore, m_spaceAfter})
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this](double) { commit(); });
    m_fontColor->colorPicked = [this] { commit(); };
    m_outlineColor->colorPicked = [this] { commit(); };
    m_backgroundColor->colorPicked = [this] { commit(); };
    connect(m_backgroundEnabled, &QCheckBox::toggled, this, [this, content = form.widgetBackground](bool enabled) {
        content->setEnabled(enabled);
        if (!m_updating && enabled && m_backgroundOpacity->value() == 0) {
            const QSignalBlocker blocker(m_backgroundOpacity); m_backgroundOpacity->setValue(100);
        }
        commit();
    });
    for (QDoubleSpinBox* spin : {m_backgroundOpacity, m_backgroundRoundness,
                                 m_backgroundExpansionX, m_backgroundExpansionY}) {
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this, spin](double value) {
            if (!m_updating && !m_expansionLinked->isChecked()) {
                QDoubleSpinBox* other = spin == m_backgroundExpansionX ? m_backgroundExpansionY
                                      : spin == m_backgroundExpansionY ? m_backgroundExpansionX : nullptr;
                if (other) { const QSignalBlocker blocker(other); other->setValue(value); }
            }
            commit();
        });
    }
    connect(m_expansionLinked, &QToolButton::clicked, this, [this] {
        if (!m_expansionLinked->isChecked()) {
            const QSignalBlocker blocker(m_backgroundExpansionY);
            m_backgroundExpansionY->setValue(m_backgroundExpansionX->value());
        }
        commit();
    });
    refreshFontStyles(m_style.fontStyle);
    rebuildOutlines();

    clearSelection();
}

void TextPanel::rebuildOutlines()
{
    if (!m_outlines) return;
    m_outlines->clear();
    m_outlines->setVisible(!m_style.additionalOutlines.isEmpty());
    m_outlines->setFixedHeight(qMin(4, int(m_style.additionalOutlines.size())) * 30 + 2);
    for (int i = 0; i < m_style.additionalOutlines.size(); ++i) {
        auto* item = new QListWidgetItem(m_outlines);
        item->setSizeHint(QSize(200, 30));
        auto* row = new QWidget(m_outlines);
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 2, 0, 2);
        layout->setSpacing(4);
        layout->addWidget(makeLabel(row, QStringLiteral("labelFontOutlineSize1"), tr("Outline %1").arg(i + 2)));
        auto* size = makeSpin(row, QStringLiteral("outlineSize%1").arg(i), 0, 400);
        size->setDecimals(2);
        size->setValue(m_style.additionalOutlines[i].size);
        size->setAccessibleName(tr("Outline %1 Size").arg(i + 2));
        layout->addWidget(size, 1);
        auto* color = new ColorSwatchButton(row);
        color->setObjectName(QStringLiteral("outlineColor%1").arg(i));
        color->setColor(m_style.additionalOutlines[i].color);
        color->setAccessibleName(tr("Outline %1 Color").arg(i + 2));
        layout->addWidget(color);
        auto* pipette = new QToolButton(row);
        pipette->setObjectName(QStringLiteral("outlinePipette%1").arg(i));
        pipette->setIcon(pipetteIcon());
        pipette->setFixedSize(22, 22);
        pipette->setToolTip(tr("Pick a color on screen"));
        pipette->setAccessibleName(tr("Outline %1: Pick a color on screen").arg(i + 2));
        layout->addWidget(pipette);
        auto* remove = new QToolButton(row);
        remove->setObjectName(QStringLiteral("removeOutline%1").arg(i));
        remove->setIcon(textIcon(QStringLiteral("remove")));
        remove->setFixedSize(22, 22);
        remove->setToolTip(tr("Remove Outline"));
        remove->setAccessibleName(tr("Remove Outline %1").arg(i + 2));
        layout->addWidget(remove);
        m_outlines->setItemWidget(item, row);
        connect(size, &QDoubleSpinBox::valueChanged, this, [this, i](double value) {
            if (m_updating || i >= m_style.additionalOutlines.size()) return;
            m_style.additionalOutlines[i].size = value;
            commit();
        });
        color->colorPicked = [this, i, color] {
            if (i >= m_style.additionalOutlines.size()) return;
            m_style.additionalOutlines[i].color = color->color();
            commit();
        };
        connect(pipette, &QToolButton::clicked, this, [this, color] { pickScreenColor(color); });
        connect(remove, &QToolButton::clicked, this, [this, i] {
            if (i >= m_style.additionalOutlines.size()) return;
            m_style.additionalOutlines.removeAt(i);
            // Do not destroy the clicked button while Qt is dispatching its event.
            QTimer::singleShot(0, this, [this] { rebuildOutlines(); });
            commit();
        });
    }
}

void TextPanel::refreshFontStyles(const QString& preferredStyle)
{
    const QSignalBlocker blocker(m_fontStyle);
    m_fontStyle->clear();
    m_fontStyle->addItems(QFontDatabase::styles(m_fontFamily->currentText()));
    if (m_fontStyle->count() == 0) m_fontStyle->addItem(QStringLiteral("Regular"));
    int index = m_fontStyle->findText(preferredStyle, Qt::MatchFixedString);
    // Preserve imported styles on refresh, even if the font is unavailable.
    if (index < 0 && m_updating && !preferredStyle.isEmpty()) {
        m_fontStyle->addItem(preferredStyle);
        index = m_fontStyle->count() - 1;
    }
    m_fontStyle->setCurrentIndex(index >= 0 ? index : 0);
}

void TextPanel::pickScreenColor(ColorSwatchButton* swatch)
{
    if (!m_hasSelection) return;
    auto* picker = new ScreenColorPicker(this, [this, swatch](const QColor& color) {
        if (color == swatch->color()) return;
        swatch->setColor(color);
        if (swatch->colorPicked) swatch->colorPicked();
    });
    picker->show();
    picker->activateWindow();
    picker->setFocus();
}

void TextPanel::setStyle(const TextStyle& style)
{
    m_style = style;
    m_hasSelection = true;
    refresh();
    updateEnabled();
}

void TextPanel::clearSelection()
{
    m_hasSelection = false;
    refresh();
    updateEnabled();
}

void TextPanel::refresh()
{
    if (!m_fontFamily) {
        return;
    }
    m_updating = true;

    const QString family = m_style.fontFamily.isEmpty() ? QApplication::font().family()
                                                       : m_style.fontFamily;
    int index = m_fontFamily->findText(family);
    if (index < 0) {
        m_fontFamily->addItem(family);
        index = m_fontFamily->count() - 1;
    }
    m_fontFamily->setCurrentIndex(index);
    refreshFontStyles(m_style.fontStyle);
    m_fontSize->setValue(m_style.fontSize);
    rebuildOutlines();
    m_fontColor->setColor(m_style.fontColor);
    m_outlineSize->setValue(m_style.outlineSize);
    m_outlineColor->setColor(m_style.outlineColor);
    m_strokeOrder->setCurrentIndex(static_cast<int>(m_style.strokeOrder));
    m_lineSpacing->setValue(m_style.lineSpacing);
    m_verticalScale->setValue(m_style.verticalScale);
    m_horizontalScale->setValue(m_style.horizontalScale);
    m_letterSpacing->setValue(m_style.letterSpacing);
    m_baselineShift->setValue(m_style.baselineShift);

    for (int i = 0; i < m_capsButtons.size(); ++i) {
        // Caps::None is 0, so the buttons run from 1.
        m_capsButtons.at(i)->setChecked(static_cast<int>(m_style.caps) == i + 1);
    }
    m_underline->setChecked(m_style.underline);
    m_strikethrough->setChecked(m_style.strikethrough);
    for (int i = 0; i < m_scriptButtons.size(); ++i) {
        m_scriptButtons.at(i)->setChecked(static_cast<int>(m_style.script) == i + 1);
    }

    for (int i = 0; i < m_alignH.size(); ++i) {
        m_alignH.at(i)->setChecked(static_cast<int>(m_style.alignH) == i);
    }
    for (int i = 0; i < m_alignV.size(); ++i) {
        m_alignV.at(i)->setChecked(static_cast<int>(m_style.alignV) == i);
    }
    m_indentLeft->setValue(m_style.indentLeft);
    m_indentRight->setValue(m_style.indentRight);
    m_indentTop->setValue(m_style.indentTop);
    m_indentBottom->setValue(m_style.indentBottom);
    m_indentFirstLine->setValue(m_style.indentFirstLine);
    m_spaceBefore->setValue(m_style.spaceBeforeParagraph);
    m_spaceAfter->setValue(m_style.spaceAfterParagraph);

    m_backgroundEnabled->setChecked(m_style.backgroundEnabled);
    findChild<QWidget*>(QStringLiteral("widgetBackground"))->setEnabled(m_style.backgroundEnabled);
    m_backgroundColor->setColor(m_style.backgroundColor);
    m_backgroundOpacity->setValue(m_style.backgroundOpacity);
    m_backgroundRoundness->setValue(m_style.backgroundRoundness);
    m_backgroundExpansionX->setValue(m_style.backgroundExpansionX);
    m_backgroundExpansionY->setValue(m_style.expansionLinked ? m_style.backgroundExpansionX
                                                          : m_style.backgroundExpansionY);
    // The reference's button reads "Enable Individual Expansion", so it is
    // checked when the two axes are *not* linked.
    m_expansionLinked->setChecked(!m_style.expansionLinked);

    m_updating = false;
}

void TextPanel::commit()
{
    if (m_updating || !m_hasSelection) {
        return;
    }

    m_style.fontFamily = m_fontFamily->currentText();
    m_style.fontStyle = m_fontStyle->currentText();
    m_style.fontSize = m_fontSize->value();
    m_style.fontColor = m_fontColor->color();
    m_style.outlineSize = m_outlineSize->value();
    m_style.outlineColor = m_outlineColor->color();
    m_style.strokeOrder = static_cast<TextStyle::StrokeOrder>(m_strokeOrder->currentIndex());
    m_style.lineSpacing = m_lineSpacing->value();
    m_style.verticalScale = m_verticalScale->value();
    m_style.horizontalScale = m_horizontalScale->value();
    m_style.letterSpacing = m_letterSpacing->value();
    m_style.baselineShift = m_baselineShift->value();

    m_style.caps = TextStyle::Caps::None;
    for (int i = 0; i < m_capsButtons.size(); ++i) {
        if (m_capsButtons.at(i)->isChecked()) {
            m_style.caps = static_cast<TextStyle::Caps>(i + 1);
            break;
        }
    }
    m_style.underline = m_underline->isChecked();
    m_style.strikethrough = m_strikethrough->isChecked();
    m_style.script = TextStyle::Script::None;
    for (int i = 0; i < m_scriptButtons.size(); ++i) {
        if (m_scriptButtons.at(i)->isChecked()) {
            m_style.script = static_cast<TextStyle::Script>(i + 1);
            break;
        }
    }

    for (int i = 0; i < m_alignH.size(); ++i) {
        if (m_alignH.at(i)->isChecked()) {
            m_style.alignH = static_cast<TextStyle::AlignH>(i);
            break;
        }
    }
    for (int i = 0; i < m_alignV.size(); ++i) {
        if (m_alignV.at(i)->isChecked()) {
            m_style.alignV = static_cast<TextStyle::AlignV>(i);
            break;
        }
    }
    m_style.indentLeft = m_indentLeft->value();
    m_style.indentRight = m_indentRight->value();
    m_style.indentTop = m_indentTop->value();
    m_style.indentBottom = m_indentBottom->value();
    m_style.indentFirstLine = m_indentFirstLine->value();
    m_style.spaceBeforeParagraph = m_spaceBefore->value();
    m_style.spaceAfterParagraph = m_spaceAfter->value();

    m_style.backgroundEnabled = m_backgroundEnabled->isChecked();
    m_style.backgroundColor = m_backgroundColor->color();
    m_style.backgroundOpacity = m_backgroundOpacity->value();
    m_style.backgroundRoundness = m_backgroundRoundness->value();
    m_style.backgroundExpansionX = m_backgroundExpansionX->value();
    m_style.backgroundExpansionY = m_backgroundExpansionY->value();
    m_style.expansionLinked = !m_expansionLinked->isChecked();

    emit styleEdited(m_style);
}

void TextPanel::updateEnabled()
{
    if (!m_fontFamily) {
        return;
    }
    if (m_styleWidgets.isEmpty()) {
        // Disable formatting when there is no selected text clip.
        const auto groups = findChildren<QGroupBox*>();
        for (QGroupBox* group : groups) {
            m_styleWidgets.append(group);
        }
    }
    for (QWidget* widget : m_styleWidgets) {
        widget->setEnabled(m_hasSelection);
    }
}

void TextPanel::setText(const QString& content)
{
    m_style.text = content;
}

QString TextPanel::text() const
{
    return m_style.text;
}

void TextPanel::setFontSize(int size)
{
    m_style.fontSize = size;
    if (m_fontSize) {
        const bool guard = m_updating;
        m_updating = true;
        m_fontSize->setValue(size);
        m_updating = guard;
    }
}

int TextPanel::fontSize() const
{
    return m_fontSize ? m_fontSize->value() : m_style.fontSize;
}

} // namespace ui
} // namespace openvegas
