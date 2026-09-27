#include "ui/Preview360VideoPanel.h"
#include "ui_Preview360.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QSignalBlocker>
#include <QPainter>
#include <QSettings>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>
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
int sampleCoordinate(int value, int limit, Preview360VideoWidget::WrapMode mode)
{
    if (mode == Preview360VideoWidget::WrapMode::Tile) return wrapped(value,limit);
    if (mode == Preview360VideoWidget::WrapMode::Reflect) {
        const int index = wrapped(value,limit*2);
        return index < limit ? index : limit*2-1-index;
    }
    return qBound(0,value,limit-1);
}
}

Preview360VideoWidget::Preview360VideoWidget(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("glWidget"));
    setProperty("stop-playback", false);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setCursor(Qt::OpenHandCursor);
    setMinimumSize(160, 120);
}

void Preview360VideoWidget::invalidate()
{
    m_projection = QImage();
    m_projectionSize = QSize();
    update();
}

void Preview360VideoWidget::setFrame(const QImage& frame)
{
    m_frame = frame.convertToFormat(QImage::Format_ARGB32);
    invalidate();
}

double Preview360VideoWidget::effectiveFov() const
{
    return qBound(1.0, m_useCameraFOV ? m_cameraFov : m_fov, 179.0);
}

void Preview360VideoWidget::setCurrentView(View view)
{
    if (int(view) < 0 || int(view) > 6) return;
    m_view = view;
    if (view != View::Custom) m_roll = 0.0;
    switch (view) {
    case View::Front:  m_yaw = 0.0;   m_pitch = 0.0; break;
    case View::Back:   m_yaw = 180.0; m_pitch = 0.0; break;
    case View::Left:   m_yaw = 90.0; m_pitch = 0.0; break;
    case View::Right:  m_yaw = -90.0;  m_pitch = 0.0; break;
    case View::Top:    m_yaw = 0.0;   m_pitch = 90.0; break;
    case View::Bottom: m_yaw = 0.0;   m_pitch = -90.0; break;
    case View::Custom: break;
    }
    invalidate(); emit currentViewChanged(view);
}

void Preview360VideoWidget::setYawPitch(double yawDegrees, double pitchDegrees)
{
    if (!std::isfinite(yawDegrees) || !std::isfinite(pitchDegrees)) return;
    m_yaw = std::remainder(yawDegrees, 360.0);
    m_pitch = qBound(-90.0, pitchDegrees, 90.0);
    m_view = View::Custom;
    invalidate(); emit currentViewChanged(m_view); emit propertiesChanged();
}

void Preview360VideoWidget::setFieldOfView(double degrees)
{
    if (!std::isfinite(degrees)) return;
    const double value = qBound(1.0, degrees, 179.0);
    if (qFuzzyCompare(value, m_fov)) return;
    m_fov = value; invalidate(); emit propertiesChanged();
}

void Preview360VideoWidget::setCameraFieldOfView(double degrees)
{
    if (!std::isfinite(degrees)) return;
    const double value = qBound(20.0, degrees, 140.0);
    if (qFuzzyCompare(value, m_cameraFov)) return;
    m_cameraFov = value; if (m_useCameraFOV) invalidate();
}

void Preview360VideoWidget::setUseCameraFOV(bool on)
{
    if (m_useCameraFOV == on) return;
    m_useCameraFOV = on; invalidate(); emit propertiesChanged();
}

void Preview360VideoWidget::setEnvWrapX(bool on)
{
    setWrapXMode(on ? WrapMode::Tile : WrapMode::No);
}

void Preview360VideoWidget::setEnvWrapY(bool on)
{
    setWrapYMode(on ? WrapMode::Tile : WrapMode::No);
}

void Preview360VideoWidget::setWrapXMode(WrapMode mode)
{
    if (int(mode)<0 || int(mode)>2 || m_wrapX==mode) return;
    m_wrapX=mode; invalidate(); emit propertiesChanged();
}
void Preview360VideoWidget::setWrapYMode(WrapMode mode)
{
    if (int(mode)<0 || int(mode)>2 || m_wrapY==mode) return;
    m_wrapY=mode; invalidate(); emit propertiesChanged();
}
void Preview360VideoWidget::inferCurrentView()
{
    View view=View::Custom;
    const auto near=[](double value,double expected) { return qAbs(value-expected)<1e-5; };
    if (near(m_roll,0)) {
        if (near(m_pitch,0)) {
            if (near(m_yaw,0)) view=View::Front;
            else if (near(qAbs(m_yaw),180)) view=View::Back;
            else if (near(m_yaw,90)) view=View::Left;
            else if (near(m_yaw,-90)) view=View::Right;
        } else if (near(m_yaw,0)) {
            if (near(m_pitch,90)) view=View::Top;
            else if (near(m_pitch,-90)) view=View::Bottom;
        }
    }
    if (view!=m_view) { m_view=view; emit currentViewChanged(view); }
}

QImage Preview360VideoWidget::projectedFrame(const QSize& requestedSize) const
{
    if (m_frame.isNull() || requestedSize.isEmpty()) return {};
    QSize outputSize = requestedSize;
    if (outputSize.width() > 960 || outputSize.height() > 540)
        outputSize.scale(QSize(960, 540), Qt::KeepAspectRatio);
    outputSize.setWidth(qMax(1, outputSize.width()));
    outputSize.setHeight(qMax(1, outputSize.height()));
    const double fov = effectiveFov();
    if (!m_projection.isNull() && m_projectionSize == outputSize
        && m_projectionFrameKey == m_frame.cacheKey()
        && qFuzzyCompare(m_projectionYaw, m_yaw) && qFuzzyCompare(m_projectionPitch, m_pitch)
        && qFuzzyCompare(m_projectionFov, fov) && qFuzzyCompare(m_projectionRoll, m_roll) && m_projectionWrapX == m_wrapX
        && m_projectionWrapY == m_wrapY) return m_projection;

    QImage result(outputSize, QImage::Format_ARGB32);
    const int sourceW = m_frame.width(), sourceH = m_frame.height();
    const double aspect = double(outputSize.width()) / outputSize.height();
    const double tanY = std::tan(radians(fov) * 0.5);
    const double tanX = tanY * aspect;
    const double cy = std::cos(radians(-m_yaw)), sy = std::sin(radians(-m_yaw));
    const double cp = std::cos(radians(m_pitch)), sp = std::sin(radians(m_pitch));
    const double cr = std::cos(radians(m_roll)), sr = std::sin(radians(m_roll));
    for (int y = 0; y < outputSize.height(); ++y) {
        QRgb* target = reinterpret_cast<QRgb*>(result.scanLine(y));
        const double cameraY = (1.0 - 2.0 * (y + 0.5) / outputSize.height()) * tanY;
        for (int x = 0; x < outputSize.width(); ++x) {
            const double cameraX = (2.0 * (x + 0.5) / outputSize.width() - 1.0) * tanX;
            const double length = std::sqrt(cameraX * cameraX + cameraY * cameraY + 1.0);
            const double localX = cameraX / length, localY = cameraY / length, localZ = 1.0 / length;
            const double rollX = cr * localX - sr * localY;
            const double rollY = sr * localX + cr * localY;
            const double worldY = cp * rollY + sp * localZ;
            const double pitchZ = -sp * rollY + cp * localZ;
            const double yawX = cy * rollX + sy * pitchZ;
            const double worldZ = -sy * rollX + cy * pitchZ;
            double longitude = std::atan2(yawX, worldZ);
            // +/-180 degrees represent one direction; keep the seam stable.
            if (qAbs(longitude + kPi) < 1e-12) longitude = kPi;
            const double latitude = std::asin(qBound(-1.0, worldY, 1.0));
            const double sourceX = (longitude / (2.0 * kPi) + 0.5) * sourceW - 0.5;
            const double sourceY = (0.5 - latitude / kPi) * sourceH - 0.5;
            const int x0 = int(std::floor(sourceX)), y0 = int(std::floor(sourceY));
            const double fx = sourceX - x0, fy = sourceY - y0;
            const auto sample = [&](int sx, int syPixel) {
                sx = sampleCoordinate(sx,sourceW,m_wrapX);
                syPixel = sampleCoordinate(syPixel,sourceH,m_wrapY);
                return reinterpret_cast<const QRgb*>(m_frame.constScanLine(syPixel))[sx];
            };
            const QColor a=QColor::fromRgba(sample(x0,y0)), b=QColor::fromRgba(sample(x0+1,y0));
            const QColor c=QColor::fromRgba(sample(x0,y0+1)), d=QColor::fromRgba(sample(x0+1,y0+1));
            const auto channel = [&](int av, int bv, int cv, int dv) {
                return qBound(0, qRound((av*(1-fx)+bv*fx)*(1-fy)+(cv*(1-fx)+dv*fx)*fy),255);
            };
            target[x] = qRgba(channel(a.red(),b.red(),c.red(),d.red()),
                channel(a.green(),b.green(),c.green(),d.green()),
                channel(a.blue(),b.blue(),c.blue(),d.blue()),
                channel(a.alpha(),b.alpha(),c.alpha(),d.alpha()));
        }
    }
    m_projection = result; m_projectionSize = outputSize; m_projectionFrameKey = m_frame.cacheKey();
    m_projectionYaw = m_yaw; m_projectionPitch = m_pitch; m_projectionFov = fov;
    m_projectionRoll = m_roll;
    m_projectionWrapX = m_wrapX; m_projectionWrapY = m_wrapY;
    return m_projection;
}

void Preview360VideoWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this); painter.fillRect(rect(), QColor(17, 17, 17));
    if (m_frame.isNull()) {
        painter.setPen(QColor(145, 145, 145)); painter.drawText(rect(), Qt::AlignCenter, tr("No frame"));
        return;
    }
    painter.drawImage(rect(), projectedFrame(size()));
}

void Preview360VideoWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true; m_lastMouse = event->pos(); setCursor(Qt::ClosedHandCursor);
        event->accept(); return;
    }
    QWidget::mousePressEvent(event);
}

void Preview360VideoWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging) {
        const QPoint delta = event->pos() - m_lastMouse; m_lastMouse = event->pos();
        setYawPitch(m_yaw + delta.x() * 0.25, m_pitch - delta.y() * 0.25);
        event->accept(); return;
    }
    QWidget::mouseMoveEvent(event);
}

void Preview360VideoWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_dragging && event->button() == Qt::LeftButton) {
        m_dragging = false; setCursor(Qt::OpenHandCursor); event->accept(); return;
    }
    QWidget::mouseReleaseEvent(event);
}

void Preview360VideoWidget::wheelEvent(QWheelEvent* event)
{
    if (event->angleDelta().y() == 0) { event->ignore(); return; }
    if (m_useCameraFOV) { m_fov = effectiveFov(); setUseCameraFOV(false); }
    setFieldOfView(m_fov - event->angleDelta().y() / 24.0);
    event->accept();
}

void Preview360VideoWidget::setRoll(double degrees)
{
    if (!std::isfinite(degrees)) return;
    const double roll = std::remainder(degrees, 360.0);
    if (qFuzzyCompare(m_roll, roll)) return;
    m_roll = roll; m_view = View::Custom; invalidate();
    emit currentViewChanged(m_view); emit propertiesChanged();
}

void Preview360VideoWidget::keyPressEvent(QKeyEvent* event)
{
    const double step = event->modifiers().testFlag(Qt::ShiftModifier) ? 10.0 : 1.0;
    switch (event->key()) {
    case Qt::Key_Left: setYawPitch(m_yaw+step,m_pitch); break;
    case Qt::Key_Right: setYawPitch(m_yaw-step,m_pitch); break;
    case Qt::Key_Up: setYawPitch(m_yaw,m_pitch+step); break;
    case Qt::Key_Down: setYawPitch(m_yaw,m_pitch-step); break;
    case Qt::Key_Home: setCurrentView(View::Front); break;
    default: QWidget::keyPressEvent(event); return;
    }
    event->accept();
}

Preview360VideoPanel::Preview360VideoPanel(QWidget* parent) : QDockWidget(parent)
{
    Ui::Preview360VideoPanel form;
    form.setupUi(this);
    form.viewer360->setObjectName(QStringLiteral("360-viewer"));
    toggleViewAction()->setText(tr("360 Viewer Panel"));
    m_views = form.comboBoxCurrentView;
    const Preview360VideoWidget::View viewOrder[] = {
        Preview360VideoWidget::View::Front, Preview360VideoWidget::View::Back,
        Preview360VideoWidget::View::Left, Preview360VideoWidget::View::Right,
        Preview360VideoWidget::View::Top, Preview360VideoWidget::View::Bottom,
        Preview360VideoWidget::View::Custom,
    };
    for (int i = 0; i < m_views->count() && i < int(std::size(viewOrder)); ++i)
        m_views->setItemData(i, int(viewOrder[i]));
    m_properties = form.toolButtonProperties;
    m_properties->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_properties->setIcon(QIcon(QStringLiteral(":/icons/settings.svg")));
    const int videoIndex = form.viewerLayout->indexOf(form.glWidget);
    form.viewerLayout->removeWidget(form.glWidget);
    delete form.glWidget;
    m_video = new Preview360VideoWidget(form.Preview360VideoPanelWidget);
    m_video->setObjectName(QStringLiteral("glWidget"));
    form.viewerLayout->insertWidget(videoIndex, m_video, 1);

    QSettings settings; settings.beginGroup(QStringLiteral("Viewer360"));
    m_video->setFieldOfView(settings.value(QStringLiteral("FieldOfView"), 90.0).toDouble());
    m_video->setUseCameraFOV(settings.value(QStringLiteral("UseCameraFOV"), false).toBool());
    m_video->setEnvWrapX(settings.value(QStringLiteral("EnvWrapX"), false).toBool());
    m_video->setEnvWrapY(settings.value(QStringLiteral("EnvWrapY"), false).toBool());
    m_video->setWrapXMode(Preview360VideoWidget::WrapMode(settings.value(QStringLiteral("WrapXMode"),int(m_video->wrapXMode())).toInt()));
    m_video->setWrapYMode(Preview360VideoWidget::WrapMode(settings.value(QStringLiteral("WrapYMode"),int(m_video->wrapYMode())).toInt()));
    m_video->setYawPitch(settings.value(QStringLiteral("Yaw"), 0.0).toDouble(),
                         settings.value(QStringLiteral("Pitch"), 0.0).toDouble());
    m_video->setRoll(settings.value(QStringLiteral("Roll"), 0.0).toDouble());
    const int savedValue = settings.value(QStringLiteral("View"), 1).toInt();
    const auto savedView = Preview360VideoWidget::View(savedValue >= 0 && savedValue <= 6 ? savedValue : 1);
    const bool legacyRotation = !settings.contains(QStringLiteral("RotationConventionVersion"));
    if (legacyRotation && savedView == Preview360VideoWidget::View::Custom)
        m_video->setYawPitch(-m_video->yaw(),m_video->pitch());
    settings.endGroup(); m_video->setCurrentView(savedView);
    m_views->setCurrentIndex(m_views->findData(int(savedView)));

    connect(m_views, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index >= 0) m_video->setCurrentView(Preview360VideoWidget::View(m_views->itemData(index).toInt()));
    });
    connect(m_video, &Preview360VideoWidget::currentViewChanged, this, [this](auto view) {
        const int index = m_views->findData(int(view));
        if (index >= 0 && m_views->currentIndex() != index) {
            const QSignalBlocker blocker(m_views); m_views->setCurrentIndex(index);
        }
        saveProperties();
    });
    connect(m_video, &Preview360VideoWidget::propertiesChanged, this, &Preview360VideoPanel::saveProperties);
    connect(m_properties, &QToolButton::clicked, this, &Preview360VideoPanel::showProperties);
    if (legacyRotation) saveProperties();
}

void Preview360VideoPanel::setFrame(const QImage& frame) { m_video->setFrame(frame); }

void Preview360VideoPanel::saveProperties()
{
    QSettings s; s.beginGroup(QStringLiteral("Viewer360"));
    s.setValue(QStringLiteral("View"), int(m_video->currentView()));
    s.setValue(QStringLiteral("Yaw"), m_video->yaw()); s.setValue(QStringLiteral("Pitch"), m_video->pitch());
    s.setValue(QStringLiteral("Roll"), m_video->roll());
    s.setValue(QStringLiteral("FieldOfView"), m_video->fieldOfView());
    s.setValue(QStringLiteral("UseCameraFOV"), m_video->useCameraFOV());
    s.setValue(QStringLiteral("EnvWrapX"), m_video->envWrapX());
    s.setValue(QStringLiteral("EnvWrapY"), m_video->envWrapY());
    s.setValue(QStringLiteral("WrapXMode"),int(m_video->wrapXMode()));
    s.setValue(QStringLiteral("WrapYMode"),int(m_video->wrapYMode())); s.endGroup();
    s.setValue(QStringLiteral("Viewer360/RotationConventionVersion"),1);
}

void Preview360VideoPanel::showProperties()
{
    QDialog dialog(this); dialog.setWindowTitle(tr("360 View Properties"));
    auto* form = new QFormLayout(&dialog);
    auto* yaw = new QDoubleSpinBox(&dialog); yaw->setObjectName(QStringLiteral("viewer360Yaw"));
    yaw->setRange(-180.0, 180.0); yaw->setSuffix(QStringLiteral("\u00b0")); yaw->setValue(m_video->yaw());
    auto* pitch = new QDoubleSpinBox(&dialog); pitch->setObjectName(QStringLiteral("viewer360Pitch"));
    pitch->setRange(-90.0, 90.0); pitch->setSuffix(QStringLiteral("\u00b0")); pitch->setValue(m_video->pitch());
    auto* roll = new QDoubleSpinBox(&dialog); roll->setObjectName(QStringLiteral("viewer360Roll"));
    roll->setRange(-180.0,180.0); roll->setSuffix(QStringLiteral("\u00b0")); roll->setValue(m_video->roll());
    auto* fov = new QDoubleSpinBox(&dialog); fov->setObjectName(QStringLiteral("viewer360Fov"));
    fov->setRange(1.0, 179.0); fov->setSuffix(QStringLiteral("\u00b0")); fov->setValue(m_video->fieldOfView());
    auto* camera = new QCheckBox(tr("Use camera FOV"), &dialog); camera->setObjectName(QStringLiteral("useCameraFOV")); camera->setChecked(m_video->useCameraFOV());
    fov->setEnabled(!camera->isChecked());
    connect(camera, &QCheckBox::toggled, fov, [fov](bool checked) { fov->setEnabled(!checked); });
    auto* wrapX = new QComboBox(&dialog); wrapX->setObjectName(QStringLiteral("envWrapX"));
    auto* wrapY = new QComboBox(&dialog); wrapY->setObjectName(QStringLiteral("envWrapY"));
    for (auto* combo : {wrapX,wrapY}) combo->addItems({tr("No"),tr("Tile"),tr("Reflect")});
    wrapX->setCurrentIndex(int(m_video->wrapXMode())); wrapY->setCurrentIndex(int(m_video->wrapYMode()));
    form->addRow(tr("Yaw"), yaw); form->addRow(tr("Pitch"), pitch); form->addRow(tr("Roll"),roll); form->addRow(tr("Field of view"), fov);
    form->addRow(QString(), camera); form->addRow(tr("Wrap X"), wrapX); form->addRow(tr("Wrap Y"), wrapY);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject); form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted) return;
    if (!qFuzzyIsNull(yaw->value()-m_video->yaw()) || !qFuzzyIsNull(pitch->value()-m_video->pitch()))
        m_video->setYawPitch(yaw->value(), pitch->value());
    m_video->setRoll(roll->value()); m_video->setFieldOfView(fov->value());
    m_video->setUseCameraFOV(camera->isChecked());
    m_video->setWrapXMode(Preview360VideoWidget::WrapMode(wrapX->currentIndex()));
    m_video->setWrapYMode(Preview360VideoWidget::WrapMode(wrapY->currentIndex()));
    m_video->inferCurrentView(); saveProperties();
}

} // namespace openvegas::ui
