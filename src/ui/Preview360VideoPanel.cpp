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
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtGlobal>

namespace openvegas::ui {

Preview360VideoWidget::Preview360VideoWidget(QWidget* parent, Viewer360View* view)
    : QWidget(parent), m_view(view ? view : new Viewer360View(this))
{
    setObjectName(QStringLiteral("glWidget"));
    setProperty("stop-playback", false);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setCursor(Qt::OpenHandCursor);
    setMinimumSize(160, 120);
    connect(m_view, &Viewer360View::currentViewChanged, this, &Preview360VideoWidget::currentViewChanged);
    connect(m_view, &Viewer360View::propertiesChanged, this, &Preview360VideoWidget::propertiesChanged);
    connect(m_view, &Viewer360View::changed, this, qOverload<>(&QWidget::update));
}

void Preview360VideoWidget::setFrame(const QImage& frame)
{
    m_frame = frame.convertToFormat(QImage::Format_ARGB32);
    update();
}

QImage Preview360VideoWidget::projectedFrame(const QSize& size) const
{
    return m_view->project(m_frame, size);
}

void Preview360VideoWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(17, 17, 17));
    if (m_frame.isNull()) {
        painter.setPen(QColor(145, 145, 145));
        painter.drawText(rect(), Qt::AlignCenter, tr("No frame"));
        return;
    }
    painter.drawImage(rect(), projectedFrame(size()));
}

void Preview360VideoWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_lastMouse = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void Preview360VideoWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging) {
        const QPoint delta = event->pos() - m_lastMouse;
        m_lastMouse = event->pos();
        m_view->turnBy(delta.x() * 0.25, -delta.y() * 0.25);
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void Preview360VideoWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_dragging && event->button() == Qt::LeftButton) {
        m_dragging = false;
        setCursor(Qt::OpenHandCursor);
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void Preview360VideoWidget::wheelEvent(QWheelEvent* event)
{
    if (event->angleDelta().y() == 0) {
        event->ignore();
        return;
    }
    m_view->zoomBy(-event->angleDelta().y() / 24.0);
    event->accept();
}

void Preview360VideoWidget::keyPressEvent(QKeyEvent* event)
{
    const double step = event->modifiers().testFlag(Qt::ShiftModifier) ? 10.0 : 1.0;
    switch (event->key()) {
    case Qt::Key_Left: m_view->turnBy(step, 0); break;
    case Qt::Key_Right: m_view->turnBy(-step, 0); break;
    case Qt::Key_Up: m_view->turnBy(0, step); break;
    case Qt::Key_Down: m_view->turnBy(0, -step); break;
    case Qt::Key_Home: m_view->setCurrentView(View::Front); break;
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
    const Viewer360View::View viewOrder[] = {
        Viewer360View::View::Front, Viewer360View::View::Back,
        Viewer360View::View::Left, Viewer360View::View::Right,
        Viewer360View::View::Top, Viewer360View::View::Bottom,
        Viewer360View::View::Custom,
    };
    for (int i = 0; i < m_views->count() && i < int(std::size(viewOrder)); ++i)
        m_views->setItemData(i, int(viewOrder[i]));
    m_properties = form.toolButtonProperties;
    m_properties->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_properties->setIcon(QIcon(QStringLiteral(":/icons/settings.svg")));

    m_view = new Viewer360View(this);
    m_view->loadSettings();
    m_layout = form.viewerLayout;
    const int videoIndex = m_layout->indexOf(form.glWidget);
    m_layout->removeWidget(form.glWidget);
    delete form.glWidget;
    m_video = new Preview360VideoWidget(form.viewer360, m_view);
    m_video->setObjectName(QStringLiteral("glWidget"));
    m_layout->insertWidget(videoIndex, m_video, 1);
    m_views->setCurrentIndex(m_views->findData(int(m_view->currentView())));

    connect(m_views, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index >= 0) m_view->setCurrentView(Viewer360View::View(m_views->itemData(index).toInt()));
    });
    connect(m_view, &Viewer360View::currentViewChanged, this, [this](Viewer360View::View view) {
        const int index = m_views->findData(int(view));
        if (index >= 0 && m_views->currentIndex() != index) {
            const QSignalBlocker blocker(m_views);
            m_views->setCurrentIndex(index);
        }
        m_view->saveSettings();
    });
    connect(m_view, &Viewer360View::propertiesChanged, this, [this] { m_view->saveSettings(); });
    connect(m_properties, &QToolButton::clicked, this, &Preview360VideoPanel::showProperties);
}

void Preview360VideoPanel::setFrame(const QImage& frame) { m_video->setFrame(frame); }

void Preview360VideoPanel::hostViewer(QWidget* page)
{
    if (!page || m_hosted == page) return;
    releaseViewer();
    m_hosted = page;
    m_video->hide();
    m_layout->addWidget(page, 1);
    page->show();
}

QWidget* Preview360VideoPanel::releaseViewer()
{
    QWidget* page = m_hosted;
    if (!page) return nullptr;
    // Still a child of the panel until the caller places it elsewhere.
    m_layout->removeWidget(page);
    page->hide();
    m_hosted = nullptr;
    m_video->show();
    return page;
}

void Preview360VideoPanel::showProperties()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("360 View Properties"));
    auto* form = new QFormLayout(&dialog);
    auto* yaw = new QDoubleSpinBox(&dialog); yaw->setObjectName(QStringLiteral("viewer360Yaw"));
    yaw->setRange(-180.0, 180.0); yaw->setSuffix(QStringLiteral("°")); yaw->setValue(m_view->yaw());
    auto* pitch = new QDoubleSpinBox(&dialog); pitch->setObjectName(QStringLiteral("viewer360Pitch"));
    pitch->setRange(-90.0, 90.0); pitch->setSuffix(QStringLiteral("°")); pitch->setValue(m_view->pitch());
    auto* roll = new QDoubleSpinBox(&dialog); roll->setObjectName(QStringLiteral("viewer360Roll"));
    roll->setRange(-180.0, 180.0); roll->setSuffix(QStringLiteral("°")); roll->setValue(m_view->roll());
    auto* fov = new QDoubleSpinBox(&dialog); fov->setObjectName(QStringLiteral("viewer360Fov"));
    fov->setRange(1.0, 179.0); fov->setSuffix(QStringLiteral("°")); fov->setValue(m_view->fieldOfView());
    auto* camera = new QCheckBox(tr("Use camera FOV"), &dialog);
    camera->setObjectName(QStringLiteral("useCameraFOV"));
    camera->setChecked(m_view->useCameraFOV());
    fov->setEnabled(!camera->isChecked());
    connect(camera, &QCheckBox::toggled, fov, [fov](bool checked) { fov->setEnabled(!checked); });
    auto* wrapX = new QComboBox(&dialog); wrapX->setObjectName(QStringLiteral("envWrapX"));
    auto* wrapY = new QComboBox(&dialog); wrapY->setObjectName(QStringLiteral("envWrapY"));
    for (auto* combo : {wrapX, wrapY}) combo->addItems({tr("No"), tr("Tile"), tr("Reflect")});
    wrapX->setCurrentIndex(int(m_view->wrapXMode()));
    wrapY->setCurrentIndex(int(m_view->wrapYMode()));
    form->addRow(tr("Yaw"), yaw);
    form->addRow(tr("Pitch"), pitch);
    form->addRow(tr("Roll"), roll);
    form->addRow(tr("Field of view"), fov);
    form->addRow(QString(), camera);
    form->addRow(tr("Wrap X"), wrapX);
    form->addRow(tr("Wrap Y"), wrapY);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted) return;
    if (!qFuzzyIsNull(yaw->value() - m_view->yaw()) || !qFuzzyIsNull(pitch->value() - m_view->pitch()))
        m_view->setYawPitch(yaw->value(), pitch->value());
    m_view->setRoll(roll->value());
    m_view->setFieldOfView(fov->value());
    m_view->setUseCameraFOV(camera->isChecked());
    m_view->setWrapXMode(Viewer360View::WrapMode(wrapX->currentIndex()));
    m_view->setWrapYMode(Viewer360View::WrapMode(wrapY->currentIndex()));
    m_view->inferCurrentView();
    m_view->saveSettings();
}

} // namespace openvegas::ui
