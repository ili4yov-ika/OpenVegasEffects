#pragma once

#include <QImage>
#include <QObject>
#include <QPointF>
#include <QSize>

namespace openvegas::ui {

// The direction, lens and wrap of a 360 view over an equirectangular (2:1)
// frame - the state the reference keeps on its 360 viewer's EffectInstance
// (envTrasnformXRot/YRot/ZRot, FOV, useCameraFOV, envWrapX/Y). Shared by
// whatever shows the view: the Viewer in its 360 mode and the standalone
// Preview360VideoWidget, so both turn together and a direction picked in one
// is the direction of the other.
class Viewer360View : public QObject
{
    Q_OBJECT
public:
    enum class View { Custom = 0, Front = 1, Back = 2, Left = 3, Right = 4, Top = 5, Bottom = 6 };
    Q_ENUM(View)
    enum class WrapMode { No = 0, Tile = 1, Reflect = 2 };
    Q_ENUM(WrapMode)

    explicit Viewer360View(QObject* parent = nullptr);

    View currentView() const { return m_view; }
    double yaw() const { return m_yaw; }
    double pitch() const { return m_pitch; }
    double roll() const { return m_roll; }
    double fieldOfView() const { return m_fov; }
    double cameraFieldOfView() const { return m_cameraFov; }
    bool useCameraFOV() const { return m_useCameraFOV; }
    WrapMode wrapXMode() const { return m_wrapX; }
    WrapMode wrapYMode() const { return m_wrapY; }
    // The vertical field of view actually projected with: the camera's when
    // the view follows it, otherwise its own.
    double effectiveFieldOfView() const;
    // Names the direction again when the angles are exactly a preset's.
    void inferCurrentView();

    // Perspective view of `frame` at `size` (at most 960x540; callers scale
    // it up). Cached for the last frame/size/direction.
    QImage project(const QImage& frame, const QSize& size) const;

    // Where a point of a `viewSize` viewport looks at on a `frameSize`
    // equirectangular frame, in that frame's pixels.
    QPointF frameAt(const QPointF& viewPoint, const QSizeF& viewSize,
                    const QSizeF& frameSize) const;
    // The viewport point showing a frame pixel; false when it is behind the
    // viewer or outside the lens.
    bool viewAt(const QPointF& framePoint, const QSizeF& frameSize, const QSizeF& viewSize,
                QPointF* viewPoint) const;

    // Loads/stores the QSettings group Viewer360 (shared with the panel).
    void loadSettings();
    void saveSettings() const;

public slots:
    void setCurrentView(View view);
    void setYawPitch(double yawDegrees, double pitchDegrees);
    void setRoll(double degrees);
    void setFieldOfView(double degrees);
    void setCameraFieldOfView(double degrees);
    void setUseCameraFOV(bool on);
    void setWrapXMode(WrapMode mode);
    void setWrapYMode(WrapMode mode);
    // A turn by screen drag: right looks left, as the reference's viewer does.
    void turnBy(double yawDegrees, double pitchDegrees);
    // Zoom by changing the lens; a view following the camera stops doing so.
    void zoomBy(double fieldOfViewDelta);

signals:
    void currentViewChanged(View view);
    void propertiesChanged();
    // Anything that changes the picture.
    void changed();

private:
    void invalidate();

    View m_view = View::Front;
    double m_yaw = 0.0;
    double m_pitch = 0.0;
    double m_roll = 0.0;
    double m_fov = 90.0;
    double m_cameraFov = 39.6;
    bool m_useCameraFOV = false;
    WrapMode m_wrapX = WrapMode::No;
    WrapMode m_wrapY = WrapMode::No;

    mutable QImage m_projection;
    mutable QSize m_projectionSize;
    mutable qint64 m_projectionFrameKey = 0;
    mutable bool m_projectionValid = false;
};

} // namespace openvegas::ui
