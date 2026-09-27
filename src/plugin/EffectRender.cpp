#include "plugin/EffectRender.h"
#include "plugin/NativeEffectRender.h"

#include <QColor>
#include <QImage>
#include <cmath>

namespace openvegas {
namespace plugin {

namespace {

void blurAxis(QImage& image, double radius, bool horizontal, bool clamp)
{
    const int length = horizontal ? image.width() : image.height();
    const int lines = horizontal ? image.height() : image.width();
    const int r = int(std::floor(radius));
    const double fraction = radius - r;
    QImage out(image.size(), image.format());
    QVector<double> prefix((length + 1) * 4);
    for (int line = 0; line < lines; ++line) {
        std::fill(prefix.begin(), prefix.begin() + 4, 0.);
        for (int x = 0; x < length; ++x) {
            const uchar* pixel = image.constScanLine(horizontal ? line : x) + (horizontal ? x : line) * 4;
            for (int ch = 0; ch < 4; ++ch) {
                const double value = ch == 3 ? pixel[ch] : pixel[ch] * pixel[3] / 255.;
                prefix[(x + 1) * 4 + ch] = prefix[x * 4 + ch] + value;
            }
        }
        for (int x = 0; x < length; ++x) {
            double channels[4];
            for (int ch = 0; ch < 4; ++ch) {
                auto average = [&](int extent) {
                    const int left = x - extent, right = x + extent + 1;
                    double sum = prefix[qMin(length, right) * 4 + ch] - prefix[qMax(0, left) * 4 + ch];
                    if (clamp) {
                        if (left < 0) sum += -left * prefix[4 + ch];
                        if (right > length) sum += (right - length) * (prefix[length * 4 + ch] - prefix[(length - 1) * 4 + ch]);
                    }
                    return sum / (extent * 2 + 1);
                };
                channels[ch] = average(r) * (1 - fraction) + average(r + 1) * fraction;
            }
            uchar* pixel = out.scanLine(horizontal ? line : x) + (horizontal ? x : line) * 4;
            for (int ch = 0; ch < 3; ++ch) pixel[ch] = uchar(qBound(0, qRound(channels[3] > 0 ? channels[ch] * 255. / channels[3] : 0.), 255));
            pixel[3] = uchar(qBound(0, qRound(channels[3]), 255));
        }
    }
    image = out;
}

void applyColorWheels(QImage& image, const QStringList& p)
{
    const double saturation = p.value(0, "1").toDouble();
    const double exposure = std::pow(2., p.value(1, "0").toDouble());
    const QColor white(p.value(2, "#ffffff"));
    for (int y = 0; y < image.height(); ++y) {
        uchar* pixel = image.scanLine(y);
        for (int x = 0; x < image.width(); ++x, pixel += 4) {
            double r = pixel[0] / 255., g = pixel[1] / 255., b = pixel[2] / 255.;
            const double luminance = .2126 * r + .7152 * g + .0722 * b;
            r = (luminance + (r - luminance) * saturation) * exposure * white.redF();
            g = (luminance + (g - luminance) * saturation) * exposure * white.greenF();
            b = (luminance + (b - luminance) * saturation) * exposure * white.blueF();
            for (int band = 0; band < 3; ++band) {
                const int i = 3 + band * 3;
                const double weight = band == 0 ? luminance * luminance : band == 1 ? 4 * luminance * (1 - luminance) : (1 - luminance) * (1 - luminance);
                const double strength = p.value(i, "0").toDouble() * weight;
                const QColor tint = QColor::fromHsvF(std::fmod(p.value(i + 1, "0").toDouble() / 360. + 100., 1.),
                    qBound(0., p.value(i + 2, "0").toDouble(), 1.), 1.);
                r += strength * (tint.redF() - .5); g += strength * (tint.greenF() - .5); b += strength * (tint.blueF() - .5);
            }
            pixel[0] = uchar(qBound(0, qRound(r * 255), 255)); pixel[1] = uchar(qBound(0, qRound(g * 255), 255)); pixel[2] = uchar(qBound(0, qRound(b * 255), 255));
        }
    }
}

void parseTintColor(const QStringList& parameters, QColor& tint)
{
    tint = QColor(0, 255, 0);
    if (parameters.size() >= 1) {
        const QColor c(parameters.at(0));
        if (c.isValid()) {
            tint = c;
        }
    }
}

double parseBrightness(const QStringList& parameters)
{
    double level = 100.0;
    if (parameters.size() >= 1) {
        bool ok = false;
        const double v = parameters.at(0).toDouble(&ok);
        if (ok) {
            level = v;
        }
    }
    return qBound(0.0, level, 200.0) / 100.0;
}

void applyGrayscale(QImage& image)
{
    uchar* bits = image.bits();
    const int bytesPerLine = image.bytesPerLine();
    for (int y = 0; y < image.height(); ++y) {
        uchar* p = bits + y * bytesPerLine;
        for (int x = 0; x < image.width(); ++x, p += 4) {
            const int gray = (p[0] * 30 + p[1] * 59 + p[2] * 11) / 100;
            p[0] = static_cast<uchar>(gray);
            p[1] = static_cast<uchar>(gray);
            p[2] = static_cast<uchar>(gray);
        }
    }
}

void applySepia(QImage& image)
{
    uchar* bits = image.bits();
    const int bytesPerLine = image.bytesPerLine();
    for (int y = 0; y < image.height(); ++y) {
        uchar* p = bits + y * bytesPerLine;
        for (int x = 0; x < image.width(); ++x, p += 4) {
            const int r = p[0];
            const int g = p[1];
            const int b = p[2];
            p[0] = static_cast<uchar>(qBound(0, (r * 393 + g * 769 + b * 189) / 1000, 255));
            p[1] = static_cast<uchar>(qBound(0, (r * 349 + g * 686 + b * 168) / 1000, 255));
            p[2] = static_cast<uchar>(qBound(0, (r * 272 + g * 534 + b * 131) / 1000, 255));
        }
    }
}

void applyInvert(QImage& image)
{
    uchar* bits = image.bits();
    const int bytesPerLine = image.bytesPerLine();
    for (int y = 0; y < image.height(); ++y) {
        uchar* p = bits + y * bytesPerLine;
        for (int x = 0; x < image.width(); ++x, p += 4) {
            p[0] = static_cast<uchar>(255 - p[0]);
            p[1] = static_cast<uchar>(255 - p[1]);
            p[2] = static_cast<uchar>(255 - p[2]);
        }
    }
}

void applyTint(QImage& image, const QColor& tint)
{
    uchar* bits = image.bits();
    const int bytesPerLine = image.bytesPerLine();
    for (int y = 0; y < image.height(); ++y) {
        uchar* p = bits + y * bytesPerLine;
        for (int x = 0; x < image.width(); ++x, p += 4) {
            const int gray = (p[0] * 30 + p[1] * 59 + p[2] * 11) / 100;
            p[0] = static_cast<uchar>(tint.red() * gray / 255);
            p[1] = static_cast<uchar>(tint.green() * gray / 255);
            p[2] = static_cast<uchar>(tint.blue() * gray / 255);
        }
    }
}

void applyBrightness(QImage& image, double factor)
{
    uchar* bits = image.bits();
    const int bytesPerLine = image.bytesPerLine();
    for (int y = 0; y < image.height(); ++y) {
        uchar* p = bits + y * bytesPerLine;
        for (int x = 0; x < image.width(); ++x, p += 4) {
            p[0] = static_cast<uchar>(qBound(0, static_cast<int>(p[0] * factor), 255));
            p[1] = static_cast<uchar>(qBound(0, static_cast<int>(p[1] * factor), 255));
            p[2] = static_cast<uchar>(qBound(0, static_cast<int>(p[2] * factor), 255));
        }
    }
}

} // namespace

bool applyEffectToImage(QImage& image, const core::Identifier& pluginId, const QStringList& parameters)
{
    if (image.isNull() || image.format() != QImage::Format_RGBA8888) {
        return false;
    }
    if (pluginId.value() == QLatin1String(kBuiltinBlur)) {
        const double radius = qBound(0., parameters.value(0).toDouble(), 400.);
        const int iterations = qBound(1, parameters.value(1, "2").toInt(), 10);
        const QString dimension = parameters.value(2, "Horizontal & Vertical");
        const bool clamp = parameters.value(3, "true") == "true" || parameters.value(3) == "1";
        if (radius > 0) for (int i = 0; i < iterations; ++i) {
            if (dimension != "Vertical") blurAxis(image, radius, true, clamp);
            if (dimension != "Horizontal") blurAxis(image, radius, false, clamp);
        }
        return true;
    }
    if (pluginId.value() == QLatin1String(kBuiltinColorWheels)) { applyColorWheels(image, parameters); return true; }

    if (pluginId.value() == QLatin1String(kBuiltinGrayscale)) {
        applyGrayscale(image);
        return true;
    }
    if (pluginId.value() == QLatin1String(kBuiltinSepia)) {
        applySepia(image);
        return true;
    }
    if (pluginId.value() == QLatin1String(kBuiltinInvert)) {
        applyInvert(image);
        return true;
    }
    if (pluginId.value() == QLatin1String(kBuiltinTint)) {
        QColor tint;
        parseTintColor(parameters, tint);
        applyTint(image, tint);
        return true;
    }
    if (pluginId.value() == QLatin1String(kBuiltinBrightness)) {
        applyBrightness(image, parseBrightness(parameters));
        return true;
    }
    return applyNativeEffectToImage(image, pluginId, parameters);
}

QVector<EffectSpec> timelineBuiltinSpecs()
{
    const auto parameter = [](QString name, QString label, QString type, QString value, double min, double max, QString unit = QString()) {
        EffectParameterSpec p; p.name = name; p.displayName = label; p.type = type; p.defaultValue = value;
        p.minimum = min; p.maximum = max; p.unit = unit; return p;
    };
    EffectSpec blur; blur.id = PluginId(kBuiltinBlur); blur.name = "blur"; blur.displayName = "Blur"; blur.category = "Blur & Sharpen";
    blur.description = "Separable box blur with fractional radius, repeated passes, direction and edge clamping.";
    blur.parameters = {parameter("radius", "Radius", "double", "0", 0, 400, "px"),
        parameter("iterations", "Iterations", "int", "2", 1, 10),
        parameter("dimension", "Dimension", "enum", "Horizontal & Vertical", 0, 0),
        parameter("clamp", "Clamp to Edge", "bool", "true", 0, 1)};
    blur.parameters[2].choices = {"Horizontal & Vertical", "Horizontal", "Vertical"};
    EffectSpec color; color.id = PluginId(kBuiltinColorWheels); color.name = "color-wheels";
    color.displayName = "Color Correction Wheels"; color.category = "Color";
    color.description = "Master saturation, exposure and RGB white balance, with luminance-weighted tonal corrections.";
    color.parameters = {parameter("saturation", "Saturation", "double", "1", 0, 5),
        parameter("exposure", "Exposure", "double", "0", -10, 10), parameter("whiteBalance", "White Balance", "color", "#ffffff", 0, 255)};
    for (auto& p : color.parameters) p.group = "Master Controls";
    for (const QString& band : {QString("Highlights"), QString("Midtones"), QString("Shadows")}) {
        auto strength = parameter(band + "Strength", "Strength", "double", "0", -2, 2); strength.group = band;
        auto hue = parameter(band + "Hue", "Hue", "double", "0", -360, 360); hue.group = band;
        auto saturation = parameter(band + "Saturation", "Saturation", "double", "0", 0, 1); saturation.group = band;
        color.parameters.append(strength); color.parameters.append(hue); color.parameters.append(saturation);
    }
    return {blur, color};
}

} // namespace plugin
} // namespace openvegas
