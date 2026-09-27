#include "ui/ViewerTransportBar.h"

#include <QHBoxLayout>
#include <QProgressBar>
#include <QIcon>
#include <QPainter>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyle>
#include <QToolButton>

class AudioLevelButton final : public QToolButton
{
public:
    explicit AudioLevelButton(QWidget* parent = nullptr) : QToolButton(parent)
    {
        setCheckable(true);
        setAutoRaise(true);
        setFixedSize(25, 20);
        setToolTip(QObject::tr("Audio Meters"));
        setAccessibleName(QObject::tr("Audio Meters"));
    }

    void setLevels(double left, double right)
    {
        m_left = qBound(0.0, left, 1.0);
        m_right = qBound(0.0, right, 1.0);
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        QToolButton::paintEvent(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, false);
        const QRect meter = rect().adjusted(4, 3, -4, -3);
        constexpr int segments = 7;
        const int gap = 1;
        const int segmentWidth = qMax(1, (meter.width() - (segments - 1) * gap) / segments);
        const double levels[] = {m_left, m_right};
        for (int channel = 0; channel < 2; ++channel) {
            const int y = meter.top() + channel * (meter.height() / 2 + 1);
            const int height = qMax(2, meter.height() / 2 - 1);
            const int lit = qRound(levels[channel] * segments);
            for (int i = 0; i < segments; ++i) {
                QColor color = i < 5 ? QColor(86, 174, 77)
                                     : (i == 5 ? QColor(218, 181, 57)
                                               : QColor(221, 76, 65));
                if (i >= lit) color = QColor(61, 69, 61);
                painter.fillRect(meter.left() + i * (segmentWidth + gap), y,
                                 segmentWidth, height, color);
            }
        }
    }

private:
    double m_left = 0.0;
    double m_right = 0.0;
};

namespace openvegas {
namespace ui {

// Icon button with the label kept as the tooltip; falls back to text when the
// SVG cannot be rasterised so a button is never blank. `objectName` is the
// reference's own name for the button, which is not derivable from the icon
// file this port happens to draw it with.
QToolButton* ViewerTransportBar::makeButton(const QString& iconName, const QString& label,
                                            const QString& objectName)
{
    auto* button = new QToolButton(this);
    const QIcon icon(QStringLiteral(":/icons/%1.svg").arg(iconName));
    if (!icon.isNull() && !icon.pixmap(QSize(18, 18)).isNull()) {
        button->setIcon(icon);
        button->setIconSize(QSize(18, 18));
    } else {
        button->setText(label);
    }
    button->setToolTip(label);
    button->setAutoRaise(true);
    button->setObjectName(objectName);
    return button;
}

ViewerTransportBar::ViewerTransportBar(QWidget* parent)
    : QWidget(parent)
{
    // Reference Viewer panel: a scrubber row with the current time on the left
    // and the duration on the right, then a button row - viewer actions left,
    // transport centred, readouts right.
    setObjectName(QStringLiteral("ViewerPlaybackWidget"));

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(6, 2, 6, 2);
    outer->setSpacing(2);

    // The reference puts a render progress bar above the two rows, styled
    // "viewer-render-progress" (141323858), and leaves it out of the way until
    // there is something to report. Same here: it is shown only while the
    // renderer is actually working on a frame.
    m_renderProgress = new QProgressBar(this);
    m_renderProgress->setObjectName(QStringLiteral("progressBarRender"));
    m_renderProgress->setProperty("special-style", QStringLiteral("viewer-render-progress"));
    m_renderProgress->setRange(0, 0);   // busy indicator: the port renders a frame at a time
    m_renderProgress->setTextVisible(false);
    m_renderProgress->setFixedHeight(3);
    m_renderProgress->hide();
    outer->addWidget(m_renderProgress);
    m_renderDelay.setSingleShot(true);
    connect(&m_renderDelay, &QTimer::timeout, this, [this] { m_renderProgress->show(); });

    // Reference container names for the two rows, carried as the style property
    // the original sets on them.
    auto* scrubRowWidget = new QWidget(this);
    scrubRowWidget->setProperty("special-style", QStringLiteral("viewer-toolbar-top"));
    auto* scrubRow = new QHBoxLayout(scrubRowWidget);
    scrubRow->setContentsMargins(0, 0, 0, 0);
    scrubRow->setSpacing(6);

    // The reference has an editable time field here (spinBoxCurrentTime, shared
    // with the timeline's transport), not a read-only label: typing a timecode
    // is how you jump to an exact frame. Kept as a QLineEdit with a mask rather
    // than a spin box, because a timecode is four fields and not one number.
    m_timecode = new QLineEdit(this);
    m_timecode->setObjectName(QStringLiteral("spinBoxCurrentTime"));
    m_timecode->setProperty("special-style", QStringLiteral("viewer-counter"));
    m_timecode->setToolTip(tr("Playhead Position"));
    m_timecode->setInputMask(QStringLiteral("99:99:99:99"));
    m_timecode->setText(QStringLiteral("00:00:00:00"));
    m_timecode->setAlignment(Qt::AlignCenter);
    m_timecode->setMinimumWidth(86);
    m_timecode->setMaximumWidth(96);
    connect(m_timecode, &QLineEdit::editingFinished, this, [this] {
        const double seconds = secondsFromTimecode(m_timecode->text());
        if (seconds >= 0.0) {
            emit scrubRequested(qBound(0.0, seconds, m_duration > 0.0 ? m_duration : seconds));
        }
    });
    scrubRow->addWidget(m_timecode);

    m_scrubber = new QSlider(Qt::Horizontal, this);
    m_scrubber->setObjectName(QStringLiteral("sliderCurrentFrame"));
    m_scrubber->setRange(0, 1000);
    m_scrubber->setValue(0);
    connect(m_scrubber, &QSlider::sliderMoved, this, [this](int v) {
        const double secs = (m_duration > 0.0) ? v * m_duration / 1000.0 : 0.0;
        emit scrubRequested(secs);
    });
    scrubRow->addWidget(m_scrubber, 1);

    // The reference's right-hand spinBoxFrameCount is editable: committing a
    // timecode changes the composition duration. Keep the four-field editor
    // consistent with spinBoxCurrentTime and let TimelineWidget own Undo/Redo.
    m_durationEdit = new QLineEdit(this);
    m_durationEdit->setObjectName(QStringLiteral("spinBoxFrameCount"));
    m_durationEdit->setProperty("special-style", QStringLiteral("viewer-counter"));
    m_durationEdit->setToolTip(tr("Timeline Duration"));
    m_durationEdit->setInputMask(QStringLiteral("99:99:99:99"));
    m_durationEdit->setText(QStringLiteral("00:00:00:00"));
    m_durationEdit->setMinimumWidth(86);
    m_durationEdit->setMaximumWidth(96);
    m_durationEdit->setAlignment(Qt::AlignCenter);
    connect(m_durationEdit, &QLineEdit::editingFinished, this, [this] {
        const double seconds = secondsFromTimecode(m_durationEdit->text());
        const double frame = m_fpsNumerator > 0
                                 ? double(m_fpsDenominator) / m_fpsNumerator
                                 : 1.0 / 30.0;
        if (seconds < frame) {
            m_durationEdit->setText(formatTimecode(m_duration));
            return;
        }
        if (qAbs(seconds - m_duration) < frame * 0.25) {
            m_durationEdit->setText(formatTimecode(m_duration));
            return;
        }
        emit durationChangeRequested(seconds);
    });
    scrubRow->addWidget(m_durationEdit);

    outer->addWidget(scrubRowWidget);

    auto* buttonRowWidget = new QWidget(this);
    buttonRowWidget->setProperty("special-style", QStringLiteral("viewer-toolbar-bottom"));
    auto* buttonRow = new QHBoxLayout(buttonRowWidget);
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->setSpacing(3);

    // Left cluster, in the reference's own build order and under its own button
    // names: toolButtonExportFrame, toolButtonLoop, toolButtonInFrame,
    // toolButtonOutFrame. The icon files are this port's; the names are the
    // reference's, and they are what the theme and the tests key off.
    QToolButton* snapBtn = makeButton(QStringLiteral("snapshot"), tr("Export Frame"),
                                      QStringLiteral("toolButtonExportFrame"));
    connect(snapBtn, &QToolButton::clicked, this, &ViewerTransportBar::snapshotRequested);
    buttonRow->addWidget(snapBtn);

    m_loopButton = makeButton(QStringLiteral("loop"), tr("Loop Playback (Ctrl+L)"),
                              QStringLiteral("toolButtonLoop"));
    m_loopButton->setCheckable(true);
    connect(m_loopButton, &QToolButton::toggled, this, &ViewerTransportBar::loopToggled);
    buttonRow->addWidget(m_loopButton);

    QToolButton* inBtn = makeButton(QStringLiteral("mark-in"), tr("Set In Point (I)"),
                                    QStringLiteral("toolButtonInFrame"));
    connect(inBtn, &QToolButton::clicked, this, &ViewerTransportBar::setInPointRequested);
    buttonRow->addWidget(inBtn);

    QToolButton* outBtn = makeButton(QStringLiteral("mark-out"), tr("Set Out Point (O)"),
                                     QStringLiteral("toolButtonOutFrame"));
    connect(outBtn, &QToolButton::clicked, this, &ViewerTransportBar::setOutPointRequested);
    buttonRow->addWidget(outBtn);

    buttonRow->addStretch(1);
    m_buttonRow = buttonRow;

    // Centre: first frame, previous, next, play - the four the reference shows
    // under the frame, in that order.
    QToolButton* startBtn = makeButton(QStringLiteral("to-start"), tr("Go to start"),
                                       QStringLiteral("toolButtonFirstFrame"));
    connect(startBtn, &QToolButton::clicked, this, &ViewerTransportBar::stopRequested);
    buttonRow->addWidget(startBtn);

    QToolButton* prevBtn = makeButton(QStringLiteral("step-back"), tr("Step one frame backward"),
                                      QStringLiteral("toolButtonPreviousFrame"));
    connect(prevBtn, &QToolButton::clicked, this, &ViewerTransportBar::stepBackRequested);
    buttonRow->addWidget(prevBtn);

    QToolButton* nextBtn = makeButton(QStringLiteral("step-fwd"), tr("Step one frame forward"),
                                      QStringLiteral("toolButtonNextFrame"));
    connect(nextBtn, &QToolButton::clicked, this, &ViewerTransportBar::stepForwardRequested);
    buttonRow->addWidget(nextBtn);

    m_playButton = makeButton(QStringLiteral("play"), tr("Play / Pause"),
                              QStringLiteral("toolButtonPlay"));
    connect(m_playButton, &QToolButton::clicked, this,
            [this] { emit playPauseRequested(); });
    buttonRow->addWidget(m_playButton);

    buttonRow->addStretch(1);

    // The reference does not label this control "Audio Meters".  It is a
    // live two-channel master meter placed immediately before Options; clicking
    // it still opens the full Meters panel.
    m_metersButton = new AudioLevelButton(buttonRowWidget);
    m_metersButton->setObjectName(QStringLiteral("audioMeterIcon"));
    connect(m_metersButton, &QToolButton::toggled,
            this, &ViewerTransportBar::metersToggled);
    buttonRow->addWidget(m_metersButton);
    outer->addWidget(buttonRowWidget);

    setFixedHeight(60);
}

// Inverse of formatTimecode: HH:MM:SS:FF back into seconds. Returns -1 for
// anything the mask let through that is not a real time, so a half-typed field
// does not move the playhead.
double ViewerTransportBar::secondsFromTimecode(const QString& text) const
{
    const QStringList parts = text.split(QLatin1Char(':'));
    if (parts.size() != 4) {
        return -1.0;
    }
    bool ok = false;
    const int h = parts.at(0).toInt(&ok);
    if (!ok) {
        return -1.0;
    }
    const int m = parts.at(1).toInt(&ok);
    if (!ok || m > 59) {
        return -1.0;
    }
    const int s = parts.at(2).toInt(&ok);
    if (!ok || s > 59) {
        return -1.0;
    }
    const int f = parts.at(3).toInt(&ok);
    if (!ok) {
        return -1.0;
    }
    const double fps = (m_fpsDenominator > 0) ? double(m_fpsNumerator) / m_fpsDenominator : 30.0;
    if (f >= int(fps + 0.5)) {
        return -1.0;
    }
    return h * 3600.0 + m * 60.0 + s + (fps > 0.0 ? f / fps : 0.0);
}

QString ViewerTransportBar::formatTimecode(double seconds) const
{
    if (seconds < 0.0) {
        seconds = 0.0;
    }
    const double fps = (m_fpsDenominator > 0) ? double(m_fpsNumerator) / m_fpsDenominator : 30.0;
    const int frames = int(seconds * fps + 0.5);
    const int ff = frames % int(fps + 0.5);
    const int totalSec = frames / int(fps + 0.5);
    const int h = totalSec / 3600;
    const int m = (totalSec % 3600) / 60;
    const int s = totalSec % 60;
    return QStringLiteral("%1:%2:%3:%4")
        .arg(h, 2, 10, QLatin1Char('0'))
        .arg(m, 2, 10, QLatin1Char('0'))
        .arg(s, 2, 10, QLatin1Char('0'))
        .arg(ff, 2, 10, QLatin1Char('0'));
}

void ViewerTransportBar::setFrameRate(int numerator, int denominator)
{
    m_fpsNumerator = numerator;
    m_fpsDenominator = denominator > 0 ? denominator : 1;
    if (m_timecode && !m_timecode->hasFocus()) {
        m_timecode->setText(formatTimecode(m_timeSeconds));
    }
    if (m_durationEdit && !m_durationEdit->hasFocus()) {
        m_durationEdit->setText(formatTimecode(m_duration));
    }
}

void ViewerTransportBar::setDuration(double seconds)
{
    m_duration = seconds > 0.0 ? seconds : 0.0;
    m_timeSeconds = qBound(0.0, m_timeSeconds, m_duration);
    if (m_scrubber) {
        m_scrubber->setEnabled(m_duration > 0.0);
        const int value = m_duration > 0.0
                              ? qRound(1000.0 * m_timeSeconds / m_duration)
                              : 0;
        m_scrubber->setValue(value);
    }
    if (m_durationEdit && !m_durationEdit->hasFocus()) {
        m_durationEdit->setText(formatTimecode(m_duration));
    }
    if (m_timecode && !m_timecode->hasFocus()) {
        m_timecode->setText(formatTimecode(m_timeSeconds));
    }
}

void ViewerTransportBar::setTimecode(double seconds)
{
    m_timeSeconds = m_duration > 0.0
                        ? qBound(0.0, seconds, m_duration)
                        : qMax(0.0, seconds);
    if (m_timecode && !m_timecode->hasFocus()) {
        // Left alone while it is being typed into, so a keystroke is not
        // overwritten by the playhead moving underneath.
        m_timecode->setText(formatTimecode(m_timeSeconds));
    }
    if (m_scrubber && m_duration > 0.0) {
        const int v = qRound(1000.0 * m_timeSeconds / m_duration);
        if (v != m_scrubber->value()) {
            m_scrubber->setValue(v);
        }
    }
}

void ViewerTransportBar::setPlaying(bool playing)
{
    m_playing = playing;
    if (!m_playButton) {
        return;
    }
    m_playButton->setChecked(playing);
    const QIcon icon(playing ? QStringLiteral(":/icons/pause.svg")
                             : QStringLiteral(":/icons/play.svg"));
    if (!icon.isNull() && !icon.pixmap(QSize(18, 18)).isNull()) {
        m_playButton->setIcon(icon);
    } else {
        m_playButton->setText(playing ? tr("Pause") : tr("Play"));
    }
    m_playButton->setToolTip(playing ? tr("Pause") : tr("Play"));
}

void ViewerTransportBar::setLooping(bool on)
{
    if (m_loopButton) {
        QSignalBlocker block(m_loopButton);   // no echo back to the action
        m_loopButton->setChecked(on);
    }
}

void ViewerTransportBar::setMetersVisible(bool visible)
{
    if (!m_metersButton) return;
    QSignalBlocker block(m_metersButton);
    m_metersButton->setChecked(visible);
}

void ViewerTransportBar::setAudioLevels(double left, double right)
{
    if (m_metersButton) m_metersButton->setLevels(left, right);
}

void ViewerTransportBar::addTrailingWidget(QWidget* widget)
{
    if (!widget || !m_buttonRow) {
        return;
    }
    // Reparented onto the row's own container, not onto the bar: the row is a
    // widget of its own now (it carries the reference's "viewer-toolbar-bottom"
    // style), and a child of the bar laid out by that row would be positioned
    // relative to the wrong parent.
    if (QWidget* rowWidget = m_buttonRow->parentWidget()) {
        widget->setParent(rowWidget);
    } else {
        widget->setParent(this);
    }
    m_buttonRow->addWidget(widget);
}

void ViewerTransportBar::setRendering(bool rendering)
{
    if (!m_renderProgress) {
        return;
    }
    if (!rendering) {
        m_renderDelay.stop();
        m_renderProgress->hide();
        return;
    }
    // Shown only once a frame has taken a noticeable while. Every playback tick
    // asks for a render, so switching the bar on the instant one starts would
    // strobe it a dozen times a second and tell nobody anything.
    if (!m_renderProgress->isVisible() && !m_renderDelay.isActive()) {
        m_renderDelay.start(250);
    }
}

} // namespace ui
} // namespace openvegas
