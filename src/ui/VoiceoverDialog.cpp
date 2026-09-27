#include "ui/VoiceoverDialog.h"
#include "app/Settings.h"
#include "media/PcmWave.h"
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QSlider>
#include <QVBoxLayout>

namespace openvegas::ui {
VoiceoverDialog::VoiceoverDialog(const QString& path, QWidget* parent)
    : QDialog(parent), m_outputPath(path)
{
    setWindowTitle(tr("Record Voiceover"));
    setObjectName(QStringLiteral("voiceoverDialog"));
    auto* layout = new QVBoxLayout(this);
    auto* destination = new QLabel(path, this);
    destination->setWordWrap(true);
    layout->addWidget(destination);
    m_status = new QLabel(tr("Ready to record"), this);
    m_status->setObjectName(QStringLiteral("voiceoverStatus"));
    m_status->setWordWrap(true);
    layout->addWidget(m_status);
    auto* gainLabel = new QLabel(this);
    auto* gain = new QSlider(Qt::Horizontal, this);
    gain->setObjectName(QStringLiteral("voiceoverInputVolume"));
    gain->setRange(0, 100);
    gain->setValue(qBound(0, app::Settings::optionSettings().value(QStringLiteral("Options/VoiceoverVolume"), 100).toInt(), 100));
    gainLabel->setText(tr("Input volume: %1%").arg(gain->value()));
    layout->addWidget(gainLabel); layout->addWidget(gain);
    connect(gain, &QSlider::valueChanged, this, [this, gainLabel](int value) {
        gainLabel->setText(tr("Input volume: %1%").arg(value));
        app::Settings::optionSettings().setValue(QStringLiteral("Options/VoiceoverVolume"), value);
        if (m_source) m_source->setVolume(value / 100.0);
    });
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_record = buttons->addButton(tr("Record"), QDialogButtonBox::ActionRole);
    m_record->setObjectName(QStringLiteral("voiceoverRecord"));
    m_stop = buttons->addButton(tr("Stop and save"), QDialogButtonBox::ActionRole);
    m_stop->setObjectName(QStringLiteral("voiceoverStop"));
    m_stop->setEnabled(false);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &VoiceoverDialog::reject);
    connect(m_record, &QPushButton::clicked, this, &VoiceoverDialog::start);
    connect(m_stop, &QPushButton::clicked, this, &VoiceoverDialog::finish);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        if (!m_recording) {
            if (--m_countdown <= 0) capture();
            else m_status->setText(tr("Recording starts in %1…").arg(m_countdown));
        } else if (m_source && m_source->failed()) {
            stopCapture();
            m_status->setText(tr("Audio recording failed. Check the device and microphone permissions."));
        } else m_status->setText(tr("Recording: %1 seconds").arg(m_elapsed.elapsed() / 1000));
    });
    resize(460, 140);
}
VoiceoverDialog::~VoiceoverDialog() { stopCapture(); }
void VoiceoverDialog::start()
{
    const QSettings settings = app::Settings::optionSettings();
    if (!media::vlcInstance()) { m_status->setText(media::vlcDescription()); return; }
    m_format.setSampleFormat(media::PcmFormat::Int16);
    m_format.setChannelCount(settings.value(QStringLiteral("Options/Voiceover/Channels"), 1).toInt());
    m_format.setSampleRate(settings.value(QStringLiteral("Options/Voiceover/SampleRate"), 48000).toInt());
    if (!m_format.isValid()) {
        m_status->setText(tr("The selected audio format is unsupported. Change Voiceover settings.")); return;
    }
    if (!m_pcm.isOpen() && !m_pcm.open()) { m_status->setText(m_pcm.errorString()); return; }
    m_pcm.resize(0); m_pcm.seek(0);
    m_source = std::make_unique<media::AudioCapture>();
    m_source->setVolume(findChild<QSlider*>(QStringLiteral("voiceoverInputVolume"))->value() / 100.0);
    m_record->setEnabled(false);
    m_countdown = qBound(0, settings.value(QStringLiteral("Options/Voiceover/Countdown"), 3).toInt(), 10);
    m_timer.start(1000);
    if (m_countdown == 0) capture();
    else m_status->setText(tr("Recording starts in %1…").arg(m_countdown));
}
void VoiceoverDialog::capture()
{
    m_recording = true;
    m_elapsed.start();
    m_stop->setEnabled(true);
    m_status->setText(tr("Recording: %1 seconds").arg(0));
    if (recordingStarted) recordingStarted();
    const QString device = app::Settings::optionSettings().value(QStringLiteral("Options/Voiceover/Device"), "default").toString();
    if (!m_source->start(device, m_format, &m_pcm)) {
        stopCapture();
        m_status->setText(tr("Audio input device is unavailable."));
    }
}
void VoiceoverDialog::stopCapture()
{
    m_timer.stop();
    const bool wasRecording = m_recording;
    m_recording = false; // Ignore the deliberate StoppedState notification.
    if (m_source) m_source->stop();
    if (wasRecording && recordingStopped) recordingStopped();
    m_record->setEnabled(true); m_stop->setEnabled(false);
}
void VoiceoverDialog::finish()
{
    stopCapture();
    QSaveFile file(m_outputPath);
    if (!file.open(QIODevice::WriteOnly)) { m_status->setText(file.errorString()); return; }
    if (!media::writePcmWave(m_pcm, file, m_format)) {
        file.cancelWriting(); m_status->setText(tr("No complete audio samples were recorded.")); return;
    }
    if (!file.commit()) { m_status->setText(file.errorString()); return; }
    accept();
}
void VoiceoverDialog::reject() { stopCapture(); QDialog::reject(); }
}
