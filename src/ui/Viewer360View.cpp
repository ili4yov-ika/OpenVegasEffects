#include "ui/Viewer360View.h"

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

// Camera space -> the frame's longitude/latitude, as the projection samples:
// roll about the view axis, then pitch, then yaw (so yaw never changes the
// latitude of a direction).
struct Rotation
{
    double cy, sy, cp, sp, cr, sr;
    Rotation(double yaw, double pitch, double roll)
        : cy(std::cos(radians(-yaw))), sy(std::sin(radians(-yaw)))
        , cp(std::cos(radians(pitch))), sp(std::sin(radians(pitch)))
        , cr(std::cos(radians(roll))), sr(std::sin(radians(roll))) {}

    void toSphere(double cameraX, double cameraY, double* longitude, double* latitude) const
    {
        const double length = std::sqrt(cameraX * cameraX + cameraY * cameraY + 1.0);
        const double localX = cameraX / length, localY = cameraY / length, localZ = 1.0 / length;
        const double rollX = cr * localX - sr * localY;
        const double rollY = sr * localX + cr * localY;
        const double worldY = cp * rollY + sp * localZ;
        const double pitchZ = -sp * rollY + cp * localZ;
        const double yawX = cy * rollX + sy * pitchZ;
        const double worldZ = -sy * rollX + cy * pitchZ;
        double lon = std::atan2(yawX, worldZ);
        // +/-180 degrees represent one direction; keep the seam stable.
        if (qAbs(lon + kPi) < 1e-12) lon = kPi;
        *longitude = lon;
        *latitude = std::asin(qBound(-1.0, worldY, 1.0));
    }

    // The inverse: false when the direction is behind the camera.
    bool toCamera(double longitude, double latitude, double* cameraX, double* cameraY) const
    {
        const double yawX = std::cos(latitude) * std::sin(longitude);
        const double worldY = std::sin(latitude);
        const double worldZ = std::cos(latitude) * std::cos(longitude);
        const double rollX = cy * yawX - sy * worldZ;
        const double pitchZ = sy * yawX + cy * worldZ;
        const double rollY = cp * worldY - sp * pitchZ;
        const double localZ = sp * worldY + cp * pitchZ;
        const double localX = cr * rollX + sr * rollY;
        const double localY = -sr * rollX + cr * rollY;
        if (localZ <= 1e-9) return false;
        *cameraX = localX / localZ;
        *cameraY = localY / localZ;
        return true;
    }
};

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
    QImage result(outputSize, QImage::Format_ARGB32);
    const int sourceW = frame.width(), sourceH = frame.height();
    const double aspect = double(outputSize.width()) / outputSize.height();
    const double tanY = std::tan(radians(effectiveFieldOfView()) * 0.5);
    const double tanX = tanY * aspect;
    const Rotation rotation(m_yaw, m_pitch, m_roll);
    for (int y = 0; y < outputSize.height(); ++y) {
        QRgb* target = reinterpret_cast<QRgb*>(result.scanLine(y));
        const double cameraY = (1.0 - 2.0 * (y + 0.5) / outputSize.height()) * tanY;
        for (int x = 0; x < outputSize.width(); ++x) {
            const double cameraX = (2.0 * (x + 0.5) / outputSize.width() - 1.0) * tanX;
            double longitude = 0.0, latitude = 0.0;
            rotation.toSphere(cameraX, cameraY, &longitude, &latitude);
            const double sourceX = (longitude / (2.0 * kPi) + 0.5) * sourceW - 0.5;
            const double sourceY = (0.5 - latitude / kPi) * sourceH - 0.5;
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
    const double tanY = std::tan(radians(effectiveFieldOfView()) * 0.5);
    const double tanX = tanY * viewSize.width() / viewSize.height();
    const double cameraX = (2.0 * viewPoint.x() / viewSize.width() - 1.0) * tanX;
    const double cameraY = (1.0 - 2.0 * viewPoint.y() / viewSize.height()) * tanY;
    double longitude = 0.0, latitude = 0.0;
    Rotation(m_yaw, m_pitch, m_roll).toSphere(cameraX, cameraY, &longitude, &latitude);
    return QPointF((longitude / (2.0 * kPi) + 0.5) * frameSize.width(),
                   (0.5 - latitude / kPi) * frameSize.height());
}

bool Viewer360View::viewAt(const QPointF& framePoint, const QSizeF& frameSize,
                           const QSizeF& viewSize, QPointF* viewPoint) const
{
    if (viewSize.isEmpty() || frameSize.isEmpty()) return false;
    const double longitude = (framePoint.x() / frameSize.width() - 0.5) * 2.0 * kPi;
    const double latitude = (0.5 - framePoint.y() / frameSize.height()) * kPi;
    double cameraX = 0.0, cameraY = 0.0;
    if (!Rotation(m_yaw, m_pitch, m_roll).toCamera(longitude, latitude, &cameraX, &cameraY)) {
        return false;
    }
    const double tanY = std::tan(radians(effectiveFieldOfView()) * 0.5);
    const double tanX = tanY * viewSize.width() / viewSize.height();
    const QPointF point((cameraX / tanX + 1.0) * 0.5 * viewSize.width(),
                        (1.0 - cameraY / tanY) * 0.5 * viewSize.height());
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
    settings.setValue(QStringLiteral("RotationConventionVersion"), 1);
    settings.endGroup();
}

} // namespace openvegas::ui
