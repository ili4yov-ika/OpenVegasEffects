#pragma once

#include <QCoreApplication>
#include <QString>
#include <QVariant>

namespace openvegas::app {

// Native ProjectSettings/Options AntialiasingMode values (FUN_140268720).
inline constexpr const char* kAntialiasingLabels[] = {
    QT_TRANSLATE_NOOP("biff::ui::common", "4x MSAA"),  QT_TRANSLATE_NOOP("biff::ui::common", "8x MSAA"),
    QT_TRANSLATE_NOOP("biff::ui::common", "16x MSAA"), QT_TRANSLATE_NOOP("biff::ui::common", "32x MSAA"),
    QT_TRANSLATE_NOOP("biff::ui::common", "8x CSAA"),  QT_TRANSLATE_NOOP("biff::ui::common", "8xQ CSAA"),
    QT_TRANSLATE_NOOP("biff::ui::common", "16x CSAA"), QT_TRANSLATE_NOOP("biff::ui::common", "16xQ CSAA"),
    QT_TRANSLATE_NOOP("biff::ui::common", "32x CSAA"),
};

inline int antialiasingMode(const QVariant& stored)
{
    bool numeric = false;
    const int mode = stored.toInt(&numeric);
    if (numeric && mode >= 1 && mode <= 9) return mode;
    const QString text = stored.toString().trimmed();
    for (int index = 0; index < 9; ++index) {
        if (text.compare(QLatin1String(kAntialiasingLabels[index]), Qt::CaseInsensitive) == 0
            || text == QCoreApplication::translate("biff::ui::common", kAntialiasingLabels[index]))
            return index + 1;
    }
    // Old Off/2x MSAA choices have no equivalent in the native 1..9 enum.
    return 1;
}

namespace detail {
struct LegacyChoice {
    const char* token;
    const char* russian;
    const char* japanese;
    const char* chinese;
};
inline QString choiceToken(const QString& stored, const LegacyChoice* choices,
                           int count, const char* fallback)
{
    const QString text = stored.trimmed();
    for (int index = 0; index < count; ++index) {
        const auto& choice = choices[index];
        if (text.compare(QLatin1String(choice.token), Qt::CaseInsensitive) == 0
            || text == QString::fromUtf8(choice.russian)
            || text == QString::fromUtf8(choice.japanese)
            || text == QString::fromUtf8(choice.chinese)
            || text == QCoreApplication::translate("openvegas::ui::OptionsDialog", choice.token))
            return QString::fromLatin1(choice.token);
    }
    return QString::fromLatin1(fallback);
}
} // namespace detail

// Migration of currentText values from all built-in catalogs, even when the
// user switches language before opening Options. Newly saved values are tokens.
inline QString proxyQualityToken(const QString& stored)
{
    static const detail::LegacyChoice choices[] = {
        {"Low", "Низкое", "低", "低"},
        {"Medium", "Среднее", "中", "中等"},
        {"High", "Высокое", "高", "高"},
    };
    return detail::choiceToken(stored, choices, 3, "Medium");
}
inline QString proxyPreviewToken(const QString& stored)
{
    static const detail::LegacyChoice choices[] = {
        {"Auto", "Автоматически", "自動", "自动"},
        {"Proxy", "Прокси", "プロキシ", "代理"},
        {"Full Resolution", "Полное разрешение", "フル解像度", "完整分辨率"},
    };
    return detail::choiceToken(stored, choices, 3, "Auto");
}
inline bool proxyPreviewEnabled(const QString& stored, bool playing)
{
    const QString mode = proxyPreviewToken(stored);
    return mode == QLatin1String("Proxy") || (mode == QLatin1String("Auto") && playing);
}

} // namespace openvegas::app
