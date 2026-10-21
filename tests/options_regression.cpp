#include "ui/OptionsDialog.h"
#include "ui/Theme.h"
#include "app/Settings.h"
#include "app/ProjectDefaults.h"
#include "app/GpuDriver.h"
#include "app/PluginFolders.h"
#include <QTranslator>
#include <QScopeGuard>
#include "ui/PromptMessage.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QSignalSpy>
#include <QFontDatabase>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QMessageBox>
#include <QTemporaryDir>
#include <QTimer>
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
        QCOMPARE(categories->count(), 14);
        QCOMPARE(stack->count(), categories->count());
        QCOMPARE(categories->item(0)->text(), QStringLiteral("General"));
        QCOMPARE(categories->item(12)->text(), QStringLiteral("Export"));
        QCOMPARE(categories->item(13)->text(), QStringLiteral("Plugins"));

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

    void promptsRememberChoiceAndFollowOptions()
    {
        const QString key = QStringLiteral("MediaMismatchPrompt");
        QSettings settings = app::Settings::optionSettings();
        settings.remove(ui::promptSettingKey(key));
        settings.remove(ui::promptSettingKey(key) + QStringLiteral("Answer"));
        settings.sync();
        QVERIFY(ui::promptEnabled(key));
        // Answer the box: tick "Do not show this again" and choose No.
        int shown = 0;
        const auto answerWith = [&](QMessageBox::StandardButton button, bool remember) {
            QTimer::singleShot(0, [&shown, button, remember] {
                auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
                if (!box) return;
                ++shown;
                if (remember) box->checkBox()->setChecked(true);
                box->button(button)->click();
            });
        };
        const auto ask = [&] {
            return ui::showPrompt(nullptr, key, QMessageBox::Question, QStringLiteral("Title"),
                                  QStringLiteral("Text"), QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::Yes);
        };
        answerWith(QMessageBox::No, false);
        QCOMPARE(ask(), QMessageBox::No);
        QCOMPARE(shown, 1);
        QVERIFY(ui::promptEnabled(key));
        answerWith(QMessageBox::No, true);
        QCOMPARE(ask(), QMessageBox::No);
        QCOMPARE(shown, 2);
        QVERIFY(!ui::promptEnabled(key));
        // Skipped from now on, with the remembered answer.
        QCOMPARE(ask(), QMessageBox::No);
        QCOMPARE(shown, 2);

        // The Options page shows the prompt as off and turning it on again
        // brings the box back.
        {
            ui::OptionsDialog dialog;
            auto* check = dialog.findChild<QCheckBox*>(QStringLiteral("checkBoxMediaMismatchPrompt"));
            QVERIFY(check);
            QVERIFY(!check->isChecked());
            check->setChecked(true);
            auto* ok = dialog.findChild<QPushButton*>(QStringLiteral("btnOK"));
            QVERIFY(ok);
            ok->click();
        }
        QVERIFY(ui::promptEnabled(key));
        answerWith(QMessageBox::Yes, false);
        QCOMPARE(ask(), QMessageBox::Yes);
        QCOMPARE(shown, 3);

        // An answer outside `rememberable` (Exit on a startup warning) is
        // returned but never stored.
        const QString gpu = QStringLiteral("GPUWarning");
        answerWith(QMessageBox::Abort, true);
        QCOMPARE(ui::showPrompt(nullptr, gpu, QMessageBox::Warning, QStringLiteral("GPU"),
                                QStringLiteral("Text"), QMessageBox::Ignore | QMessageBox::Abort,
                                QMessageBox::Ignore, {}, QMessageBox::Ignore),
                 QMessageBox::Abort);
        QVERIFY(ui::promptEnabled(gpu));
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

    // The reference's startup driver check (FUN_1401bffe0, FUN_1401c2ce0):
    // Intel integrated graphics by generation, each with the driver build it
    // needs; the warning names the device, version and date and sends one to
    // Intel's download centre; "Exit" there leaves, "Do not show again"
    // skips it with Continue.
    void gpuDriverWarningFollowsTheReference()
    {
        QCOMPARE(app::gpuClassOf("Intel(R) UHD Graphics 620"), 7);
        QCOMPARE(app::gpuClassOf("intel(r) uhd graphics 630"), 7);           // case aside
        QCOMPARE(app::gpuClassOf("Radeon RX Vega M GH Graphics"), 7);
        QCOMPARE(app::gpuClassOf("Intel(R) HD Graphics 620"), 6);
        QCOMPARE(app::gpuClassOf("Intel(R) Iris(R) Plus Graphics 640"), 6);
        QCOMPARE(app::gpuClassOf("Intel(R) HD Graphics 520"), 5);
        QCOMPARE(app::gpuClassOf("Intel(R) HD Graphics 5500"), 4);
        QCOMPARE(app::gpuClassOf("Intel(R) HD Graphics 5000"), 3);           // not " HD Graphics 500"
        QCOMPARE(app::gpuClassOf("Intel(R) HD Graphics 4000"), 2);
        QCOMPARE(app::gpuClassOf("Intel(R) HD Graphics 3000"), 1);
        QCOMPARE(app::gpuClassOf("NVIDIA GeForce RTX 3060/PCIe/SSE2"), 8);
        QCOMPARE(app::gpuClassOf(QString()), 8);

        // Generations 5 to 7 need Intel's 100.6326.
        QVERIFY(app::checkGpuDriver("Intel(R) UHD Graphics 620", "26.20.100.6325").outdated());
        QVERIFY(!app::checkGpuDriver("Intel(R) UHD Graphics 620", "26.20.100.6326").outdated());
        QVERIFY(!app::checkGpuDriver("Intel(R) HD Graphics 520", "31.0.101.5186").outdated());
        QCOMPARE(app::checkGpuDriver("Intel(R) HD Graphics 520", "31.0.101.5186").installed, qint64(1015186));
        // 3 and 4 the build 5057, 2 the build 5059.
        QVERIFY(app::checkGpuDriver("Intel(R) HD Graphics 5500", "20.19.15.5056").outdated());
        QVERIFY(!app::checkGpuDriver("Intel(R) HD Graphics 4600", "20.19.15.5057").outdated());
        QVERIFY(app::checkGpuDriver("Intel(R) HD Graphics 4000", "10.18.10.5058").outdated());
        QVERIFY(!app::checkGpuDriver("Intel(R) HD Graphics 4000", "10.18.10.5059").outdated());
        // Older ones and everything else are not checked; nor is a version
        // that is no number.
        QVERIFY(!app::checkGpuDriver("Intel(R) HD Graphics 3000", "8.15.10.1").outdated());
        QVERIFY(!app::checkGpuDriver("NVIDIA GeForce RTX 3060/PCIe/SSE2", "1.2").outdated());
        QVERIFY(!app::checkGpuDriver("Intel(R) UHD Graphics 620", "unknown").outdated());

        // Without a matching adapter: the renderer, and GL_VERSION's last word.
        const app::GpuDriverInfo info =
            app::gpuDriverInfo("Intel(R) UHD Graphics 620 (test)", "4.6.0 - Build 26.20.100.6000");
        QCOMPARE(info.device, QString("Intel(R) UHD Graphics 620 (test)"));
        QCOMPARE(info.version, QString("26.20.100.6000"));
        const QString text = app::gpuDriverWarningText({"26.20.100.6000", "1-1-2019", "Intel(R) UHD Graphics 620"});
        QVERIFY(text.startsWith("Your GPU driver is out of date.<br>"));
        QVERIFY(text.contains("<br>Device: Intel(R) UHD Graphics 620"));
        QVERIFY(text.contains("<br>Driver Version: 26.20.100.6000"));
        QVERIFY(text.contains("<br>Driver Date: 1-1-2019"));
        QVERIFY(text.contains("<a href=\"https://downloadcenter.intel.com/\">"));

        // The prompt: Exit leaves; an up-to-date driver asks nothing.
        QTimer::singleShot(0, [] {
            if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
                box->button(QMessageBox::Abort)->click();
        });
        QVERIFY(!app::warnAboutGpuDriver(nullptr, "Intel(R) UHD Graphics 620", "4.6.0 - Build 26.20.100.6000"));
        QVERIFY(app::warnAboutGpuDriver(nullptr, "Intel(R) UHD Graphics 620", "4.6.0 - Build 26.20.100.8681"));
        QVERIFY(app::warnAboutGpuDriver(nullptr, "NVIDIA GeForce RTX 3060/PCIe/SSE2", "4.6.0 NVIDIA 555.85"));
    }

    void templateSelectionSurvivesChangedDisplayText() {
        {
            ui::OptionsDialog dialog;
            auto* combo = dialog.findChild<QComboBox*>("comboBoxTemplate"); QVERIFY(combo);
            combo->setCurrentIndex(combo->findData(QStringLiteral("uhd30")));
            QTest::mouseClick(dialog.findChild<QPushButton*>("btnOK"), Qt::LeftButton);
        }
        auto settings = app::Settings::optionSettings();
        QCOMPARE(settings.value("Options/DefaultTemplateId").toString(), QStringLiteral("uhd30"));
        settings.setValue("Options/DefaultTemplate", "display text from another language"); settings.sync();
        ui::OptionsDialog reopened;
        QCOMPARE(reopened.findChild<QComboBox*>("comboBoxTemplate")->currentData().toString(), QStringLiteral("uhd30"));
        QMetaObject::invokeMethod(&reopened, "reject", Qt::DirectConnection);
    }

    void localizedOptionValues_data()
    {
        QTest::addColumn<QString>("locale");
        for (const auto* locale : {"en", "ru", "ja", "zh_CN"})
            QTest::newRow(locale) << QString::fromLatin1(locale);
    }

    void localizedOptionValues()
    {
        QFETCH(QString, locale);
        QTranslator translator;
        if (locale != QLatin1String("en")) {
            const QString catalog = QDir(QString::fromUtf8(TRANSLATION_TEST_ROOT))
                .filePath(locale + "/openvegaseffects_" + locale + ".qm");
            QVERIFY(translator.load(catalog));
            QVERIFY(qApp->installTranslator(&translator));
        }
        const auto removeTranslator = qScopeGuard([&] { qApp->removeTranslator(&translator); });
        const QStringList qualities {"Low", "Medium", "High"};
        const QStringList previews {"Auto", "Proxy", "Full Resolution"};
        for (int index = 0; index < qualities.size(); ++index) {
            const auto translated = [](const QString& token) {
                return QCoreApplication::translate("openvegas::ui::OptionsDialog", token.toUtf8().constData());
            };
            auto settings = app::Settings::optionSettings();
            settings.setValue("Options/Antialiasing", "8x MSAA");
            settings.setValue("Options/ProxyQuality", translated(qualities[index]));
            settings.setValue("Options/PreviewMode", translated(previews[index]));
            settings.sync();
            QCOMPARE(app::defaultProjectRenderSettings().antialiasingMode, 2);
            // A legacy localized value must load even after switching to English.
            qApp->removeTranslator(&translator);
            {
                ui::OptionsDialog migrated;
                QCOMPARE(migrated.findChild<QComboBox*>("comboBoxProxyQuality")->currentData().toString(), qualities[index]);
                QCOMPARE(migrated.findChild<QComboBox*>("comboBoxPreviewMode")->currentData().toString(), previews[index]);
                QMetaObject::invokeMethod(&migrated, "reject", Qt::DirectConnection);
            }
            if (locale != QLatin1String("en")) qApp->installTranslator(&translator);
            {
                ui::OptionsDialog dialog;
                auto* antialiasing = dialog.findChild<QComboBox*>("comboBoxAntialiasing");
                auto* quality = dialog.findChild<QComboBox*>("comboBoxProxyQuality");
                auto* preview = dialog.findChild<QComboBox*>("comboBoxPreviewMode");
                QVERIFY(antialiasing && quality && preview);
                QCOMPARE(antialiasing->currentData().toInt(), 2);
                QCOMPARE(quality->currentData().toString(), qualities[index]);
                QCOMPARE(preview->currentData().toString(), previews[index]);
                antialiasing->setCurrentIndex(antialiasing->findData(3));
                // Display strings must never be used as serialized values.
                quality->setItemText(quality->currentIndex(), "changed display label");
                preview->setItemText(preview->currentIndex(), "changed display label");
                dialog.findChild<QPushButton*>("btnOK")->click();
            }
            settings.sync();
            QCOMPARE(settings.value("Options/Antialiasing").toInt(), 3);
            QCOMPARE(settings.value("Options/ProxyQuality").toString(), qualities[index]);
            QCOMPARE(settings.value("Options/PreviewMode").toString(), previews[index]);
            QCOMPARE(app::defaultProjectRenderSettings().antialiasingMode, 3);
            // Reopen in English, then return to the test's language.
            qApp->removeTranslator(&translator);
            {
                ui::OptionsDialog reopened;
                QCOMPARE(reopened.findChild<QComboBox*>("comboBoxAntialiasing")->currentData().toInt(), 3);
                QCOMPARE(reopened.findChild<QComboBox*>("comboBoxProxyQuality")->currentData().toString(), qualities[index]);
                QCOMPARE(reopened.findChild<QComboBox*>("comboBoxPreviewMode")->currentData().toString(), previews[index]);
                QMetaObject::invokeMethod(&reopened, "reject", Qt::DirectConnection);
            }
            if (locale != QLatin1String("en")) qApp->installTranslator(&translator);
        }
    }

    // Where plugins are looked for besides the user's folder: the VEGAS
    // Effects installation under Program Files (when used), the folders
    // chosen on the Plugins page - another FXhome program's among them -
    // each once; Options opens there by itself once, when none has any.
    // Options > Render's 3D map sizes are what a new project's shadow and
    // reflection maps are made at (ProjectRenderSettings, which the renderer
    // and the .vegfx read).
    void renderMapSizesReachNewProjects()
    {
        ui::OptionsDialog dialog;
        auto* shadows = dialog.findChild<QSpinBox*>(QStringLiteral("spinBoxShadowMapSize"));
        auto* reflections = dialog.findChild<QSpinBox*>(QStringLiteral("spinBoxReflectionMapSize"));
        QVERIFY(shadows && reflections);
        shadows->setValue(1024);
        reflections->setValue(256);
        dialog.findChild<QPushButton*>(QStringLiteral("btnOK"))->click();
        const composition::ProjectRenderSettings settings = app::defaultProjectRenderSettings();
        QCOMPARE(settings.shadowMapSize, shadows->value());
        QCOMPARE(settings.reflectionMapSize, reflections->value());
        QVERIFY(settings.shadowMapSize != 2048 || settings.reflectionMapSize != 512);
    }

    void pluginsPageChoosesFolders()
    {
        QTemporaryDir programFiles;
        QVERIFY(programFiles.isValid());
        const QDir root(programFiles.path());
        const auto touch = [&](const QString& relative) {
            QVERIFY(root.mkpath(QFileInfo(root.filePath(relative)).path()));
            QFile file(root.filePath(relative));
            QVERIFY(file.open(QIODevice::WriteOnly));
        };
        touch(QStringLiteral("FXhome/VEGAS Effects/Plugins/Behavior/Fade.hfpl"));
        touch(QStringLiteral("FXhome/HitFilm Pro/Plugins/Distort/Ripple.hfpl"));
        touch(QStringLiteral("FXhome/HitFilm Pro/Plugins/Glow.hfpl"));
        QVERIFY(root.mkpath(QStringLiteral("FXhome/Imerge Pro")));
        const QStringList roots {programFiles.path()};
        const QString installation = QDir::cleanPath(root.filePath(QStringLiteral("FXhome/VEGAS Effects/Plugins")));
        const QString hitfilm = QDir::cleanPath(root.filePath(QStringLiteral("FXhome/HitFilm Pro/Plugins")));
        QCOMPARE(app::PluginFolders::installationPluginFolder(roots), installation);
        QCOMPARE(app::PluginFolders::fxhomePluginFolders(roots), (QStringList {installation, hitfilm}));
        QCOMPARE(app::PluginFolders::pluginCount(installation), 1);
        QCOMPARE(app::PluginFolders::pluginCount(hitfilm), 2);
        QCOMPARE(app::PluginFolders::scannedFolders(roots), (QStringList {installation}));
        QVERIFY(!app::PluginFolders::shouldPrompt(roots));
        // No installation where it is looked for: nothing to scan, Options
        // asks - once.
        const QStringList elsewhere {root.filePath(QStringLiteral("nowhere"))};
        QVERIFY(app::PluginFolders::scannedFolders(elsewhere).isEmpty());
        QVERIFY(app::PluginFolders::shouldPrompt(elsewhere));
        app::PluginFolders::setPromptShown(true);
        QVERIFY(!app::PluginFolders::shouldPrompt(elsewhere));
        app::PluginFolders::setPromptShown(false);

        ui::OptionsDialog dialog;
        dialog.showCategory(QStringLiteral("Plugins"));
        auto* categories = dialog.findChild<QListWidget*>(QStringLiteral("categoryList"));
        QCOMPARE(categories->currentItem()->text(), QStringLiteral("Plugins"));
        auto* list = dialog.findChild<QListWidget*>(QStringLiteral("listWidgetPluginFolders"));
        auto* use = dialog.findChild<QCheckBox*>(QStringLiteral("checkBoxUseInstallationPlugins"));
        QVERIFY(list && use);
        QVERIFY(dialog.findChild<QPushButton*>(QStringLiteral("pushButtonAddPluginFolder")));
        QVERIFY(dialog.findChild<QPushButton*>(QStringLiteral("pushButtonFindFxhomePlugins")));
        QVERIFY(dialog.findChild<QLabel*>(QStringLiteral("labelInstallationPluginStatus")));
        QCOMPARE(list->count(), 0);
        dialog.addPluginFolder(hitfilm);
        dialog.addPluginFolder(QDir::toNativeSeparators(hitfilm));   // once
        QCOMPARE(list->count(), 1);
        QVERIFY2(list->item(0)->text().contains(QStringLiteral("2")), qPrintable(list->item(0)->text()));
        QSignalSpy changed(&dialog, &ui::OptionsDialog::pluginFoldersChanged);
        dialog.findChild<QPushButton*>(QStringLiteral("btnOK"))->click();
        QCOMPARE(changed.count(), 1);
        QCOMPARE(app::PluginFolders::chosenFolders(), (QStringList {hitfilm}));
        QCOMPARE(app::PluginFolders::scannedFolders(roots), (QStringList {installation, hitfilm}));
        // Not using the installation's: only the chosen folder.
        app::PluginFolders::setUseInstallation(false);
        QCOMPARE(app::PluginFolders::scannedFolders(roots), (QStringList {hitfilm}));
        app::PluginFolders::setUseInstallation(true);
        // Nothing changed: no new scan.
        dialog.findChild<QPushButton*>(QStringLiteral("btnOK"))->click();
        QCOMPARE(changed.count(), 1);
        // The page's defaults: the installation used, no folder of its own.
        dialog.showCategory(QStringLiteral("Plugins"));
        dialog.findChild<QPushButton*>(QStringLiteral("btnRestoreDefaults"))->click();
        QCOMPARE(list->count(), 0);
        QVERIFY(use->isChecked());
        dialog.findChild<QPushButton*>(QStringLiteral("btnOK"))->click();
        QCOMPARE(changed.count(), 2);
        QVERIFY(app::PluginFolders::chosenFolders().isEmpty());
    }

    void nativeAntialiasingModesPersist()
    {
        ui::OptionsDialog dialog;
        auto* combo = dialog.findChild<QComboBox*>("comboBoxAntialiasing");
        QVERIFY(combo);
        QCOMPARE(combo->count(), 9);
        for (int mode = 1; mode <= 9; ++mode) {
            combo->setCurrentIndex(combo->findData(mode));
            dialog.findChild<QPushButton*>("btnOK")->click();
            QCOMPARE(app::defaultProjectRenderSettings().antialiasingMode, mode);
        }
        dialog.findChild<QListWidget*>("categoryList")->setCurrentRow(
            ui::OptionsDialog::categoryNames().indexOf(QStringLiteral("Render")));
        dialog.findChild<QPushButton*>("btnRestoreDefaults")->click();
        QCOMPARE(combo->currentData().toInt(), 1); // Native default: 4x MSAA.
        dialog.findChild<QPushButton*>("btnOK")->click();
        QCOMPARE(app::defaultProjectRenderSettings().antialiasingMode, 1);
        auto settings = app::Settings::optionSettings();
        for (const auto* obsolete : {"Off", "2x MSAA"}) {
            settings.setValue("Options/Antialiasing", obsolete); settings.sync();
            QCOMPARE(app::defaultProjectRenderSettings().antialiasingMode, 1);
            ui::OptionsDialog reopened;
            QCOMPARE(reopened.findChild<QComboBox*>("comboBoxAntialiasing")->currentData().toInt(), 1);
        }
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
