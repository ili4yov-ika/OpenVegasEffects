#include "ui/PromptMessage.h"

#include "app/Settings.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QSettings>

namespace openvegas {
namespace ui {

QString promptSettingKey(const QString& key)
{
    return QStringLiteral("Options/Prompts/") + key;
}

bool promptEnabled(const QString& key)
{
    return app::Settings::optionSettings().value(promptSettingKey(key), true).toBool();
}

QMessageBox::StandardButton showPrompt(
    QWidget* parent, const QString& key, QMessageBox::Icon icon, const QString& title,
    const QString& text, QMessageBox::StandardButtons buttons,
    QMessageBox::StandardButton defaultButton,
    const std::function<void(QMessageBox&)>& customize,
    QMessageBox::StandardButtons rememberable)
{
    const QString answerKey = promptSettingKey(key) + QStringLiteral("Answer");
    if (!promptEnabled(key)) {
        const int answer = app::Settings::optionSettings().value(answerKey, int(defaultButton)).toInt();
        return buttons.testFlag(QMessageBox::StandardButton(answer))
            ? QMessageBox::StandardButton(answer) : defaultButton;
    }
    QMessageBox box(icon, title, text, buttons, parent);
    box.setObjectName(QStringLiteral("prompt") + key);
    box.setDefaultButton(defaultButton);
    // The reference's wording, from its BiffEventFilter context.
    auto* again = new QCheckBox(
        QCoreApplication::translate("BiffEventFilter", "Do not show this again"), &box);
    again->setObjectName(QStringLiteral("promptDoNotShowAgain"));
    again->setToolTip(QCoreApplication::translate(
        "BiffEventFilter", "This setting can be reset in the Options dialog"));
    box.setCheckBox(again);
    if (customize) customize(box);
    box.exec();
    QMessageBox::StandardButton answer = box.standardButton(box.clickedButton());
    if (answer == QMessageBox::NoButton) {
        // Closed with Escape or the title bar: whatever the box treats as
        // its escape answer, and nothing to remember.
        answer = box.escapeButton() ? box.standardButton(box.escapeButton()) : defaultButton;
        return answer == QMessageBox::NoButton ? defaultButton : answer;
    }
    if (again->isChecked() && rememberable.testFlag(answer)) {
        QSettings settings = app::Settings::optionSettings();
        settings.setValue(promptSettingKey(key), false);
        settings.setValue(answerKey, int(answer));
    }
    return answer;
}

} // namespace ui
} // namespace openvegas
