#include "ui/AudioMetersPanel.h"
#include "ui_AudioMeters.h"

#include "app/Settings.h"

#include <QCheckBox>
#include <QColor>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QSettings>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

class AudioMeterDisplay : public QWidget
{
public:
    explicit AudioMeterDisplay(QWidget* parent = nullptr) : QWidget(parent)
    {
        setMinimumSize(32, 90);
        QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                           openvegas::app::Settings::organizationName(),
                           openvegas::app::Settings::applicationName());
        m_background = QColor(settings.value(QStringLiteral("Theme/meterBackgroundColor"),
                                             QStringLiteral("#111111")).toString());
        m_peakColor = QColor(settings.value(QStringLiteral("Theme/holdPeakLineColor"),
                                            QStringLiteral("#eeeeee")).toString());
    }

    void setLevels(double left, double right)
    {
        m_left = qBound(0.0, left, 1.0);
        m_right = qBound(0.0, right, 1.0);
        m_peakLeft = qMax(m_peakLeft, m_left);
        m_peakRight = qMax(m_peakRight, m_right);
        update();
    }
    void setHoldPeaks(bool hold) { m_hold = hold; if (!hold) resetPeaks(); update(); }
    void resetPeaks() { m_peakLeft = m_left; m_peakRight = m_right; update(); }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), m_background);
        constexpr int ledCount = 20;
        const double channelWidth = width() / 2.0;
        const double ledHeight = height() / double(ledCount);
        const double levels[] = {m_left, m_right};
        const double peaks[] = {m_peakLeft, m_peakRight};
        for (int channel = 0; channel < 2; ++channel) {
            const int lit = qRound(levels[channel] * ledCount);
            for (int led = 0; led < ledCount; ++led) {
                QColor color = led < 15 ? QColor(48, 150, 64)
                                        : (led < 18 ? QColor(220, 180, 55)
                                                    : QColor(225, 70, 55));
                if (led >= lit) color = m_background.lighter(150);
                painter.fillRect(QRectF(channel * channelWidth + 2,
                                        height() - (led + 1) * ledHeight + 1,
                                        channelWidth - 4, qMax(1.0, ledHeight - 2)), color);
            }
            if (m_hold && peaks[channel] > 0.0) {
                painter.fillRect(QRectF(channel * channelWidth + 2,
                                        height() - peaks[channel] * height(),
                                        channelWidth - 4, 2), m_peakColor);
            }
        }
    }

private:
    double m_left = 0.0;
    double m_right = 0.0;
    double m_peakLeft = 0.0;
    double m_peakRight = 0.0;
    bool m_hold = true;
    QColor m_background;
    QColor m_peakColor;
};

namespace openvegas {
namespace ui {

AudioMetersPanel::AudioMetersPanel(QWidget* parent) : QDockWidget(parent)
{
    Ui::AudioMetersPanel form;
    form.setupUi(this);
    m_holdPeaks = form.AudioMetersHoldPeaks;
    QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                       app::Settings::organizationName(), app::Settings::applicationName());
    m_holdPeaks->setChecked(settings.value(QStringLiteral("Options/AudioMetersHoldPeaks"),
                                           true).toBool());
    QToolButton* reset = form.toolButtonResetPeaks;
    connect(reset, &QToolButton::clicked, this, &AudioMetersPanel::resetPeaks);
    const int inputIndex = form.inputColumn->indexOf(form.InputLevels);
    form.inputColumn->removeWidget(form.InputLevels);
    delete form.InputLevels;
    m_inputMeter = new AudioMeterDisplay(form.AudioMetersWidget);
    m_inputMeter->setObjectName(QStringLiteral("InputLevels"));
    form.inputColumn->insertWidget(inputIndex, m_inputMeter, 1);
    const int outputIndex = form.outputColumn->indexOf(form.OutputLevels);
    form.outputColumn->removeWidget(form.OutputLevels);
    delete form.OutputLevels;
    m_outputMeter = new AudioMeterDisplay(form.AudioMetersWidget);
    m_outputMeter->setObjectName(QStringLiteral("OutputLevels"));
    form.outputColumn->insertWidget(outputIndex, m_outputMeter, 1);

    connect(m_holdPeaks, &QCheckBox::toggled, this, [this](bool enabled) {
        setHoldPeaks(enabled);
        QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                           app::Settings::organizationName(), app::Settings::applicationName());
        settings.setValue(QStringLiteral("Options/AudioMetersHoldPeaks"), enabled);
    });
    setHoldPeaks(m_holdPeaks->isChecked());
}

QSize AudioMetersPanel::minimumSizeHint() const { return QSize(90, 150); }
void AudioMetersPanel::setLevels(double left, double right) { setOutputLevels(left, right); }
void AudioMetersPanel::setInputLevels(double left, double right) { m_inputMeter->setLevels(left, right); }
void AudioMetersPanel::setOutputLevels(double left, double right) { m_outputMeter->setLevels(left, right); }
void AudioMetersPanel::setHoldPeaks(bool enabled)
{
    m_inputMeter->setHoldPeaks(enabled);
    m_outputMeter->setHoldPeaks(enabled);
    if (m_holdPeaks->isChecked() != enabled) m_holdPeaks->setChecked(enabled);
}
bool AudioMetersPanel::holdPeaks() const { return m_holdPeaks->isChecked(); }
void AudioMetersPanel::resetPeaks() { m_inputMeter->resetPeaks(); m_outputMeter->resetPeaks(); }

} // namespace ui
} // namespace openvegas
