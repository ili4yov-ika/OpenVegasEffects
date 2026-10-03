#include "ui/ProjectSettingsDialog.h"

#include "app/ProjectDefaults.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

namespace openvegas {
namespace ui {
namespace {

// "Color Bit Depth" choices as its viewer menu lists them (FUN_1408d2da0),
// each a BPC value plus the linear-colour switch.
struct BitDepthChoice { const char* label; int bpc; bool linear; };
const BitDepthChoice kBitDepths[] = {
    {QT_TRANSLATE_NOOP("biff::ui::viewer::ViewerActions", "8-bit Integer"), 1000, false},
    {QT_TRANSLATE_NOOP("biff::ui::viewer::ViewerActions", "16-bit Float"), 1001, false},
    {QT_TRANSLATE_NOOP("biff::ui::viewer::ViewerActions", "16-bit Float - Linear Color"), 1001, true},
    {QT_TRANSLATE_NOOP("biff::ui::viewer::ViewerActions", "32-bit Float"), 1002, false},
    {QT_TRANSLATE_NOOP("biff::ui::viewer::ViewerActions", "32-bit Float - Linear Color"), 1002, true},
};

// Antialiasing modes 1..9 (FUN_140268720).
const char* const kAntialiasing[] = {
    QT_TRANSLATE_NOOP("biff::ui::common", "4x MSAA"),  QT_TRANSLATE_NOOP("biff::ui::common", "8x MSAA"),
    QT_TRANSLATE_NOOP("biff::ui::common", "16x MSAA"), QT_TRANSLATE_NOOP("biff::ui::common", "32x MSAA"),
    QT_TRANSLATE_NOOP("biff::ui::common", "8x CSAA"),  QT_TRANSLATE_NOOP("biff::ui::common", "8xQ CSAA"),
    QT_TRANSLATE_NOOP("biff::ui::common", "16x CSAA"), QT_TRANSLATE_NOOP("biff::ui::common", "16xQ CSAA"),
    QT_TRANSLATE_NOOP("biff::ui::common", "32x CSAA"),
};

const char* const kFrameRates[] = {
    QT_TRANSLATE_NOOP("ProjectSettingsDialog", "23.976"), QT_TRANSLATE_NOOP("ProjectSettingsDialog", "24"),
    QT_TRANSLATE_NOOP("ProjectSettingsDialog", "25 (PAL)"),
    QT_TRANSLATE_NOOP("ProjectSettingsDialog", "29.97 (NTSC)"),
    QT_TRANSLATE_NOOP("ProjectSettingsDialog", "30"), QT_TRANSLATE_NOOP("ProjectSettingsDialog", "50"),
    QT_TRANSLATE_NOOP("ProjectSettingsDialog", "59.94"), QT_TRANSLATE_NOOP("ProjectSettingsDialog", "60"),
};

QSpinBox* mapSizeSpin(QWidget* parent, const char* name)
{
    auto* spin = new QSpinBox(parent);
    spin->setObjectName(QString::fromLatin1(name));
    spin->setRange(64, 16384);
    spin->setSingleStep(64);
    spin->setSuffix(QCoreApplication::translate("ProjectSettingsDialog", " pixels"));
    return spin;
}

double leadingNumber(const QString& value, double fallback)
{
    const QRegularExpressionMatch match =
        QRegularExpression(QStringLiteral("^\\s*([0-9]+(?:[.,][0-9]+)?)")).match(value);
    if (!match.hasMatch()) return fallback;
    bool ok = false;
    const double number = QString(match.captured(1)).replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
    return ok && number > 0 ? number : fallback;
}

} // namespace

ProjectSettingsDialog::ProjectSettingsDialog(const Values& values, bool newProject, QWidget* parent)
    : QDialog(parent), m_render(values.render)
{
    setObjectName(QStringLiteral("ProjectSettingsDialog"));
    setWindowTitle(newProject ? QCoreApplication::translate("ProjectSettingsDialog", "New Project Settings") : QCoreApplication::translate("ProjectSettingsDialog", "Project Settings"));
    auto* layout = new QVBoxLayout(this);
    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("tabWidgetProjectSettings"));
    layout->addWidget(m_tabs);

    // --- Editor: the editor timeline's format ------------------------------
    auto* editor = new QWidget(m_tabs);
    auto* editorLayout = new QVBoxLayout(editor);
    auto* video = new QGroupBox(QCoreApplication::translate("ProjectSettingsDialog", "Video"), editor);
    auto* videoForm = new QFormLayout(video);
    m_width = new QSpinBox(video);
    m_width->setObjectName(QStringLiteral("spinBoxWidth"));
    m_width->setRange(16, 8192);
    m_width->setSuffix(QCoreApplication::translate("ProjectSettingsDialog", " pixels"));
    m_height = new QSpinBox(video);
    m_height->setObjectName(QStringLiteral("spinBoxHeight"));
    m_height->setRange(16, 8192);
    m_height->setSuffix(QCoreApplication::translate("ProjectSettingsDialog", " pixels"));
    m_preserveAspect = new QCheckBox(QCoreApplication::translate("ProjectSettingsDialog", "Preserve aspect ratio"), video);
    m_preserveAspect->setObjectName(QStringLiteral("checkBoxPreserveAspectRatio"));
    videoForm->addRow(QCoreApplication::translate("ProjectSettingsDialog", "Width:"), m_width);
    videoForm->addRow(QCoreApplication::translate("ProjectSettingsDialog", "Height:"), m_height);
    videoForm->addRow(QString(), m_preserveAspect);
    m_frameRate = new QComboBox(video);
    m_frameRate->setObjectName(QStringLiteral("comboBoxFrameRate"));
    m_frameRate->setEditable(true);
    for (const char* rate : kFrameRates)
        m_frameRate->addItem(QCoreApplication::translate("ProjectSettingsDialog", rate));
    videoForm->addRow(QCoreApplication::translate("ProjectSettingsDialog", "Frame Rate:"), m_frameRate);
    m_duration = new QDoubleSpinBox(video);
    m_duration->setObjectName(QStringLiteral("spinBoxDuration"));
    m_duration->setRange(0.1, 86400.0);
    m_duration->setDecimals(2);
    videoForm->addRow(QCoreApplication::translate("ProjectSettingsDialog", "Duration:"), m_duration);
    editorLayout->addWidget(video);
    auto* audio = new QGroupBox(QCoreApplication::translate("ProjectSettingsDialog", "Audio"), editor);
    auto* audioForm = new QFormLayout(audio);
    m_sampleRate = new QComboBox(audio);
    m_sampleRate->setObjectName(QStringLiteral("comboBoxSampleRate"));
    for (int rate : {22050, 32000, 44100, 48000, 96000})
        m_sampleRate->addItem(QStringLiteral("%1 Hz").arg(rate), rate);
    audioForm->addRow(QCoreApplication::translate("ProjectSettingsDialog", "Sample Rate:"), m_sampleRate);
    editorLayout->addWidget(audio);
    editorLayout->addStretch();
    auto* editorDefaults = new QPushButton(QCoreApplication::translate("ProjectSettingsDialog", "Restore Defaults"), editor);
    editorDefaults->setObjectName(QStringLiteral("pushButtonRestoreEditorDefaults"));
    editorLayout->addWidget(editorDefaults, 0, Qt::AlignRight);
    m_tabs->addTab(editor, QCoreApplication::translate("ProjectSettingsDialog", "Editor"));

    // --- Rendering: Project/ProjectSettings ---------------------------------
    auto* rendering = new QWidget(m_tabs);
    auto* renderLayout = new QVBoxLayout(rendering);
    auto* renderForm = new QFormLayout;
    m_bitDepth = new QComboBox(rendering);
    m_bitDepth->setObjectName(QStringLiteral("comboBoxColorBitDepth"));
    for (const BitDepthChoice& choice : kBitDepths)
        m_bitDepth->addItem(QCoreApplication::translate("biff::ui::viewer::ViewerActions", choice.label));
    renderForm->addRow(QCoreApplication::translate("ProjectSettingsDialog", "Color Bit Depth:"), m_bitDepth);
    m_antialiasing = new QComboBox(rendering);
    m_antialiasing->setObjectName(QStringLiteral("comboBoxAntialiasingMode"));
    for (int mode = 1; mode <= 9; ++mode)
        m_antialiasing->addItem(QCoreApplication::translate("biff::ui::common", kAntialiasing[mode - 1]), mode);
    renderForm->addRow(QCoreApplication::translate("ProjectSettingsDialog", "Antialiasing Mode:"), m_antialiasing);
    m_reflection = mapSizeSpin(rendering, "spinBoxReflectionMapSize");
    renderForm->addRow(QCoreApplication::translate("ProjectSettingsDialog", "Reflection Map Size:"), m_reflection);
    m_shadow = mapSizeSpin(rendering, "spinBoxShadowMapSize");
    renderForm->addRow(QCoreApplication::translate("ProjectSettingsDialog", "Shadow Map Size:"), m_shadow);
    m_modelMaps = mapSizeSpin(rendering, "spinBoxModelTextureMaxSize");
    renderForm->addRow(QCoreApplication::translate("ProjectSettingsDialog", "Maximum 3D Model Map Size:"), m_modelMaps);
    renderLayout->addLayout(renderForm);
    auto* note = new QLabel(tr("Defaults for these settings are specified in the Options dialog."),
                            rendering);
    note->setStyleSheet(QStringLiteral("font-style: italic; color: palette(mid);"));
    note->setAlignment(Qt::AlignCenter);
    renderLayout->addWidget(note);
    renderLayout->addStretch();
    auto* renderDefaults = new QPushButton(QCoreApplication::translate("ProjectSettingsDialog", "Restore Defaults"), rendering);
    renderDefaults->setObjectName(QStringLiteral("pushButtonRestoreRenderingDefaults"));
    renderLayout->addWidget(renderDefaults, 0, Qt::AlignRight);
    m_tabs->addTab(rendering, QCoreApplication::translate("ProjectSettingsDialog", "Rendering"));

    auto* buttons = new QDialogButtonBox(this);
    buttons->addButton(QCoreApplication::translate("ProjectSettingsDialog", "OK"), QDialogButtonBox::AcceptRole)->setDefault(true);
    buttons->addButton(QCoreApplication::translate("ProjectSettingsDialog", "Cancel"), QDialogButtonBox::RejectRole);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(editorDefaults, &QPushButton::clicked, this, &ProjectSettingsDialog::restoreEditorDefaults);
    connect(renderDefaults, &QPushButton::clicked, this, &ProjectSettingsDialog::restoreRenderingDefaults);

    // Preserve aspect ratio ties the two sizes to the ratio they had when it
    // was ticked.
    connect(m_preserveAspect, &QCheckBox::toggled, this, [this](bool on) {
        if (on && m_height->value() > 0) m_aspect = double(m_width->value()) / m_height->value();
    });
    connect(m_width, &QSpinBox::valueChanged, this, [this](int width) {
        if (!m_preserveAspect->isChecked() || m_aspect <= 0) return;
        const QSignalBlocker blocker(m_height);
        m_height->setValue(qMax(16, qRound(width / m_aspect)));
    });
    connect(m_height, &QSpinBox::valueChanged, this, [this](int height) {
        if (!m_preserveAspect->isChecked() || m_aspect <= 0) return;
        const QSignalBlocker blocker(m_width);
        m_width->setValue(qMax(16, qRound(height * m_aspect)));
    });

    m_width->setValue(values.width);
    m_height->setValue(values.height);
    m_frameRate->setCurrentText(QString::number(values.fps, 'g', 6));
    for (int i = 0; i < m_frameRate->count(); ++i) {
        if (qAbs(leadingNumber(m_frameRate->itemText(i), 0) - values.fps) < 0.001) {
            m_frameRate->setCurrentIndex(i);
            break;
        }
    }
    m_duration->setValue(values.durationSeconds);
    if (m_sampleRate->findData(values.sampleRate) < 0)
        m_sampleRate->addItem(QStringLiteral("%1 Hz").arg(values.sampleRate), values.sampleRate);
    m_sampleRate->setCurrentIndex(m_sampleRate->findData(values.sampleRate));
    setRendering(values.render);
}

void ProjectSettingsDialog::setRendering(const composition::ProjectRenderSettings& render)
{
    int bitDepth = 0;
    for (int i = 0; i < int(std::size(kBitDepths)); ++i) {
        if (kBitDepths[i].bpc == render.bitDepth && kBitDepths[i].linear == render.useLinearColor) {
            bitDepth = i;
            break;
        }
    }
    m_bitDepth->setCurrentIndex(bitDepth);
    m_antialiasing->setCurrentIndex(qBound(0, render.antialiasingMode - 1, 8));
    m_reflection->setValue(render.reflectionMapSize);
    m_shadow->setValue(render.shadowMapSize);
    m_modelMaps->setValue(render.modelTextureMaxSize);
}

ProjectSettingsDialog::Values ProjectSettingsDialog::values() const
{
    Values v;
    v.width = m_width->value();
    v.height = m_height->value();
    v.fps = leadingNumber(m_frameRate->currentText(), 30.0);
    v.durationSeconds = m_duration->value();
    v.sampleRate = m_sampleRate->currentData().toInt();
    v.render = m_render;   // keeps what the dialog does not show
    const BitDepthChoice& choice = kBitDepths[qBound(0, m_bitDepth->currentIndex(), 4)];
    v.render.bitDepth = choice.bpc;
    v.render.useLinearColor = choice.linear;
    v.render.antialiasingMode = m_antialiasing->currentData().toInt();
    v.render.reflectionMapSize = m_reflection->value();
    v.render.shadowMapSize = m_shadow->value();
    v.render.modelTextureMaxSize = m_modelMaps->value();
    return v;
}

void ProjectSettingsDialog::restoreEditorDefaults()
{
    composition::Composition fresh;
    app::applyNewProjectDefaults(fresh);
    const Values defaults = fromComposition(fresh);
    m_preserveAspect->setChecked(false);
    m_width->setValue(defaults.width);
    m_height->setValue(defaults.height);
    m_frameRate->setCurrentText(QString::number(defaults.fps, 'g', 6));
    m_duration->setValue(defaults.durationSeconds);
    const int sample = m_sampleRate->findData(defaults.sampleRate);
    if (sample >= 0) m_sampleRate->setCurrentIndex(sample);
}

void ProjectSettingsDialog::restoreRenderingDefaults()
{
    setRendering(app::defaultProjectRenderSettings());
}

ProjectSettingsDialog::Values ProjectSettingsDialog::fromComposition(const composition::Composition& composition)
{
    const composition::EditorSequence& sequence = composition.editorSequence();
    Values v;
    v.width = sequence.width > 0 ? sequence.width : composition.width();
    v.height = sequence.height > 0 ? sequence.height : composition.height();
    v.fps = sequence.fps > 0 ? sequence.fps
                             : double(composition.fpsNumerator()) / qMax(1, composition.fpsDenominator());
    v.durationSeconds = sequence.frameCount > 0 && v.fps > 0 ? sequence.frameCount / v.fps
                                                              : composition.durationSeconds();
    v.sampleRate = sequence.audioSampleRate > 0 ? sequence.audioSampleRate : 48000;
    v.render = composition.projectSettings();
    return v;
}

void ProjectSettingsDialog::apply(const Values& values, composition::Composition& composition)
{
    composition::EditorSequence& sequence = composition.editorSequence();
    sequence.width = values.width;
    sequence.height = values.height;
    sequence.fps = values.fps;
    sequence.frameCount = qMax<qint64>(1, qRound64(values.durationSeconds * values.fps));
    if (sequence.outPoint <= 0 || sequence.outPoint > sequence.frameCount)
        sequence.outPoint = sequence.frameCount;
    sequence.audioSampleRate = values.sampleRate;
    composition.projectSettings() = values.render;
}

} // namespace ui
} // namespace openvegas
