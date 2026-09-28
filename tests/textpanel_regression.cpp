#include "ui/TextPanel.h"
#include "ui/Theme.h"
#include "render/TextRender.h"
#include "ui/TextEditCommand.h"
#include "ui/TextSettingsDialog.h"
#include "project/VegfxSerializer.h"
#include <QCheckBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QTemporaryDir>
#include <QUndoStack>
#include <QLineEdit>
#include <QListWidget>
#include <QApplication>
#include <QComboBox>
#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QFontDatabase>
#include <QGroupBox>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QSpinBox>
#include <QToolButton>
#include <QTimer>
#include <QtTest>

using openvegas::composition::TextStyle;
using openvegas::ui::TextPanel;

static TextStyle sampleStyle()
{
    TextStyle style;
    style.text = "Mixed CASE words wrap across this paragraph and shape ligatures fi ff.\nSecond paragraph.";
    style.fontFamily = "Arial";
    style.fontSize = 28;
    style.alignH = TextStyle::AlignH::Left;
    style.alignV = TextStyle::AlignV::Top;
    style.outlineSize = 1;
    style.outlineColor = Qt::red;
    style.backgroundEnabled = true;
    style.backgroundColor = Qt::blue;
    style.backgroundOpacity = 40;
    style.textMode = TextStyle::TextMode::Paragraph;
    style.paragraphSize = QSizeF(350, 260);
    return style;
}
static QImage renderStyle(const TextStyle& style)
{
    QImage image(480, 360, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    openvegas::render::drawStyledText(painter, QRectF(30, 40, 350, 260), style);
    return image;
}

class TextPanelRegression : public QObject
{
    Q_OBJECT
private slots:
    void glyphOpacityKeepsShapedTextIndependent()
    {
        TextStyle style = sampleStyle();
        style.text = QStringLiteral("OO");
        style.backgroundEnabled = false;
        style.underline = false;
        style.strikethrough = false;
        style.outlineSize = 0;
        const auto alphaSum = [](const QImage& image) {
            qint64 sum = 0;
            for (int y = 0; y < image.height(); ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    sum += qAlpha(image.pixel(x, y));
                }
            }
            return sum;
        };
        const qint64 both = alphaSum(renderStyle(style));
        QVERIFY(both > 0);
        QImage single(480, 360, QImage::Format_ARGB32_Premultiplied);
        single.fill(Qt::transparent);
        QPainter painter(&single);
        int glyphCount = 0;
        openvegas::render::drawStyledText(
            painter, QRectF(30, 40, 350, 260), style,
            [&](QVector<openvegas::render::GlyphRenderState>& glyphs) {
                glyphCount = glyphs.size();
                if (glyphs.size() == 2) glyphs[1].opacity = 0.0f;
            });
        painter.end();
        QCOMPARE(glyphCount, 2);
        const qint64 one = alphaSum(single);
        QVERIFY(one > 0);
        QVERIFY(one < both);

        const auto rightmostInk = [](const QImage& image) {
            for (int x = image.width() - 1; x >= 0; --x) {
                for (int y = 0; y < image.height(); ++y) {
                    if (qAlpha(image.pixel(x, y)) > 0) return x;
                }
            }
            return -1;
        };
        QImage shifted(480, 360, QImage::Format_ARGB32_Premultiplied);
        shifted.fill(Qt::transparent);
        QPainter shiftedPainter(&shifted);
        openvegas::render::drawStyledText(
            shiftedPainter, QRectF(30, 40, 350, 260), style,
            [](QVector<openvegas::render::GlyphRenderState>& glyphs) {
                if (glyphs.size() == 2) {
                    glyphs[0].transformation = QTransform::fromTranslate(100.0, 0.0);
                }
            });
        shiftedPainter.end();
        QVERIFY(rightmostInk(shifted) > rightmostInk(renderStyle(style)) + 25);
    }

    void missingAudioSurvivesProjectLoad()
    {
        using namespace openvegas;
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString audioPath = directory.filePath("offline.mp3");
        media::MediaManager originalMedia;
        originalMedia.registerMissingFile(audioPath);
        composition::Composition original;
        original.addClip("Audio", originalMedia.assetByFilePath(audioPath).id(), 0.0, 5.0);
        const QString projectPath = directory.filePath("offline.vegfx");
        QVERIFY(project::VegfxSerializer::saveToFile(projectPath, original, originalMedia).isSuccess());
        media::MediaManager loadedMedia;
        composition::Composition loaded;
        QVERIFY(project::VegfxSerializer::loadFromFile(projectPath, &loaded, &loadedMedia).isSuccess());
        const auto asset = loadedMedia.assetByFilePath(audioPath);
        QVERIFY(asset.isValid());
        QCOMPARE(asset.kind(), media::MediaKind::Audio);
        QCOMPARE(loaded.layers().first().clips.first().mediaId, asset.id());

        QVERIFY(QDir(directory.path()).mkpath("renamed"));
        QFile recovered(directory.filePath("renamed/offline.mp3"));
        QVERIFY(recovered.open(QIODevice::WriteOnly));
        recovered.close();
        QVERIFY(project::VegfxSerializer::loadFromFile(projectPath, &loaded, &loadedMedia).isSuccess());
        QVERIFY(loadedMedia.assetByFilePath(recovered.fileName()).isValid());
        QCOMPARE(loaded.layers().first().clips.first().mediaId,
                 loadedMedia.assetByFilePath(recovered.fileName()).id());

        QVERIFY(QDir(directory.path()).mkpath("duplicate"));
        QFile duplicate(directory.filePath("duplicate/offline.mp3"));
        QVERIFY(duplicate.open(QIODevice::WriteOnly));
        duplicate.close();
        QVERIFY(project::VegfxSerializer::loadFromFile(projectPath, &loaded, &loadedMedia).isSuccess());
        QVERIFY(loadedMedia.assetByFilePath(audioPath).isValid());
    }

    void numericControls_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<double>("value");
        const QList<QPair<QString, double>> cases = {
            {"doubleSpinBoxFontOutlineSize", 4}, {"doubleSpinBoxLineHeight", 150},
            {"doubleSpinBoxVerticalScale", 130}, {"doubleSpinBoxHorizontalScale", 120},
            {"doubleSpinBoxLetterSpacing", 150}, {"doubleSpinBoxBaselineShift", 25},
            {"doubleSpinBoxIndentLeft", 30}, {"doubleSpinBoxIndentRight", 130},
            {"doubleSpinBoxIndentTop", 30}, {"doubleSpinBoxIndentBottom", 60},
            {"doubleSpinBoxIndentFirstLine", 40}, {"doubleSpinBoxSpaceBeforeParagraph", 25},
            {"doubleSpinBoxSpaceAfterParagraph", 25}, {"doubleSpinBoxBackgroundOpacity", 90},
            {"doubleSpinBoxBackgroundRoundness", 20}, {"doubleSpinBoxBackgroundExpansionX", 20},
            {"doubleSpinBoxBackgroundExpansionY", 30}
        };
        for (const auto& item : cases) QTest::newRow(qPrintable(item.first)) << item.first << item.second;
    }
    void numericControls()
    {
        QFETCH(QString, name); QFETCH(double, value);
        TextPanel panel;
        auto style = sampleStyle();
        style.alignV = TextStyle::AlignV::Middle;
        panel.setStyle(style);
        const QImage before = renderStyle(panel.style());
        auto* spin = panel.findChild<QDoubleSpinBox*>(name);
        QVERIFY(spin); QVERIFY(spin->isEnabled());
        QSignalSpy edits(&panel, &TextPanel::styleEdited);
        spin->setValue(value);
        QCOMPARE(edits.count(), 1);
        QVERIFY2(before != renderStyle(panel.style()), qPrintable(name));
        const auto restored = openvegas::composition::textStyleFromParameters(
            openvegas::composition::textStyleToParameters(panel.style()));
        QCOMPARE(renderStyle(restored), renderStyle(panel.style()));
    }
    void toggles_data()
    {
        QTest::addColumn<QString>("name");
        for (const char* name : {"toolButtonAllCaps", "toolButtonSmallCaps", "toolButtonTitleCase",
             "toolButtonLowerCase", "toolButtonUnderline", "toolButtonStrikethrough",
             "toolButtonSuperscript", "toolButtonSubscript", "toolButtonAlignCenter",
             "toolButtonAlignRight", "toolButtonAlignLeftJustify", "toolButtonAlignCenterJustify",
             "toolButtonAlignRightJustify", "toolButtonAlignJustify", "toolButtonAlignMiddle",
             "toolButtonAlignBottom"}) QTest::newRow(name) << QString::fromLatin1(name);
    }
    void toggles()
    {
        QFETCH(QString, name);
        TextPanel panel; panel.setStyle(sampleStyle());
        auto* button = panel.findChild<QToolButton*>(name);
        QVERIFY(button); QVERIFY(!button->icon().isNull());
        const auto before = renderStyle(panel.style());
        QSignalSpy edits(&panel, &TextPanel::styleEdited);
        button->click();
        QCOMPARE(edits.count(), 1);
        QVERIFY2(before != renderStyle(panel.style()), qPrintable(name));
    }
    void fontAndStrokeChoices()
    {
        TextPanel panel; panel.setStyle(sampleStyle());
        auto* size = panel.findChild<QSpinBox*>("spinBoxFontSize");
        auto* face = panel.findChild<QComboBox*>("comboBoxFontStyle");
        auto* order = panel.findChild<QComboBox*>("comboBoxStrokeOrder");
        QVERIFY(size); QVERIFY(face); QVERIFY(order);
        auto before = renderStyle(panel.style());
        size->setValue(42);
        QVERIFY(before != renderStyle(panel.style()));
        const int bold = face->findText("Bold"); QVERIFY(bold >= 0);
        before = renderStyle(panel.style()); face->setCurrentIndex(bold);
        QVERIFY(before != renderStyle(panel.style()));
        QList<QImage> modes;
        for (int i = 0; i < order->count(); ++i) {
            order->setCurrentIndex(i);
            const auto rendered = renderStyle(panel.style());
            for (const auto& previous : modes) QVERIFY(previous != rendered);
            modes.append(rendered);
        }
    }
    void allJustificationsDiffer()
    {
        auto style = sampleStyle();
        style.backgroundEnabled = false;
        QList<QImage> modes;
        for (int i = 0; i < 7; ++i) {
            style.alignH = static_cast<TextStyle::AlignH>(i);
            const auto rendered = renderStyle(style);
            for (int j = 0; j < modes.size(); ++j)
                QVERIFY2(modes[j] != rendered, qPrintable(QString("alignment %1 equals %2").arg(i).arg(j)));
            modes.append(rendered);
        }
    }
    void outlinesAndBackground()
    {
        TextPanel panel; panel.setStyle(sampleStyle());
        auto* add = panel.findChild<QToolButton*>("toolButtonAddOutline"); QVERIFY(add);
        auto before = renderStyle(panel.style()); add->click();
        QCOMPARE(panel.style().additionalOutlines.size(), 1);
        QVERIFY(before != renderStyle(panel.style()));
        auto* size = panel.findChild<QDoubleSpinBox*>("outlineSize0"); QVERIFY(size);
        before = renderStyle(panel.style()); size->setValue(8);
        QVERIFY(before != renderStyle(panel.style()));
        auto* remove = panel.findChild<QToolButton*>("removeOutline0"); QVERIFY(remove);
        remove->click();
        QCoreApplication::processEvents();
        QVERIFY(panel.style().additionalOutlines.isEmpty());
        auto* background = panel.findChild<QCheckBox*>("checkBoxBackground"); QVERIFY(background);
        before = renderStyle(panel.style()); background->click();
        QVERIFY(!panel.style().backgroundEnabled);
        QVERIFY(before != renderStyle(panel.style()));
        QVERIFY(!panel.findChild<QDoubleSpinBox*>("doubleSpinBoxBackgroundOpacity")->isEnabled());
    }
    void typedAndDraggedValue()
    {
        TextPanel panel; panel.setStyle(sampleStyle()); panel.show();
        auto* spin = panel.findChild<QDoubleSpinBox*>("doubleSpinBoxLineHeight"); QVERIFY(spin);
        auto* edit = spin->findChild<QLineEdit*>(); QVERIFY(edit);
        edit->setFocus(); edit->selectAll();
        QTest::keyClicks(edit, "175"); QTest::keyClick(edit, Qt::Key_Return);
        QCOMPARE(panel.style().lineSpacing, 175.0);
        const QPoint pos = edit->rect().center();
        QTest::mousePress(edit, Qt::LeftButton, Qt::NoModifier, pos);
        QMouseEvent move(QEvent::MouseMove, QPointF(pos + QPoint(30, 0)),
            QPointF(edit->mapToGlobal(pos + QPoint(30, 0))), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(edit, &move);
        QTest::mouseRelease(edit, Qt::LeftButton, Qt::NoModifier, pos + QPoint(30, 0));
        QVERIFY(panel.style().lineSpacing > 175);
        QVERIFY(!QApplication::overrideCursor());
    }
    void undoAndSaveRoundTrip()
    {
        using namespace openvegas;
        auto composition = std::make_shared<composition::Composition>();
        composition->addLayer("Text").kind = composition::LayerKind::Text;
        auto* clip = composition->addClip("Text", core::Identifier("media:text"), 0, 5);
        composition::Effect effect; effect.pluginId = core::Identifier("text"); effect.name = "Text";
        auto original = sampleStyle();
        original.text = "  Line one\n\nПривет <&> 世界  ";
        original.additionalOutlines.append({5, Qt::green});
        effect.parameterValues = composition::textStyleToParameters(original);
        clip->effects.append(effect);
        const auto before = effect.parameterValues;
        auto changed = original; changed.fontSize = 40;
        const auto after = composition::textStyleToParameters(changed);
        QUndoStack history;
        history.push(new ui::TextEditCommand(composition, 0, 0, 0, before, after, [] {}));
        QCOMPARE(composition->clipAt(0, 0)->effects[0].parameterValues, after);
        history.undo(); QCOMPARE(composition->clipAt(0, 0)->effects[0].parameterValues, before);
        history.redo(); QCOMPARE(composition->clipAt(0, 0)->effects[0].parameterValues, after);
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto file = directory.filePath("text.vegfx");
        media::MediaManager media;
        auto result = project::VegfxSerializer::saveToFile(file, *composition, media);
        QVERIFY2(result.isSuccess(), qPrintable(result.message()));
        composition::Composition restored;
        result = project::VegfxSerializer::loadFromFile(file, &restored, &media);
        QVERIFY2(result.isSuccess(), qPrintable(result.message()));
        auto* loaded = restored.clipAt(0, 0); QVERIFY(loaded); QVERIFY(!loaded->effects.isEmpty());
        QCOMPARE(loaded->effects[0].parameterValues, after);
        QCOMPARE(renderStyle(composition::textStyleFromParameters(loaded->effects[0].parameterValues)), renderStyle(changed));
    }

    void colorDialogCancellation()
    {
        TextPanel panel;
        panel.setStyle(TextStyle());
        QSignalSpy changes(&panel, &TextPanel::styleEdited);
        auto* swatch = panel.findChild<QToolButton*>("toolButtonFontColor");
        QVERIFY(swatch);
        bool dialogFits = false;
        QTimer::singleShot(0, &panel, [&dialogFits] {
            if (auto* dialog = qobject_cast<QColorDialog*>(QApplication::activeModalWidget())) {
                dialogFits = dialog->width() >= 400 && dialog->height() >= 250;
                for (auto* input : dialog->findChildren<QLineEdit*>()) {
                    dialogFits = dialogFits && input->maximumWidth() > 56;
                }
                dialog->grab().save(QCoreApplication::applicationDirPath() + "/color-dialog.png");
                dialog->reject();
            }
        });
        swatch->click();
        QVERIFY(dialogFits);
        QCOMPARE(changes.count(), 0);
        QCOMPARE(panel.style().fontColor, QColor(Qt::white));
    }
    void textModeRoundTripAndDialog()
    {
        TextStyle style = sampleStyle();
        style.textMode = TextStyle::TextMode::Paragraph;
        style.paragraphSize = QSizeF(180, 90);
        const QStringList values = openvegas::composition::textStyleToParameters(style);
        const TextStyle restored = openvegas::composition::textStyleFromParameters(values);
        QCOMPARE(restored.textMode, TextStyle::TextMode::Paragraph);
        QCOMPARE(restored.paragraphSize, QSizeF(180, 90));
        openvegas::ui::TextSettingsDialog dialog;
        dialog.setStyle(restored);
        QCOMPARE(dialog.objectName(), QStringLiteral("TextSettingsDialog"));
        QVERIFY(dialog.findChild<QPlainTextEdit*>(QStringLiteral("plainTextEdit")));
        QVERIFY(dialog.findChild<QComboBox*>(QStringLiteral("comboBoxTextMode")));
        QCOMPARE(dialog.style().text, style.text);
        QCOMPARE(dialog.style().textMode, TextStyle::TextMode::Paragraph);
    }
    void colorControls_data()
    {
        QTest::addColumn<QString>("swatchName");
        QTest::addColumn<QString>("pipetteName");
        QTest::newRow("fill") << "toolButtonFontColor" << "toolButtonFontColorPipette";
        QTest::newRow("stroke") << "toolButtonFontOutlineColor" << "toolButtonFontOutlineColorPipette";
        QTest::newRow("background") << "toolButtonBackgroundColor" << "toolButtonBackgroundColorPipette";
        QTest::newRow("extra-stroke") << "outlineColor0" << "outlinePipette0";
    }
    void colorControls()
    {
        QFETCH(QString, swatchName);
        QFETCH(QString, pipetteName);
        TextPanel panel;
        auto style = sampleStyle();
        style.strokeOrder = TextStyle::StrokeOrder::UnderFill;
        style.additionalOutlines.append({5, Qt::black});
        panel.setStyle(style);
        panel.show();
        QSignalSpy changes(&panel, &TextPanel::styleEdited);
        auto* swatch = panel.findChild<QToolButton*>(swatchName);
        auto* pipette = panel.findChild<QToolButton*>(pipetteName);
        QVERIFY(swatch); QVERIFY(pipette);
        const auto before = renderStyle(panel.style());
        QTimer::singleShot(0, &panel, [] {
            if (auto* dialog = qobject_cast<QColorDialog*>(QApplication::activeModalWidget())) {
                dialog->setCurrentColor(QColor("#71edc3"));
                dialog->accept();
            }
        });
        swatch->click();
        QCOMPARE(changes.count(), 1);
        QVERIFY(renderStyle(panel.style()) != before);
        const auto accepted = openvegas::composition::textStyleToParameters(panel.style());
        changes.clear();
        pipette->click();
        QApplication::processEvents();
        auto* picker = panel.findChild<QWidget*>("screenColorPicker");
        QVERIFY(picker); QVERIFY(picker->isVisible());
        QTest::keyClick(picker, Qt::Key_Escape);
        QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCOMPARE(changes.count(), 0);
        QCOMPARE(openvegas::composition::textStyleToParameters(panel.style()), accepted);
        QVERIFY(!panel.findChild<QWidget*>("screenColorPicker"));
    }
    void selectionAndFonts()
    {
        TextPanel panel;
        QSignalSpy changes(&panel, &TextPanel::styleEdited);
        auto* family = panel.findChild<QComboBox*>("comboBoxFontFamily");
        auto* face = panel.findChild<QComboBox*>("comboBoxFontStyle");
        QVERIFY(family); QVERIFY(face);
        QVERIFY(!family->isEnabled());
        TextStyle style;
        style.text = "Selected text";
        style.fontFamily = "Unavailable imported font";
        style.fontStyle = "Imported face";
        panel.setStyle(style);
        QCOMPARE(family->currentText(), style.fontFamily);
        QCOMPARE(face->currentText(), style.fontStyle);
        QCOMPARE(changes.count(), 0);
        TextStyle defaults;
        panel.setStyle(defaults);
        QCOMPARE(family->currentText(), QApplication::font().family());
        QCOMPARE(changes.count(), 0);
        const QStringList families = QFontDatabase::families();
        QVERIFY(!families.isEmpty());
        family->setCurrentText(families.first());
        QVERIFY(QFontDatabase::styles(family->currentText()).contains(face->currentText())
                || face->currentText() == "Regular");
        panel.clearSelection();
        QVERIFY(!family->isEnabled());
    }
    void linkedExpansion()
    {
        TextPanel panel;
        TextStyle style;
        style.backgroundEnabled = true;
        style.backgroundExpansionX = 12;
        style.backgroundExpansionY = 99;
        panel.setStyle(style);
        auto* x = panel.findChild<QDoubleSpinBox*>("doubleSpinBoxBackgroundExpansionX");
        auto* y = panel.findChild<QDoubleSpinBox*>("doubleSpinBoxBackgroundExpansionY");
        auto* independent = panel.findChild<QToolButton*>("toolButtonExpansionLinked");
        QVERIFY(x); QVERIFY(y); QVERIFY(independent);
        QCOMPARE(y->value(), 12.0);
        QSignalSpy changes(&panel, &TextPanel::styleEdited);
        y->setValue(24);
        QCOMPARE(x->value(), 24.0);
        QCOMPARE(panel.style().backgroundExpansionX, 24.0);
        QCOMPARE(changes.count(), 1);
        independent->click();
        y->setValue(48);
        QCOMPARE(x->value(), 24.0);
        QVERIFY(!panel.style().expansionLinked);
        independent->click();
        QCOMPARE(y->value(), x->value());
        QVERIFY(panel.style().expansionLinked);
    }
    void layoutAndAlignment()
    {
        TextPanel panel;
        panel.resize(400, 720);
        TextStyle style;
        style.text = "Text preview";
        panel.setStyle(style);
        panel.show();
        QTest::qWait(50);
        QCOMPARE(panel.findChildren<QGroupBox*>().size(), 3);
        QVERIFY(!panel.findChild<QWidget*>("groupBoxNewTextLayer"));
        auto* scroll = panel.findChild<QScrollArea*>();
        QVERIFY(scroll);
        QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
        auto* left = panel.findChild<QToolButton*>("toolButtonAlignLeft");
        auto* center = panel.findChild<QToolButton*>("toolButtonAlignCenter");
        auto* right = panel.findChild<QToolButton*>("toolButtonAlignRight");
        QVERIFY(left); QVERIFY(center); QVERIFY(right);
        QVERIFY(left->icon().pixmap(20).toImage() != center->icon().pixmap(20).toImage());
        QVERIFY(right->icon().pixmap(20).toImage() != center->icon().pixmap(20).toImage());
        left->click();
        QCOMPARE(panel.style().alignH, TextStyle::AlignH::Left);
        QVERIFY(!center->isChecked());
        QVERIFY(panel.grab().save(QCoreApplication::applicationDirPath() + "/text-panel.png"));
        panel.resize(327, 720);
        QTest::qWait(50);
        QCOMPARE(panel.width(), 327);
        QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
        QCOMPARE(panel.findChild<QToolButton*>("toolButtonFontColor")->width(), 56);
        QVERIFY(panel.grab().save(QCoreApplication::applicationDirPath() + "/text-panel-narrow.png"));
    }
};

int main(int argc, char** argv)
{
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication application(argc, argv);
#ifdef Q_OS_WIN
    // The offscreen Windows plugin has no system font discovery.
    QFontDatabase::addApplicationFont("C:/Windows/Fonts/arial.ttf");
    QFontDatabase::addApplicationFont("C:/Windows/Fonts/arialbd.ttf");
    QFontDatabase::addApplicationFont("C:/Windows/Fonts/ariali.ttf");
    application.setFont(QFont("Arial", 10));
#endif
    openvegas::ui::applyTheme(&application);
    TextPanelRegression tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "textpanel_regression.moc"
