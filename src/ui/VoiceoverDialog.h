#pragma once
#include <QDialog>
#include "media/AudioCapture.h"
#include <memory>
#include <QTemporaryFile>
#include <QTimer>
#include <QElapsedTimer>
#include <functional>
class QLabel;
class QPushButton;

namespace openvegas::ui {
class VoiceoverDialog : public QDialog
{
    Q_OBJECT
public:
    explicit VoiceoverDialog(const QString& outputPath, QWidget* parent = nullptr);
    ~VoiceoverDialog() override;
    // Transport starts only after the countdown and stops on every exit path.
    std::function<void()> recordingStarted;
    std::function<void()> recordingStopped;
    void reject() override;
private:
    void start();
    void capture();
    void finish();
    void stopCapture();
    QString m_outputPath;
    QTemporaryFile m_pcm;
    media::PcmFormat m_format;
    std::unique_ptr<media::AudioCapture> m_source;
    QLabel* m_status = nullptr;
    QPushButton* m_record = nullptr;
    QPushButton* m_stop = nullptr;
    QTimer m_timer;
    QElapsedTimer m_elapsed;
    int m_countdown = 0;
    bool m_recording = false;
};
}
