#include "media/AudioPlayer.h"
#include <QDataStream>
#include <QFile>
#include <QSignalSpy>
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
private slots:
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
