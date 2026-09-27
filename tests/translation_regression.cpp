#include "app/Translations.h"
#include <QCoreApplication>
#include <QtTest>

class TranslationRegression : public QObject
{
    Q_OBJECT
private slots:
    void cataloguesLoadAndSwitch()
    {
        auto* app = QCoreApplication::instance();
        const QString root = QString::fromUtf8(TRANSLATION_TEST_ROOT);
        const QStringList locales {"ru", "ja", "zh_CN"};
        const QStringList expected {QStringLiteral("Длительность (секунды)"),
            QStringLiteral("長さ（秒）"), QStringLiteral("时长（秒）")};
        for (int i = 0; i < locales.size(); ++i) {
            QCOMPARE(openvegas::app::Translations::install(app, locales[i], root), locales[i]);
            QCOMPARE(QCoreApplication::translate("openvegas::ui::TimelineWidget", "Duration (seconds)"),
                     expected[i]);
            const QString color = QCoreApplication::translate(
                "openvegas::ui::ColorSwatchButton", "Pick a color");
            QVERIFY(!color.isEmpty());
            QVERIFY(color != QStringLiteral("Pick a color"));
            const QString status = QCoreApplication::translate("openvegas::app::AppMain", "Loading plug-ins...");
            QVERIFY(status != QStringLiteral("Loading plug-ins..."));
            for (int count : {1, 2, 5, 21}) {
                const QString plural = QCoreApplication::translate("openvegas::ui::MainWindow",
                    "%n plugin(s) used by this project are missing", nullptr, count);
                QVERIFY(plural.contains(QString::number(count)));
                QVERIFY(!plural.contains(QStringLiteral("%n")));
                QVERIFY(!plural.contains(QStringLiteral("plugin(s)")));
                if (locales[i] == QLatin1String("ru")) {
                    const QString noun = count == 2 ? QStringLiteral("плагина")
                        : count == 5 ? QStringLiteral("плагинов") : QStringLiteral("плагин,");
                    QVERIFY(plural.contains(noun));
                }
            }
        }
        QCOMPARE(openvegas::app::Translations::install(app, "en", root), QStringLiteral("en"));
        QCOMPARE(QCoreApplication::translate("openvegas::ui::TimelineWidget", "Duration (seconds)"),
                 QStringLiteral("Duration (seconds)"));
    }
};

QTEST_GUILESS_MAIN(TranslationRegression)
#include "translation_regression.moc"
