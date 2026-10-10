#include "render/VideoEncoder.h"

#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QProcess>

#include <cmath>

namespace openvegas {
namespace render {

namespace {

const QString kSoftwareEncoder = QStringLiteral("libx264");

bool encodesHere(const QString& ffmpeg, const QString& encoder)
{
    // A real encode of a few frames: listing alone is not enough, NVENC and
    // friends are compiled in whether or not the GPU and driver exist.
    QProcess process;
    QStringList arguments {QStringLiteral("-hide_banner"), QStringLiteral("-v"),
                           QStringLiteral("error"), QStringLiteral("-f"),
                           QStringLiteral("lavfi"), QStringLiteral("-i"),
                           QStringLiteral("color=c=gray:s=256x144:r=30:d=0.2")};
    arguments << h264EncoderArguments(encoder) << QStringLiteral("-f") << QStringLiteral("null")
              << QStringLiteral("-");
    process.start(ffmpeg, arguments);
    if (!process.waitForFinished(10000)) {
        process.kill();
        process.waitForFinished(1000);
        return false;
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

} // namespace

QString sampleAspectRatio(double pixelAspect)
{
    if (!(pixelAspect > 0.0)) return QStringLiteral("1/1");
    for (int denominator = 1; denominator <= 10000; ++denominator) {
        const long long numerator = std::llround(pixelAspect * denominator);
        if (numerator > 0 && std::abs(double(numerator) / denominator - pixelAspect) < 1e-6)
            return QStringLiteral("%1/%2").arg(numerator).arg(denominator);
    }
    return QStringLiteral("%1/10000").arg(std::llround(pixelAspect * 10000));
}

QStringList h264EncoderArguments(const QString& encoder)
{
    if (encoder == QLatin1String("h264_nvenc")) {
        return {QStringLiteral("-c:v"), encoder, QStringLiteral("-preset"), QStringLiteral("p5"),
                QStringLiteral("-rc"), QStringLiteral("vbr"), QStringLiteral("-cq"),
                QStringLiteral("19"), QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p")};
    }
    if (encoder == QLatin1String("h264_qsv")) {
        return {QStringLiteral("-c:v"), encoder, QStringLiteral("-global_quality"),
                QStringLiteral("20"), QStringLiteral("-pix_fmt"), QStringLiteral("nv12")};
    }
    if (encoder == QLatin1String("h264_amf")) {
        return {QStringLiteral("-c:v"), encoder, QStringLiteral("-quality"),
                QStringLiteral("quality"), QStringLiteral("-rc"), QStringLiteral("cqp"),
                QStringLiteral("-qp_i"), QStringLiteral("19"), QStringLiteral("-qp_p"),
                QStringLiteral("21"), QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p")};
    }
    if (encoder == QLatin1String("h264_mf")) {
        return {QStringLiteral("-c:v"), encoder, QStringLiteral("-rate_control"),
                QStringLiteral("quality"), QStringLiteral("-quality"), QStringLiteral("80"),
                QStringLiteral("-pix_fmt"), QStringLiteral("nv12")};
    }
    return {QStringLiteral("-c:v"), kSoftwareEncoder, QStringLiteral("-pix_fmt"),
            QStringLiteral("yuv420p")};
}

QStringList h264EncoderArguments(const QString& encoder, const H264Rate& rate)
{
    static const char* const kProfiles[] = {"baseline", "main", "high"};
    const QString profile = QString::fromLatin1(kProfiles[qBound(0, rate.profile, 2)]);
    const QString bitRate = QString::number(qMax(100000, rate.bitRate));
    const QString maxBitRate = QString::number(qMax(rate.bitRate, rate.maxBitRate));
    const QString buffer = QString::number(2LL * qMax(rate.bitRate, rate.maxBitRate));
    QStringList args {QStringLiteral("-c:v"), encoder.isEmpty() ? kSoftwareEncoder : encoder};
    if (encoder == QLatin1String("h264_nvenc")) {
        args << QStringLiteral("-preset") << QStringLiteral("p5") << QStringLiteral("-profile:v") << profile
             << QStringLiteral("-rc") << (rate.constant ? QStringLiteral("cbr") : QStringLiteral("vbr"))
             << QStringLiteral("-b:v") << bitRate << QStringLiteral("-maxrate")
             << (rate.constant ? bitRate : maxBitRate) << QStringLiteral("-bufsize") << buffer
             << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p");
    } else if (encoder == QLatin1String("h264_qsv")) {
        args << QStringLiteral("-profile:v") << profile << QStringLiteral("-b:v") << bitRate
             << QStringLiteral("-maxrate") << (rate.constant ? bitRate : maxBitRate)
             << QStringLiteral("-pix_fmt") << QStringLiteral("nv12");
    } else if (encoder == QLatin1String("h264_amf")) {
        args << QStringLiteral("-profile:v") << profile << QStringLiteral("-rc")
             << (rate.constant ? QStringLiteral("cbr") : QStringLiteral("vbr_peak"))
             << QStringLiteral("-b:v") << bitRate << QStringLiteral("-maxrate")
             << (rate.constant ? bitRate : maxBitRate) << QStringLiteral("-pix_fmt")
             << QStringLiteral("yuv420p");
    } else if (encoder == QLatin1String("h264_mf")) {
        args << QStringLiteral("-rate_control") << (rate.constant ? QStringLiteral("cbr") : QStringLiteral("pc_vbr"))
             << QStringLiteral("-b:v") << bitRate << QStringLiteral("-pix_fmt") << QStringLiteral("nv12");
    } else {
        // libx264: a constant rate is held with HRD signalling; a variable
        // one aims at the bit rate under the ceiling.
        args << QStringLiteral("-profile:v") << profile << QStringLiteral("-b:v") << bitRate
             << QStringLiteral("-maxrate") << (rate.constant ? bitRate : maxBitRate)
             << QStringLiteral("-bufsize") << buffer;
        if (rate.constant) {
            args << QStringLiteral("-minrate") << bitRate << QStringLiteral("-x264-params")
                 << QStringLiteral("nal-hrd=cbr");
        }
        args << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p");
    }
    if (rate.keyFrameInterval > 0) args << QStringLiteral("-g") << QString::number(rate.keyFrameInterval);
    return args;
}

bool h264EncoderWorks(const QString& ffmpeg, const QString& encoder)
{
    if (ffmpeg.isEmpty() || encoder.isEmpty()) return false;
    static QMutex mutex;
    static QHash<QString, bool> known; // by FFmpeg path and encoder
    const QString key = ffmpeg + QLatin1Char('|') + encoder;
    QMutexLocker lock(&mutex);
    const auto it = known.constFind(key);
    if (it != known.constEnd()) return it.value();
    const bool works = encodesHere(ffmpeg, encoder);
    known.insert(key, works);
    return works;
}

QString chooseH264Encoder(const QString& ffmpeg, bool hardware)
{
    if (!hardware || ffmpeg.isEmpty()) return kSoftwareEncoder;
    static QMutex mutex;
    static QHash<QString, QString> chosen; // by FFmpeg path
    QMutexLocker lock(&mutex);
    const auto known = chosen.constFind(ffmpeg);
    if (known != chosen.constEnd()) return known.value();
    QString encoder = kSoftwareEncoder;
    for (const QString& candidate : {QStringLiteral("h264_nvenc"), QStringLiteral("h264_qsv"),
                                     QStringLiteral("h264_amf"), QStringLiteral("h264_mf")}) {
        if (encodesHere(ffmpeg, candidate)) {
            encoder = candidate;
            break;
        }
    }
    chosen.insert(ffmpeg, encoder);
    return encoder;
}

} // namespace render
} // namespace openvegas
