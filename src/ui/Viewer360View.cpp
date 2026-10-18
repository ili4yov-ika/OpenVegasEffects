#include "ui/Viewer360View.h"

#include "plugin/EffectRender.h"
#include "plugin/NativeEffectRender.h"

#include <QColor>
#include <QSettings>
#include <QtGlobal>

#include <cmath>

namespace openvegas::ui {

namespace {

constexpr double kPi = 3.14159265358979323846;
double radians(double degrees) { return degrees * kPi / 180.0; }

int wrapped(int value, int limit)
{
    if (limit <= 0) return 0;
    value %= limit;
    return value < 0 ? value + limit : value;
}

int sampleCoordinate(int value, int limit, Viewer360View::WrapMode mode)
{
    if (mode == Viewer360View::WrapMode::Tile) return wrapped(value, limit);
    if (mode == Viewer360View::WrapMode::Reflect) {
        const int index = wrapped(value, limit * 2);
        return index < limit ? index : limit * 2 - 1 - index;
    }
    return qBound(0, value, limit - 1);
}

double wrappedCoordinate(double value, double limit, Viewer360View::WrapMode mode)
{
    if (limit <= 0.0) return 0.0;
    if (mode == Viewer360View::WrapMode::Tile) {
        value = std::fmod(value, limit);
        return value < 0.0 ? value + limit : value;
    }
    if (mode == Viewer360View::WrapMode::Reflect) {
        value = std::fmod(value, limit * 2.0);
        if (value < 0.0) value += limit * 2.0;
        return value < limit ? value : limit * 2.0 - value;
    }
    return qBound(0.0, value, limit);
}

// Camera space -> world, as EnvironmentMapViewer turns its rays (measured on
// the module with a panorama whose pixels carry their own longitude and
// latitude, to well under a degree): pitch about X, then yaw about Y, then
// roll about the world's Z - world = Rz(roll) Ry(-yaw) Rx(-pitch) camera.
// Yaw never changes the latitude of a direction.
struct Rotation
{
    double cy, sy, cp, sp, cr, sr;
    Rotation(double yaw, double pitch, double roll)
        : cy(std::cos(radians(-yaw))), sy(std::sin(radians(-yaw)))
        , cp(std::cos(radians(pitch))), sp(std::sin(radians(pitch)))
        , cr(std::cos(radians(roll))), sr(std::sin(radians(roll))) {}

    void toWorld(const double camera[3], double world[3]) const
    {
        const double pitchY = cp * camera[1] + sp * camera[2];
        const double pitchZ = -sp * camera[1] + cp * camera[2];
        const double yawX = cy * camera[0] + sy * pitchZ;
        world[2] = -sy * camera[0] + cy * pitchZ;
        world[0] = cr * yawX - sr * pitchY;
        world[1] = sr * yawX + cr * pitchY;
    }

    void toCamera(const double world[3], double camera[3]) const
    {
        const double yawX = cr * world[0] + sr * world[1];
        const double pitchY = -sr * world[0] + cr * world[1];
        camera[0] = cy * yawX - sy * world[2];
        const double pitchZ = sy * yawX + cy * world[2];
        camera[1] = cp * pitchY - sp * pitchZ;
        camera[2] = sp * pitchY + cp * pitchZ;
    }
};

// The lens: a viewport point in half-heights from the centre (x right, y up)
// <-> a unit ray in camera space (z ahead). Both of the module's lenses
// measure the field of view across the height.
struct Lens
{
    bool fishEye;
    double half;    // half the field of view, radians
    double tanHalf;
    Lens(bool fish, double fieldOfView)
        : fishEye(fish), half(radians(fieldOfView) * 0.5), tanHalf(std::tan(half)) {}

    void ray(double x, double y, double out[3]) const
    {
        if (fishEye) {
            const double r = std::hypot(x, y);
            const double angle = r * half;
            const double s = r > 1e-12 ? std::sin(angle) / r : half;
            out[0] = x * s; out[1] = y * s; out[2] = std::cos(angle);
            return;
        }
        const double cx = x * tanHalf, cy = y * tanHalf;
        const double length = std::sqrt(cx * cx + cy * cy + 1.0);
        out[0] = cx / length; out[1] = cy / length; out[2] = 1.0 / length;
    }

    // False when the lens doesn't see the direction.
    bool point(const double camera[3], double* x, double* y) const
    {
        if (fishEye) {
            const double angle = std::acos(qBound(-1.0, camera[2], 1.0));
            if (angle >= kPi - 1e-9) return false;
            const double s = std::hypot(camera[0], camera[1]);
            const double r = angle / half;
            *x = s > 1e-12 ? camera[0] / s * r : 0.0;
            *y = s > 1e-12 ? camera[1] / s * r : 0.0;
            return true;
        }
        if (camera[2] <= 1e-9) return false;
        *x = camera[0] / camera[2] / tanHalf;
        *y = camera[1] / camera[2] / tanHalf;
        return true;
    }
};

// A world direction <-> the panorama's texture coordinates (0..1 across the
// frame, v down), with the module's envTexScale and envScaleRatio: v = 0.5 -
// latitude/pi * 100/scale, u = 0.5 + longitude/2pi * 100/scale * 2/ratio.
struct Texture
{
    double u, v;
    Texture(double scale, double ratio)
        : u(100.0 / qBound(1.0, scale, 200.0) * 2.0 / qBound(0.1, ratio, 10.0))
        , v(100.0 / qBound(1.0, scale, 200.0)) {}

    void coordinates(const double world[3], double* x, double* y) const
    {
        double longitude = std::atan2(world[0], world[2]);
        // +/-180 degrees represent one direction; keep the seam stable.
        if (qAbs(longitude + kPi) < 1e-12) longitude = kPi;
        const double latitude = std::asin(qBound(-1.0, world[1], 1.0));
        *x = 0.5 + longitude / (2.0 * kPi) * u;
        *y = 0.5 - latitude / kPi * v;
    }

    // False for a point of the frame no direction shows.
    bool direction(double x, double y, double world[3]) const
    {
        const double longitude = (x - 0.5) * 2.0 * kPi / u;
        const double latitude = (0.5 - y) * kPi / v;
        if (qAbs(longitude) > kPi + 1e-9 || qAbs(latitude) > kPi * 0.5 + 1e-9) return false;
        world[0] = std::cos(latitude) * std::sin(longitude);
        world[1] = std::sin(latitude);
        world[2] = std::cos(latitude) * std::cos(longitude);
        return true;
    }
};

QString number(double value) { return QString::number(value, 'g', 10); }

// The layer EnvironmentMapViewer is pointed at: any ID but its own (the null
// one), which the host answers with NativeFrameTime::layerImage. A module
// whose environment_map names no other layer maps its own source instead.
constexpr char kPanoramaLayer[] = "5f6c0e1d-360a-4e6b-9d1c-0e7a2b360360";

} // namespace

Viewer360View::Viewer360View(QObject* parent) : QObject(parent) {}

double Viewer360View::effectiveFieldOfView() const
{
    return qBound(1.0, m_useCameraFOV ? m_cameraFov : m_fov, 179.0);
}

void Viewer360View::invalidate()
{
    m_projectionValid = false;
    emit changed();
}

void Viewer360View::setCurrentView(View view)
{
    if (int(view) < 0 || int(view) > 6) return;
    m_view = view;
    if (view != View::Custom) m_roll = 0.0;
    switch (view) {
    case View::Front:  m_yaw = 0.0;   m_pitch = 0.0; break;
    case View::Back:   m_yaw = 180.0; m_pitch = 0.0; break;
    case View::Left:   m_yaw = 90.0;  m_pitch = 0.0; break;
    case View::Right:  m_yaw = -90.0; m_pitch = 0.0; break;
    case View::Top:    m_yaw = 0.0;   m_pitch = 90.0; break;
    case View::Bottom: m_yaw = 0.0;   m_pitch = -90.0; break;
    case View::Custom: break;
    }
    invalidate();
    emit currentViewChanged(view);
}

void Viewer360View::setFishEye(bool on)
{
    if (m_fishEye == on) return;
    m_fishEye = on;
    invalidate();
    emit propertiesChanged();
}

void Viewer360View::setTextureScale(double percent)
{
    if (!std::isfinite(percent)) return;
    const double value = qBound(1.0, percent, 200.0);
    if (qFuzzyCompare(value, m_textureScale)) return;
    m_textureScale = value;
    invalidate();
    emit propertiesChanged();
}

void Viewer360View::setScaleRatio(double ratio)
{
    if (!std::isfinite(ratio)) return;
    const double value = qBound(0.1, ratio, 10.0);
    if (qFuzzyCompare(value, m_scaleRatio)) return;
    m_scaleRatio = value;
    invalidate();
    emit propertiesChanged();
}

void Viewer360View::setNativeRendering(bool on)
{
    if (m_nativeRendering == on) return;
    m_nativeRendering = on;
    invalidate();
}

void Viewer360View::setYawPitch(double yawDegrees, double pitchDegrees)
{
    if (!std::isfinite(yawDegrees) || !std::isfinite(pitchDegrees)) return;
    m_yaw = std::remainder(yawDegrees, 360.0);
    m_pitch = qBound(-90.0, pitchDegrees, 90.0);
    m_view = View::Custom;
    invalidate();
    emit currentViewChanged(m_view);
    emit propertiesChanged();
}

void Viewer360View::setRoll(double degrees)
{
    if (!std::isfinite(degrees)) return;
    const double roll = std::remainder(degrees, 360.0);
    if (qFuzzyCompare(m_roll, roll)) return;
    m_roll = roll;
    m_view = View::Custom;
    invalidate();
    emit currentViewChanged(m_view);
    emit propertiesChanged();
}

void Viewer360View::setFieldOfView(double degrees)
{
    if (!std::isfinite(degrees)) return;
    const double value = qBound(1.0, degrees, 179.0);
    if (qFuzzyCompare(value, m_fov)) return;
    m_fov = value;
    invalidate();
    emit propertiesChanged();
}

void Viewer360View::setCameraFieldOfView(double degrees)
{
    if (!std::isfinite(degrees)) return;
    const double value = qBound(20.0, degrees, 140.0);
    if (qFuzzyCompare(value, m_cameraFov)) return;
    m_cameraFov = value;
    if (m_useCameraFOV) invalidate();
}

void Viewer360View::setUseCameraFOV(bool on)
{
    if (m_useCameraFOV == on) return;
    m_useCameraFOV = on;
    invalidate();
    emit propertiesChanged();
}

void Viewer360View::setWrapXMode(WrapMode mode)
{
    if (int(mode) < 0 || int(mode) > 2 || m_wrapX == mode) return;
    m_wrapX = mode;
    invalidate();
    emit propertiesChanged();
}

void Viewer360View::setWrapYMode(WrapMode mode)
{
    if (int(mode) < 0 || int(mode) > 2 || m_wrapY == mode) return;
    m_wrapY = mode;
    invalidate();
    emit propertiesChanged();
}

void Viewer360View::turnBy(double yawDegrees, double pitchDegrees)
{
    setYawPitch(m_yaw + yawDegrees, m_pitch + pitchDegrees);
}

void Viewer360View::zoomBy(double fieldOfViewDelta)
{
    if (m_useCameraFOV) {
        m_fov = effectiveFieldOfView();
        setUseCameraFOV(false);
    }
    setFieldOfView(m_fov + fieldOfViewDelta);
}

void Viewer360View::inferCurrentView()
{
    View view = View::Custom;
    const auto near = [](double value, double expected) { return qAbs(value - expected) < 1e-5; };
    if (near(m_roll, 0)) {
        if (near(m_pitch, 0)) {
            if (near(m_yaw, 0)) view = View::Front;
            else if (near(qAbs(m_yaw), 180)) view = View::Back;
            else if (near(m_yaw, 90)) view = View::Left;
            else if (near(m_yaw, -90)) view = View::Right;
        } else if (near(m_yaw, 0)) {
            if (near(m_pitch, 90)) view = View::Top;
            else if (near(m_pitch, -90)) view = View::Bottom;
        }
    }
    if (view != m_view) {
        m_view = view;
        emit currentViewChanged(view);
    }
}

QImage Viewer360View::projectNatively(const QImage& frame, const QSize& size) const
{
    const core::Identifier module = plugin::nativeModuleNamed(QStringLiteral("EnvironmentMapViewer"));
    if (!module.isValid()) return {};
    // The values the reference's viewer gives its instance (FUN_140380680
    // sets useCameraFOV false and the wraps; the rest are the Properties').
    // The host hands the module its textures top row first where the
    // reference's run bottom-up, so the module's up is the frame's down:
    // the turns about X and Z take the other sign here.
    QStringList values;
    for (const plugin::EffectParameterSpec& parameter : plugin::nativeParameters(module)) {
        const QString& name = parameter.name;
        QString value = parameter.defaultValue;
        if (name == QLatin1String("useFishEyeLens")) value = m_fishEye ? QStringLiteral("true") : QStringLiteral("false");
        else if (name == QLatin1String("useCameraFOV")) value = QStringLiteral("false");
        else if (name == QLatin1String("environment_map")) value = QString::fromLatin1(kPanoramaLayer);
        else if (name == QLatin1String("fov")) value = number(effectiveFieldOfView());
        else if (name == QLatin1String("envTexScale")) value = number(m_textureScale);
        else if (name == QLatin1String("envScaleRatio")) value = number(m_scaleRatio);
        else if (name == QLatin1String("envWrapX")) value = QString::number(int(m_wrapX));
        else if (name == QLatin1String("envWrapY")) value = QString::number(int(m_wrapY));
        else if (name == QLatin1String("envTrasnformXRot")) value = number(-m_pitch);
        else if (name == QLatin1String("envTrasnformYRot")) value = number(m_yaw);
        else if (name == QLatin1String("envTrasnformZRot")) value = number(-m_roll);
        values << value;
    }
    // The module draws into its source's size and reads the panorama as the
    // layer it is given (environment_map).
    QImage target(size, QImage::Format_RGBA8888);
    target.fill(Qt::transparent);
    plugin::NativeFrameTime time;
    time.layerImage = frame;
    if (!plugin::applyEffectToImage(target, module, values, time)) return {};
    return target.convertToFormat(QImage::Format_ARGB32);
}

QImage Viewer360View::project(const QImage& source, const QSize& requestedSize) const
{
    if (source.isNull() || requestedSize.isEmpty()) return {};
    QSize outputSize = requestedSize;
    if (outputSize.width() > 960 || outputSize.height() > 540)
        outputSize.scale(QSize(960, 540), Qt::KeepAspectRatio);
    outputSize.setWidth(qMax(1, outputSize.width()));
    outputSize.setHeight(qMax(1, outputSize.height()));
    if (m_projectionValid && m_projectionSize == outputSize
        && m_projectionFrameKey == source.cacheKey()) {
        return m_projection;
    }
    const QImage frame = source.format() == QImage::Format_ARGB32
                             ? source : source.convertToFormat(QImage::Format_ARGB32);
    QImage result = m_nativeRendering ? projectNatively(frame, outputSize) : QImage();
    m_projectionNative = !result.isNull();
    if (result.isNull()) {
        result = QImage(outputSize, QImage::Format_ARGB32);
        const int sourceW = frame.width(), sourceH = frame.height();
        const double aspect = double(outputSize.width()) / outputSize.height();
        const Rotation rotation(m_yaw, m_pitch, m_roll);
        const Lens lens(m_fishEye, effectiveFieldOfView());
        const Texture texture(m_textureScale, m_scaleRatio);
        for (int y = 0; y < outputSize.height(); ++y) {
            QRgb* target = reinterpret_cast<QRgb*>(result.scanLine(y));
            const double viewY = 1.0 - 2.0 * (y + 0.5) / outputSize.height();
            for (int x = 0; x < outputSize.width(); ++x) {
                const double viewX = (2.0 * (x + 0.5) / outputSize.width() - 1.0) * aspect;
                double camera[3], world[3], u = 0.0, v = 0.0;
                lens.ray(viewX, viewY, camera);
                rotation.toWorld(camera, world);
                texture.coordinates(world, &u, &v);
                const double sourceX = u * sourceW - 0.5;
                const double sourceY = v * sourceH - 0.5;
                const int x0 = int(std::floor(sourceX)), y0 = int(std::floor(sourceY));
                const double fx = sourceX - x0, fy = sourceY - y0;
                const auto sample = [&](int sx, int syPixel) {
                    sx = sampleCoordinate(sx, sourceW, m_wrapX);
                    syPixel = sampleCoordinate(syPixel, sourceH, m_wrapY);
                    return reinterpret_cast<const QRgb*>(frame.constScanLine(syPixel))[sx];
                };
                const QRgb a = sample(x0, y0), b = sample(x0 + 1, y0);
                const QRgb c = sample(x0, y0 + 1), d = sample(x0 + 1, y0 + 1);
                const auto channel = [&](int av, int bv, int cv, int dv) {
                    return qBound(0, qRound((av * (1 - fx) + bv * fx) * (1 - fy)
                                            + (cv * (1 - fx) + dv * fx) * fy), 255);
                };
                target[x] = qRgba(channel(qRed(a), qRed(b), qRed(c), qRed(d)),
                                  channel(qGreen(a), qGreen(b), qGreen(c), qGreen(d)),
                                  channel(qBlue(a), qBlue(b), qBlue(c), qBlue(d)),
                                  channel(qAlpha(a), qAlpha(b), qAlpha(c), qAlpha(d)));
            }
        }
    }
    m_projection = result;
    m_projectionSize = outputSize;
    m_projectionFrameKey = source.cacheKey();
    m_projectionValid = true;
    return m_projection;
}

QPointF Viewer360View::frameAt(const QPointF& viewPoint, const QSizeF& viewSize,
                               const QSizeF& frameSize) const
{
    if (viewSize.isEmpty() || frameSize.isEmpty()) return {};
    const double viewX = (2.0 * viewPoint.x() / viewSize.width() - 1.0) * viewSize.width() / viewSize.height();
    const double viewY = 1.0 - 2.0 * viewPoint.y() / viewSize.height();
    double camera[3], world[3], u = 0.0, v = 0.0;
    Lens(m_fishEye, effectiveFieldOfView()).ray(viewX, viewY, camera);
    Rotation(m_yaw, m_pitch, m_roll).toWorld(camera, world);
    Texture(m_textureScale, m_scaleRatio).coordinates(world, &u, &v);
    return QPointF(wrappedCoordinate(u * frameSize.width(), frameSize.width(), m_wrapX),
                   wrappedCoordinate(v * frameSize.height(), frameSize.height(), m_wrapY));
}

bool Viewer360View::viewAt(const QPointF& framePoint, const QSizeF& frameSize,
                           const QSizeF& viewSize, QPointF* viewPoint) const
{
    if (viewSize.isEmpty() || frameSize.isEmpty()) return false;
    double world[3], camera[3], viewX = 0.0, viewY = 0.0;
    if (!Texture(m_textureScale, m_scaleRatio).direction(framePoint.x() / frameSize.width(),
                                                         framePoint.y() / frameSize.height(), world)) {
        return false;
    }
    Rotation(m_yaw, m_pitch, m_roll).toCamera(world, camera);
    if (!Lens(m_fishEye, effectiveFieldOfView()).point(camera, &viewX, &viewY)) return false;
    const double aspect = viewSize.width() / viewSize.height();
    const QPointF point((viewX / aspect + 1.0) * 0.5 * viewSize.width(),
                        (1.0 - viewY) * 0.5 * viewSize.height());
    if (viewPoint) *viewPoint = point;
    return std::isfinite(point.x()) && std::isfinite(point.y());
}

void Viewer360View::loadSettings()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("Viewer360"));
    setFieldOfView(settings.value(QStringLiteral("FieldOfView"), 90.0).toDouble());
    setUseCameraFOV(settings.value(QStringLiteral("UseCameraFOV"), false).toBool());
    // The bool keys predate the three wrap modes; Tile is what "on" meant.
    const auto legacyWrap = [&](const char* key) {
        return settings.value(QLatin1String(key), false).toBool() ? WrapMode::Tile : WrapMode::No;
    };
    setWrapXMode(WrapMode(settings.value(QStringLiteral("WrapXMode"),
                                         int(legacyWrap("EnvWrapX"))).toInt()));
    setWrapYMode(WrapMode(settings.value(QStringLiteral("WrapYMode"),
                                         int(legacyWrap("EnvWrapY"))).toInt()));
    setFishEye(settings.value(QStringLiteral("FishEyeLens"), true).toBool());
    setTextureScale(settings.value(QStringLiteral("TextureScale"), 100.0).toDouble());
    setScaleRatio(settings.value(QStringLiteral("ScaleRatio"), 2.0).toDouble());
    setYawPitch(settings.value(QStringLiteral("Yaw"), 0.0).toDouble(),
                settings.value(QStringLiteral("Pitch"), 0.0).toDouble());
    setRoll(settings.value(QStringLiteral("Roll"), 0.0).toDouble());
    const int savedValue = settings.value(QStringLiteral("View"), 1).toInt();
    const View savedView = View(savedValue >= 0 && savedValue <= 6 ? savedValue : 1);
    // Custom yaw was stored with the opposite sign before the reference's
    // rotation convention was adopted.
    const bool legacyRotation = !settings.contains(QStringLiteral("RotationConventionVersion"));
    if (legacyRotation && savedView == View::Custom) setYawPitch(-m_yaw, m_pitch);
    settings.endGroup();
    setCurrentView(savedView);
    if (legacyRotation) saveSettings();
}

void Viewer360View::saveSettings() const
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("Viewer360"));
    settings.setValue(QStringLiteral("View"), int(m_view));
    settings.setValue(QStringLiteral("Yaw"), m_yaw);
    settings.setValue(QStringLiteral("Pitch"), m_pitch);
    settings.setValue(QStringLiteral("Roll"), m_roll);
    settings.setValue(QStringLiteral("FieldOfView"), m_fov);
    settings.setValue(QStringLiteral("UseCameraFOV"), m_useCameraFOV);
    settings.setValue(QStringLiteral("EnvWrapX"), m_wrapX != WrapMode::No);
    settings.setValue(QStringLiteral("EnvWrapY"), m_wrapY != WrapMode::No);
    settings.setValue(QStringLiteral("WrapXMode"), int(m_wrapX));
    settings.setValue(QStringLiteral("WrapYMode"), int(m_wrapY));
    settings.setValue(QStringLiteral("FishEyeLens"), m_fishEye);
    settings.setValue(QStringLiteral("TextureScale"), m_textureScale);
    settings.setValue(QStringLiteral("ScaleRatio"), m_scaleRatio);
    settings.setValue(QStringLiteral("RotationConventionVersion"), 1);
    settings.endGroup();
}

} // namespace openvegas::ui
