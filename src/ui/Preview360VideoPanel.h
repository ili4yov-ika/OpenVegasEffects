#pragma once

#include <QDockWidget>
#include <QImage>
#include <QPoint>
#include <QPointer>
#include <QWidget>

#include "ui/Viewer360View.h"

class QComboBox;
class QToolButton;
class QVBoxLayout;

namespace openvegas::ui {

// Perspective viewport over an equirectangular (2:1) image. The reference
// implements this as Preview360VideoWidget/GL; this CPU implementation keeps
// the same interaction and data flow without requiring another GL context.
// The direction and lens live in a Viewer360View, which the Viewer's 360 mode
// shares.
class Preview360VideoWidget : public QWidget
{
    Q_OBJECT
public:
    using View = Viewer360View::View;
    using WrapMode = Viewer360View::WrapMode;

    explicit Preview360VideoWidget(QWidget* parent = nullptr, Viewer360View* view = nullptr);
    void setFrame(const QImage& frame);
    QImage frame() const { return m_frame; }
    QImage projectedFrame(const QSize& size) const;
    Viewer360View* view() const { return m_view; }

    View currentView() const { return m_view->currentView(); }
    double yaw() const { return m_view->yaw(); }
    double pitch() const { return m_view->pitch(); }
    double roll() const { return m_view->roll(); }
    double fieldOfView() const { return m_view->fieldOfView(); }
    bool useCameraFOV() const { return m_view->useCameraFOV(); }
    bool envWrapX() const { return m_view->wrapXMode() != WrapMode::No; }
    bool envWrapY() const { return m_view->wrapYMode() != WrapMode::No; }
    WrapMode wrapXMode() const { return m_view->wrapXMode(); }
    WrapMode wrapYMode() const { return m_view->wrapYMode(); }
    void inferCurrentView() { m_view->inferCurrentView(); }

public slots:
    void setCurrentView(View view) { m_view->setCurrentView(view); }
    void setYawPitch(double yawDegrees, double pitchDegrees) { m_view->setYawPitch(yawDegrees, pitchDegrees); }
    void setFieldOfView(double degrees) { m_view->setFieldOfView(degrees); }
    void setRoll(double degrees) { m_view->setRoll(degrees); }
    void setCameraFieldOfView(double degrees) { m_view->setCameraFieldOfView(degrees); }
    void setUseCameraFOV(bool on) { m_view->setUseCameraFOV(on); }
    void setEnvWrapX(bool on) { m_view->setWrapXMode(on ? WrapMode::Tile : WrapMode::No); }
    void setEnvWrapY(bool on) { m_view->setWrapYMode(on ? WrapMode::Tile : WrapMode::No); }
    void setWrapXMode(WrapMode mode) { m_view->setWrapXMode(mode); }
    void setWrapYMode(WrapMode mode) { m_view->setWrapYMode(mode); }

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
    Viewer360View* m_view = nullptr;
    QImage m_frame;
    bool m_dragging = false;
    QPoint m_lastMouse;
};

// The reference's 360 Viewer panel: the direction combo and the Properties
// button over a view of the frame. Inside the main window the view is the
// Viewer itself in its 360 mode (hostViewer), with its tools, transport and
// overlays; on its own the panel shows its Preview360VideoWidget.
class Preview360VideoPanel : public QDockWidget
{
    Q_OBJECT
public:
    explicit Preview360VideoPanel(QWidget* parent = nullptr);
    void setFrame(const QImage& frame);
    Preview360VideoWidget* videoWidget() const { return m_video; }
    Viewer360View* view() const { return m_view; }

    // Puts `page` (the Viewer page) under the header in place of the
    // standalone canvas; releaseViewer takes it out again and returns it.
    void hostViewer(QWidget* page);
    QWidget* releaseViewer();
    bool hostsViewer() const { return !m_hosted.isNull(); }

private:
    void showProperties();
    QComboBox* m_views = nullptr;
    QToolButton* m_properties = nullptr;
    Viewer360View* m_view = nullptr;
    Preview360VideoWidget* m_video = nullptr;
    QVBoxLayout* m_layout = nullptr;
    QPointer<QWidget> m_hosted;
};

} // namespace openvegas::ui
