#pragma once

#include <QMessageBox>
#include <QString>

#include <functional>

namespace openvegas {
namespace ui {

// Setting that says whether a prompt is shown: Options/Prompts/<key> in the
// options ini, true by default. The Options dialog's "Prompts & Warnings"
// page edits the same keys.
QString promptSettingKey(const QString& key);
bool promptEnabled(const QString& key);

// A message box gated by one of those settings, like the reference's
// BiffEventFilter for boxes tagged "add-messagebox-auto-accept" or
// "add-messagebox-remember-choice": it adds "Do not show this again"; once
// that is ticked the box is skipped and the button chosen then is returned
// (Options/Prompts/<key>Answer). Only answers in `rememberable` are stored, so
// "Exit" on a startup warning cannot lock the application out. `customize`
// may rename buttons or set the text format before the box is shown.
QMessageBox::StandardButton showPrompt(
    QWidget* parent, const QString& key, QMessageBox::Icon icon, const QString& title,
    const QString& text, QMessageBox::StandardButtons buttons,
    QMessageBox::StandardButton defaultButton,
    const std::function<void(QMessageBox&)>& customize = {},
    QMessageBox::StandardButtons rememberable = QMessageBox::StandardButtons(~0u));

} // namespace ui
} // namespace openvegas
