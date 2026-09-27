#pragma once

#include <QDockWidget>

class QCheckBox;
class AudioMeterDisplay;

namespace openvegas {
namespace ui {

// Mirrors the reference "Meters" panel (type 2051), reference class
// biff::ui::AudioMetersPanel: narrow strip of audio level meters.
class AudioMetersPanel : public QDockWidget
{
    Q_OBJECT

public:
    explicit AudioMetersPanel(QWidget* parent = nullptr);

    // Levels in [0.0, 1.0]. 0 ch = mono / left, 1 ch = right.
    void setLevels(double left, double right);
    void setInputLevels(double left, double right);
    void setOutputLevels(double left, double right);
    void setHoldPeaks(bool enabled);
    bool holdPeaks() const;
    QSize minimumSizeHint() const override;

public slots:
    void resetPeaks();

private:
    AudioMeterDisplay* m_inputMeter = nullptr;
    AudioMeterDisplay* m_outputMeter = nullptr;
    QCheckBox* m_holdPeaks = nullptr;
};

} // namespace ui
} // namespace openvegas
