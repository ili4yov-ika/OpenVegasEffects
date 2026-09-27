#pragma once

#include <QTimer>
#include <QWidget>

class QHBoxLayout;
class QLineEdit;
class QProgressBar;
class QSlider;
class QToolButton;
class AudioLevelButton;

namespace openvegas {
namespace ui {

// Inline transport bar under the Viewer (reference: Transport Controls with
// timecode, scrubber and Start/Prev/Play-Pause/Next/End transport buttons).
class ViewerTransportBar : public QWidget
{
    Q_OBJECT

public:
    // Keeps the button in step when the toggle is flipped from its shortcut.
    void setLooping(bool on);
    // Places a widget at the right end of the button row. The reference keeps
    // the viewer's view options there, on the transport line, rather than in a
    // bar of their own above the canvas.
    void addTrailingWidget(QWidget* widget);

    explicit ViewerTransportBar(QWidget* parent = nullptr);

    void setTimecode(double seconds);
    void setDuration(double seconds);
    void setFrameRate(int numerator, int denominator);
    void setMetersVisible(bool visible);
    void setAudioLevels(double left, double right);

public slots:
    void setPlaying(bool playing);
    // Reference "viewer-render-progress" (141323858): the thin bar above the
    // transport, shown only while a frame is being rendered.
    void setRendering(bool rendering);

signals:
    void playPauseRequested();
    void stopRequested();
    // Reference "Export Frame": writes the displayed frame into the snapshot
    // directory.
    void snapshotRequested();
    // Loop playback toggle, second in the reference's left cluster.
    void loopToggled(bool on);
    void setInPointRequested();
    void setOutPointRequested();
    void stepBackRequested();
    void stepForwardRequested();
    // Scrubber moved by the user to an absolute time (seconds).
    void scrubRequested(double seconds);
    // User committed a new HH:MM:SS:FF value in the right-hand Timeline
    // Duration editor.
    void durationChangeRequested(double seconds);
    void metersToggled(bool visible);

private:
    QHBoxLayout* m_buttonRow = nullptr;
    QToolButton* m_loopButton = nullptr;
    QString formatTimecode(double seconds) const;
    // Parses what the user typed into the time field; -1 when it is not a
    // complete, valid timecode.
    double secondsFromTimecode(const QString& text) const;
    QToolButton* makeButton(const QString& iconName, const QString& label,
                            const QString& objectName);

    QLineEdit* m_timecode = nullptr;
    QLineEdit* m_durationEdit = nullptr;
    QSlider* m_scrubber = nullptr;
    QProgressBar* m_renderProgress = nullptr;
    // Delays the progress bar so a fast frame never shows it.
    QTimer m_renderDelay;
    double m_duration = 0.0;
    double m_timeSeconds = 0.0;
    int m_fpsNumerator = 30;
    int m_fpsDenominator = 1;
    bool m_playing = false;
    QToolButton* m_playButton = nullptr;
    AudioLevelButton* m_metersButton = nullptr;
};

} // namespace ui
} // namespace openvegas
