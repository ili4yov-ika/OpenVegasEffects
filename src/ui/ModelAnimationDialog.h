#pragma once

#include <QDialog>
#include <QString>

#include "composition/Layer.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace openvegas::ui {

// "3D Model Animation Properties": the reference's
// biff::ui::media::AnimationSettingsDialog, whose own strings name its rows -
// Name, Path (with Relink / "Relink Animation"), Animation (one of the file's,
// "Invalid Animation" when it holds none), Frame Rate ("From File", 23.976,
// 24, 25 (PAL), 29.97 (NTSC), 30, 50, 59.94, 60) and Duration as a timecode.
// It edits what the reference keeps on the Model3DAnimationAsset an instance
// plays: its file, AnimationIndex and frame rate override.
class ModelAnimationDialog : public QDialog
{
    Q_OBJECT

public:
    // `modelFile`: the layer's model, whose animations an instance without a
    // file of its own plays.
    ModelAnimationDialog(const composition::ModelAnimationInstance& instance, const QString& modelFile,
                         QWidget* parent = nullptr);

    // The instance as edited.
    composition::ModelAnimationInstance instance() const;
    // What Relink does once a file is chosen: the animation comes from it.
    void setFilePath(const QString& path);
    // "HH:MM:SS:FF" at `rate` frames a second for `seconds`.
    static QString timecode(double seconds, double rate);

private:
    void readFile();
    void showDuration();

    composition::ModelAnimationInstance m_instance;
    QString m_modelFile;
    QLineEdit* m_name = nullptr;
    QLineEdit* m_path = nullptr;
    QPushButton* m_relink = nullptr;
    QComboBox* m_animation = nullptr;
    QComboBox* m_rate = nullptr;
    QLabel* m_duration = nullptr;
    QLabel* m_problem = nullptr;
    QVector<double> m_durations;   // seconds, per animation in the file
    QVector<double> m_rates;       // and the rate its frames are counted in
};

} // namespace openvegas::ui
