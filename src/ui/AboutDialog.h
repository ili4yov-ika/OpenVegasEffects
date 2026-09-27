#pragma once

#include <QDialog>

namespace openvegas {
namespace ui {

namespace Ui {
class AboutDialog;
}

// First window built from a Qt Designer form (ui/AboutDialog.ui) rather than
// laid out in code. The reference ships an About window too - its localised
// licence text lives in Translations/<locale>/AboutLicenseWin.txt.
class AboutDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AboutDialog(QWidget* parent = nullptr);
    ~AboutDialog() override;

private:
    Ui::AboutDialog* m_ui = nullptr;
};

} // namespace ui
} // namespace openvegas
