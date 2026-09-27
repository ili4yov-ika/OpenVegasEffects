#pragma once

#include <QDockWidget>
#include <QImage>
#include <QPoint>
#include <QWidget>

class QComboBox;
class QToolButton;

namespace openvegas::ui {

// Perspective viewport over an equirectangular (2:1) image. The reference
// implements this as Preview360VideoWidget/GL; this CPU implementation keeps
// the same interaction and data flow without requiring another GL context.
class Preview360VideoWidget : public QWidget
{
    Q_OBJECT
public:
    enum class View { Custom = 0, Front = 1, Back = 2, Left = 3, Right = 4, Top = 5, Bottom = 6 };
    Q_ENUM(View)
    enum class WrapMode { No = 0, Tile = 1, Reflect = 2 };
    Q_ENUM(WrapMode)

    explicit Preview360VideoWidget(QWidget* parent = nullptr);
    void setFrame(const QImage& frame);
    QImage frame() const { return m_frame; }
    QImage projectedFrame(const QSize& size) const;

    View currentView() const { return m_view; }
    double yaw() const { return m_yaw; }
    double pitch() const { return m_pitch; }
    double roll() const { return m_roll; }
    double fieldOfView() const { return m_fov; }
    bool useCameraFOV() const { return m_useCameraFOV; }
    bool envWrapX() const { return m_wrapX != WrapMode::No; }
    bool envWrapY() const { return m_wrapY != WrapMode::No; }
    WrapMode wrapXMode() const { return m_wrapX; }
    WrapMode wrapYMode() const { return m_wrapY; }
    void inferCurrentView();

public slots:
    void setCurrentView(View view);
    void setYawPitch(double yawDegrees, double pitchDegrees);
    void setFieldOfView(double degrees);
    void setRoll(double degrees);
    void setCameraFieldOfView(double degrees);
    void setUseCameraFOV(bool on);
    void setEnvWrapX(bool on);
    void setEnvWrapY(bool on);
    void setWrapXMode(WrapMode mode);
    void setWrapYMode(WrapMode mode);

signals:
    void currentViewChanged(View view);
    void propertiesChanged();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;

private:
    void invalidate();
    double effectiveFov() const;

    QImage m_frame;
    View m_view = View::Front;
    double m_yaw = 0.0;
    double m_pitch = 0.0;
    double m_fov = 90.0;
    double m_cameraFov = 39.6;
    double m_roll = 0.0;
    bool m_useCameraFOV = false;
    WrapMode m_wrapX = WrapMode::No;
    WrapMode m_wrapY = WrapMode::No;
    bool m_dragging = false;
    QPoint m_lastMouse;
    mutable QImage m_projection;
    mutable QSize m_projectionSize;
    mutable qint64 m_projectionFrameKey = 0;
    mutable double m_projectionYaw = 0.0;
    mutable double m_projectionPitch = 0.0;
    mutable double m_projectionFov = 0.0;
    mutable double m_projectionRoll = 0.0;
    mutable WrapMode m_projectionWrapX = WrapMode::No;
    mutable WrapMode m_projectionWrapY = WrapMode::No;
};

class Preview360VideoPanel : public QDockWidget
{
    Q_OBJECT
public:
    explicit Preview360VideoPanel(QWidget* parent = nullptr);
    void setFrame(const QImage& frame);
    Preview360VideoWidget* videoWidget() const { return m_video; }

private:
    void showProperties();
    void saveProperties();
    QComboBox* m_views = nullptr;
    QToolButton* m_properties = nullptr;
    Preview360VideoWidget* m_video = nullptr;
};

} // namespace openvegas::ui
