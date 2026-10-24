#include "ui/ModelAnimationDialog.h"

#include "model3d/ModelLoader.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <cmath>

// 3D Model Animation Properties: one animation of a model layer - its name,
// the file it comes from (the model itself, or another file relinked to it),
// which of the file's animation clips it plays, and the frame rate it plays
// at (the file's, or one chosen). The duration shown follows the choices.

namespace openvegas::ui {

namespace {

// The reference's context for the dialog's strings, so its catalogs apply.
QString text(const char* source)
{
    return QCoreApplication::translate("AnimationSettingsDialog", source);
}

} // namespace

ModelAnimationDialog::ModelAnimationDialog(const composition::ModelAnimationInstance& instance,
                                           const QString& modelFile, QWidget* parent)
    : QDialog(parent), m_instance(instance), m_modelFile(modelFile)
{
    setObjectName(QStringLiteral("modelAnimationDialog"));
    setWindowTitle(QCoreApplication::translate("biff::ui::media::AnimationSettingsDialog",
                                               "3D Model Animation Properties"));
    setMinimumWidth(480);   // room for the path
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    layout->addLayout(form);

    m_name = new QLineEdit(instance.name, this);
    m_name->setObjectName(QStringLiteral("lineEditAnimationName"));
    form->addRow(text(QT_TRANSLATE_NOOP("AnimationSettingsDialog", "Name:")), m_name);

    auto* pathRow = new QHBoxLayout;
    m_path = new QLineEdit(this);
    m_path->setObjectName(QStringLiteral("lineEditAnimationPath"));
    m_path->setReadOnly(true);
    pathRow->addWidget(m_path, 1);
    m_relink = new QPushButton(text(QT_TRANSLATE_NOOP("AnimationSettingsDialog", "Relink")), this);
    m_relink->setObjectName(QStringLiteral("pushButtonRelinkAnimation"));
    pathRow->addWidget(m_relink);
    form->addRow(text(QT_TRANSLATE_NOOP("AnimationSettingsDialog", "Path:")), pathRow);

    m_animation = new QComboBox(this);
    m_animation->setObjectName(QStringLiteral("comboBoxAnimation"));
    form->addRow(text(QT_TRANSLATE_NOOP("AnimationSettingsDialog", "Animation")), m_animation);

    // "From File" (0) keeps the file's rate; an override that is none of the
    // listed rates gets an item of its own so it is shown as it is.
    m_rate = new QComboBox(this);
    m_rate->setObjectName(QStringLiteral("comboBoxAnimationFrameRate"));
    m_rate->addItem(text(QT_TRANSLATE_NOOP("AnimationSettingsDialog", "From File")), 0.0);
    const struct { const char* label; double rate; } rates[] = {
        {QT_TRANSLATE_NOOP("AnimationSettingsDialog", "23.976"), 23.976},
        {QT_TRANSLATE_NOOP("AnimationSettingsDialog", "24"), 24.0},
        {QT_TRANSLATE_NOOP("AnimationSettingsDialog", "25 (PAL)"), 25.0},
        {QT_TRANSLATE_NOOP("AnimationSettingsDialog", "29.97 (NTSC)"), 29.97},
        {QT_TRANSLATE_NOOP("AnimationSettingsDialog", "30"), 30.0},
        {QT_TRANSLATE_NOOP("AnimationSettingsDialog", "50"), 50.0},
        {QT_TRANSLATE_NOOP("AnimationSettingsDialog", "59.94"), 59.94},
        {QT_TRANSLATE_NOOP("AnimationSettingsDialog", "60"), 60.0},
    };
    for (const auto& rate : rates) m_rate->addItem(text(rate.label), rate.rate);
    if (instance.frameRateOverridden && instance.frameRate > 0.0) {
        int found = -1;
        for (int i = 1; i < m_rate->count(); ++i)
            if (std::abs(m_rate->itemData(i).toDouble() - instance.frameRate) < 0.001) found = i;
        if (found < 0) {
            m_rate->addItem(QString::number(instance.frameRate, 'g', 6), instance.frameRate);
            found = m_rate->count() - 1;
        }
        m_rate->setCurrentIndex(found);
    }
    form->addRow(text(QT_TRANSLATE_NOOP("AnimationSettingsDialog", "Frame Rate:")), m_rate);

    m_duration = new QLabel(this);
    m_duration->setObjectName(QStringLiteral("labelAnimationDuration"));
    form->addRow(text(QT_TRANSLATE_NOOP("AnimationSettingsDialog", "Duration:")), m_duration);

    m_problem = new QLabel(this);
    m_problem->setObjectName(QStringLiteral("labelAnimationProblem"));
    m_problem->setWordWrap(true);
    m_problem->hide();
    layout->addWidget(m_problem);

    auto* buttons = new QDialogButtonBox(this);
    buttons->addButton(text(QT_TRANSLATE_NOOP("AnimationSettingsDialog", "OK")), QDialogButtonBox::AcceptRole);
    buttons->addButton(text(QT_TRANSLATE_NOOP("AnimationSettingsDialog", "Cancel")), QDialogButtonBox::RejectRole);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(m_relink, &QPushButton::clicked, this, [this] {
        const QString start = QFileInfo(m_instance.filePath.isEmpty() ? m_modelFile : m_instance.filePath).absolutePath();
        const QString chosen = QFileDialog::getOpenFileName(this, text(QT_TRANSLATE_NOOP("AnimationSettingsDialog", "Relink Animation")), start,
                                                            model3d::animationFileFilter());
        if (!chosen.isEmpty()) setFilePath(chosen);
    });
    connect(m_animation, &QComboBox::currentIndexChanged, this, [this] { showDuration(); });
    connect(m_rate, &QComboBox::currentIndexChanged, this, [this] { showDuration(); });
    readFile();
}

// Relinks the animation to a file. Choosing the model's own file stores no
// path, so the animation follows the model if that is relinked later.
void ModelAnimationDialog::setFilePath(const QString& path)
{
    m_instance.filePath = QFileInfo(path).absoluteFilePath() == QFileInfo(m_modelFile).absoluteFilePath()
        ? QString() : path;
    m_instance.animationIndex = 0;
    readFile();
}

// Lists the animation clips of the current file with their durations and
// rates, or says why there are none (missing file, unreadable animation).
void ModelAnimationDialog::readFile()
{
    const QString file = m_instance.filePath.isEmpty() ? m_modelFile : m_instance.filePath;
    m_path->setText(QDir::toNativeSeparators(file));
    const QSignalBlocker block(m_animation);
    m_animation->clear();
    m_durations.clear();
    m_rates.clear();
    m_problem->hide();
    if (!QFileInfo::exists(file)) {
        m_problem->setText(text(QT_TRANSLATE_NOOP("AnimationSettingsDialog", "The animation file could not be found.")));
        m_problem->show();
    } else if (const auto source = model3d::animationSource(file)) {
        for (const model3d::ModelAnimation& clip : source->animations) {
            m_animation->addItem(clip.name);
            m_durations.append(clip.duration);
            m_rates.append(clip.frameRate);
        }
    } else {
        m_problem->setText(text(QT_TRANSLATE_NOOP("AnimationSettingsDialog", "The animation could not be imported.")));
        m_problem->show();
    }
    if (m_animation->count() == 0) {
        m_animation->addItem(text(QT_TRANSLATE_NOOP("AnimationSettingsDialog", "Invalid Animation")));
        m_animation->setEnabled(false);
    } else {
        m_animation->setEnabled(true);
        m_animation->setCurrentIndex(qBound(0, m_instance.animationIndex, m_animation->count() - 1));
    }
    showDuration();
}

void ModelAnimationDialog::showDuration()
{
    const int index = m_animation->currentIndex();
    if (index < 0 || index >= m_durations.size()) {
        m_duration->setText(timecode(0.0, 24.0));
        return;
    }
    // Its frames (in the file's rate, as the reference counts them) at the
    // rate it plays at.
    composition::ModelAnimationInstance edited = instance();
    const double fileRate = m_rates.value(index, model3d::kModelAnimationFrameRate);
    const double rate = edited.rate(fileRate);
    const int frames = qMax(1, int(m_durations.at(index) * fileRate));
    m_duration->setText(timecode(frames / rate, rate));
}

// The animation as edited in the dialog.
composition::ModelAnimationInstance ModelAnimationDialog::instance() const
{
    composition::ModelAnimationInstance result = m_instance;
    result.name = m_name->text();
    if (m_animation->isEnabled()) result.animationIndex = qMax(0, m_animation->currentIndex());
    const double rate = m_rate->currentData().toDouble();
    result.frameRateOverridden = rate > 0.0;
    if (rate > 0.0) result.frameRate = rate;
    return result;
}

// HH:MM:SS:FF at the given rate (rounded to whole frames per second).
QString ModelAnimationDialog::timecode(double seconds, double rate)
{
    const int perSecond = qMax(1, int(std::lround(rate)));
    const qint64 total = qMax<qint64>(0, std::llround(seconds * rate));
    const qint64 frames = total % perSecond;
    const qint64 wholeSeconds = total / perSecond;
    return QStringLiteral("%1:%2:%3:%4")
        .arg(wholeSeconds / 3600, 2, 10, QLatin1Char('0'))
        .arg((wholeSeconds / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(wholeSeconds % 60, 2, 10, QLatin1Char('0'))
        .arg(frames, 2, 10, QLatin1Char('0'));
}

} // namespace openvegas::ui
