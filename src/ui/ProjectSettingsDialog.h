#pragma once

#include "composition/Composition.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QTabWidget;

namespace openvegas {
namespace ui {

// The reference's ProjectSettingsDialog (setupUi FUN_1402aeb30, "Project
// Settings", or "New Project Settings" when a project is being created,
// FUN_1402b3b30). The Editor tab holds the editor timeline's video and audio
// format; Rendering holds Project/ProjectSettings - colour bit depth,
// antialiasing and the 3D map sizes - each tab with Restore Defaults taking
// the values Options would give a new project.
class ProjectSettingsDialog : public QDialog
{
    Q_OBJECT
public:
    struct Values
    {
        int width = 1920;
        int height = 1080;
        double fps = 30.0;
        double durationSeconds = 30.0;
        int sampleRate = 48000;
        composition::ProjectRenderSettings render;
    };

    explicit ProjectSettingsDialog(const Values& values, bool newProject, QWidget* parent = nullptr);
    Values values() const;

    // Read from and written to the root composition: its editor sequence
    // carries the Editor tab, its project settings the Rendering tab.
    static Values fromComposition(const composition::Composition& composition);
    static void apply(const Values& values, composition::Composition& composition);

private:
    void restoreEditorDefaults();
    void restoreRenderingDefaults();
    void setRendering(const composition::ProjectRenderSettings& render);

    QTabWidget* m_tabs = nullptr;
    QSpinBox* m_width = nullptr;
    QSpinBox* m_height = nullptr;
    QCheckBox* m_preserveAspect = nullptr;
    QComboBox* m_frameRate = nullptr;
    QDoubleSpinBox* m_duration = nullptr;
    QComboBox* m_sampleRate = nullptr;
    QComboBox* m_bitDepth = nullptr;
    QComboBox* m_antialiasing = nullptr;
    QSpinBox* m_reflection = nullptr;
    QSpinBox* m_shadow = nullptr;
    QSpinBox* m_modelMaps = nullptr;
    composition::ProjectRenderSettings m_render;
    double m_aspect = 16.0 / 9.0;
};

} // namespace ui
} // namespace openvegas
