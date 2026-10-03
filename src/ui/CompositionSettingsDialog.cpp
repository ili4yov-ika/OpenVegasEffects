#include "ui/CompositionSettingsDialog.h"

#include "ui/Theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPixmap>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

namespace openvegas {
namespace ui {
namespace {

// The Frame Rate list of the reference (FUN_140730a50), as typed values.
struct RateChoice { const char* label; int numerator; int denominator; };
const RateChoice kRates[] = {
    {QT_TRANSLATE_NOOP("CompositionSettingsDialog", "23.976"), 24000, 1001},
    {QT_TRANSLATE_NOOP("CompositionSettingsDialog", "24"), 24, 1},
    {QT_TRANSLATE_NOOP("CompositionSettingsDialog", "25 (PAL)"), 25, 1},
    {QT_TRANSLATE_NOOP("CompositionSettingsDialog", "29.97 (NTSC)"), 30000, 1001},
    {QT_TRANSLATE_NOOP("CompositionSettingsDialog", "30"), 30, 1},
    {QT_TRANSLATE_NOOP("CompositionSettingsDialog", "50"), 50, 1},
    {QT_TRANSLATE_NOOP("CompositionSettingsDialog", "59.94"), 60000, 1001},
    {QT_TRANSLATE_NOOP("CompositionSettingsDialog", "60"), 60, 1},
};

// The aspect names of the reference's PAR model (FUN_1403963f0), in the
// order of its PAR enum.
const char* const kAspects[] = {
    QT_TRANSLATE_NOOP("CompositionSettingsDialog", "Square Pixels (1.0)"),
    QT_TRANSLATE_NOOP("CompositionSettingsDialog", "DV NTSC (0.91)"),
    QT_TRANSLATE_NOOP("CompositionSettingsDialog", "DV NTSC Wide (1.21)"),
    QT_TRANSLATE_NOOP("CompositionSettingsDialog", "DV PAL (1.09)"),
    QT_TRANSLATE_NOOP("CompositionSettingsDialog", "DV PAL Wide (1.46)"),
    QT_TRANSLATE_NOOP("CompositionSettingsDialog", "HD Anamorphic 1080 (1.33)"),
    QT_TRANSLATE_NOOP("CompositionSettingsDialog", "DVCPro HD (1.5)"),
    QT_TRANSLATE_NOOP("CompositionSettingsDialog", "Anamorphic 2:1 (2.0)"),
    QT_TRANSLATE_NOOP("CompositionSettingsDialog", "Custom"),
};

QString text(const char* source)
{
    return QCoreApplication::translate("CompositionSettingsDialog", source);
}

bool sameRate(double a, double b) { return std::abs(a - b) < 1e-3; }

} // namespace

bool CompositionSettingsDialog::Values::operator==(const Values& o) const
{
    return name == o.name && width == o.width && height == o.height
           && fpsNumerator == o.fpsNumerator && fpsDenominator == o.fpsDenominator
           && pixelAspect == o.pixelAspect && customPixelAspect == o.customPixelAspect
           && durationSeconds == o.durationSeconds && audioSampleRate == o.audioSampleRate
           && render == o.render;
}

CompositionSettingsDialog::Values CompositionSettingsDialog::fromComposition(
    const composition::Composition& composition)
{
    Values v;
    v.name = composition.name();
    v.width = composition.width();
    v.height = composition.height();
    v.fpsNumerator = composition.fpsNumerator();
    v.fpsDenominator = qMax(1, composition.fpsDenominator());
    v.pixelAspect = composition.pixelAspect();
    v.customPixelAspect = composition.customPixelAspect();
    v.durationSeconds = composition.durationSeconds();
    v.audioSampleRate = composition.audioSampleRate();
    v.render = composition.renderSettings();
    return v;
}

void CompositionSettingsDialog::apply(const Values& v, composition::Composition& composition)
{
    composition.setName(v.name);
    composition.setSize(v.width, v.height);
    composition.setFrameRate(v.fpsNumerator, qMax(1, v.fpsDenominator));
    composition.setPixelAspect(v.pixelAspect, v.customPixelAspect);
    composition.setDurationSeconds(v.durationSeconds);
    composition.setAudioSampleRate(v.audioSampleRate);
    composition.renderSettings() = v.render;
}

CompositionSettingsDialog::CompositionSettingsDialog(const Values& values, const Values& timeline,
                                                     QWidget* parent)
    : QDialog(parent), m_initial(values), m_timeline(timeline)
{
    setObjectName(QStringLiteral("CompositionSettingsDialog"));
    setWindowTitle(QCoreApplication::translate("CompositionSettingsDialog", "Composite Shot Properties"));
    setMinimumWidth(480);
    auto* layout = new QVBoxLayout(this);

    // --- Template, Name, Duration ------------------------------------------
    auto* main = new QWidget(this);
    main->setObjectName(QStringLiteral("widgetMain"));
    auto* top = new QFormLayout(main);
    auto* templateRow = new QHBoxLayout;
    m_template = new QComboBox(main);
    m_template->setObjectName(QStringLiteral("comboBoxTemplate"));
    m_template->setToolTip(QCoreApplication::translate("CompositionSettingsDialog", "Templates for common video formats."));
    m_template->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_saveTemplate = new QToolButton(main);
    m_saveTemplate->setObjectName(QStringLiteral("toolButtonSave"));
    m_saveTemplate->setIcon(QIcon(QStringLiteral(":/icons/save.svg")));
    m_saveTemplate->setToolTip(QCoreApplication::translate("CompositionSettingsDialog", "Save settings as a template"));
    m_deleteTemplate = new QToolButton(main);
    m_deleteTemplate->setObjectName(QStringLiteral("toolButtonDelete"));
    m_deleteTemplate->setIcon(QIcon(QStringLiteral(":/text-icons/remove.svg")));
    m_deleteTemplate->setToolTip(QCoreApplication::translate("CompositionSettingsDialog", "Delete template"));
    templateRow->addWidget(m_template, 1);
    templateRow->addWidget(m_saveTemplate);
    templateRow->addWidget(m_deleteTemplate);
    top->addRow(QCoreApplication::translate("CompositionSettingsDialog", "Template:"), templateRow);
    m_name = new QLineEdit(values.name, main);
    m_name->setObjectName(QStringLiteral("lineEditName"));
    m_name->setPlaceholderText(QStringLiteral("<MyComp>"));
    top->addRow(QCoreApplication::translate("CompositionSettingsDialog", "Name:"), m_name);
    m_duration = new QDoubleSpinBox(main);
    m_duration->setObjectName(QStringLiteral("spinBoxDuration"));
    m_duration->setDecimals(3);
    m_duration->setRange(0.001, 86400.0);
    m_duration->setValue(values.durationSeconds);
    top->addRow(QCoreApplication::translate("CompositionSettingsDialog", "Duration:"), m_duration);
    layout->addWidget(main);

    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("tabWidget"));
    layout->addWidget(m_tabs);

    // --- Standard: Video and Audio ------------------------------------------
    auto* standard = new QWidget(m_tabs);
    standard->setObjectName(QStringLiteral("tabStandard"));
    auto* standardLayout = new QVBoxLayout(standard);
    auto* video = new QGroupBox(QCoreApplication::translate("CompositionSettingsDialog", "Video"), standard);
    video->setObjectName(QStringLiteral("groupBoxVideo"));
    auto* videoForm = new QFormLayout(video);
    m_width = new QSpinBox(video);
    m_width->setObjectName(QStringLiteral("spinBoxWidth"));
    m_width->setRange(16, 8192);
    m_height = new QSpinBox(video);
    m_height->setObjectName(QStringLiteral("spinBoxHeight"));
    m_height->setRange(16, 8192);
    auto* match = new QToolButton(video);
    match->setObjectName(QStringLiteral("toolButtonMatchTimeline"));
    match->setText(QCoreApplication::translate("CompositionSettingsDialog", "Match Timeline"));
    match->setToolButtonStyle(Qt::ToolButtonTextOnly);
    auto* widthRow = new QHBoxLayout;
    widthRow->addWidget(m_width, 1);
    widthRow->addWidget(match);
    videoForm->addRow(QCoreApplication::translate("CompositionSettingsDialog", "Width:"), widthRow);
    videoForm->addRow(QCoreApplication::translate("CompositionSettingsDialog", "Height:"), m_height);
    m_frameRate = new QComboBox(video);
    m_frameRate->setObjectName(QStringLiteral("comboBoxFrameRate"));
    m_frameRate->setEditable(true);
    for (const RateChoice& rate : kRates)
        m_frameRate->addItem(text(rate.label), double(rate.numerator) / rate.denominator);
    videoForm->addRow(QCoreApplication::translate("CompositionSettingsDialog", "Frame Rate:"), m_frameRate);
    m_aspect = new QComboBox(video);
    m_aspect->setObjectName(QStringLiteral("comboBoxAspectRatio"));
    for (int i = 0; i < composition::Composition::PixelAspectCount; ++i)
        m_aspect->addItem(text(kAspects[i]), i);
    videoForm->addRow(QCoreApplication::translate("CompositionSettingsDialog", "Aspect Ratio:"), m_aspect);
    standardLayout->addWidget(video);
    auto* audio = new QGroupBox(QCoreApplication::translate("CompositionSettingsDialog", "Audio"), standard);
    audio->setObjectName(QStringLiteral("groupBoxAudio"));
    auto* audioForm = new QFormLayout(audio);
    // Shown, not edited: a shot's audio follows the project's.
    m_sampleRate = new QLabel(QStringLiteral("%1 Hz").arg(values.audioSampleRate), audio);
    m_sampleRate->setObjectName(QStringLiteral("labelValueAudioSampleRate"));
    audioForm->addRow(QCoreApplication::translate("CompositionSettingsDialog", "Sample Rate:"), m_sampleRate);
    standardLayout->addWidget(audio);
    standardLayout->addStretch();
    m_tabs->addTab(standard, QCoreApplication::translate("CompositionSettingsDialog", "Standard"));

    // --- Advanced: Fog and Motion Blur ---------------------------------------
    auto* advanced = new QWidget(m_tabs);
    advanced->setObjectName(QStringLiteral("tabAdvanced"));
    auto* advancedLayout = new QVBoxLayout(advanced);
    const composition::CompositionRenderSettings& r = values.render;
    auto* fog = new QGroupBox(QCoreApplication::translate("CompositionSettingsDialog", "Fog"), advanced);
    fog->setObjectName(QStringLiteral("groupBoxFog"));
    auto* fogForm = new QFormLayout(fog);
    m_fogOn = new QCheckBox(fog);
    m_fogOn->setObjectName(QStringLiteral("checkBoxFogEnable"));
    m_fogOn->setChecked(r.fogEnabled);
    fogForm->addRow(QCoreApplication::translate("CompositionSettingsDialog", "Enable:"), m_fogOn);
    const auto distance = [fog](const char* object, double value) {
        auto* editor = new QDoubleSpinBox(fog);
        editor->setObjectName(QString::fromLatin1(object));
        editor->setDecimals(2);
        editor->setRange(0.0, 999999999.0);
        editor->setValue(value);
        return editor;
    };
    m_fogNear = distance("doubleSpinBoxNearClipDistance", r.fogNearDistance);
    fogForm->addRow(QCoreApplication::translate("CompositionSettingsDialog", "Near Clip Distance:"), m_fogNear);
    m_fogFar = distance("doubleSpinBoxFarClipDistance", r.fogFarDistance);
    fogForm->addRow(QCoreApplication::translate("CompositionSettingsDialog", "Far Clip Distance:"), m_fogFar);
    m_fogDensity = distance("doubleSpinBoxDensity", r.fogDensity);
    fogForm->addRow(QCoreApplication::translate("CompositionSettingsDialog", "Density:"), m_fogDensity);
    m_fogFalloff = new QComboBox(fog);
    m_fogFalloff->setObjectName(QStringLiteral("comboBoxFallOff"));
    m_fogFalloff->addItems({QCoreApplication::translate("CompositionSettingsDialog", "Linear"),
                            QCoreApplication::translate("CompositionSettingsDialog", "Exponential"),
                            QCoreApplication::translate("CompositionSettingsDialog", "Exponential²")});
    m_fogFalloff->setCurrentIndex(qBound(0, r.fogFalloff, 2));
    fogForm->addRow(QCoreApplication::translate("CompositionSettingsDialog", "Fall Off:"), m_fogFalloff);
    m_fogColor = new QToolButton(fog);
    m_fogColor->setObjectName(QStringLiteral("widgetColor"));
    m_fogColorValue = r.fogColor;
    const auto paintSwatch = [this] {
        QPixmap swatch(40, 14);
        swatch.fill(m_fogColorValue);
        m_fogColor->setIcon(QIcon(swatch));
        m_fogColor->setIconSize(swatch.size());
        m_fogColor->setToolTip(m_fogColorValue.name());
    };
    paintSwatch();
    connect(m_fogColor, &QToolButton::clicked, this, [this, paintSwatch] {
        const QColor picked = interfaceColor(m_fogColorValue, this,
                                             QCoreApplication::translate("CompositionSettingsDialog", "Fog"));
        if (picked.isValid()) {
            m_fogColorValue = picked;
            paintSwatch();
        }
    });
    fogForm->addRow(QCoreApplication::translate("CompositionSettingsDialog", "Color:"), m_fogColor);
    for (QWidget* editor : {static_cast<QWidget*>(m_fogNear), static_cast<QWidget*>(m_fogFar),
                            static_cast<QWidget*>(m_fogDensity), static_cast<QWidget*>(m_fogFalloff),
                            static_cast<QWidget*>(m_fogColor)}) {
        editor->setEnabled(m_fogOn->isChecked());
        connect(m_fogOn, &QCheckBox::toggled, editor, &QWidget::setEnabled);
    }
    advancedLayout->addWidget(fog);

    auto* blur = new QGroupBox(QCoreApplication::translate("CompositionSettingsDialog", "Motion Blur"), advanced);
    blur->setObjectName(QStringLiteral("groupBoxMotionBlur"));
    auto* blurForm = new QFormLayout(blur);
    m_blurOn = new QCheckBox(blur);
    m_blurOn->setObjectName(QStringLiteral("checkBoxMotionBlurEnable"));
    m_blurOn->setChecked(r.motionBlurEnabled);
    blurForm->addRow(QCoreApplication::translate("CompositionSettingsDialog", "Enable:"), m_blurOn);
    m_shutterAngle = new QDoubleSpinBox(blur);
    m_shutterAngle->setObjectName(QStringLiteral("doubleSpinBoxShutterAngle"));
    m_shutterAngle->setRange(0.0, 720.0);
    m_shutterAngle->setDecimals(1);
    m_shutterAngle->setSuffix(QStringLiteral("°"));
    m_shutterAngle->setValue(r.shutterAngle);
    blurForm->addRow(QCoreApplication::translate("CompositionSettingsDialog", "Shutter Angle:"), m_shutterAngle);
    m_shutterPhase = new QDoubleSpinBox(blur);
    m_shutterPhase->setObjectName(QStringLiteral("doubleSpinBoxShutterPhase"));
    m_shutterPhase->setRange(-360.0, 360.0);
    m_shutterPhase->setDecimals(1);
    m_shutterPhase->setSuffix(QStringLiteral("°"));
    m_shutterPhase->setValue(r.shutterPhase);
    blurForm->addRow(QCoreApplication::translate("CompositionSettingsDialog", "Shutter Phase:"), m_shutterPhase);
    m_maxSamples = new QSpinBox(blur);
    m_maxSamples->setObjectName(QStringLiteral("spinBoxMaxSamples"));
    m_maxSamples->setRange(1, 100);
    m_maxSamples->setValue(r.maxNumOfSamples);
    blurForm->addRow(QCoreApplication::translate("CompositionSettingsDialog", "Max Samples:"), m_maxSamples);
    m_adaptive = new QCheckBox(blur);
    m_adaptive->setObjectName(QStringLiteral("checkBoxUseAdaptive"));
    m_adaptive->setChecked(r.useAdaptiveSamples);
    blurForm->addRow(QCoreApplication::translate("CompositionSettingsDialog", "Use Adaptive:"), m_adaptive);
    for (QWidget* editor : {static_cast<QWidget*>(m_shutterAngle), static_cast<QWidget*>(m_shutterPhase),
                            static_cast<QWidget*>(m_maxSamples), static_cast<QWidget*>(m_adaptive)}) {
        editor->setEnabled(m_blurOn->isChecked());
        connect(m_blurOn, &QCheckBox::toggled, editor, &QWidget::setEnabled);
    }
    advancedLayout->addWidget(blur);
    advancedLayout->addStretch();
    m_tabs->addTab(advanced, QCoreApplication::translate("CompositionSettingsDialog", "Advanced"));

    // --- Footer ---------------------------------------------------------------
    auto* footer = new QHBoxLayout;
    auto* cancel = new QToolButton(this);
    cancel->setObjectName(QStringLiteral("toolButtonCancel"));
    cancel->setText(QCoreApplication::translate("CompositionSettingsDialog", "Cancel"));
    auto* ok = new QToolButton(this);
    ok->setObjectName(QStringLiteral("toolButtonOK"));
    ok->setText(QCoreApplication::translate("CompositionSettingsDialog", "OK"));
    footer->addWidget(cancel);
    footer->addStretch();
    footer->addWidget(ok);
    layout->addLayout(footer);
    connect(cancel, &QToolButton::clicked, this, &QDialog::reject);
    connect(ok, &QToolButton::clicked, this, &QDialog::accept);

    setFormat(values.width, values.height, values.fpsNumerator, values.fpsDenominator,
              values.pixelAspect, values.customPixelAspect);
    refreshTemplates(QString());

    connect(m_template, &QComboBox::activated, this, &CompositionSettingsDialog::applyTemplate);
    connect(m_saveTemplate, &QToolButton::clicked, this, &CompositionSettingsDialog::saveTemplate);
    connect(m_deleteTemplate, &QToolButton::clicked, this, &CompositionSettingsDialog::deleteTemplate);
    connect(match, &QToolButton::clicked, this, [this] {
        setFormat(m_timeline.width, m_timeline.height, m_timeline.fpsNumerator,
                  m_timeline.fpsDenominator, m_timeline.pixelAspect, m_timeline.customPixelAspect);
        matchTemplate();
    });
    for (QSpinBox* spin : {m_width, m_height})
        connect(spin, &QSpinBox::valueChanged, this, &CompositionSettingsDialog::matchTemplate);
    connect(m_frameRate, &QComboBox::currentTextChanged, this, &CompositionSettingsDialog::matchTemplate);
    connect(m_aspect, &QComboBox::currentIndexChanged, this, &CompositionSettingsDialog::matchTemplate);
}

void CompositionSettingsDialog::setFormat(int width, int height, int numerator, int denominator,
                                          int aspect, double custom)
{
    m_width->setValue(width);
    m_height->setValue(height);
    const double fps = double(numerator) / qMax(1, denominator);
    int rateIndex = -1;
    for (int i = 0; i < m_frameRate->count(); ++i)
        if (sameRate(m_frameRate->itemData(i).toDouble(), fps)) rateIndex = i;
    if (rateIndex >= 0) {
        m_frameRate->setCurrentIndex(rateIndex);
    } else {
        m_frameRate->setCurrentIndex(-1);
        m_frameRate->setEditText(QString::number(std::round(fps * 1000.0) / 1000.0, 'g', 10));
    }
    m_aspect->setCurrentIndex(qBound(0, aspect, int(composition::Composition::PixelAspectCount) - 1));
    m_initial.customPixelAspect = custom > 0.0 ? custom : 1.0;
}

double CompositionSettingsDialog::frameRate() const
{
    const int index = m_frameRate->findText(m_frameRate->currentText());
    if (index >= 0) return m_frameRate->itemData(index).toDouble();
    // A typed rate: its leading number ("29.97 (NTSC)" or "12.5").
    const QRegularExpressionMatch number =
        QRegularExpression(QStringLiteral("^\\s*([0-9]+(?:[.,][0-9]+)?)")).match(m_frameRate->currentText());
    const double value = number.hasMatch()
        ? QString(number.captured(1)).replace(QLatin1Char(','), QLatin1Char('.')).toDouble() : 0.0;
    return value > 0.0 ? value : double(m_initial.fpsNumerator) / qMax(1, m_initial.fpsDenominator);
}

CompositionSettingsDialog::Values CompositionSettingsDialog::values() const
{
    Values v = m_initial;
    const QString name = m_name->text().trimmed();
    if (!name.isEmpty()) v.name = name;
    v.width = m_width->value();
    v.height = m_height->value();
    composition::Composition::frameRateFraction(frameRate(), &v.fpsNumerator, &v.fpsDenominator);
    v.pixelAspect = m_aspect->currentData().toInt();
    v.durationSeconds = m_duration->value();
    v.render.fogEnabled = m_fogOn->isChecked();
    v.render.fogNearDistance = m_fogNear->value();
    v.render.fogFarDistance = m_fogFar->value();
    v.render.fogDensity = m_fogDensity->value();
    v.render.fogFalloff = m_fogFalloff->currentIndex();
    v.render.fogColor = m_fogColorValue;
    v.render.motionBlurEnabled = m_blurOn->isChecked();
    v.render.shutterAngle = m_shutterAngle->value();
    v.render.shutterPhase = m_shutterPhase->value();
    v.render.maxNumOfSamples = m_maxSamples->value();
    v.render.useAdaptiveSamples = m_adaptive->isChecked();
    return v;
}

void CompositionSettingsDialog::refreshTemplates(const QString& selectId)
{
    m_templates = app::allTemplates();
    {
        const QSignalBlocker blocker(m_template);
        m_template->clear();
        m_template->addItem(QCoreApplication::translate("CompositionSettingsDialog", "Custom"), QString());
        for (const app::AVTemplate& t : m_templates) m_template->addItem(t.name, t.id);
    }
    if (!selectId.isEmpty()) {
        const int index = m_template->findData(selectId);
        if (index >= 0) {
            m_template->setCurrentIndex(index);
            m_deleteTemplate->setEnabled(!m_templates.at(index - 1).system);
            return;
        }
    }
    matchTemplate();
}

void CompositionSettingsDialog::matchTemplate()
{
    if (m_applyingTemplate) return;
    const double fps = frameRate();
    const int aspect = m_aspect->currentData().toInt();
    int found = 0;
    for (int i = 0; i < m_templates.size(); ++i) {
        const app::AVTemplate& t = m_templates.at(i);
        if (t.width == m_width->value() && t.height == m_height->value() && sameRate(t.frameRate, fps)
            && t.pixelAspect == aspect) {
            found = i + 1;
            break;
        }
    }
    const QSignalBlocker blocker(m_template);
    m_template->setCurrentIndex(found);
    m_deleteTemplate->setEnabled(found > 0 && !m_templates.at(found - 1).system);
}

void CompositionSettingsDialog::applyTemplate(int index)
{
    if (index <= 0 || index > m_templates.size()) {
        m_deleteTemplate->setEnabled(false);
        return;
    }
    const app::AVTemplate& t = m_templates.at(index - 1);
    int numerator = 30, denominator = 1;
    composition::Composition::frameRateFraction(t.frameRate, &numerator, &denominator);
    m_applyingTemplate = true;
    setFormat(t.width, t.height, numerator, denominator, t.pixelAspect, t.pixelAspectValue);
    m_applyingTemplate = false;
    m_deleteTemplate->setEnabled(!t.system);
}

void CompositionSettingsDialog::saveTemplate()
{
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, QCoreApplication::translate("CompositionSettingsDialog", "New Template"),
        QCoreApplication::translate("CompositionSettingsDialog", "Name:"), QLineEdit::Normal,
        QCoreApplication::translate("CompositionSettingsDialog", "My Template"), &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    app::AVTemplate t;
    t.name = name;
    t.width = m_width->value();
    t.height = m_height->value();
    t.frameRate = frameRate();
    t.pixelAspect = m_aspect->currentData().toInt();
    t.pixelAspectValue = composition::Composition::pixelAspectValue(t.pixelAspect, m_initial.customPixelAspect);
    t.audioSampleRate = m_initial.audioSampleRate;
    const app::AVTemplate saved = app::saveUserTemplate(t);
    if (saved.id.isEmpty()) {
        QMessageBox::warning(this, windowTitle(),
                             QCoreApplication::translate("CompositionSettingsDialog",
                                                         "The template file could not be created."));
        return;
    }
    refreshTemplates(saved.id);
}

void CompositionSettingsDialog::deleteTemplate()
{
    const int index = m_template->currentIndex();
    if (index <= 0 || index > m_templates.size() || m_templates.at(index - 1).system) return;
    // The reference's own question (1412d45b0), its odd space included.
    if (QMessageBox::question(this, QCoreApplication::translate("CompositionSettingsDialog", "Delete Template"),
                              QCoreApplication::translate("CompositionSettingsDialog",
                                                          "This will remove the current template.\n\n Are you sure you want to continue?"))
        != QMessageBox::Yes)
        return;
    if (!app::deleteUserTemplate(m_templates.at(index - 1))) {
        QMessageBox::warning(this, windowTitle(),
                             QCoreApplication::translate("CompositionSettingsDialog",
                                                         "The template file could not be deleted."));
        return;
    }
    refreshTemplates(QString());
}

} // namespace ui
} // namespace openvegas
