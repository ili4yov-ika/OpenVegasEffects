#include "ui/TextSettingsDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QVBoxLayout>

namespace openvegas {
namespace ui {

TextSettingsDialog::TextSettingsDialog(QWidget* parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("TextSettingsDialog"));
    setWindowTitle(tr("Text Properties"));
    resize(460, 360);
    QVBoxLayout* outer = new QVBoxLayout(this);
    m_text = new QPlainTextEdit(this);
    m_text->setObjectName(QStringLiteral("plainTextEdit"));
    m_text->setPlaceholderText(tr("Text"));
    outer->addWidget(m_text, 1);
    QFormLayout* form = new QFormLayout;
    m_mode = new QComboBox(this);
    m_mode->setObjectName(QStringLiteral("comboBoxTextMode"));
    m_mode->addItem(tr("Point Text"), int(composition::TextStyle::TextMode::Point));
    m_mode->addItem(tr("Paragraph Text"), int(composition::TextStyle::TextMode::Paragraph));
    form->addRow(tr("Mode:"), m_mode);
    m_font = new QFontComboBox(this);
    m_font->setObjectName(QStringLiteral("comboBoxFont"));
    form->addRow(tr("Font:"), m_font);
    m_fontSize = new QSpinBox(this);
    m_fontSize->setObjectName(QStringLiteral("spinBoxFontSize"));
    m_fontSize->setRange(1, 1000);
    form->addRow(tr("Size:"), m_fontSize);
    m_width = new QDoubleSpinBox(this);
    m_width->setObjectName(QStringLiteral("spinBoxTextBoxWidth"));
    m_width->setRange(1, 100000); m_width->setDecimals(1);
    form->addRow(tr("Box width:"), m_width);
    m_height = new QDoubleSpinBox(this);
    m_height->setObjectName(QStringLiteral("spinBoxTextBoxHeight"));
    m_height->setRange(1, 100000); m_height->setDecimals(1);
    form->addRow(tr("Box height:"), m_height);
    outer->addLayout(form);
    const auto updateMode = [this] {
        const bool paragraph = m_mode->currentData().toInt()
                               == int(composition::TextStyle::TextMode::Paragraph);
        m_width->setEnabled(paragraph);
        m_height->setEnabled(paragraph);
    };
    connect(m_mode, &QComboBox::currentIndexChanged, this, [updateMode](int) { updateMode(); });
    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok |
                                                      QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    outer->addWidget(buttons);
    setStyle(composition::TextStyle());
}

void TextSettingsDialog::setStyle(const composition::TextStyle& style)
{
    m_style = style;
    m_text->setPlainText(style.text);
    m_mode->setCurrentIndex(m_mode->findData(int(style.textMode)));
    if (!style.fontFamily.isEmpty()) m_font->setCurrentFont(QFont(style.fontFamily));
    m_fontSize->setValue(style.fontSize);
    m_width->setValue(style.paragraphSize.width());
    m_height->setValue(style.paragraphSize.height());
    const bool paragraph = style.textMode == composition::TextStyle::TextMode::Paragraph;
    m_width->setEnabled(paragraph); m_height->setEnabled(paragraph);
}

composition::TextStyle TextSettingsDialog::style() const
{
    composition::TextStyle result = m_style;
    result.text = m_text->toPlainText();
    result.textMode = composition::TextStyle::TextMode(m_mode->currentData().toInt());
    result.fontFamily = m_font->currentFont().family();
    result.fontSize = m_fontSize->value();
    result.paragraphSize = QSizeF(m_width->value(), m_height->value());
    return result;
}

} // namespace ui
} // namespace openvegas
