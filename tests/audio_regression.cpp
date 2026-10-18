#include "media/AudioPlayer.h"
#include "plugin/NativeEffectRender.h"
#include "plugin/NativePlugin.h"
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>
#include <QtEndian>

class AudioRegression : public QObject
{
    Q_OBJECT
    static QString constantWave(const QTemporaryDir& dir, const QString& name, int value) {
        const QString path = dir.filePath(name);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return {};
        QDataStream stream(&file); stream.setByteOrder(QDataStream::LittleEndian);
        constexpr int frames = 48000 * 2;
        stream.writeRawData("RIFF", 4); stream << quint32(36 + frames * 4);
        stream.writeRawData("WAVEfmt ", 8);
        stream << quint32(16) << quint16(1) << quint16(2) << quint32(48000)
               << quint32(192000) << quint16(4) << quint16(16);
        stream.writeRawData("data", 4); stream << quint32(frames * 4);
        for (int i = 0; i < frames; ++i) {
            const int sample = value == -32768 ? (i < 12000 ? 0 : i < 24000 ? 1000 : i < 36000 ? 2000 : 0) : value;
            stream << qint16(sample) << qint16(sample);
        }
        return path;
    }
    static bool containsSample(const QSignalSpy& spy, int value, int tolerance = 3) {
        for (const auto& entry : spy) {
            const auto bytes = entry[0].toByteArray();
            if (bytes.size() >= 400 && std::abs(int(qFromLittleEndian<qint16>(bytes.constData() + 200)) - value) <= tolerance) return true;
        }
        return false;
    }
    static void sampleRatesData() {
        QTest::addColumn<int>("sampleRate");
        for (int rate : {32000, 44100, 48000, 96000})
            QTest::newRow(qPrintable(QString::number(rate))) << rate;
    }
private slots:
    void compositionRatePreservesTimingAndMasterPcm_data() { sampleRatesData(); }
    void compositionRatePreservesTimingAndMasterPcm() {
        QFETCH(int, sampleRate);
        if (!openvegas::media::AudioPlayer::isAvailable()) QSKIP("libVLC is required");
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString steps = constantWave(dir, "steps.wav", -32768);
        const QString base = constantWave(dir, "base.wav", 1000);
        const QVector<openvegas::media::AudioClip> clips {
            {steps, 0.1, 0.6, 0.25, 2, 0.5}, {base, 0, 0.8, 0, 1, 0.25}};
        openvegas::media::AudioPlayer audio;
        QSignalSpy pcm(&audio, &openvegas::media::AudioPlayer::pcmMixed);
        QSignalSpy errors(&audio, &openvegas::media::AudioPlayer::playbackError);
        audio.setClips(clips, 0.8, {}, sampleRate);
        QCOMPARE(audio.sampleRate(), sampleRate);
        const auto valueNear = [&](double seconds, int expected) {
            for (const auto& entry : pcm) {
                const auto bytes = entry[0].toByteArray();
                if (qAbs(entry[1].toDouble() - seconds) > 0.011) continue;
                if (bytes.size() != sampleRate / 100 * 4) continue;
                const int offset = sampleRate / 400 * 4; // 2.5 ms into the block.
                if (qAbs(int(qFromLittleEndian<qint16>(bytes.constData() + offset)) - expected) < 5)
                    return true;
            }
            return false;
        };
        audio.play(0);
        QTRY_VERIFY_WITH_TIMEOUT(valueNear(0.04, 250), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(valueNear(0.14, 750), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(valueNear(0.28, 1250), 5000);
        audio.seek(0.26); pcm.clear();
        QTRY_VERIFY_WITH_TIMEOUT(valueNear(0.28, 1250), 5000);
        audio.stop(); const auto count = pcm.count();
        QTest::qWait(50); QCOMPARE(pcm.count(), count);
        QVERIFY2(errors.isEmpty(), errors.isEmpty() ? "" : qPrintable(errors[0][0].toString()));
        audio.setSource(base); QCOMPARE(audio.sampleRate(), 48000);
        audio.setClips(clips, 0.8, {}, 0); QCOMPARE(audio.sampleRate(), 48000);
        audio.setClips(clips, 0.8, {}, INT_MAX); QCOMPARE(audio.sampleRate(), 48000);
    }

    void selectedAudioStreamReachesMasterPcm()
    {
        const QString ffmpeg = QStandardPaths::findExecutable("ffmpeg");
        if (ffmpeg.isEmpty() || !openvegas::media::AudioPlayer::isAvailable())
            QSKIP("FFmpeg and libVLC are needed for the multi-stream audio test");
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString quiet = constantWave(dir, "quiet.wav", 1000);
        const QString loud = constantWave(dir, "loud.wav", 3000);
        const QString source = dir.filePath("streams.mkv");
        QProcess make;
        make.start(ffmpeg, {"-v", "error", "-y", "-i", quiet, "-i", loud,
                            "-map", "0:a:0", "-map", "1:a:0", "-c:a", "pcm_s16le", source});
        QVERIFY(make.waitForFinished(30000));
        QVERIFY2(make.exitCode() == 0, make.readAllStandardError().constData());
        openvegas::media::AudioPlayer audio;
        QSignalSpy pcm(&audio, &openvegas::media::AudioPlayer::pcmMixed);
        for (const int index : {0, 1}) {
            openvegas::media::AudioClip clip {source, 0, 1, 0, 1, 1};
            clip.audioStreamIndex = index;
            pcm.clear();
            audio.setClips({clip}, 1);
            audio.play(0);
            QTRY_VERIFY_WITH_TIMEOUT(containsSample(pcm, index == 0 ? 1000 : 3000, 10), 5000);
            QVERIFY(!containsSample(pcm, index == 0 ? 3000 : 1000, 10));
            audio.stop();
        }
    }

    void unavailableNativeEffectKeepsDryAudio()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        openvegas::media::AudioClip clip {
            constantWave(dir, QStringLiteral("dry.wav"), 1000),
            0, 0.5, 0, 1, 1};
        clip.nativeEffects.append({
            openvegas::core::Identifier(QStringLiteral("test.missing.native.audio")),
            {}, QStringLiteral("missing-instance")});
        openvegas::media::AudioPlayer audio;
        QSignalSpy pcm(&audio, &openvegas::media::AudioPlayer::pcmMixed);
        audio.setClips({clip}, 0.5);
        audio.play(0);
        QTRY_VERIFY_WITH_TIMEOUT(containsSample(pcm, 1000), 3000);
        audio.stop();
    }
    void nativeBalanceChangesRealtimeMaster_data() { sampleRatesData(); }
    void nativeBalanceChangesRealtimeMaster()
    {
        QFETCH(int, sampleRate);
        const QString modulePath = qEnvironmentVariable("OPENVEGAS_HFPL_BALANCE");
        if (modulePath.isEmpty() || !QFileInfo::exists(modulePath)) {
            QSKIP("Set OPENVEGAS_HFPL_BALANCE to the reference Balance.hfpl for this integration test");
        }
        const QString dependencyDir = QFileInfo(modulePath).absoluteDir().absoluteFilePath(
            QStringLiteral("../../.."));
        const auto metadata = openvegas::plugin::loadNativePluginMetadata(
            modulePath, dependencyDir);
        QVERIFY(metadata.lifecycleCompatible);
        const openvegas::core::Identifier id(QStringLiteral("test.audio.balance"));
        openvegas::plugin::registerNativeAudioEffectModule(
            id, modulePath, dependencyDir, true, metadata.parameters);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = constantWave(dir, QStringLiteral("balance.wav"), 1000);
        openvegas::media::AudioClip clip {path, 0, 1.5, 0, 1, 1};
        clip.nativeEffects.append({id, {QStringLiteral("100")},
                                   QStringLiteral("balance-instance")});
        auto& module = clip.nativeEffects.last();
        module.sourceEffect.pluginId = id;
        module.sourceEffect.parameterValues = {QStringLiteral("100")};
        openvegas::composition::KeyFrameList balanceCurve(QStringLiteral("100"));
        balanceCurve.setCanInterpolate(false);
        balanceCurve.set(0, 100, openvegas::composition::TemporalType::Hold);
        balanceCurve.set(20, -100, openvegas::composition::TemporalType::Hold);
        module.sourceEffect.animation.insert(0, balanceCurve);
        openvegas::media::AudioPlayer audio;
        QSignalSpy pcm(&audio, &openvegas::media::AudioPlayer::pcmMixed);
        audio.setClips({clip}, 1.5, {}, sampleRate);
        audio.play(0);
        const auto panned = [&] {
            for (const auto& entry : pcm) {
                const QByteArray data = entry[0].toByteArray();
                if (data.size() < 404) continue;
                const int left = qFromLittleEndian<qint16>(data.constData() + 200);
                const int right = qFromLittleEndian<qint16>(data.constData() + 202);
                if (qAbs(left - right) >= 400 && qMax(qAbs(left), qAbs(right)) >= 400)
                    return true;
            }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(panned(), 5000);
        const auto oppositePanDirections = [&] {
            bool positive = false;
            bool negative = false;
            for (const auto& entry : pcm) {
                const QByteArray data = entry[0].toByteArray();
                if (data.size() < 404) continue;
                const int left = qFromLittleEndian<qint16>(data.constData() + 200);
                const int right = qFromLittleEndian<qint16>(data.constData() + 202);
                const int difference = left - right;
                if (difference > 400) positive = true;
                if (difference < -400) negative = true;
            }
            return positive && negative;
        };
        QTRY_VERIFY_WITH_TIMEOUT(oppositePanDirections(), 5000);
        audio.stop();
        openvegas::plugin::clearNativeEffectModules();
    }
    void nativeBalanceAnimatesOfflineBlocks()
    {
        const QString modulePath = qEnvironmentVariable("OPENVEGAS_HFPL_BALANCE");
        if (modulePath.isEmpty() || !QFileInfo::exists(modulePath)) {
            QSKIP("Set OPENVEGAS_HFPL_BALANCE to the reference Balance.hfpl for this integration test");
        }
        const QString dependencyDir = QFileInfo(modulePath).absoluteDir().absoluteFilePath(
            QStringLiteral("../../.."));
        const auto metadata = openvegas::plugin::loadNativePluginMetadata(
            modulePath, dependencyDir);
        QVERIFY(metadata.lifecycleCompatible);
        const openvegas::core::Identifier id(QStringLiteral("test.audio.balance.export"));
        openvegas::plugin::registerNativeAudioEffectModule(
            id, modulePath, dependencyDir, true, metadata.parameters);
        // One second of constant stereo PCM; the pan flips at 0.5 s, the same
        // Hold keyframe switch the export path samples per 10 ms block.
        QVector<qint16> samples(48000 * 2, qint16(1000));
        QVector<qint64> requestedOffsets;
        QVERIFY(openvegas::plugin::applyNativeAudioEffectBlocks(
            samples, 2, 48000, 96000, id,
            [&](qint64 frameOffset) {
                requestedOffsets.append(frameOffset);
                return QStringList {frameOffset < 24000 ? QStringLiteral("100")
                                                        : QStringLiteral("-100")};
            },
            QStringLiteral("export-test")));
        openvegas::plugin::releaseNativeEffectThreadRenderer();
        QCOMPARE(requestedOffsets.size(), 100);
        QCOMPARE(requestedOffsets.at(1), qint64(480));
        const int early = samples.at(4800 * 2) - samples.at(4800 * 2 + 1);
        const int late = samples.at(43200 * 2) - samples.at(43200 * 2 + 1);
        QVERIFY2(qAbs(early) >= 400 && qAbs(late) >= 400,
                 qPrintable(QStringLiteral("early=%1 late=%2").arg(early).arg(late)));
        QVERIFY2((early > 0) != (late > 0),
                 qPrintable(QStringLiteral("early=%1 late=%2").arg(early).arg(late)));
        openvegas::plugin::clearNativeEffectModules();
    }
    void nativeSourceRangesFeedHistoryAndLookAhead()
    {
        const QString balancePath = qEnvironmentVariable("OPENVEGAS_HFPL_BALANCE");
        if (balancePath.isEmpty() || !QFileInfo::exists(balancePath)) {
            QSKIP("Set OPENVEGAS_HFPL_BALANCE to the reference Balance.hfpl for this integration test");
        }
        const QDir moduleDir = QFileInfo(balancePath).absoluteDir();
        const QString dependencyDir = moduleDir.absoluteFilePath(QStringLiteral("../../.."));
        const auto registerModule = [&](const QString& name,
                                        const QHash<QString, QString>& overrides) {
            const QString path = moduleDir.absoluteFilePath(name + QStringLiteral(".hfpl"));
            const auto metadata = openvegas::plugin::loadNativePluginMetadata(path, dependencyDir);
            const openvegas::core::Identifier id(QStringLiteral("test.audio.") + name);
            openvegas::plugin::registerNativeAudioEffectModule(
                id, path, dependencyDir, true, metadata.parameters);
            QStringList values;
            for (const auto& parameter : metadata.parameters) {
                values.append(overrides.value(parameter.name, parameter.defaultValue));
            }
            return std::make_pair(id, values);
        };
        // GetLayerInfoV2 reports whole video frames, like the reference layer;
        // 12800 samples are exactly eight frames at 30 fps.
        constexpr int frames = 12800;
        const openvegas::plugin::NativeAudioLayer layer {frames, 30.0};
        const auto render = [&](const std::pair<openvegas::core::Identifier, QStringList>& module,
                                const QVector<qint16>& input) {
            QVector<qint16> output = input;
            const bool ok = openvegas::plugin::applyNativeAudioEffectBlocks(
                output, 2, 48000, 0, module.first,
                [&](qint64) { return module.second; }, {}, 480, layer);
            return ok ? output : QVector<qint16>();
        };

        // Reverse needs the end of the layer while rendering its start.
        QVector<qint16> ramp(frames * 2);
        for (int frame = 0; frame < frames; ++frame) {
            ramp[frame * 2] = qint16(frame - 6000);
            ramp[frame * 2 + 1] = qint16(6000 - frame);
        }
        const QVector<qint16> reversed = render(registerModule(QStringLiteral("AudioReverse"), {}), ramp);
        QCOMPARE(reversed.size(), ramp.size());
        for (int frame : {0, 1, 479, 480, 6000, frames - 1}) {
            QCOMPARE(reversed.at(frame * 2), ramp.at((frames - 1 - frame) * 2));
        }

        // Echo reads delay * echo history through GetAudioSamplesV2.
        QVector<qint16> impulse(frames * 2, qint16(0));
        impulse[1000 * 2] = impulse[1000 * 2 + 1] = 16000;
        const QVector<qint16> echoed = render(registerModule(
            QStringLiteral("AudioEcho"),
            {{QStringLiteral("delay"), QStringLiteral("100")},
             {QStringLiteral("numberOfEchoes"), QStringLiteral("1")},
             {QStringLiteral("falloff"), QStringLiteral("50")}}), impulse);
        QCOMPARE(echoed.size(), impulse.size());
        QVERIFY2(qAbs(echoed.at(1000 * 2) - 16000) <= 2, qPrintable(QString::number(echoed.at(2000))));
        QVERIFY2(qAbs(echoed.at(5800 * 2) - 8000) <= 2, qPrintable(QString::number(echoed.at(11600))));

        // Equaliser convolves the range it requested around each block; with
        // flat bands it must stay close to the input instead of silence.
        QVector<qint16> tone(frames * 2);
        for (int frame = 0; frame < frames; ++frame) {
            tone[frame * 2] = tone[frame * 2 + 1] = qint16(std::lround(8000 * std::sin(frame * 0.05)));
        }
        const QVector<qint16> equalised = render(registerModule(QStringLiteral("Equaliser"), {}), tone);
        QCOMPARE(equalised.size(), tone.size());
        int maxDifference = 0;
        for (int i = 0; i < tone.size(); ++i) {
            maxDifference = qMax(maxDifference, qAbs(int(equalised.at(i)) - int(tone.at(i))));
        }
        QVERIFY2(maxDifference <= 16, qPrintable(QString::number(maxDifference)));
        openvegas::plugin::releaseNativeEffectThreadRenderer();
        openvegas::plugin::clearNativeEffectModules();
    }
    void nativeEffectsSeeLookAheadWhilePlaying_data() { sampleRatesData(); }
    void nativeEffectsSeeLookAheadWhilePlaying()
    {
        QFETCH(int, sampleRate);
        const QString balancePath = qEnvironmentVariable("OPENVEGAS_HFPL_BALANCE");
        if (balancePath.isEmpty() || !QFileInfo::exists(balancePath)) {
            QSKIP("Set OPENVEGAS_HFPL_BALANCE to the reference Balance.hfpl for this integration test");
        }
        QTemporaryDir dir; QVERIFY(dir.isValid());
        // 0.2 s: 1000 then 3000, so AudioReverse needs the clip's end - the
        // future - while rendering its start. The look-ahead covers it.
        const QString path = dir.filePath("steps.wav");
        {
            QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
            QDataStream stream(&file); stream.setByteOrder(QDataStream::LittleEndian);
            constexpr int frames = 9600;
            stream.writeRawData("RIFF", 4); stream << quint32(36 + frames * 4);
            stream.writeRawData("WAVEfmt ", 8);
            stream << quint32(16) << quint16(1) << quint16(2) << quint32(48000)
                   << quint32(192000) << quint16(4) << quint16(16);
            stream.writeRawData("data", 4); stream << quint32(frames * 4);
            for (int i = 0; i < frames; ++i) {
                const qint16 value = i < frames / 2 ? 1000 : 3000;
                stream << value << value;
            }
        }
        const QDir moduleDir = QFileInfo(balancePath).absoluteDir();
        const QString reversePath = moduleDir.absoluteFilePath("AudioReverse.hfpl");
        const QString dependencyDir = moduleDir.absoluteFilePath("../../..");
        const auto metadata = openvegas::plugin::loadNativePluginMetadata(reversePath, dependencyDir);
        const openvegas::core::Identifier id(QStringLiteral("test.audio.reverse.realtime"));
        openvegas::plugin::registerNativeAudioEffectModule(id, reversePath, dependencyDir, true,
                                                           metadata.parameters);
        openvegas::media::AudioClip clip {path, 0, 0.2, 0, 1, 1};
        openvegas::media::NativeAudioModule module;
        module.pluginId = id; module.instanceKey = QStringLiteral("reverse-instance");
        module.sourceEffect.pluginId = id;
        module.layerStart = 0; module.layerDuration = 0.2; module.shotFps = 30;
        clip.nativeEffects.append(module);
        openvegas::media::AudioPlayer audio;
        QSignalSpy pcm(&audio, &openvegas::media::AudioPlayer::pcmMixed);
        audio.setClips({clip}, 0.3, {}, sampleRate);
        audio.play(0);
        const auto valueNear = [&](double seconds) {
            for (const auto& entry : pcm) {
                const QByteArray data = entry[0].toByteArray();
                if (data.size() >= 404 && qAbs(entry[1].toDouble() - seconds) < 0.011)
                    return int(qFromLittleEndian<qint16>(data.constData() + 200));
            }
            return INT_MIN;
        };
        QTRY_VERIFY_WITH_TIMEOUT(valueNear(0.16) != INT_MIN, 5000);
        audio.stop();
        QVERIFY2(qAbs(valueNear(0.04) - 3000) <= 4, qPrintable(QString::number(valueNear(0.04))));
        QVERIFY2(qAbs(valueNear(0.16) - 1000) <= 4, qPrintable(QString::number(valueNear(0.16))));
        openvegas::plugin::releaseNativeEffectThreadRenderer();
        openvegas::plugin::clearNativeEffectModules();
    }
    // A clip whose effects ask playback for dry samples it does not have
    // yet - AudioReverse over more than the look-ahead, an effect after
    // another - is reported once, by its key; one the look-ahead covers is
    // not.
    void nativeEffectsNeedingTheWholeClipAreReported()
    {
        const QString balancePath = qEnvironmentVariable("OPENVEGAS_HFPL_BALANCE");
        if (balancePath.isEmpty() || !QFileInfo::exists(balancePath)) {
            QSKIP("Set OPENVEGAS_HFPL_BALANCE to the reference Balance.hfpl for this integration test");
        }
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString path = constantWave(dir, QStringLiteral("long.wav"), 1000);
        const QDir moduleDir = QFileInfo(balancePath).absoluteDir();
        const QString dependencyDir = moduleDir.absoluteFilePath("../../..");
        const openvegas::core::Identifier reverse(QStringLiteral("test.audio.reverse.whole"));
        const openvegas::core::Identifier balance(QStringLiteral("test.audio.balance.whole"));
        for (const auto& [id, file] : QVector<QPair<openvegas::core::Identifier, QString>> {
                 {reverse, moduleDir.absoluteFilePath("AudioReverse.hfpl")}, {balance, balancePath}}) {
            const auto metadata = openvegas::plugin::loadNativePluginMetadata(file, dependencyDir);
            openvegas::plugin::registerNativeAudioEffectModule(id, file, dependencyDir, true, metadata.parameters);
        }
        const auto module = [](const openvegas::core::Identifier& id, const QString& key, double duration) {
            openvegas::media::NativeAudioModule m;
            m.pluginId = id; m.instanceKey = key; m.sourceEffect.pluginId = id;
            m.layerStart = 0; m.layerDuration = duration; m.shotFps = 30;
            return m;
        };
        const auto reported = [&](const QVector<openvegas::media::NativeAudioModule>& effects, double duration) {
            openvegas::media::AudioClip clip {path, 0, duration, 0, 1, 1};
            clip.nativeEffects = effects;
            clip.processKey = QStringLiteral("shot/layer/0");
            openvegas::media::AudioPlayer audio;
            QSignalSpy wanted(&audio, &openvegas::media::AudioPlayer::nativeEffectsNeedWholeClip);
            QSignalSpy pcm(&audio, &openvegas::media::AudioPlayer::pcmMixed);
            audio.setClips({clip}, duration);
            audio.play(0);
            if (!QTest::qWaitFor([&] { return pcm.size() > 3; }, 5000)) return QStringLiteral("no playback");
            QTest::qWait(300);
            audio.stop();
            QCoreApplication::processEvents();
            if (wanted.size() > 1) return QStringLiteral("reported %1 times").arg(wanted.size());
            return wanted.isEmpty() ? QString() : wanted.first().at(0).toString();
        };
        // Reverse over 1.5 s wants the clip's end at its start.
        QCOMPARE(reported({module(reverse, "r1", 1.5)}, 1.5), QStringLiteral("shot/layer/0"));
        // Over 0.2 s the look-ahead has it all.
        QCOMPARE(reported({module(reverse, "r2", 0.2)}, 0.2), QString());
        // After Balance the Reverse sees no future at all.
        QCOMPARE(reported({module(balance, "b3", 0.2), module(reverse, "r3", 0.2)}, 0.2),
                 QStringLiteral("shot/layer/0"));
        openvegas::plugin::releaseNativeEffectThreadRenderer();
        openvegas::plugin::clearNativeEffectModules();
    }
    void transitionsCombineClipsInRealtime()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString a = constantWave(dir, QStringLiteral("a.wav"), 1000);
        const QString b = constantWave(dir, QStringLiteral("b.wav"), 3000);
        // Cut at 1 s, 0.5 s window: both clips extend into their handles.
        const QVector<openvegas::media::AudioClip> clips {{a, 0, 1.25, 0, 1, 1},
                                                          {b, 0.75, 2, 0, 1, 1}};
        const auto valueNear = [](const QSignalSpy& spy, double seconds) {
            for (const auto& entry : spy) {
                const QByteArray data = entry[0].toByteArray();
                if (data.size() >= 404 && qAbs(entry[1].toDouble() - seconds) < 0.011)
                    return int(qFromLittleEndian<qint16>(data.constData() + 200));
            }
            return INT_MIN;
        };
        const auto play = [&](const QVector<openvegas::media::AudioTransition>& transitions,
                              QSignalSpy& pcm, openvegas::media::AudioPlayer& audio) {
            audio.setClips(clips, 1.6, transitions);
            audio.play(0.6);
            QTRY_VERIFY_WITH_TIMEOUT(valueNear(pcm, 1.4) != INT_MIN, 5000);
            audio.stop();
        };
        openvegas::media::AudioTransition transition;
        transition.fromClip = 0; transition.toClip = 1;
        transition.start = 0.75; transition.end = 1.25; transition.cut = 1.0;

        // A module this build cannot run cross-fades instead of summing.
        transition.pluginId = openvegas::core::Identifier(QStringLiteral("test.audio.missing"));
        {
            openvegas::media::AudioPlayer audio;
            QSignalSpy pcm(&audio, &openvegas::media::AudioPlayer::pcmMixed);
            play({transition}, pcm, audio);
            QVERIFY2(qAbs(valueNear(pcm, 0.7) - 1000) <= 4, qPrintable(QString::number(valueNear(pcm, 0.7))));
            QVERIFY2(qAbs(valueNear(pcm, 1.0) - 2000) <= 60, qPrintable(QString::number(valueNear(pcm, 1.0))));
            QVERIFY2(qAbs(valueNear(pcm, 1.4) - 3000) <= 4, qPrintable(QString::number(valueNear(pcm, 1.4))));
        }

        const QString balancePath = qEnvironmentVariable("OPENVEGAS_HFPL_BALANCE");
        if (balancePath.isEmpty() || !QFileInfo::exists(balancePath)) {
            QSKIP("Set OPENVEGAS_HFPL_BALANCE to test the native Fade transition");
        }
        // Native Fade dips to silence at the edit point: A fades out before
        // it and B fades in after it, with no sum of the two.
        const QDir pluginDir = QFileInfo(balancePath).absoluteDir();
        const QString fadePath = pluginDir.absoluteFilePath(QStringLiteral("../AudioTransitions/Fade.hfpl"));
        const QString dependencyDir = pluginDir.absoluteFilePath(QStringLiteral("../../.."));
        const auto metadata = openvegas::plugin::loadNativePluginMetadata(fadePath, dependencyDir);
        QVERIFY(metadata.lifecycleCompatible);
        transition.pluginId = openvegas::core::Identifier(QStringLiteral("test.audio.fade"));
        openvegas::plugin::registerNativeAudioTransitionModule(
            transition.pluginId, fadePath, dependencyDir, true, metadata.parameters);
        {
            openvegas::media::AudioPlayer audio;
            QSignalSpy pcm(&audio, &openvegas::media::AudioPlayer::pcmMixed);
            play({transition}, pcm, audio);
            const int beforeCut = valueNear(pcm, 0.86);   // 0.11/0.25 into A's fade
            const int atCut = valueNear(pcm, 0.98);
            const int afterCut = valueNear(pcm, 1.14);    // 0.14/0.25 into B's rise
            QVERIFY2(qAbs(beforeCut - 1000 * (1.0 - (0.86 - 0.75 + 0.001) / 0.25)) <= 40,
                     qPrintable(QString::number(beforeCut)));
            QVERIFY2(qAbs(atCut) <= 120, qPrintable(QString::number(atCut)));
            QVERIFY2(qAbs(afterCut - 3000 * (1.14 - 1.0 + 0.001) / 0.25) <= 120,
                     qPrintable(QString::number(afterCut)));
        }
        openvegas::plugin::releaseNativeEffectThreadRenderer();
        openvegas::plugin::clearNativeEffectModules();
    }
    void masterMixGainMuteAndQueuedStop() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const auto a = constantWave(dir, "one.wav", 1000);
        const auto b = constantWave(dir, "two.wav", 2000);
        openvegas::media::AudioPlayer audio;
        QSignalSpy pcm(&audio, &openvegas::media::AudioPlayer::pcmMixed);
        audio.setClips({{a, 0, 2, 0, 1, 1}, {b, 0, 2, 0, 1, 0.5}}, 3);
        audio.play(0);
        QTRY_VERIFY_WITH_TIMEOUT(containsSample(pcm, 2000), 5000);
        pcm.clear(); audio.setMuted(true);
        QTRY_VERIFY_WITH_TIMEOUT(containsSample(pcm, 0), 3000);
        pcm.clear(); audio.setMuted(false);
        QTRY_VERIFY_WITH_TIMEOUT(containsSample(pcm, 2000), 3000);
        audio.stop(); const auto count = pcm.size();
        QTest::qWait(100); QCOMPARE(pcm.size(), count);
    }
    void oppositeSourcesCancelAndFutureClipStarts() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const auto a = constantWave(dir, "positive.wav", 1000);
        const auto b = constantWave(dir, "negative.wav", -1000);
        openvegas::media::AudioPlayer audio;
        QSignalSpy pcm(&audio, &openvegas::media::AudioPlayer::pcmMixed);
        // The first interval cancels; after it only the positive clip remains.
        audio.setClips({{a, 0, 2, 0, 1, 1}, {b, 0, 0.3, 0, 1, 1}}, 2);
        audio.play(0);
        QTRY_VERIFY_WITH_TIMEOUT(containsSample(pcm, 0), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(containsSample(pcm, 1000), 3000);
        audio.stop(); pcm.clear();
        audio.setClips({{a, 0.3, 1.5, 0.2, 2, 0.5}}, 2);
        audio.play(0);
        QTRY_VERIFY_WITH_TIMEOUT(containsSample(pcm, 0), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(containsSample(pcm, 500), 3000);
        audio.seek(0.5); pcm.clear();
        QTRY_VERIFY_WITH_TIMEOUT(containsSample(pcm, 500), 3000);
        QVERIFY(pcm.last()[1].toDouble() >= 0.5);
        audio.stop();
    }
    void sourceOffsetAndSpeedAffectPcm() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const auto path = constantWave(dir, "steps.wav", -32768);
        openvegas::media::AudioPlayer audio;
        QSignalSpy pcm(&audio, &openvegas::media::AudioPlayer::pcmMixed);
        audio.setClips({{path, 0, 0.6, 0.25, 2, 1}}, 0.8);
        auto at = [&](double first, double last, int value) {
            for (const auto& entry : pcm) {
                const auto bytes = entry[0].toByteArray();
                const double time = entry[1].toDouble();
                if (time >= first && time <= last && bytes.size() >= 400
                    && std::abs(int(qFromLittleEndian<qint16>(bytes.constData() + 200)) - value) <= 3) return true;
            }
            return false;
        };
        audio.play(0);
        QTRY_VERIFY_WITH_TIMEOUT(at(0.02, 0.1, 1000), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(at(0.15, 0.22, 2000), 3000);
        audio.seek(0.18); pcm.clear();
        QTRY_VERIFY_WITH_TIMEOUT(at(0.19, 0.22, 2000), 3000);
        audio.stop();
    }
    void decodedAudioReachesOutput()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("tone.wav");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QDataStream stream(&file);
        stream.setByteOrder(QDataStream::LittleEndian);
        constexpr int frames = 48000 * 3;
        stream.writeRawData("RIFF", 4);
        stream << quint32(36 + frames * 4);
        stream.writeRawData("WAVEfmt ", 8);
        stream << quint32(16) << quint16(1) << quint16(2) << quint32(48000)
               << quint32(192000) << quint16(4) << quint16(16);
        stream.writeRawData("data", 4);
        stream << quint32(frames * 4);
        for (int i = 0; i < frames; ++i) {
            const qint16 sample = qint16(1000 * std::sin(i * 440.0 * 6.283185307 / 48000));
            stream << sample << sample;
        }
        file.close();

        openvegas::media::AudioPlayer audio;
        QSignalSpy levels(&audio, &openvegas::media::AudioPlayer::levelsChanged);
        audio.setSource(path);
        audio.play(0.5);
        QVERIFY(openvegas::media::AudioPlayer::isAvailable());
        QTRY_VERIFY_WITH_TIMEOUT(audio.isPlaying(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(audio.position() >= 0.5, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!levels.isEmpty() && levels.last().at(0).toDouble() > 0, 10000);
        audio.pause();
        QVERIFY(!audio.isPlaying());
        QCOMPARE(levels.last().at(0).toDouble(), 0.0);
        audio.stop();
        QVERIFY(!audio.isPlaying());
    }
};

QTEST_GUILESS_MAIN(AudioRegression)
#include "audio_regression.moc"
