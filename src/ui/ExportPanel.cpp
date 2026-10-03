#include "ui/ExportPanel.h"

#include "ui_ExportPanel.h"

#include <QComboBox>
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
    outer->addWidget(body);

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

QString ExportPanel::currentPreset() const
{
    return m_ui->comboBoxPreset->currentText();
}

QString ExportPanel::extensionForPreset() const
{
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
    // A single frame is an image even when the contents preset is a movie.
    return buildOutputPath(extension == QLatin1String("mp4") || extension == QLatin1String("mov")
                               ? QStringLiteral("png") : extension);
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
