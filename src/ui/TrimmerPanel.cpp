#include "ui/TrimmerPanel.h"
#include "ui_Trimmer.h"

#include <QBrush>
#include <QAction>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QFrame>
#include <QIcon>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMouseEvent>
#include <QPainter>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QPen>
#include <QSlider>
#include <QSignalBlocker>
#include <QMimeData>
#include <QUrl>
#include <QToolButton>
#include <QVBoxLayout>

namespace openvegas {
namespace ui {

namespace {
const int kFrameGuideSpacing = 80; // px between trimmer frame guides
const int kMarkerHitWidth = 8;
} // namespace

// ---------------------------------------------------------------- TrimmerWidget

TrimmerWidget::TrimmerWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("trimmer-frame"));
    setMinimumHeight(64);
    setMouseTracking(true);
}

void TrimmerWidget::setAsset(const media::MediaAsset& asset)
{
    m_asset = asset;
    m_frameCount = qMax(1, qRound(asset.durationSeconds() * m_frameRate));
    m_trimIn = qBound(0, asset.trimInPoint(), qMax(0, m_frameCount - 1));
    m_trimOut = asset.trimOutPoint() > m_trimIn
        ? qBound(m_trimIn + 1, asset.trimOutPoint(), m_frameCount)
        : m_frameCount;
    m_position = m_trimIn / m_frameRate;
    update();
}

void TrimmerWidget::setFrameRate(double framesPerSecond)
{
    const double next = qMax(1.0, framesPerSecond);
    if (qFuzzyCompare(m_frameRate, next)) {
        return;
    }
    m_frameRate = next;
    if (m_asset.isValid()) {
        setAsset(m_asset);
    }
}

void TrimmerWidget::clearAsset()
{
    m_asset = media::MediaAsset();
    m_frameCount = 1;
    m_trimIn = 0;
    m_trimOut = 1;
    m_position = 0.0;
    update();
}

void TrimmerWidget::setTrimInPoint(int frames)
{
    const int next = qBound(0, frames, qMax(0, m_trimOut - 1));
    if (next == m_trimIn) {
        return;
    }
    m_trimIn = next;
    emit trimInChanged(m_trimIn);
    update();
}

void TrimmerWidget::setTrimOutPoint(int frames)
{
    const int next = qBound(qMin(m_trimIn + 1, m_frameCount), frames, m_frameCount);
    if (next == m_trimOut) {
        return;
    }
    m_trimOut = next;
    emit trimOutChanged(m_trimOut);
    update();
}

void TrimmerWidget::setPositionSeconds(double seconds)
{
    const double next = qBound(0.0, seconds, m_asset.durationSeconds());
    if (qFuzzyCompare(m_position + 1.0, next + 1.0)) {
        return;
    }
    m_position = next;
    emit positionChanged(m_position);
    update();
}

double TrimmerWidget::secondsFromX(int x) const
{
    const double w = qMax(1.0, double(qMax(1, width() - 1)));
    return (double(x) / w) * m_asset.durationSeconds();
}

int TrimmerWidget::xFromSeconds(double seconds) const
{
    const double w = qMax(1.0, double(qMax(1, width() - 1)));
    return int((seconds / qMax(0.0001, m_asset.durationSeconds())) * w);
}

int TrimmerWidget::frameFromX(int x) const
{
    return qBound(0, qRound(secondsFromX(x) * m_frameRate), m_frameCount);
}

void TrimmerWidget::updateDragPosition(int x)
{
    switch (m_dragMode) {
    case DragMode::TrimIn:
        setTrimInPoint(frameFromX(x));
        setPositionSeconds(m_trimIn / m_frameRate);
        break;
    case DragMode::TrimOut:
        setTrimOutPoint(frameFromX(x));
        setPositionSeconds(m_trimOut / m_frameRate);
        break;
    case DragMode::Playhead:
        setPositionSeconds(secondsFromX(x));
        break;
    case DragMode::None:
        break;
    }
}

void TrimmerWidget::mousePressEvent(QMouseEvent* event)
{
    if (!m_asset.isValid() || event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    const int x = event->position().toPoint().x();
    const int xIn = xFromSeconds(m_trimIn / m_frameRate);
    const int xOut = xFromSeconds(m_trimOut / m_frameRate);
    if (qAbs(x - xIn) <= kMarkerHitWidth) {
        m_dragMode = DragMode::TrimIn;
    } else if (qAbs(x - xOut) <= kMarkerHitWidth) {
        m_dragMode = DragMode::TrimOut;
    } else {
        m_dragMode = DragMode::Playhead;
    }
    updateDragPosition(x);
}

void TrimmerWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragMode == DragMode::None || !m_asset.isValid()) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    updateDragPosition(event->position().toPoint().x());
}

void TrimmerWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragMode = DragMode::None;
    }
    QWidget::mouseReleaseEvent(event);
}

void TrimmerWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.fillRect(rect(), palette().base());

    if (!m_asset.isValid()) {
        p.setPen(palette().placeholderText().color());
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("No media selected"));
        return;
    }

    const double w = qMax(1.0, double(qMax(1, width() - 1)));

    // Clip strip.
    const QRect strip(0, 6, width(), height() - 26);
    p.fillRect(strip, QColor(48, 48, 48));

    // In/out selection band.
    const int xIn = int((m_trimIn / double(m_frameCount)) * w);
    const int xOut = int((m_trimOut / double(m_frameCount)) * w);
    p.fillRect(QRect(xIn, 6, qMax(1, xOut - xIn), strip.height()), QColor(70, 110, 170, 140));

    // Frame guides.
    p.setPen(QPen(QColor(90, 90, 90), 1));
    for (int x = 0; x < width(); x += kFrameGuideSpacing) {
        p.drawLine(x, 6, x, strip.bottom());
    }

    // In/out markers.
    p.setPen(QPen(QColor(0, 200, 90), 2));
    p.drawLine(xIn, 4, xIn, strip.bottom());
    p.setPen(QPen(QColor(220, 70, 70), 2));
    p.drawLine(xOut, 4, xOut, strip.bottom());

    // Scrub playhead.
    const int px = xFromSeconds(m_position);
    p.setPen(QPen(palette().highlight().color(), 2));
    p.drawLine(px, 2, px, height() - 2);

    // Labels.
    p.setPen(palette().text().color());
    p.drawText(QRect(4, strip.bottom() + 2, width() - 8, 18), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("%1  |  trim %2..%3 / %4")
                   .arg(m_asset.fileName())
                   .arg(m_trimIn)
                   .arg(m_trimOut)
                   .arg(m_frameCount));
}

// ---------------------------------------------------------------- TrimmerPanel

TrimmerPanel::TrimmerPanel(QWidget* parent)
    : QWidget(parent)
{
    Ui::TrimmerPanel form;
    form.setupUi(this);
    // The Designer identifier is sanitized to trimmer_panel by uic; external
    // layout state and the reference inventory use the hyphenated name.
    setObjectName(QStringLiteral("trimmer-panel"));
    form.trimmer_toolbar_top->setObjectName(QStringLiteral("trimmer-toolbar-top"));
    form.trimmer_in_out->setObjectName(QStringLiteral("trimmer-in-out"));
    form.trimmer_toolbar_bottom->setObjectName(QStringLiteral("trimmer-toolbar-bottom"));
    setAcceptDrops(true);
    QToolButton* setIn = form.trimmerSetIn;
    setIn->setIcon(QIcon(QStringLiteral(":/icons/mark-in.svg")));
    connect(setIn, &QToolButton::clicked, this, &TrimmerPanel::setTrimInPoint);
    QToolButton* setOut = form.trimmerSetOut;
    setOut->setIcon(QIcon(QStringLiteral(":/icons/mark-out.svg")));
    connect(setOut, &QToolButton::clicked, this, &TrimmerPanel::setTrimOutPoint);
    QToolButton* insertBtn = form.trimmerInsert;
    insertBtn->setIcon(QIcon(QStringLiteral(":/icons/add.svg")));
    connect(insertBtn, &QToolButton::clicked, this, [this] {
        if (m_widget->hasAsset()) {
            emit insertRequested(m_widget->asset().filePath(), m_widget->trimInPoint(),
                                 m_widget->trimOutPoint());
        }
    });

    QToolButton* overlayBtn = form.trimmerOverlay;
    overlayBtn->setIcon(QIcon(QStringLiteral(":/icons/paste.svg")));
    connect(overlayBtn, &QToolButton::clicked, this, [this] {
        if (m_widget->hasAsset()) {
            emit overlayRequested(m_widget->asset().filePath(), m_widget->trimInPoint(),
                                  m_widget->trimOutPoint());
        }
    });

    m_positionLabel = form.trimmerPosition;
    const int frameIndex = form.inOutLayout->indexOf(form.trimmer_frame);
    form.inOutLayout->removeWidget(form.trimmer_frame);
    delete form.trimmer_frame;
    m_widget = new TrimmerWidget(form.trimmer_in_out);
    m_widget->setObjectName(QStringLiteral("trimmer-frame"));
    form.inOutLayout->insertWidget(frameIndex, m_widget, 1);
    m_scrub = form.TrimmerSlider;
    connect(m_scrub, &QSlider::valueChanged, this, [this](int value) {
        if (m_widget->hasAsset()) {
            const double t = (value / 1000.0) * m_widget->asset().durationSeconds();
            m_widget->setPositionSeconds(t);
        }
    });
    connect(m_widget, &TrimmerWidget::positionChanged, this,
            [this](double seconds) {
                if (m_widget->hasAsset() && m_widget->asset().durationSeconds() > 0.0) {
                    const QSignalBlocker blocker(m_scrub);
                    m_scrub->setValue(int((seconds / m_widget->asset().durationSeconds()) * 1000.0));
                }
                updateReadouts();
            });
    m_rangeLabel = form.trimmerRange;
    m_assetList = form.trimmerSourceList;
    connect(m_assetList, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (!item || !m_manager) {
            return;
        }
        pickAsset(item->data(Qt::UserRole).toString());
    });

    connect(m_widget, &TrimmerWidget::trimInChanged, this, [this] {
        persistTrimRange();
        updateReadouts();
    });
    connect(m_widget, &TrimmerWidget::trimOutChanged, this, [this] {
        persistTrimRange();
        updateReadouts();
    });

    auto* insertAction = new QAction(this);
    insertAction->setObjectName(QStringLiteral("InsertTrimmerAsset"));
    insertAction->setShortcut(QKeySequence(Qt::Key_B));
    insertAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(insertAction, &QAction::triggered, insertBtn, &QToolButton::click);
    addAction(insertAction);
    auto* overlayAction = new QAction(this);
    overlayAction->setObjectName(QStringLiteral("OverlayTrimmerAsset"));
    overlayAction->setShortcut(QKeySequence(Qt::Key_N));
    overlayAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(overlayAction, &QAction::triggered, overlayBtn, &QToolButton::click);
    addAction(overlayAction);

    updateReadouts();
}

void TrimmerPanel::setMediaManager(media::MediaManager* manager)
{
    m_manager = manager;
    m_assetList->clear();
    if (!m_manager) {
        return;
    }
    const QVector<media::MediaAsset> assets = m_manager->assets();
    for (int i = 0; i < assets.size(); ++i) {
        const media::MediaAsset& asset = assets.at(i);
        QListWidgetItem* item = new QListWidgetItem(asset.fileName());
        item->setData(Qt::UserRole, asset.filePath());
        item->setToolTip(asset.filePath());
        m_assetList->addItem(item);
    }
    if (m_widget->hasAsset()) {
        const media::MediaAsset current = m_manager->assetByFilePath(m_widget->asset().filePath());
        if (current.isValid()) {
            openAsset(current);
        } else {
            openAsset(media::MediaAsset());
        }
    }
}

void TrimmerPanel::openAsset(const media::MediaAsset& asset)
{
    if (!asset.isValid()) {
        m_widget->clearAsset();
        const QSignalBlocker blocker(m_scrub);
        m_scrub->setValue(0);
        updateReadouts();
        return;
    }
    m_widget->setAsset(asset);
    {
        const QSignalBlocker blocker(m_scrub);
        m_scrub->setValue(asset.durationSeconds() > 0.0
            ? qRound((m_widget->positionSeconds() / asset.durationSeconds()) * 1000.0) : 0);
    }
    updateReadouts();
    for (int i = 0; i < m_assetList->count(); ++i) {
        if (m_assetList->item(i)->data(Qt::UserRole).toString() == asset.filePath()) {
            m_assetList->setCurrentRow(i);
            break;
        }
    }
}

void TrimmerPanel::setTrimInPoint()
{
    if (!m_widget->hasAsset()) {
        return;
    }
    const int frame = qRound(m_widget->positionSeconds() * m_widget->frameRate());
    m_widget->setTrimInPoint(frame);
}

void TrimmerPanel::setTrimOutPoint()
{
    if (!m_widget->hasAsset()) {
        return;
    }
    const int frame = qRound(m_widget->positionSeconds() * m_widget->frameRate());
    m_widget->setTrimOutPoint(frame);
}

void TrimmerPanel::setFrameRate(double framesPerSecond)
{
    m_widget->setFrameRate(framesPerSecond);
    updateReadouts();
}

void TrimmerPanel::persistTrimRange()
{
    if (!m_manager || !m_widget->hasAsset()) {
        return;
    }
    media::MediaAsset* asset = m_manager->assetByFilePathForEdit(m_widget->asset().filePath());
    if (!asset) {
        return;
    }
    asset->setTrimInPoint(m_widget->trimInPoint());
    asset->setTrimOutPoint(m_widget->trimOutPoint());
    emit trimRangeChanged(asset->filePath(), asset->trimInPoint(), asset->trimOutPoint());
}

QString TrimmerPanel::timecode(double seconds) const
{
    const int fps = qMax(1, qRound(m_widget->frameRate()));
    qint64 frames = qMax<qint64>(0, qRound64(seconds * m_widget->frameRate()));
    const int ff = int(frames % fps);
    const qint64 totalSeconds = frames / fps;
    const int ss = int(totalSeconds % 60);
    const int mm = int((totalSeconds / 60) % 60);
    const qint64 hh = totalSeconds / 3600;
    return QStringLiteral("%1:%2:%3:%4")
        .arg(hh, 2, 10, QLatin1Char('0')).arg(mm, 2, 10, QLatin1Char('0'))
        .arg(ss, 2, 10, QLatin1Char('0')).arg(ff, 2, 10, QLatin1Char('0'));
}

void TrimmerPanel::updateReadouts()
{
    if (!m_widget->hasAsset()) {
        m_positionLabel->setText(QStringLiteral("00:00:00:00"));
        m_rangeLabel->setText(tr("No source"));
        return;
    }
    m_positionLabel->setText(timecode(m_widget->positionSeconds()));
    const double fps = m_widget->frameRate();
    m_rangeLabel->setText(tr("In %1  Out %2  Duration %3")
        .arg(timecode(m_widget->trimInPoint() / fps))
        .arg(timecode(m_widget->trimOutPoint() / fps))
        .arg(timecode((m_widget->trimOutPoint() - m_widget->trimInPoint()) / fps)));
}

void TrimmerPanel::dragEnterEvent(QDragEnterEvent* event)
{
    bool supported = event->mimeData()->hasUrls();
    for (const QString& format : event->mimeData()->formats()) {
        supported = supported || format.startsWith(QStringLiteral("application/biff-media-trimmer-"));
    }
    if (supported) {
        event->acceptProposedAction();
    }
}

void TrimmerPanel::dropEvent(QDropEvent* event)
{
    if (!m_manager) {
        return;
    }
    for (const QUrl& url : event->mimeData()->urls()) {
        const QString path = url.toLocalFile();
        if (!path.isEmpty() && m_manager->assetByFilePath(path).isValid()) {
            pickAsset(path);
            event->acceptProposedAction();
            return;
        }
    }
    for (const QString& format : event->mimeData()->formats()) {
        if (!format.startsWith(QStringLiteral("application/biff-media-trimmer-"))) {
            continue;
        }
        const QString path = QString::fromUtf8(event->mimeData()->data(format));
        if (m_manager->assetByFilePath(path).isValid()) {
            pickAsset(path);
            event->acceptProposedAction();
            return;
        }
    }
}

void TrimmerPanel::pickAsset(const QString& filePath)
{
    if (m_manager) {
        openAsset(m_manager->assetByFilePath(filePath));
    }
}

} // namespace ui
} // namespace openvegas
