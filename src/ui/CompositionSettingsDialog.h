#pragma once

#include "app/AVTemplates.h"
#include "composition/Composition.h"

#include <QDialog>
#include <QVector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QTabWidget;
class QToolButton;

namespace openvegas {
namespace ui {

// Composite Shot Properties, laid out as the reference's
// CompositionSettingsDialog (FUN_14072b930, texts FUN_140730a50):
// Template (with Save / Delete), Name and Duration on top; a Standard tab
// with the Video group - Width, Height, Match Timeline, an editable Frame Rate
// and Aspect Ratio - and the Audio sample rate; an Advanced tab with Fog and
// Motion Blur; Cancel and OK at the foot.
class CompositionSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    struct Values
    {
        QString name;
        int width = 1920;
        int height = 1080;
        int fpsNumerator = 30;
        int fpsDenominator = 1;
        int pixelAspect = composition::Composition::SquarePixels;
        double customPixelAspect = 1.0;
        double durationSeconds = 10.0;
        int audioSampleRate = 48000;
        composition::CompositionRenderSettings render;

        bool operator==(const Values& o) const;
        bool operator!=(const Values& o) const { return !(*this == o); }
    };

    static Values fromComposition(const composition::Composition& composition);
    static void apply(const Values& values, composition::Composition& composition);

    // `timeline` is the editor timeline's format, which Match Timeline takes.
    CompositionSettingsDialog(const Values& values, const Values& timeline, QWidget* parent = nullptr);

    Values values() const;

private:
    void setFormat(int width, int height, int numerator, int denominator, int aspect, double custom);
    void refreshTemplates(const QString& selectId);
    // Selects the template the fields match, or "Custom".
    void matchTemplate();
    void applyTemplate(int index);
    void saveTemplate();
    void deleteTemplate();
    double frameRate() const;

    Values m_initial;
    Values m_timeline;
    QVector<app::AVTemplate> m_templates;
    bool m_applyingTemplate = false;

    QComboBox* m_template = nullptr;
    QToolButton* m_saveTemplate = nullptr;
    QToolButton* m_deleteTemplate = nullptr;
    QLineEdit* m_name = nullptr;
    QDoubleSpinBox* m_duration = nullptr;
    QTabWidget* m_tabs = nullptr;
    QSpinBox* m_width = nullptr;
    QSpinBox* m_height = nullptr;
    QComboBox* m_frameRate = nullptr;
    QComboBox* m_aspect = nullptr;
    QLabel* m_sampleRate = nullptr;
    QCheckBox* m_fogOn = nullptr;
    QDoubleSpinBox* m_fogNear = nullptr;
    QDoubleSpinBox* m_fogFar = nullptr;
    QDoubleSpinBox* m_fogDensity = nullptr;
    QComboBox* m_fogFalloff = nullptr;
    QToolButton* m_fogColor = nullptr;
    QColor m_fogColorValue;
    QCheckBox* m_blurOn = nullptr;
    QDoubleSpinBox* m_shutterAngle = nullptr;
    QDoubleSpinBox* m_shutterPhase = nullptr;
    QSpinBox* m_maxSamples = nullptr;
    QCheckBox* m_adaptive = nullptr;
};

} // namespace ui
} // namespace openvegas
