#include "ui/OptionsDialog.h"
#include "ui/Theme.h"
#include "app/Settings.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QKeySequenceEdit>
#include <QFontDatabase>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace openvegas;

class OptionsRegression : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        m_settingsRoot = std::make_unique<QTemporaryDir>();
        QVERIFY(m_settingsRoot->isValid());
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           m_settingsRoot->path());
        QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                           app::Settings::organizationName(), app::Settings::applicationName());
        settings.clear();
    }

    void allReferenceCategoriesHavePages()
    {
        ui::OptionsDialog dialog;
        auto* categories = dialog.findChild<QListWidget*>(QStringLiteral("categoryList"));
        auto* stack = dialog.findChild<QStackedWidget*>(QStringLiteral("stack"));
        QVERIFY(categories);
        QVERIFY(stack);
        QCOMPARE(categories->count(), 13);
        QCOMPARE(stack->count(), categories->count());
        QCOMPARE(categories->item(0)->text(), QStringLiteral("General"));
        QCOMPARE(categories->item(12)->text(), QStringLiteral("Export"));

        const QStringList required = {
            QStringLiteral("spinBoxMaxUndo"),
            QStringLiteral("comboBoxOverrideLanguage"),
            QStringLiteral("checkBoxEnableHighDPI"),
            QStringLiteral("checkBoxShowCheckerboard2D"),
            QStringLiteral("checkBoxUseHardwareDecoding"),
            QStringLiteral("comboBoxPlaybackQualityProfile"),
            QStringLiteral("checkBoxGPUWarning"),
            QStringLiteral("tableLabels"),
            QStringLiteral("lineEditTimelineCache"),
            QStringLiteral("comboBoxVoiceoverDevice"),
            QStringLiteral("lineEditProxyDirectoryPath"),
            QStringLiteral("checkBoxAutoSaving"),
            QStringLiteral("tableShortcuts"),
            QStringLiteral("lineEditExportDirectory")
        };
        for (const QString& objectName : required) {
            QVERIFY2(dialog.findChild<QWidget*>(objectName), qPrintable(objectName));
        }
    }

    void dependentEditorsFollowTheirToggles()
    {
        ui::OptionsDialog dialog;
        auto* motion = dialog.findChild<QCheckBox*>(QStringLiteral("checkBoxShowMotionPath"));
        auto* frames = dialog.findChild<QSpinBox*>(QStringLiteral("spinBoxMotionPathFrames"));
        auto* automatic = dialog.findChild<QCheckBox*>(
            QStringLiteral("checkBoxUseAutomaticRenderCache"));
        auto* delay = dialog.findChild<QSpinBox*>(QStringLiteral("spinBoxRenderCacheDelay"));
        auto* autosave = dialog.findChild<QCheckBox*>(QStringLiteral("checkBoxAutoSaving"));
        auto* frequency = dialog.findChild<QSpinBox*>(QStringLiteral("spinBoxSaveFrequency"));
        QVERIFY(motion && frames && automatic && delay && autosave && frequency);

        motion->setChecked(false);
        QVERIFY(!frames->isEnabled());
        motion->setChecked(true);
        QVERIFY(frames->isEnabled());
        automatic->setChecked(false);
        QVERIFY(!delay->isEnabled());
        autosave->setChecked(false);
        QVERIFY(!frequency->isEnabled());
    }

    void referenceShortcutDefaults()
    {
        ui::OptionsDialog dialog;
        auto* table = dialog.findChild<QTableWidget*>(QStringLiteral("tableShortcuts"));
        QVERIFY(table);
        const auto shortcutFor = [table](const QString& actionName) {
            for (int row = 0; row < table->rowCount(); ++row) {
                if (table->item(row, 0)->data(Qt::UserRole).toString() == actionName) {
                    return qobject_cast<QKeySequenceEdit*>(table->cellWidget(row, 1))
                        ->keySequence().toString(QKeySequence::PortableText);
                }
            }
            return QString();
        };
        QCOMPARE(shortcutFor(QStringLiteral("actionSaveAs")), QStringLiteral("Ctrl+Alt+S"));
        QCOMPARE(shortcutFor(QStringLiteral("actionImport")), QStringLiteral("Ctrl+Shift+O"));
        QCOMPARE(shortcutFor(QStringLiteral("actionReset")), QStringLiteral("Ctrl+R"));
        QCOMPARE(shortcutFor(QStringLiteral("actionCloseActivePanel")), QStringLiteral("Ctrl+W"));
    }

    void settingsPersistCancelDiscardsAndShortcutsApply()
    {
        {
            ui::OptionsDialog dialog;
            dialog.findChild<QSpinBox*>(QStringLiteral("spinBoxMaxUndo"))->setValue(123);
            dialog.findChild<QLineEdit*>(QStringLiteral("lineEditProxyDirectoryPath"))
                ->setText(QStringLiteral("D:/cache/proxies"));
            dialog.findChild<QKeySequenceEdit*>(QStringLiteral("shortcut_actionNew"))
                ->setKeySequence(QKeySequence(QStringLiteral("Alt+N")));
            QTest::mouseClick(dialog.findChild<QPushButton*>(QStringLiteral("btnOK")),
                              Qt::LeftButton);
        }

        ui::OptionsDialog dialog;
        QCOMPARE(dialog.findChild<QSpinBox*>(QStringLiteral("spinBoxMaxUndo"))->value(), 123);
        QCOMPARE(dialog.findChild<QLineEdit*>(QStringLiteral("lineEditProxyDirectoryPath"))->text(),
                 QDir::toNativeSeparators(QStringLiteral("D:/cache/proxies")));

        QWidget actionRoot;
        QAction action(&actionRoot);
        action.setObjectName(QStringLiteral("actionNew"));
        action.setShortcut(QKeySequence::New);
        ui::OptionsDialog::applyShortcuts(&actionRoot);
        QCOMPARE(action.shortcut(), QKeySequence(QStringLiteral("Alt+N")));

        dialog.findChild<QSpinBox*>(QStringLiteral("spinBoxMaxUndo"))->setValue(999);
        QTest::mouseClick(dialog.findChild<QPushButton*>(QStringLiteral("btnCancel")),
                          Qt::LeftButton);
        QCOMPARE(dialog.findChild<QSpinBox*>(QStringLiteral("spinBoxMaxUndo"))->value(), 123);
    }

    void restoreDefaultsAndSnapshots()
    {
        ui::OptionsDialog dialog;
        auto* categories = dialog.findChild<QListWidget*>(QStringLiteral("categoryList"));
        auto* restore = dialog.findChild<QPushButton*>(QStringLiteral("btnRestoreDefaults"));
        QVERIFY(categories && restore);

        categories->setCurrentRow(1);
        auto* highDpi = dialog.findChild<QCheckBox*>(QStringLiteral("checkBoxEnableHighDPI"));
        highDpi->setChecked(false);
        QTest::mouseClick(restore, Qt::LeftButton);
        QVERIFY(highDpi->isChecked());

        dialog.show();
        QTest::qWait(30);
        categories->setCurrentRow(0);
        QVERIFY(dialog.grab().save(QCoreApplication::applicationDirPath()
                                   + QStringLiteral("/options-general.png")));
        categories->setCurrentRow(9);
        QTest::qWait(20);
        QVERIFY(dialog.grab().save(QCoreApplication::applicationDirPath()
                                   + QStringLiteral("/options-proxies.png")));
        categories->setCurrentRow(12);
        QTest::qWait(20);
        QVERIFY(dialog.grab().save(QCoreApplication::applicationDirPath()
                                   + QStringLiteral("/options-export.png")));
    }

private:
    std::unique_ptr<QTemporaryDir> m_settingsRoot;
};

int main(int argc, char** argv)
{
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication application(argc, argv);
#ifdef Q_OS_WIN
    QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/arial.ttf"));
    application.setFont(QFont(QStringLiteral("Arial"), 9));
#endif
    ui::applyTheme(&application);
    OptionsRegression tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "options_regression.moc"
