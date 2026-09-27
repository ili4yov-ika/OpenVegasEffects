#include <QtTest>
#include <QBuffer>
#include <QComboBox>
#include <QFile>
#include <QTemporaryDir>
#include <QWheelEvent>
#include <QPushButton>
#include "media/PcmWave.h"
#include "ui/VoiceoverDialog.h"
#include "ui/Theme.h"
#include "ui/ExportPanel.h"

class UiStubsRegression : public QObject {
    Q_OBJECT
    QTemporaryDir m_settings;
private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("OpenVegasTests");
        QCoreApplication::setApplicationName("UiStubs");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
    }
    void wavePreservesPcmAndTruncatesIncompleteFrame() {
        openvegas::media::PcmFormat format;
        format.setChannelCount(2); format.setSampleRate(48000);
        format.setSampleFormat(openvegas::media::PcmFormat::Int16);
        QByteArray samples(1003, '\0');
        for (int i = 0; i < samples.size(); ++i) samples[i] = char(i % 127);
        QBuffer pcm(&samples); QVERIFY(pcm.open(QIODevice::ReadOnly));
        QByteArray result; QBuffer output(&result); QVERIFY(output.open(QIODevice::WriteOnly));
        QVERIFY(openvegas::media::writePcmWave(pcm, output, format));
        QCOMPARE(result.size(), 1044);
        QCOMPARE(result.left(4), QByteArray("RIFF"));
        QCOMPARE(result.mid(8, 8), QByteArray("WAVEfmt "));
        QCOMPARE(result.mid(36, 4), QByteArray("data"));
        QCOMPARE(result.mid(44), samples.left(1000));
        QBuffer read(&result); read.open(QIODevice::ReadOnly); read.seek(16);
        QDataStream stream(&read); stream.setByteOrder(QDataStream::LittleEndian);
        quint32 chunkSize, sampleRate, byteRate; quint16 codec, channels, blockSize, bits;
        stream >> chunkSize >> codec >> channels >> sampleRate >> byteRate >> blockSize >> bits;
        QCOMPARE(codec, quint16(1)); QCOMPARE(channels, quint16(2));
        QCOMPARE(sampleRate, quint32(48000)); QCOMPARE(byteRate, quint32(192000));
        QCOMPARE(blockSize, quint16(4)); QCOMPARE(bits, quint16(16));
    }
    void waveRejectsEmptyAndUnsupportedData() {
        openvegas::media::PcmFormat format; format.setChannelCount(1); format.setSampleRate(44100);
        format.setSampleFormat(openvegas::media::PcmFormat::Int16);
        QBuffer pcm; pcm.open(QIODevice::ReadOnly);
        QBuffer output; output.open(QIODevice::WriteOnly);
        QVERIFY(!openvegas::media::writePcmWave(pcm, output, format));
        pcm.close(); pcm.setData(QByteArray(10, 'x')); pcm.open(QIODevice::ReadOnly);
        format.setSampleFormat(openvegas::media::PcmFormat::Float);
        QVERIFY(!openvegas::media::writePcmWave(pcm, output, format));
        QVERIFY(output.data().isEmpty());
    }
    void cancellingRecordingPreservesExistingFile() {
        QTemporaryDir dir; const QString path = dir.filePath("voice.wav");
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("original"); file.close();
        int start = 0, stop = 0;
        {
            openvegas::ui::VoiceoverDialog dialog(path);
            dialog.recordingStarted = [&] { ++start; };
            dialog.recordingStopped = [&] { ++stop; };
            dialog.reject();
            QCOMPARE(dialog.result(), int(QDialog::Rejected));
        }
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), QByteArray("original"));
        QCOMPARE(start, 0); QCOMPARE(stop, 0);
    }
    void closedComboHonoursWheelPreference() {
        openvegas::ui::installInterfacePreferenceFilter(qApp);
        QComboBox combo; combo.addItems({"first", "second", "third"});
        combo.setCurrentIndex(1); combo.show(); combo.setFocus();
        const auto wheel = [&] {
            QWheelEvent event(QPointF(10, 10), QPointF(combo.mapToGlobal(QPoint(10, 10))),
                QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(&combo, &event);
        };
        QSettings().setValue("Options/EnableWheelScrollMenus", false);
        wheel(); QCOMPARE(combo.currentIndex(), 1);
        QSettings().setValue("Options/EnableWheelScrollMenus", true);
        wheel(); QCOMPARE(combo.currentIndex(), 0);
    }
    void exportNamesRemoveOnlyKnownSourceExtension() {
        QSettings settings(QSettings::IniFormat, QSettings::UserScope, "OpenVegas", "OpenVegasEffects");
        openvegas::ui::ExportPanel panel;
        auto* presets = panel.findChild<QComboBox*>("comboBoxPreset"); QVERIFY(presets);
        for (int i = 0; i < presets->count(); ++i)
            if (presets->itemText(i).contains(".png")) { presets->setCurrentIndex(i); break; }
        panel.setOutputName("Shot.mp4");
        settings.setValue("Options/RemoveExtensions", true); settings.sync();
        QString name = QFileInfo(panel.outputPath()).fileName();
        QVERIFY(name.startsWith("Shot-")); QVERIFY(name.endsWith(".png"));
        settings.setValue("Options/RemoveExtensions", false); settings.sync();
        QVERIFY(QFileInfo(panel.outputPath()).fileName().startsWith("Shot.mp4-"));
        settings.setValue("Options/RemoveExtensions", true); settings.sync();
        panel.setOutputName("Version.2");
        QVERIFY(QFileInfo(panel.outputPath()).fileName().startsWith("Version.2-"));
        panel.setOutputName("bad/name:*?");
        const QString sanitised = QFileInfo(panel.outputPath()).fileName();
        QVERIFY(sanitised.startsWith("bad_name___-"));
    }
    void moviePresetExportsSingleFrameAsImage() {
        openvegas::ui::ExportPanel panel;
        auto* presets = panel.findChild<QComboBox*>("comboBoxPreset"); QVERIFY(presets);
        presets->setCurrentIndex(0); QVERIFY(presets->currentText().contains(".mp4"));
        QCOMPARE(QFileInfo(panel.outputPath()).suffix(), QString("png"));
        QSignalSpy contents(&panel, &openvegas::ui::ExportPanel::exportContentsRequested);
        auto* button = panel.findChild<QPushButton*>("pushButtonExportContents"); QVERIFY(button);
        button->click(); QCOMPARE(contents.count(), 1);
        QCOMPARE(QFileInfo(contents[0][0].toString()).suffix(), QString("mp4"));
    }
};
QTEST_MAIN(UiStubsRegression)
#include "ui_stubs_regression.moc"
