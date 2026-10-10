#include "ui/ExportPanel.h"

#include "ui_ExportPanel.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QToolButton>
#include <QVBoxLayout>

#include "core/Log.h"
#include "app/Settings.h"
#include "ui/ExportPresets.h"

#include <QSignalBlocker>
#include <QTabWidget>

namespace openvegas {
namespace ui {

namespace {
const QString kSettingKey = QStringLiteral("Options/ExportDirectory");

QSettings exportSettings()
{
    return QSettings(QSettings::IniFormat, QSettings::UserScope,
                     app::Settings::organizationName(), app::Settings::applicationName());
}
} // namespace

ExportPanel::ExportPanel(QWidget* parent)
    : QWidget(parent)
    , m_ui(new Ui::ExportPanel)
{
    setWindowTitle(tr("Export"));
    setObjectName(QStringLiteral("export-panel"));

    // Layout comes from ui/ExportPanel.ui; only behaviour lives here.
    auto* body = new QWidget(this);
    m_ui->setupUi(body);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("export-panel-tabs"));
    m_tabs->setDocumentMode(true);
    m_tabs->addTab(body, QCoreApplication::translate("ExportPanelWidget", "Queue"));
    outer->addWidget(m_tabs);

    connect(m_ui->toolButtonExportDirectory, &QToolButton::clicked,
            this, &ExportPanel::chooseDirectory);
    connect(m_ui->pushButtonExportFrame, &QPushButton::clicked,
            this, &ExportPanel::exportFrame);
    connect(m_ui->pushButtonExportContents, &QPushButton::clicked, this, [this] {
        emit exportContentsRequested(buildOutputPath(extensionForPreset()));
    });
    connect(m_ui->pushButtonAddToQueue, &QPushButton::clicked, this, [this] {
        emit addToQueueRequested(buildOutputPath(extensionForPreset()));
        QSettings settings = exportSettings();
        settings.setValue(kSettingKey, exportDirectory());
    });

    // Restore the last-used export directory (reference ExportDirectory setting).
    const QSettings settings = exportSettings();
    QString saved = settings.value(kSettingKey).toString();
    if (saved.isEmpty()) {
        // Migrate builds that wrote the old panel-local key.
        saved = settings.value(QStringLiteral("Export/ExportDirectory")).toString();
    }
    if (!saved.isEmpty()) {
        m_ui->lineEditExportDirectory->setText(saved);
    }
}

ExportPanel::~ExportPanel()
{
    delete m_ui;
}

void ExportPanel::setQueueWidget(QWidget* widget)
{
    if (!widget) return;
    // Above the progress log, which keeps the space it has.
    m_ui->rootLayout->insertWidget(m_ui->rootLayout->indexOf(m_ui->plainTextProgress), widget, 3);
}

QString ExportPanel::exportDirectory() const
{
    return m_ui->lineEditExportDirectory->text().trimmed();
}

void ExportPanel::setExportDirectory(const QString& path)
{
    m_ui->lineEditExportDirectory->setText(path);
}

void ExportPanel::setPresetManager(ExportPresetManager* manager)
{
    m_presets = manager;
    if (!manager) return;
    if (!m_presetsPage) {
        m_presetsPage = new ExportPresetsPage(manager, m_tabs);
        m_tabs->addTab(m_presetsPage, QCoreApplication::translate("ExportPanelWidget", "Presets"));
    }
    connect(manager, &ExportPresetManager::presetsChanged, this, &ExportPanel::fillPresets);
    fillPresets();
}

void ExportPanel::fillPresets()
{
    if (!m_presets) return;
    // The choice stays when it still exists; otherwise the default.
    QString chosen = m_ui->comboBoxPreset->currentData().toString();
    if (!m_presets->contains(chosen)) chosen = m_presets->defaultPreset();
    const QSignalBlocker blocker(m_ui->comboBoxPreset);
    m_ui->comboBoxPreset->clear();
    for (const render::ExportPreset& preset : m_presets->presets())
        m_ui->comboBoxPreset->addItem(render::exportPresetDisplayName(preset), preset.name);
    m_ui->comboBoxPreset->setCurrentIndex(qMax(0, m_ui->comboBoxPreset->findData(chosen)));
}

QString ExportPanel::currentPreset() const
{
    const QString name = m_ui->comboBoxPreset->currentData().toString();
    return name.isEmpty() ? m_ui->comboBoxPreset->currentText() : name;
}

render::ExportPreset ExportPanel::currentExportPreset() const
{
    return m_presets ? m_presets->preset(currentPreset()) : render::ExportPreset();
}

QString ExportPanel::extensionForPreset() const
{
    const render::ExportPreset preset = currentExportPreset();
    if (preset.isValid()) return preset.extension();
    const QString p = currentPreset().toLower();
    if (p.contains(QStringLiteral(".mp4"))) {
        return QStringLiteral("mp4");
    }
    if (p.contains(QStringLiteral(".exr"))) {
        return QStringLiteral("exr");
    }
    if (p.contains(QStringLiteral(".jpg"))) {
        return QStringLiteral("jpg");
    }
    if (p.contains(QStringLiteral(".mov"))) {
        return QStringLiteral("mov");
    }
    return QStringLiteral("png");
}

QString ExportPanel::buildOutputPath(const QString& extension) const
{
    QString dir = exportDirectory();
    if (dir.isEmpty()) {
        dir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    }
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss"));
    QString name = m_outputName.trimmed();
    const QString suffix = QFileInfo(name).suffix().toLower();
    const QStringList known {"mp4", "mov", "avi", "mkv", "webm", "png", "jpg", "jpeg", "bmp",
        "tga", "exr", "dpx", "wav", "mp3", "flac", "ogg", "m4a", "aac", "wma", "vegfx"};
    if (exportSettings().value(QStringLiteral("Options/RemoveExtensions"), true).toBool() && known.contains(suffix))
        name.chop(suffix.size() + 1);
    name.replace(QRegularExpression(QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1f]")), QStringLiteral("_"));
    while (name.endsWith('.') || name.endsWith(' ')) name.chop(1);
    if (name.isEmpty()) name = QStringLiteral("frame");
    return QDir(dir).filePath(QStringLiteral("%1-%2.%3").arg(name, stamp, extension));
}

QString ExportPanel::outputPath() const
{
    const QString extension = extensionForPreset();
    // A single frame is an image even when the contents preset is a movie
    // or a sound.
    const bool image = extension == QLatin1String("png") || extension == QLatin1String("jpg")
                       || extension == QLatin1String("bmp") || extension == QLatin1String("exr");
    return buildOutputPath(image ? extension : QStringLiteral("png"));
}

void ExportPanel::chooseDirectory()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Export directory"), exportDirectory());
    if (!dir.isEmpty()) {
        m_ui->lineEditExportDirectory->setText(QDir::toNativeSeparators(dir));
    }
}

void ExportPanel::exportFrame()
{
    const QString path = outputPath();
    emit exportFrameRequested(path);
    m_ui->plainTextProgress->appendPlainText(
        QStringLiteral("Exporting frame... %1").arg(path));

    QSettings settings = exportSettings();
    settings.setValue(kSettingKey, exportDirectory());
}

} // namespace ui
} // namespace openvegas
