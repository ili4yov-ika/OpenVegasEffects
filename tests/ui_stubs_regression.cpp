#include <QtTest>
#include <QBuffer>
#include <QComboBox>
#include <QFile>
#include <QTemporaryDir>
#include <QWheelEvent>
#include <QPushButton>
#include <QTimer>
#include "media/PcmWave.h"
#include "ui/VoiceoverDialog.h"
#include "ui/Theme.h"
#include "ui/ExportPanel.h"
#include "ui/ExportPresets.h"
#include "render/ExportPreset.h"
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QTabWidget>
#include <QTreeWidget>

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
    // The reference's built-in presets (names and comments) with this
    // port's settings, each exportable from a 1080p30 shot; a preset file
    // (.hfxp, <Preset><MP4 Version="4"><ExportPreset/><PropertyManager/>)
    // written and read back for every kind, and one in the reference's own
    // shape read.
    void exportPresetsBuiltInAndFiles() {
        using namespace openvegas::render;
        const QVector<ExportPreset> builtIn = builtInExportPresets();
        QCOMPARE(builtIn.size(), 21);
        QStringList names;
        for (const ExportPreset& preset : builtIn) {
            names << preset.name;
            QVERIFY(!preset.userCreated);
            QVERIFY2(validateExportPreset(preset, QSize(1920, 1080), 30.0).isEmpty(), qPrintable(preset.name));
        }
        QVERIFY(names.contains(defaultExportPresetName()));
        QCOMPARE(defaultExportPresetName(), QString("YouTube 1080p HD"));
        for (const char* name : {"YouTube 2160p UHD", "Apple ProRes 4444 (Alpha)", "PNG Sequence with Alpha",
                                 "PCM Audio Only (WAV)", "Instagram Square", "Match Source"})
            QVERIFY2(names.contains(QString::fromLatin1(name)), name);
        QCOMPARE(names.removeDuplicates(), 0);

        QTemporaryDir dir; QVERIFY(dir.isValid());
        ExportPreset mp4;
        mp4.name = "Web"; mp4.comment = "For the site"; mp4.format = ExportFormat::MP4;
        mp4.widthUseSource = false; mp4.heightUseSource = false; mp4.width = 1280; mp4.height = 720;
        mp4.frameRateUseSource = false; mp4.frameRate = 25.0;
        mp4.scaleMode = ExportPreset::ScaleMode::KeepAspectRatioByExpanding;
        mp4.h264Profile = ExportPreset::H264Profile::Main; mp4.bitRateMode = ExportPreset::BitRateMode::Constant;
        mp4.bitRate = 4000000; mp4.maxBitRate = 6000000; mp4.audioSampleRate = 44100; mp4.audioBitRate = 192000;
        ExportPreset mov = mp4; mov.name = "Master"; mov.format = ExportFormat::MOV;
        mov.movCodec = ExportPreset::MovCodec::PhotoJpeg; mov.quality = 70; mov.audioCodec = ExportPreset::AudioCodec::Pcm;
        mov.keyFrameInterval = 12;
        ExportPreset avi = mp4; avi.name = "Cine"; avi.format = ExportFormat::AVI;
        avi.aviCodec = ExportPreset::AviCodec::CineForm; avi.cineFormRgb = true; avi.alpha = true;
        avi.audioCodec = ExportPreset::AudioCodec::Pcm;
        ExportPreset wav; wav.name = "Sound"; wav.format = ExportFormat::WAV; wav.exportVideo = false;
        wav.audioSampleRate = 96000; wav.audioCodec = ExportPreset::AudioCodec::Pcm;
        ExportPreset sequence; sequence.name = "Frames"; sequence.format = ExportFormat::ImageSequence;
        sequence.imageFormat = ExportPreset::ImageFormat::Jpg; sequence.quality = 55; sequence.prefix = "shot_";
        sequence.exportAudio = false; sequence.widthUseSource = false; sequence.width = 640;
        for (const ExportPreset& preset : {mp4, mov, avi, wav, sequence}) {
            const QString path = dir.filePath(preset.name + "." + exportPresetFileSuffix());
            QVERIFY(writeExportPreset(path, preset));
            ExportPreset back;
            QVERIFY(readExportPreset(path, &back));
            QCOMPARE(back.name, preset.name); QCOMPARE(back.comment, preset.comment);
            QCOMPARE(int(back.format), int(preset.format)); QVERIFY(back.userCreated);
            QCOMPARE(back.exportVideo, preset.exportVideo); QCOMPARE(back.exportAudio, preset.exportAudio);
            QCOMPARE(back.extension(), preset.extension());
            QCOMPARE(back.audioSampleRate, preset.audioSampleRate);
            if (preset.format != ExportFormat::WAV) {
                QCOMPARE(back.widthUseSource, preset.widthUseSource); QCOMPARE(back.width, preset.width);
                QCOMPARE(int(back.scaleMode), int(preset.scaleMode));
            }
        }
        ExportPreset back;
        QVERIFY(readExportPreset(dir.filePath("Web.hfxp"), &back));
        QCOMPARE(back.frameRate, 25.0); QVERIFY(!back.frameRateUseSource);
        QCOMPARE(int(back.h264Profile), int(ExportPreset::H264Profile::Main));
        QCOMPARE(int(back.bitRateMode), int(ExportPreset::BitRateMode::Constant));
        QCOMPARE(back.bitRate, 4000000); QCOMPARE(back.maxBitRate, 6000000); QCOMPARE(back.audioBitRate, 192000);
        QVERIFY(readExportPreset(dir.filePath("Master.hfxp"), &back));
        QCOMPARE(int(back.movCodec), int(ExportPreset::MovCodec::PhotoJpeg)); QCOMPARE(back.quality, 70);
        QCOMPARE(back.keyFrameInterval, 12);
        QVERIFY(readExportPreset(dir.filePath("Cine.hfxp"), &back));
        QVERIFY(back.cineFormRgb && back.alpha);
        QVERIFY(readExportPreset(dir.filePath("Frames.hfxp"), &back));
        QCOMPARE(back.prefix, QString("shot_")); QCOMPARE(back.quality, 55); QCOMPARE(back.extension(), QString("jpg"));
        // A file in the reference's shape: Static values, other attributes.
        const QString reference = dir.filePath("Reference.hfxp");
        QFile file(reference); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("<?xml version=\"1.0\"?><Preset><MP4 Version=\"4\"><ExportPreset Version=\"0\" IsUserCreated=\"0\">"
                   "<Comment>Shipped</Comment></ExportPreset><PropertyManager Version=\"7\">"
                   "<Prop Type=\"0\" Spatial=\"0\" CanInterpT=\"1\"><Name>width</Name><Static V=\"4\"><i>3840</i></Static></Prop>"
                   "<Prop Type=\"0\" Spatial=\"0\" CanInterpT=\"1\"><Name>width_useSource</Name><Default V=\"4\"><b>0</b></Default></Prop>"
                   "<Prop Type=\"0\" Spatial=\"0\" CanInterpT=\"1\"><Name>h264_targetBitRate</Name><Default V=\"4\"><i>40000000</i></Default></Prop>"
                   "</PropertyManager></MP4></Preset>");
        file.close();
        QVERIFY(readExportPreset(reference, &back));
        QCOMPARE(back.name, QString("Reference")); QCOMPARE(back.comment, QString("Shipped"));
        QVERIFY(!back.userCreated); QCOMPARE(back.width, 3840); QVERIFY(!back.widthUseSource);
        QCOMPARE(back.bitRate, 40000000);
        QVERIFY(!readExportPreset(dir.filePath("missing.hfxp"), &back));
    }
    // What FFmpeg is told for a preset: codec and rate per encoder, the
    // filter that brings the shot to the preset's size by its scale mode
    // (a non-square shot first to its square shape), the audio; and what a
    // preset cannot export, in the reference's words.
    void exportPresetArguments() {
        using namespace openvegas::render;
        ExportPreset youtube;
        for (const ExportPreset& preset : builtInExportPresets())
            if (preset.name == "YouTube 1080p HD") youtube = preset;
        QVERIFY(youtube.isValid());
        const QStringList x264 = exportVideoArguments(youtube, "libx264");
        const QString joined = x264.join(' ');
        QVERIFY2(joined.contains("-c:v libx264") && joined.contains("-profile:v high")
                     && joined.contains("-b:v 12000000") && joined.contains("-maxrate 16000000")
                     && joined.contains("+faststart"), qPrintable(joined));
        QVERIFY(!joined.contains("nal-hrd"));
        ExportPreset constant = youtube; constant.bitRateMode = ExportPreset::BitRateMode::Constant;
        QVERIFY(exportVideoArguments(constant, "libx264").join(' ').contains("-minrate 12000000 -x264-params nal-hrd=cbr"));
        QVERIFY(exportVideoArguments(youtube, "h264_nvenc").join(' ').contains("-rc vbr -b:v 12000000"));
        QCOMPARE(exportVideoFilter(youtube, QSize(1920, 1080), 1.0), QString());
        QCOMPARE(exportVideoFilter(youtube, QSize(1280, 720), 1.0),
                 QString("scale=1920:1080:force_original_aspect_ratio=decrease,pad=1920:1080:(ow-iw)/2:(oh-ih)/2,setsar=1"));
        ExportPreset square = youtube; square.width = square.height = 1080;
        square.scaleMode = ExportPreset::ScaleMode::KeepAspectRatioByExpanding;
        QCOMPARE(exportVideoFilter(square, QSize(1920, 1080), 1.0),
                 QString("scale=1080:1080:force_original_aspect_ratio=increase,crop=1080:1080,setsar=1"));
        // A 2:1 shot of 960x1080 pixels is 1920 wide first.
        QCOMPARE(exportVideoFilter(youtube, QSize(960, 1080), 2.0),
                 QString("scale=1920:1080,setsar=1,scale=1920:1080:force_original_aspect_ratio=decrease,"
                         "pad=1920:1080:(ow-iw)/2:(oh-ih)/2,setsar=1"));
        ExportPreset source = youtube; source.widthUseSource = source.heightUseSource = true;
        QCOMPARE(exportVideoFilter(source, QSize(960, 1080), 2.0), QString("setsar=2/1"));
        QCOMPARE(exportAudioArguments(youtube).join(' '), QString("-c:a aac -b:a 320000 -ar 48000 -ac 2"));

        ExportPreset prores; prores.format = ExportFormat::MOV;
        prores.movCodec = ExportPreset::MovCodec::ProRes4444; prores.alpha = true;
        QVERIFY(exportVideoArguments(prores, QString()).join(' ').contains("prores_ks -profile:v 4 -pix_fmt yuva444p10le"));
        prores.movCodec = ExportPreset::MovCodec::ProResHQ; prores.alpha = false;
        QVERIFY(exportVideoArguments(prores, QString()).join(' ').contains("-profile:v 3 -pix_fmt yuv422p10le"));
        prores.movCodec = ExportPreset::MovCodec::PhotoJpeg; prores.quality = 100;
        QVERIFY(exportVideoArguments(prores, QString()).join(' ').contains("mjpeg -q:v 2"));
        prores.audioCodec = ExportPreset::AudioCodec::Pcm; prores.audioSampleRate = 44100;
        QCOMPARE(exportAudioArguments(prores).join(' '), QString("-c:a pcm_s16le -ar 44100 -ac 2"));
        ExportPreset avi; avi.format = ExportFormat::AVI; avi.alpha = true;
        QVERIFY(exportVideoArguments(avi, QString()).join(' ').contains("rawvideo -pix_fmt bgra"));
        ExportPreset wav; wav.format = ExportFormat::WAV; wav.exportVideo = false;
        QCOMPARE(exportVideoArguments(wav, QString()), QStringList{"-vn"});
        ExportPreset silent = youtube; silent.exportAudio = false;
        QCOMPARE(exportAudioArguments(silent), QStringList{"-an"});

        ExportPreset odd = youtube; odd.width = 1279; odd.height = 721;
        QCOMPARE(validateExportPreset(odd, QSize(1920, 1080), 30.0),
                 QStringList{"(H264) The width and height (1279 x 721) must be multiples of 2."});
        ExportPreset nothing = youtube; nothing.exportVideo = false; nothing.exportAudio = false;
        QCOMPARE(validateExportPreset(nothing, QSize(1920, 1080), 30.0),
                 QStringList{"Video and/or Audio export must be enabled."});
        ExportPreset cine; cine.format = ExportFormat::MOV; cine.movCodec = ExportPreset::MovCodec::CineForm;
        cine.alpha = true; cine.widthUseSource = cine.heightUseSource = false; cine.width = 1000; cine.height = 563;
        QCOMPARE(validateExportPreset(cine, QSize(1920, 1080), 30.0).size(), 2);
        ExportPreset pal; pal.format = ExportFormat::AVI; pal.aviCodec = ExportPreset::AviCodec::DvPal;
        QCOMPARE(validateExportPreset(pal, QSize(1920, 1080), 30.0),
                 QStringList{"(DV PAL) The dimensions and frame rate must be 720 x 576 @ 25 fps."});
        pal.widthUseSource = pal.heightUseSource = false; pal.width = 720; pal.height = 576;
        pal.frameRateUseSource = false; pal.frameRate = 25.0;
        QVERIFY(validateExportPreset(pal, QSize(1920, 1080), 30.0).isEmpty());
        ExportPreset mute; mute.format = ExportFormat::WAV; mute.exportAudio = false;
        QCOMPARE(validateExportPreset(mute, QSize(1920, 1080), 30.0), QStringList{"Audio export must be enabled."});
    }
    // The manager keeps user presets as files (save, rename, duplicate,
    // remove; built-in ones untouched) and the default; the Presets tab
    // lists them in two groups and searches them; Edit Preset rebuilds its
    // rows with the format and codec; the Export panel's list follows.
    void exportPresetManagerAndPage() {
        using namespace openvegas;
        using render::ExportPreset;
        QTemporaryDir dir; QVERIFY(dir.isValid());
        ui::ExportPresetManager manager(dir.filePath("ExportPresets"));
        QCOMPARE(manager.presets().size(), 21);
        QCOMPARE(manager.defaultPreset(), QString("YouTube 1080p HD"));
        QSignalSpy changed(&manager, &ui::ExportPresetManager::presetsChanged);
        ExportPreset mine = manager.preset("YouTube 1080p HD");
        mine.name = "Mine"; mine.width = 1280; mine.height = 720;
        QString error;
        QVERIFY2(manager.savePreset(mine, QString(), &error), qPrintable(error));
        QVERIFY(QFileInfo::exists(dir.filePath("ExportPresets/Mine.hfxp")));
        QVERIFY(manager.preset("Mine").userCreated);
        QCOMPARE(manager.preset("Mine").width, 1280);
        QVERIFY(!changed.isEmpty());
        // A name in use is refused; a built-in one cannot be removed.
        QVERIFY(!manager.savePreset(mine, QString(), &error));
        QVERIFY(error.contains("Mine"));
        QVERIFY(manager.setDefaultPreset("Mine"));
        mine.name = "Renamed";
        QVERIFY(manager.savePreset(mine, "Mine"));
        QVERIFY(!QFileInfo::exists(dir.filePath("ExportPresets/Mine.hfxp")));
        QCOMPARE(manager.defaultPreset(), QString("Renamed"));
        QCOMPARE(manager.duplicatePresets({"Renamed", "PNG Sequence"}),
                 QStringList({"Renamed 1", "PNG Sequence 1"}));
        QVERIFY(manager.preset("PNG Sequence 1").userCreated);
        QVERIFY(manager.removePresets({"Renamed", "PNG Sequence"}));
        QVERIFY(!manager.contains("Renamed"));
        QVERIFY(manager.contains("PNG Sequence"));
        QCOMPARE(manager.defaultPreset(), QString("YouTube 1080p HD"));
        QCOMPARE(manager.uniqueName("Renamed 1"), QString("Renamed 1 1"));
        QVERIFY(!manager.setDefaultPreset("Nothing"));
        // Read again from the folder.
        ui::ExportPresetManager reopened(dir.filePath("ExportPresets"));
        QCOMPARE(reopened.presets().size(), 23);

        ui::ExportPresetsPage page(&manager);
        page.resize(700, 420); page.show(); QTest::qWait(20);
        QTreeWidget* tree = page.tree();
        QCOMPARE(tree->topLevelItemCount(), 2);
        QCOMPARE(tree->topLevelItem(0)->childCount(), 21);
        QCOMPARE(tree->topLevelItem(1)->childCount(), 2);
        QCOMPARE(tree->topLevelItem(0)->child(0)->text(0), QString("YouTube 1080p HD"));
        QCOMPARE(tree->topLevelItem(0)->child(0)->text(1), QString("MPEG-4 (.mp4)"));
        QCOMPARE(tree->topLevelItem(0)->child(0)->text(2), QString("1920 x 1080"));
        QCOMPARE(tree->topLevelItem(0)->child(0)->text(3), QString("From Source"));
        QVERIFY(tree->topLevelItem(0)->child(0)->font(0).bold());   // the default
        auto* search = page.findChild<QLineEdit*>("lineEditPresetSearch"); QVERIFY(search);
        search->setText("ProRes");
        int shown = 0;
        for (int i = 0; i < tree->topLevelItem(0)->childCount(); ++i)
            if (!tree->topLevelItem(0)->child(i)->isHidden()) ++shown;
        QCOMPARE(shown, 3);
        search->clear();
        auto* defaults = page.findChild<QComboBox*>("comboBoxDefaultPreset"); QVERIFY(defaults);
        defaults->setCurrentIndex(defaults->findData("PNG Sequence"));
        QCOMPARE(manager.defaultPreset(), QString("PNG Sequence"));
        page.makeDefault("YouTube 1080p HD");
        QVERIFY(page.grab().save(QDir(QDir::tempPath()).filePath("openvegas-export-presets.png")));

        // Edit Preset: Use Source off and a width; the rows follow the format.
        ui::ExportPresetDialog dialog(manager.preset("YouTube 1080p HD"), true, [](const QString&) { return false; });
        auto* width = dialog.findChild<QWidget*>("presetProperty_width"); QVERIFY(width);
        width->findChild<QSpinBox*>()->setValue(1281);
        QCOMPARE(dialog.preset().width, 1281);
        auto* problems = dialog.findChild<QLabel*>("labelPresetError");
        QVERIFY(problems->text().contains("multiples of 2"));
        width->findChild<QSpinBox*>()->setValue(1280);
        QVERIFY(problems->text().isEmpty());
        QVERIFY(dialog.findChild<QWidget*>("presetProperty_maxBitRate"));
        dialog.findChild<QComboBox*>("presetProperty_encoding")->setCurrentIndex(0);
        QTRY_VERIFY(!dialog.findChild<QWidget*>("presetProperty_maxBitRate"));
        QCOMPARE(int(dialog.preset().bitRateMode), int(ExportPreset::BitRateMode::Constant));
        auto* format = dialog.findChild<QComboBox*>("comboBoxPresetFormat");
        format->setCurrentIndex(format->findData(int(render::ExportFormat::MOV)));
        QVERIFY(dialog.findChild<QComboBox*>("presetProperty_codec"));
        QVERIFY(!dialog.findChild<QWidget*>("presetProperty_quality"));
        dialog.findChild<QComboBox*>("presetProperty_codec")->setCurrentIndex(int(ExportPreset::MovCodec::PhotoJpeg));
        QTRY_VERIFY(dialog.findChild<QWidget*>("presetProperty_quality"));
        QCOMPARE(dialog.preset().extension(), QString("mov"));
        format->setCurrentIndex(format->findData(int(render::ExportFormat::WAV)));
        QTRY_VERIFY(!dialog.findChild<QWidget*>("presetProperty_width"));
        QVERIFY(dialog.findChild<QComboBox*>("presetProperty_sampleRate"));

        // The Export panel: two tabs, the presets in its list, the default
        // chosen, the file's suffix from the chosen preset.
        ui::ExportPanel panel;
        panel.setExportDirectory(dir.path());
        panel.setPresetManager(&manager);
        QCOMPARE(panel.findChild<QTabWidget*>("export-panel-tabs")->count(), 2);
        auto* presets = panel.findChild<QComboBox*>("comboBoxPreset");
        QCOMPARE(presets->count(), manager.presets().size());
        QCOMPARE(panel.currentPreset(), QString("YouTube 1080p HD"));
        QVERIFY(panel.contentsOutputPath().endsWith(".mp4"));
        QVERIFY(panel.outputPath().endsWith(".png"));
        presets->setCurrentIndex(presets->findData("PCM Audio Only (WAV)"));
        QVERIFY(panel.contentsOutputPath().endsWith(".wav"));
        QVERIFY(panel.currentExportPreset().isValid());
        presets->setCurrentIndex(presets->findData("PNG Sequence 1"));
        manager.removePresets({"PNG Sequence 1"});
        QCOMPARE(panel.currentPreset(), QString("YouTube 1080p HD"));   // gone: back to the default
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
        openvegas::app::Settings::optionSettings().setValue("Options/EnableWheelScrollMenus", false);
        wheel(); QCOMPARE(combo.currentIndex(), 1);
        openvegas::app::Settings::optionSettings().setValue("Options/EnableWheelScrollMenus", true);
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
    void colorPickerUsesOptionsIniStore() {
        auto settings = openvegas::app::Settings::optionSettings();
        QCOMPARE(settings.format(), QSettings::IniFormat);
        settings.setValue("Options/UseNativeColorPicker", false); settings.sync();
        bool inspected = false;
        QTimer::singleShot(0, [&] {
            auto* dialog = qobject_cast<QColorDialog*>(QApplication::activeModalWidget());
            if (dialog) {
                inspected = dialog->testOption(QColorDialog::DontUseNativeDialog);
                dialog->reject();
            }
        });
        const QColor result = openvegas::ui::interfaceColor(Qt::red, nullptr);
        QVERIFY(inspected); QVERIFY(!result.isValid());
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
