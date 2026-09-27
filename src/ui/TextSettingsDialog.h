#pragma once

#include <QDialog>

#include "composition/TextStyle.h"

class QComboBox;
class QDoubleSpinBox;
class QFontComboBox;
class QPlainTextEdit;
class QSpinBox;

namespace openvegas {
namespace ui {

class TextSettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit TextSettingsDialog(QWidget* parent = nullptr);
    void setStyle(const composition::TextStyle& style);
    composition::TextStyle style() const;

private:
    QPlainTextEdit* m_text = nullptr;
    QComboBox* m_mode = nullptr;
    QFontComboBox* m_font = nullptr;
    QSpinBox* m_fontSize = nullptr;
    QDoubleSpinBox* m_width = nullptr;
    QDoubleSpinBox* m_height = nullptr;
    composition::TextStyle m_style;
};

} // namespace ui
} // namespace openvegas
