#include "ui/OptionsDialog.h"
#include "app/ProjectDefaults.h"

#include "ui_OptionsDialog.h"
#include "ui/Theme.h"

#include "app/Settings.h"
#include "app/Translations.h"

#include <QAction>
#include "media/AudioCapture.h"
#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHash>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTimeEdit>
#include <QVBoxLayout>

namespace openvegas {
namespace ui {

namespace {

const char* const kMaxUndo             = "Options/MaxUndo";
const char* const kDefaultTemplate     = "Options/DefaultTemplate";
const char* const kShotDuration        = "Options/CompositeShotDefaultDuration";
const char* const kAudioWaveforms      = "Options/AudioWaveforms";
const char* const kIncludeLayout       = "Options/IncludeScreenLayout";
const char* const kRelativePaths       = "Options/UseRelativePaths";
const char* const kCloseMediaInactive  = "Options/CloseMediaOnInactive";
const char* const kPlayAudioOnScrub    = "Options/PlayAudioOnScrub";
const char* const kLogWaveform         = "Options/LogWaveform";
const char* const kAnalytics           = "Options/Analytics";
const char* const kEditorDuration      = "Options/EditorDefaultDuration";
const char* const kPlaneDuration       = "Options/PlaneDefaultDuration";
const char* const kUseNativeMenuBar    = "Options/UseNativeMenuBar";
const char* const kShowMenuBarQuick    = "Options/ShowMenuBarQuickActions";
const char* const kWheelScrollMenus    = "Options/EnableWheelScrollMenus";
const char* const kUseNativePicker     = "Options/UseNativeColorPicker";
const char* const kHideFullScreen      = "Options/HideFullScreenPreview";
const char* const kTheme               = "Options/Theme";
const char* const kLanguage            = "Options/Language";
const char* const kMediaCacheDb        = "Options/MediaCacheDB";
const char* const kMediaCacheFiles     = "Options/MediaCacheFiles";
const char* const kDaysToKeepCache     = "Options/DaysToKeepMediaCacheFiles";
const char* const kTimelineCache       = "Options/TimelineCache";
const char* const kTimelineCacheDays   = "Options/TimelineCacheDays";
const char* const kAutomaticRenderCache = "Options/UseAutomaticRenderCache";
const char* const kRenderCacheDelay    = "Options/RenderCacheDelay";
const char* const kExportDir           = "Options/ExportDirectory";
const char* const kSnapshotDir         = "Options/SnapshotDirectory";
const char* const kTimeFormat          = "Options/TimeFormat";
const char* const kRemoveExt           = "Options/RemoveExtensions";
const char* const kBeepSpeaker         = "Options/BeepOnCompletion";
const char* const kThumbnailCacheMb    = "Options/ThumbnailCacheSizeMB";
const char* const kTurboRender         = "Options/TurboRendering";
const char* const kThreadCount         = "Options/RenderThreads";
const char* const kHardwareDecoding    = "Options/UseHardwareDecoding";
const char* const kHardwareEncoding    = "Options/UseHardwareEncoding";
const char* const kLimitDecode8Bit     = "Options/LimitVideoDecodingTo8bit";
const char* const kModelTextureMaxSize = "Options/ModelTextureMaxSize";
const char* const kShadowMapSize       = "Options/ShadowMapSize";
const char* const kReflectionMapSize   = "Options/ReflectionMapSize";
const char* const kAntialiasing        = "Options/Antialiasing";
const char* const kAutosaveEnabled     = "Options/AutoSaveEnabled";
const char* const kAutosaveSeconds     = "Options/AutoSaveIntervalSeconds";
const char* const kVoiceDevice         = "Options/Voiceover/Device";
const char* const kVoiceChannels       = "Options/Voiceover/Channels";
const char* const kVoiceSampleRate     = "Options/Voiceover/SampleRate";
const char* const kVoiceCountdown      = "Options/Voiceover/Countdown";
const char* const kVoiceMuteOutput     = "Options/Voiceover/MuteOutput";
const char* const kProxyPath           = "Options/ProxyDirectoryPath";
const char* const kPreRenderPath       = "Options/PreRenderDirectoryPath";
const char* const kProxyQuality        = "Options/ProxyQuality";
const char* const kPreviewMode         = "Options/PreviewMode";
const char* const kPreferIntegratedGpu = "Options/PreferIntegratedGPU";

struct PromptDefinition
{
    const char* objectName;
    const char* key;
    const char* text;
    bool defaultValue;
};

const PromptDefinition kPromptDefinitions[] = {
    {"checkBoxGPUWarning", "GPUWarning", "Warn when the GPU does not meet requirements", true},
    {"checkBoxGPUDriverWarning", "GPUDriverWarning", "Warn when the GPU driver is out of date", true},
    {"checkBoxOversizedAssets", "OversizedAssets", "Warn before importing oversized assets", true},
    {"checkBoxImageSequenceImportPrompt", "ImageSequenceImportPrompt", "Ask before importing an image sequence", true},
    {"checkBoxMediaMismatchPrompt", "MediaMismatchPrompt", "Ask when media settings differ from the project", true},
    {"checkBoxQuickTime", "QuickTimeWarning", "Show QuickTime compatibility warnings", true},
    {"checkBoxAdding3DCameras", "Adding3DCameras", "Ask before adding cameras from a 3D model", true},
    {"checkBoxRemoving3DCameras", "Removing3DCameras", "Ask before removing imported 3D cameras", true},
    {"checkBoxRemovingExportTasks", "RemovingExportTasks", "Ask before removing export tasks", true},
    {"checkBoxShowProjectSettings", "ShowProjectSettings", "Show project settings when creating a project", true},
};

struct ShortcutDefinition
{
    const char* actionName;
    const char* label;
    const char* defaultSequence;
};

const ShortcutDefinition kShortcutDefinitions[] = {
    {"actionNew", "New Project", "Ctrl+N"},
    {"actionOpen", "Open Project", "Ctrl+O"},
    {"actionSave", "Save Project", "Ctrl+S"},
    {"actionSaveAs", "Save Project As", "Ctrl+Alt+S"},
    {"actionExit", "Exit", "Alt+F4"},
    {"actionImport", "Import Media", "Ctrl+Shift+O"},
    {"actionRecordVoiceover", "Create New Voiceover", "Ctrl+Shift+R"},
    {"actionUndo", "Undo", "Ctrl+Z"},
    {"actionRedo", "Redo", "Ctrl+Y"},
    {"actionCut", "Cut", "Ctrl+X"},
    {"actionCopy", "Copy", "Ctrl+C"},
    {"actionPaste", "Paste", "Ctrl+V"},
    {"actionDuplicate", "Duplicate", "Ctrl+D"},
    {"actionSelectAll", "Select All", "Ctrl+A"},
    {"actionDelete", "Delete", "Delete"},
    {"actionRename", "Rename", "F2"},
    {"actionReset", "Reset", "Ctrl+R"},
    {"actionSlice", "Slice Selected Objects / Layers", "Ctrl+Shift+D"},
    {"actionPasteAttributes", "Paste Attributes", "Ctrl+Shift+V"},
    {"actionRemoveAttributes", "Remove Attributes", "Ctrl+Shift+X"},
    {"actionRemoveEffects", "Remove Effects", "Ctrl+Alt+X"},
    {"transportPlay", "Play / Pause", "Space"},
    {"transportLoop", "Loop Playback", "Ctrl+L"},
    {"transportStart", "Go To Start", "Home"},
    {"transportEnd", "Go To End", "End"},
    {"transportSetIn", "Set In Point", "I"},
    {"transportSetOut", "Set Out Point", "O"},
    {"transportPreviousFrame", "Previous Frame", ","},
    {"transportNextFrame", "Next Frame", "."},
    {"transportJumpBack10", "Jump Back by 10 Frames", "Shift+,"},
    {"transportJumpForward10", "Jump Forward by 10 Frames", "Shift+."},
    {"timelinePreviousKey", "Previous Keyframe", "Alt+,"},
    {"timelineNextKey", "Next Keyframe", "Alt+."},
    {"timelineNewLayerMenu", "New Layer Menu", "Ctrl+Alt+N"},
    {"transportShuttleBackward", "Shuttle Backward", "J"},
    {"transportShuttleStop", "Stop Shuttle", "K"},
    {"transportShuttleForward", "Shuttle Forward", "L"},
    {"actionFullScreenPreview", "Toggle Full Screen Preview", "Ctrl+Shift+F"},
    {"actionCloseActivePanel", "Close Active Panel", "Ctrl+W"},
};

QSettings optionSettings()
{
    return QSettings(QSettings::IniFormat, QSettings::UserScope,
                     app::Settings::organizationName(), app::Settings::applicationName());
}

QString defaultDataDirectory(const QString& child)
{
    return QDir::toNativeSeparators(
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
            .filePath(child));
}

QString shortcutKey(const QString& actionName)
{
    return QStringLiteral("Options/Shortcuts/") + actionName;
}

QString promptKey(const PromptDefinition& definition)
{
    return QStringLiteral("Options/Prompts/") + QLatin1String(definition.key);
}

const char* const kDefaultLabelNames[] = {
    QT_TRANSLATE_NOOP("openvegas::ui::OptionsDialog", "Red"),
    QT_TRANSLATE_NOOP("openvegas::ui::OptionsDialog", "Orange"),
    QT_TRANSLATE_NOOP("openvegas::ui::OptionsDialog", "Yellow"),
    QT_TRANSLATE_NOOP("openvegas::ui::OptionsDialog", "Green"),
    QT_TRANSLATE_NOOP("openvegas::ui::OptionsDialog", "Cyan"),
    QT_TRANSLATE_NOOP("openvegas::ui::OptionsDialog", "Blue"),
    QT_TRANSLATE_NOOP("openvegas::ui::OptionsDialog", "Purple"),
    QT_TRANSLATE_NOOP("openvegas::ui::OptionsDialog", "Pink")
};

const char* const kDefaultLabelColors[] = {
    "#c94b4b", "#d9823b", "#d1b849", "#55a868",
    "#4aa6a6", "#4f78b8", "#8662b0", "#b95f8a"
};

void setLabelColorButton(QPushButton* button, const QColor& color)
{
    const QColor valid = color.isValid() ? color : QColor(QStringLiteral("#808080"));
    button->setProperty("labelColor", valid.name());
    button->setText(valid.name().toUpper());
    button->setStyleSheet(QStringLiteral(
        "QPushButton { background:%1; color:%2; border:1px solid #555; }"
        "QPushButton:hover { border:1px solid white; }")
        .arg(valid.name(), valid.lightness() < 135 ? QStringLiteral("white")
                                                   : QStringLiteral("black")));
}

QWidget* makePageLabel(const QString& text)
{
    auto* label = new QLabel(text);
    label->setWordWrap(true);
    label->setStyleSheet(QStringLiteral("color: %1;")
                             .arg(themeColors().textDisabled.name()));
    return label;
}

} // namespace

OptionsDialog::OptionsDialog(QWidget* parent)
    : QDialog(parent)
    , m_ui(new Ui::OptionsDialog)
{
    m_ui->setupUi(this);
    setObjectName(QStringLiteral("optionsDialog"));
    resize(900, 640);

    m_ui->categoryList->addItems(categoryNames());
    m_ui->categoryList->setCurrentRow(0);

    // Pages stay in code: which page a row gets depends on categoryNames().
    // Keeping the list and stack in one place prevents the category/page drift
    // the former placeholder loop allowed.
    const QStringList& cats = categoryNames();
    m_ui->stack->addWidget(buildGeneralPage());   // 0 General
    m_ui->stack->addWidget(buildInterfacePage()); // 1 Interface
    for (int i = 2; i < cats.size(); ++i) {
        const QString& cat = cats.at(i);
        if (cat == QStringLiteral("Display")) {
            m_ui->stack->addWidget(buildDisplayPage());
        } else if (cat == QStringLiteral("Render")) {
            m_ui->stack->addWidget(buildRenderPage());
        } else if (cat == QStringLiteral("Quality Profiles")) {
            m_ui->stack->addWidget(buildQualityProfilesPage());
        } else if (cat == QStringLiteral("Prompts & Warnings")) {
            m_ui->stack->addWidget(buildPromptsPage());
        } else if (cat == QStringLiteral("Labels")) {
            m_ui->stack->addWidget(buildLabelsPage());
        } else if (cat == QStringLiteral("Cache")) {
            m_ui->stack->addWidget(buildCachePage());
        } else if (cat == QStringLiteral("Voiceover")) {
            m_ui->stack->addWidget(buildVoiceoverPage());
        } else if (cat == QStringLiteral("Proxies & Pre-Renders")) {
            m_ui->stack->addWidget(buildProxiesPage());
        } else if (cat == QStringLiteral("Auto Save")) {
            m_ui->stack->addWidget(buildAutoSavePage());
        } else if (cat == QStringLiteral("Shortcuts")) {
            m_ui->stack->addWidget(buildShortcutsPage());
        } else if (cat == QStringLiteral("Export")) {
            m_ui->stack->addWidget(buildExportPage());
        }
    }

    m_ui->footerNote->setStyleSheet(QStringLiteral("font-style: italic; color: %1;")
                                        .arg(themeColors().textDisabled.name()));

    connect(m_ui->btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_ui->btnRestoreDefaults, &QPushButton::clicked, this,
            &OptionsDialog::onRestoreDefaults);
    connect(m_ui->btnOK, &QPushButton::clicked, this, [this] {
        if (saveSettings()) accept();
    });
    updateRestoreLabel();

    connect(m_ui->categoryList, &QListWidget::currentRowChanged, this,
            &OptionsDialog::onCategoryChanged);
    connect(m_ui->categoryList, &QListWidget::currentRowChanged, this,
            [this](int row) { (void)row; updateRestoreLabel(); });

    loadSettings();
}

OptionsDialog::~OptionsDialog()
{
    delete m_ui;
}

void OptionsDialog::reject()
{
    // The window instance is reused by MainWindow. Discard edits here so a
    // cancelled value does not reappear when Options is opened a second time.
    loadSettings();
    QDialog::reject();
}

void OptionsDialog::applyShortcuts(QWidget* root)
{
    if (!root) {
        return;
    }
    const QSettings settings = optionSettings();
    for (const ShortcutDefinition& definition : kShortcutDefinitions) {
        if (QAction* action = root->findChild<QAction*>(
                QLatin1String(definition.actionName), Qt::FindChildrenRecursively)) {
            const QString stored = settings.value(
                shortcutKey(QLatin1String(definition.actionName)),
                QLatin1String(definition.defaultSequence)).toString();
            action->setShortcut(QKeySequence::fromString(stored, QKeySequence::PortableText));
        }
    }
}

const QStringList& OptionsDialog::categoryNames()
{
    static const QStringList names = {
        QStringLiteral("General"),
        QStringLiteral("Interface"),
        QStringLiteral("Display"),
        QStringLiteral("Render"),
        QStringLiteral("Quality Profiles"),
        QStringLiteral("Prompts & Warnings"),
        QStringLiteral("Labels"),
        QStringLiteral("Cache"),
        QStringLiteral("Voiceover"),
        QStringLiteral("Proxies & Pre-Renders"),
        QStringLiteral("Auto Save"),
        QStringLiteral("Shortcuts"),
        QStringLiteral("Export"),
    };
    return names;
}

QWidget* OptionsDialog::buildGeneralPage()
{
    auto* page = new QWidget;
    auto* form = new QFormLayout(page);
    form->setVerticalSpacing(12);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    m_maxUndo = new QSpinBox(page);
    m_maxUndo->setObjectName(QStringLiteral("spinBoxMaxUndo"));
    m_maxUndo->setRange(1, 1000);
    m_maxUndo->setValue(30);
    m_maxUndo->setSuffix(tr(" levels"));
    form->addRow(tr("Maximum Undo*:"), m_maxUndo);

    m_template = new QComboBox(page);
    m_template->setObjectName(QStringLiteral("comboBoxTemplate"));
    m_template->addItem(tr("1080p Full HD @ 30 fps"), QStringLiteral("fullhd30"));
    m_template->addItem(tr("1080p Full HD @ 60 fps"), QStringLiteral("fullhd60"));
    m_template->addItem(tr("4K UHD @ 30 fps"), QStringLiteral("uhd30"));
    form->addRow(tr("Default Template:"), m_template);

    m_shotDuration = new QTimeEdit(page);
    m_shotDuration->setObjectName(QStringLiteral("spinBoxCompDuration"));
    m_shotDuration->setDisplayFormat(QStringLiteral("hh:mm:ss.zzz"));
    m_shotDuration->setTime(QTime(0, 0, 30));
    form->addRow(tr("Composite Shot Default Duration:"), m_shotDuration);

    m_editorDuration = new QTimeEdit(page);
    m_editorDuration->setObjectName(QStringLiteral("spinBoxEditorDuration"));
    m_editorDuration->setDisplayFormat(QStringLiteral("hh:mm:ss.zzz"));
    m_editorDuration->setTime(QTime(0, 5, 0));
    form->addRow(tr("Editor Default Duration:"), m_editorDuration);

    m_planeDuration = new QTimeEdit(page);
    m_planeDuration->setObjectName(QStringLiteral("spinBoxPlaneDuration"));
    m_planeDuration->setDisplayFormat(QStringLiteral("hh:mm:ss.zzz"));
    m_planeDuration->setTime(QTime(0, 0, 30));
    form->addRow(tr("Plane Default Duration:"), m_planeDuration);

    m_waveforms = new QComboBox(page);
    m_waveforms->setObjectName(QStringLiteral("comboBoxAudioWaveforms"));
    m_waveforms->addItem(tr("RMS Amplitude"));
    m_waveforms->addItem(tr("Peak Amplitude"));
    form->addRow(tr("Audio Waveforms:"), m_waveforms);

    auto* checks = new QVBoxLayout;
    checks->setSpacing(8);
    m_includeLayout = new QCheckBox(
        tr("Include screen layout when saving projects"), page);
    m_includeLayout->setObjectName(QStringLiteral("checkBoxSaveScreenLayout"));
    m_relativePaths = new QCheckBox(
        tr("Use relative paths in saved projects"), page);
    m_relativePaths->setObjectName(QStringLiteral("checkBoxRelativePaths"));
    m_closeMediaOnInactive = new QCheckBox(
        tr("Close all media files when application is not active"), page);
    m_closeMediaOnInactive->setObjectName(QStringLiteral("checkBoxCloseOpenReload"));
    m_playAudioOnScrub = new QCheckBox(
        tr("Play audio when scrubbing timeline"), page);
    m_playAudioOnScrub->setObjectName(QStringLiteral("checkBoxPlayAudioScrubbing"));
    m_logWaveform = new QCheckBox(
        tr("Use logarithmic waveform scaling"), page);
    m_logWaveform->setObjectName(QStringLiteral("checkBoxLogarithmicWaveforms"));
    m_analytics = new QCheckBox(tr("Share anonymous usage analytics"), page);
    m_analytics->setObjectName(QStringLiteral("checkBoxAnalytics"));
    m_playAudioOnScrub->setChecked(true);
    m_logWaveform->setChecked(true);
    for (QCheckBox* chk : {m_includeLayout, m_relativePaths, m_closeMediaOnInactive,
                           m_playAudioOnScrub, m_logWaveform, m_analytics}) {
        checks->addWidget(chk);
        chk->setStyleSheet(QStringLiteral("QCheckBox::indicator:checked { "
                                          "background-color: %1; }")
                               .arg(themeColors().focus.name()));
    }
    form->addRow(checks);

    return page;
}

QWidget* OptionsDialog::buildInterfacePage()
{
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->setSpacing(8);

    // Language selector. Marked with '*' like the reference marks settings
    // that only take effect on the next start.
    auto* langForm = new QFormLayout();
    langForm->setContentsMargins(0, 0, 0, 0);
    m_language = new QComboBox(page);
    m_language->setObjectName(QStringLiteral("comboBoxOverrideLanguage"));
    m_language->addItem(tr("Follow system"), QString());
    for (const QString& loc : app::Translations::availableLocales()) {
        m_language->addItem(app::Translations::displayName(loc), loc);
    }
    langForm->addRow(tr("Language*:"), m_language);

    m_theme = new QComboBox(page);
    m_theme->setObjectName(QStringLiteral("comboBoxTheme"));
    m_theme->addItem(tr("VEGAS Dark"), QStringLiteral("Dark"));
    m_theme->addItem(tr("System"), QStringLiteral("System"));
    langForm->addRow(tr("Theme:"), m_theme);
    v->addLayout(langForm);

    m_useNativeMenuBar = new QCheckBox(
        tr("Use native menu bar"), page);
    m_useNativeMenuBar->setObjectName(QStringLiteral("checkBoxUseNativeMenuBar"));
    m_useNativeColorPicker = new QCheckBox(
        tr("Use native color picker"), page);
    m_useNativeColorPicker->setObjectName(QStringLiteral("checkBoxUseNativeColorPicker"));
    m_enableHighDpi = new QCheckBox(tr("Enable high-DPI scaling*"), page);
    m_enableHighDpi->setObjectName(QStringLiteral("checkBoxEnableHighDPI"));
    m_showMenuBarQuickActions = new QCheckBox(
        tr("Show menu bar quick actions"), page);
    m_showMenuBarQuickActions->setObjectName(QStringLiteral("checkBoxShowQuickActions"));
    m_wheelScrollMenus = new QCheckBox(
        tr("Enable mouse wheel scrolling in dropdown menus"), page);
    m_wheelScrollMenus->setObjectName(QStringLiteral("checkBoxEnableComboBoxScroll"));
    m_hideFullScreenPreview = new QCheckBox(
        tr("Hide panels while full-screen preview is active"), page);
    m_hideFullScreenPreview->setObjectName(QStringLiteral("checkBoxHideFullScreenPreview"));
    for (QCheckBox* chk : {m_useNativeMenuBar, m_useNativeColorPicker, m_enableHighDpi,
                           m_showMenuBarQuickActions, m_wheelScrollMenus,
                           m_hideFullScreenPreview}) {
        v->addWidget(chk);
        chk->setStyleSheet(QStringLiteral("QCheckBox::indicator:checked { "
                                          "background-color: %1; }")
                               .arg(themeColors().focus.name()));
    }
    v->addStretch();
    return page;
}

QWidget* OptionsDialog::buildExportPage()
{
    auto* page = new QWidget;
    auto* form = new QFormLayout(page);
    form->setVerticalSpacing(12);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    auto makeDirRow = [this, page](QLineEdit** field, const QString& objectName,
                                   const QString& placeholder) {
        auto* row = new QHBoxLayout;
        row->setSpacing(6);
        *field = new QLineEdit(page);
        (*field)->setObjectName(objectName);
        (*field)->setPlaceholderText(placeholder);
        row->addWidget(*field, 1);
        auto* browse = new QPushButton(tr("Select Folder..."), page);
        browse->setObjectName(QStringLiteral("btnSelectFolder"));
        connect(browse, &QPushButton::clicked, this,
                [this, field] { browseDirectory(*field); });
        row->addWidget(browse);
        return row;
    };

    form->addRow(tr("Default Export Directory:"),
                 makeDirRow(&m_exportDir, QStringLiteral("lineEditExportDirectory"),
                            QStringLiteral("C:\\Users\\VEGAS Effects")));
    form->addRow(tr("Default Snapshot Directory:"),
                 makeDirRow(&m_snapshotDir, QStringLiteral("lineEditSnapshotDirectory"),
                            QStringLiteral("ExportSnapshots")));

    m_timeFormat = new QComboBox(page);
    m_timeFormat->setObjectName(QStringLiteral("comboBoxTimeFormat"));
    m_timeFormat->addItem(tr("Timecode"));
    m_timeFormat->addItem(tr("Frames"));
    m_timeFormat->addItem(tr("SMPTE"));
    form->addRow(tr("Time Format:"), m_timeFormat);

    auto* checks = new QVBoxLayout;
    checks->setSpacing(8);
    m_removeExt = new QCheckBox(
        tr("Remove known file extensions from export names"), page);
    m_removeExt->setObjectName(QStringLiteral("checkBoxRemoveExtensions"));
    m_beepSpeaker = new QCheckBox(
        tr("Beep speaker on completion"), page);
    m_beepSpeaker->setObjectName(QStringLiteral("checkBoxBeepSpeaker"));
    m_removeExt->setChecked(true);
    m_beepSpeaker->setChecked(true);
    for (QCheckBox* chk : {m_removeExt, m_beepSpeaker}) {
        checks->addWidget(chk);
        chk->setStyleSheet(QStringLiteral("QCheckBox::indicator:checked { "
                                          "background-color: %1; }")
                               .arg(themeColors().focus.name()));
    }
    form->addRow(checks);

    return page;
}

void OptionsDialog::browseDirectory(QLineEdit* target)
{
    const QString start = target->text().trimmed();
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Select Folder"),
        start.isEmpty() ? QDir::homePath() : start);
    if (!dir.isEmpty()) {
        target->setText(QDir::toNativeSeparators(dir));
    }
}

void OptionsDialog::browseFile(QLineEdit* target, const QString& filter)
{
    const QFileInfo current(target->text().trimmed());
    const QString start = current.exists() ? current.absoluteFilePath()
                                           : current.absolutePath();
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Select File"), start, filter);
    if (!path.isEmpty()) {
        target->setText(QDir::toNativeSeparators(path));
    }
}

void OptionsDialog::clearManagedPath(const QString& path, const QString& description,
                                     bool removeRoot)
{
    const QFileInfo info(path);
    const QString absolute = info.absoluteFilePath();
    const QString home = QDir::cleanPath(QDir::homePath());
    const QString root = QDir::cleanPath(QDir(absolute).rootPath());
    if (path.trimmed().isEmpty() || QDir::cleanPath(absolute) == home
        || QDir::cleanPath(absolute) == root) {
        QMessageBox::warning(this, tr("Clear Cache"),
                             tr("The selected path is not a safe cache location."));
        return;
    }
    if (!info.exists()) {
        QMessageBox::information(this, tr("Clear Cache"),
                                 tr("%1 is already empty.").arg(description));
        return;
    }
    if (QMessageBox::question(this, tr("Clear Cache"),
            tr("Delete %1 at\n%2?").arg(description, QDir::toNativeSeparators(absolute)),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) {
        return;
    }

    bool ok = true;
    if (info.isFile()) {
        ok = QFile::remove(absolute);
    } else if (removeRoot) {
        ok = QDir(absolute).removeRecursively();
    } else {
        QDir directory(absolute);
        const QFileInfoList entries = directory.entryInfoList(
            QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System);
        for (const QFileInfo& entry : entries) {
            ok = (entry.isDir() ? QDir(entry.absoluteFilePath()).removeRecursively()
                                : QFile::remove(entry.absoluteFilePath())) && ok;
        }
    }
    if (!ok) {
        QMessageBox::warning(this, tr("Clear Cache"),
                             tr("Some files in %1 could not be removed.").arg(description));
    }
}

// Cache page. Field set and semantics come from the reference: the media cache
// is addressed by two paths and pruned after a number of days, capped at 365
// with a default of 30 (Settings mirrors the same clamping).
QWidget* OptionsDialog::buildCachePage()
{
    auto* page = new QWidget(this);
    auto* root = new QVBoxLayout(page);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(10);

    auto* mediaGroup = new QGroupBox(tr("Media Cache"), page);
    mediaGroup->setObjectName(QStringLiteral("groupBoxCacheMedia"));
    auto* form = new QFormLayout(mediaGroup);
    form->setSpacing(8);

    auto* dbRow = new QHBoxLayout();
    m_cacheDbPath = new QLineEdit(page);
    m_cacheDbPath->setObjectName(QStringLiteral("lineEditMediaCacheDB"));
    dbRow->addWidget(m_cacheDbPath, 1);
    auto* dbBrowse = new QPushButton(tr("..."), page);
    dbBrowse->setObjectName(QStringLiteral("toolButtonSelectMediaCacheDB"));
    dbBrowse->setFixedWidth(32);
    connect(dbBrowse, &QPushButton::clicked, this,
            [this] { browseFile(m_cacheDbPath, tr("Cache database (*.db);;All files (*)")); });
    dbRow->addWidget(dbBrowse);
    form->addRow(tr("Media Cache Database*:"), dbRow);

    auto* filesRow = new QHBoxLayout();
    m_cacheFilesPath = new QLineEdit(page);
    m_cacheFilesPath->setObjectName(QStringLiteral("lineEditMediaCacheFiles"));
    filesRow->addWidget(m_cacheFilesPath, 1);
    auto* filesBrowse = new QPushButton(tr("..."), page);
    filesBrowse->setObjectName(QStringLiteral("toolButtonSelectMediaCacheFiles"));
    filesBrowse->setFixedWidth(32);
    connect(filesBrowse, &QPushButton::clicked, this,
            [this] { browseDirectory(m_cacheFilesPath); });
    filesRow->addWidget(filesBrowse);
    form->addRow(tr("Media Cache Files*:"), filesRow);

    m_daysToKeepCache = new QSpinBox(page);
    m_daysToKeepCache->setObjectName(QStringLiteral("spinBoxDaysToKeepMediaCacheFiles"));
    m_daysToKeepCache->setRange(0, app::Settings::maxDaysToKeepMediaCacheFiles());
    m_daysToKeepCache->setValue(app::Settings::defaultDaysToKeepMediaCacheFiles());
    m_daysToKeepCache->setSuffix(tr(" day(s)"));
    m_daysToKeepCache->setSpecialValueText(tr("Keep forever"));
    form->addRow(tr("Days To Keep Media Cache Files:"), m_daysToKeepCache);

    auto* clearMedia = new QPushButton(tr("Delete Media Cache"), mediaGroup);
    clearMedia->setObjectName(QStringLiteral("toolButtonDeleteCache"));
    connect(clearMedia, &QPushButton::clicked, this, [this] {
        clearManagedPath(m_cacheFilesPath->text(), tr("media cache files"));
        clearManagedPath(m_cacheDbPath->text(), tr("media cache database"), true);
    });
    form->addRow(clearMedia);
    root->addWidget(mediaGroup);

    auto* timelineGroup = new QGroupBox(tr("Timeline Cache"), page);
    timelineGroup->setObjectName(QStringLiteral("groupBoxCacheTimeline"));
    auto* timelineForm = new QFormLayout(timelineGroup);
    auto* pathRow = new QHBoxLayout;
    m_timelineCachePath = new QLineEdit(timelineGroup);
    m_timelineCachePath->setObjectName(QStringLiteral("lineEditTimelineCache"));
    pathRow->addWidget(m_timelineCachePath, 1);
    auto* browseTimeline = new QPushButton(tr("..."), timelineGroup);
    browseTimeline->setObjectName(QStringLiteral("toolButtonSelectTimelineCache"));
    browseTimeline->setFixedWidth(32);
    connect(browseTimeline, &QPushButton::clicked, this,
            [this] { browseDirectory(m_timelineCachePath); });
    pathRow->addWidget(browseTimeline);
    timelineForm->addRow(tr("Timeline Cache:"), pathRow);

    m_timelineCacheDays = new QSpinBox(timelineGroup);
    m_timelineCacheDays->setObjectName(QStringLiteral("spinBoxTimelineDays"));
    m_timelineCacheDays->setRange(0, 365);
    m_timelineCacheDays->setSpecialValueText(tr("Keep forever"));
    m_timelineCacheDays->setSuffix(tr(" day(s)"));
    timelineForm->addRow(tr("Days To Keep Timeline Cache:"), m_timelineCacheDays);

    m_automaticRenderCache = new QCheckBox(tr("Use automatic render cache"), timelineGroup);
    m_automaticRenderCache->setObjectName(QStringLiteral("checkBoxUseAutomaticRenderCache"));
    timelineForm->addRow(m_automaticRenderCache);
    m_renderCacheDelay = new QSpinBox(timelineGroup);
    m_renderCacheDelay->setObjectName(QStringLiteral("spinBoxRenderCacheDelay"));
    m_renderCacheDelay->setRange(0, 60);
    m_renderCacheDelay->setSuffix(tr(" sec"));
    timelineForm->addRow(tr("Automatic cache delay:"), m_renderCacheDelay);
    connect(m_automaticRenderCache, &QCheckBox::toggled,
            m_renderCacheDelay, &QWidget::setEnabled);

    auto* clearTimeline = new QPushButton(tr("Delete Timeline Cache"), timelineGroup);
    clearTimeline->setObjectName(QStringLiteral("toolButtonDeleteTimelineCache"));
    connect(clearTimeline, &QPushButton::clicked, this, [this] {
        clearManagedPath(m_timelineCachePath->text(), tr("timeline cache"));
    });
    timelineForm->addRow(clearTimeline);
    root->addWidget(timelineGroup);
    root->addStretch();

    return page;
}

QWidget* OptionsDialog::buildDisplayPage()
{
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);
    form->setContentsMargins(16, 16, 16, 16);
    form->setSpacing(8);

    m_thumbnailCacheMb = new QSpinBox(page);
    m_thumbnailCacheMb->setObjectName(QStringLiteral("spinBoxThumbnailCacheMB"));
    m_thumbnailCacheMb->setRange(1, 2048);
    m_thumbnailCacheMb->setSuffix(tr(" MB"));
    form->addRow(tr("Thumbnail cache size:"), m_thumbnailCacheMb);

    auto* checks = new QVBoxLayout;
    checks->setSpacing(8);
    // Reference Preferences Viewer page (FUN_1403a6bb0), whose group box is
    // "Viewer". All four are read where they matter - the overlay and the
    // checkerboard in ViewerWidget, the playback tick in MainWindow - rather
    // than only stored.
    m_checkerboard = new QCheckBox(tr("Show checkerboard background for 2D Views"), page);
    m_checkerboard->setObjectName(QStringLiteral("checkBoxShowCheckerboard2D"));
    m_showMouseCoordinates = new QCheckBox(tr("Show mouse coordinates"), page);
    m_showMouseCoordinates->setObjectName(QStringLiteral("checkBoxShowMouseCoordinates"));
    m_enablePlaybackUpdate = new QCheckBox(tr("Update the viewer during playback"), page);
    m_enablePlaybackUpdate->setObjectName(QStringLiteral("checkBoxEnablePlaybackUpdate"));
    m_showMotionPath = new QCheckBox(tr("Show motion path"), page);
    m_showMotionPath->setObjectName(QStringLiteral("checkBoxShowMotionPath"));
    m_checkerboard3d = new QCheckBox(tr("Show checkerboard background for 3D Views"), page);
    m_checkerboard3d->setObjectName(QStringLiteral("checkBoxShowCheckerboard3D"));
    m_showFloorPlane = new QCheckBox(tr("Show floor plane in 3D Views"), page);
    m_showFloorPlane->setObjectName(QStringLiteral("checkBoxShowFloorPlane"));
    for (QCheckBox* chk : {m_checkerboard, m_checkerboard3d, m_showMouseCoordinates,
                           m_enablePlaybackUpdate, m_showMotionPath, m_showFloorPlane}) {
        checks->addWidget(chk);
        chk->setStyleSheet(QStringLiteral("QCheckBox::indicator:checked { "
                                          "background-color: %1; }")
                               .arg(themeColors().focus.name()));
    }
    form->addRow(checks);

    // The reference pairs the motion-path toggle with a key count
    // (labelMotionPathFrames / spinBoxMotionPathFrames) so a long animation
    // does not bury the frame under its own path.
    m_motionPathFrames = new QSpinBox(page);
    m_motionPathFrames->setObjectName(QStringLiteral("spinBoxMotionPathFrames"));
    m_motionPathFrames->setRange(1, 1000);
    form->addRow(tr("Motion path key frames:"), m_motionPathFrames);
    connect(m_showMotionPath, &QCheckBox::toggled,
            m_motionPathFrames, &QWidget::setEnabled);
    return page;
}

QWidget* OptionsDialog::buildRenderPage()
{
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);
    form->setContentsMargins(16, 16, 16, 16);
    form->setSpacing(8);

    m_turboRender = new QCheckBox(tr("Use turbo rendering where available"), page);
    m_turboRender->setObjectName(QStringLiteral("checkBoxTurboRendering"));
    m_turboRender->setStyleSheet(QStringLiteral("QCheckBox::indicator:checked { "
                                                "background-color: %1; }")
                                     .arg(themeColors().focus.name()));
    form->addRow(m_turboRender);

    m_threadCount = new QComboBox(page);
    m_threadCount->setObjectName(QStringLiteral("comboBoxRenderThreads"));
    m_threadCount->addItem(tr("Automatic (default)"), 0);
    for (int n = 1; n <= 8; ++n) {
        m_threadCount->addItem(tr("%1 thread(s)").arg(n), n);
    }
    form->addRow(tr("Render threads:"), m_threadCount);

    m_hardwareDecoding = new QCheckBox(tr("Use hardware video decoding"), page);
    m_hardwareDecoding->setObjectName(QStringLiteral("checkBoxUseHardwareDecoding"));
    form->addRow(m_hardwareDecoding);
    m_hardwareEncoding = new QCheckBox(tr("Use hardware video encoding"), page);
    m_hardwareEncoding->setObjectName(QStringLiteral("checkBoxUseHardwareEncoding"));
    form->addRow(m_hardwareEncoding);
    m_limitDecode8Bit = new QCheckBox(tr("Limit video decoding to 8 bit"), page);
    m_limitDecode8Bit->setObjectName(QStringLiteral("checkBoxLimitVideoDecodingTo8bit"));
    form->addRow(m_limitDecode8Bit);

    auto makePowerOfTwoSpin = [page](const QString& name) {
        auto* spin = new QSpinBox(page);
        spin->setObjectName(name);
        spin->setRange(256, 16384);
        spin->setSingleStep(256);
        spin->setSuffix(QObject::tr(" px"));
        return spin;
    };
    m_modelTextureMaxSize = makePowerOfTwoSpin(QStringLiteral("spinBoxModelTextureMaxSize"));
    m_shadowMapSize = makePowerOfTwoSpin(QStringLiteral("spinBoxShadowMapSize"));
    m_reflectionMapSize = makePowerOfTwoSpin(QStringLiteral("spinBoxReflectionMapSize"));
    form->addRow(tr("Maximum model texture size:"), m_modelTextureMaxSize);
    form->addRow(tr("Shadow map size:"), m_shadowMapSize);
    form->addRow(tr("Reflection map size:"), m_reflectionMapSize);

    m_antialiasing = new QComboBox(page);
    m_antialiasing->setObjectName(QStringLiteral("comboBoxAntialiasing"));
    m_antialiasing->addItems({tr("Off"), tr("2x MSAA"), tr("4x MSAA"), tr("8x MSAA")});
    form->addRow(tr("3D antialiasing:"), m_antialiasing);
    return page;
}

QWidget* OptionsDialog::buildQualityProfilesPage()
{
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);
    form->setContentsMargins(16, 16, 16, 16);
    form->setSpacing(8);

    auto makeQuality = [page](const QString& name) {
        auto* combo = new QComboBox(page);
        combo->setObjectName(name);
        for (const QString& value : {QStringLiteral("Final"), QStringLiteral("Draft"),
                                     QStringLiteral("Quick"), QStringLiteral("Fastest")}) {
            combo->addItem(value, value);
        }
        return combo;
    };
    auto makeResolution = [page](const QString& name) {
        auto* combo = new QComboBox(page);
        combo->setObjectName(name);
        for (const QString& value : {QStringLiteral("Antialiased"), QStringLiteral("Full"),
                                     QStringLiteral("1/2"), QStringLiteral("1/4")}) {
            combo->addItem(value, value);
        }
        return combo;
    };
    m_playbackQuality = makeQuality(QStringLiteral("comboBoxPlaybackQualityProfile"));
    m_pausedQuality = makeQuality(QStringLiteral("comboBoxPausedQualityProfile"));
    m_playbackResolution = makeResolution(QStringLiteral("comboBoxPlaybackDownsampleMode"));
    m_pausedResolution = makeResolution(QStringLiteral("comboBoxPausedDownsampleMode"));
    form->addRow(tr("Playback quality:"), m_playbackQuality);
    form->addRow(tr("Playback resolution:"), m_playbackResolution);
    form->addRow(tr("Paused quality:"), m_pausedQuality);
    form->addRow(tr("Paused resolution:"), m_pausedResolution);
    return page;
}

QWidget* OptionsDialog::buildPromptsPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(7);
    m_promptChecks.clear();
    for (const PromptDefinition& definition : kPromptDefinitions) {
        auto* check = new QCheckBox(tr(definition.text), page);
        check->setObjectName(QLatin1String(definition.objectName));
        layout->addWidget(check);
        m_promptChecks.push_back(check);
    }
    layout->addStretch();
    return page;
}

QWidget* OptionsDialog::buildLabelsPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->addWidget(makePageLabel(
        tr("Labels are available to timeline layers and media items. Edit a name or click its color.")));

    auto* table = new QTableWidget(8, 2, page);
    table->setObjectName(QStringLiteral("tableLabels"));
    table->setHorizontalHeaderLabels({tr("Label"), tr("Color")});
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setStretchLastSection(false);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    m_labelNames.clear();
    m_labelColors.clear();
    for (int row = 0; row < 8; ++row) {
        auto* name = new QLineEdit(table);
        name->setObjectName(QStringLiteral("lineEditLabel%1").arg(row + 1));
        auto* color = new QPushButton(table);
        color->setObjectName(QStringLiteral("buttonLabelColor%1").arg(row + 1));
        setLabelColorButton(color, QColor(QLatin1String(kDefaultLabelColors[row])));
        connect(color, &QPushButton::clicked, this, [this, color] {
            const QColor selected = interfaceColor(
                QColor(color->property("labelColor").toString()), this, tr("Choose Label Color"));
            if (selected.isValid()) {
                setLabelColorButton(color, selected);
            }
        });
        table->setCellWidget(row, 0, name);
        table->setCellWidget(row, 1, color);
        m_labelNames.push_back(name);
        m_labelColors.push_back(color);
    }
    layout->addWidget(table, 1);
    return page;
}

QWidget* OptionsDialog::buildVoiceoverPage()
{
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);
    form->setContentsMargins(16, 16, 16, 16);
    form->setSpacing(10);

    m_voiceDevice = new QComboBox(page);
    m_voiceDevice->setObjectName(QStringLiteral("comboBoxVoiceoverDevice"));
    m_voiceDevice->addItem(tr("System Default"), QStringLiteral("default"));
    for (const auto& device : media::audioInputDevices())
        m_voiceDevice->addItem(device.description, device.id);
    form->addRow(tr("Input device:"), m_voiceDevice);

    m_voiceChannels = new QComboBox(page);
    m_voiceChannels->setObjectName(QStringLiteral("comboBoxVoiceoverChannels"));
    m_voiceChannels->addItem(tr("Mono"), 1);
    m_voiceChannels->addItem(tr("Stereo"), 2);
    form->addRow(tr("Channels:"), m_voiceChannels);

    m_voiceSampleRate = new QComboBox(page);
    m_voiceSampleRate->setObjectName(QStringLiteral("comboBoxVoiceoverSampleRate"));
    for (int rate : {44100, 48000, 96000}) {
        m_voiceSampleRate->addItem(tr("%1 Hz").arg(rate), rate);
    }
    form->addRow(tr("Sample rate:"), m_voiceSampleRate);

    m_voiceCountdown = new QSpinBox(page);
    m_voiceCountdown->setObjectName(QStringLiteral("spinBoxVoiceoverCountdown"));
    m_voiceCountdown->setRange(0, 10);
    m_voiceCountdown->setSuffix(tr(" sec"));
    form->addRow(tr("Recording countdown:"), m_voiceCountdown);

    m_voiceMuteOutput = new QCheckBox(tr("Mute project audio while recording"), page);
    m_voiceMuteOutput->setObjectName(QStringLiteral("checkBoxVoiceoverMuteOutput"));
    form->addRow(m_voiceMuteOutput);
    return page;
}

QWidget* OptionsDialog::buildProxiesPage()
{
    auto* page = new QWidget(this);
    auto* root = new QVBoxLayout(page);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(10);

    auto makePathRow = [this](QWidget* parent, QLineEdit** edit, const QString& editName,
                              const QString& browseName) {
        auto* row = new QHBoxLayout;
        *edit = new QLineEdit(parent);
        (*edit)->setObjectName(editName);
        row->addWidget(*edit, 1);
        auto* browse = new QPushButton(tr("..."), parent);
        browse->setObjectName(browseName);
        browse->setFixedWidth(32);
        connect(browse, &QPushButton::clicked, this,
                [this, edit] { browseDirectory(*edit); });
        row->addWidget(browse);
        return row;
    };

    auto* proxyGroup = new QGroupBox(tr("Proxy"), page);
    proxyGroup->setObjectName(QStringLiteral("widgetProxy"));
    auto* proxyForm = new QFormLayout(proxyGroup);
    proxyForm->addRow(tr("Proxy directory:"),
        makePathRow(proxyGroup, &m_proxyPath, QStringLiteral("lineEditProxyDirectoryPath"),
                    QStringLiteral("toolButtonSelectProxyDirectoryPath")));
    m_proxyQuality = new QComboBox(proxyGroup);
    m_proxyQuality->setObjectName(QStringLiteral("comboBoxProxyQuality"));
    m_proxyQuality->addItems({tr("Low"), tr("Medium"), tr("High")});
    proxyForm->addRow(tr("Proxy quality:"), m_proxyQuality);

    auto* proxyButtons = new QHBoxLayout;
    auto* deleteProjectProxy = new QPushButton(tr("Delete Project Proxies"), proxyGroup);
    deleteProjectProxy->setObjectName(QStringLiteral("toolButtonDeleteProxiesForProject"));
    auto* deleteAllProxy = new QPushButton(tr("Delete All Proxies"), proxyGroup);
    deleteAllProxy->setObjectName(QStringLiteral("toolButtonDeleteAllProxies"));
    proxyButtons->addWidget(deleteProjectProxy);
    proxyButtons->addWidget(deleteAllProxy);
    proxyForm->addRow(proxyButtons);
    connect(deleteAllProxy, &QPushButton::clicked, this, [this] {
        clearManagedPath(m_proxyPath->text(), tr("all proxy files"));
    });
    connect(deleteProjectProxy, &QPushButton::clicked, this, [this] {
        const QString proxyRoot = m_proxyPath->text().trimmed();
        if (proxyRoot.isEmpty()) {
            QMessageBox::warning(this, tr("Delete Project Proxies"),
                                 tr("Choose a proxy directory first."));
            return;
        }
        const QString project = QFileInfo(optionSettings().value(
            QStringLiteral("general/lastProjectPath")).toString()).completeBaseName();
        if (project.isEmpty()) {
            QMessageBox::information(this, tr("Delete Project Proxies"),
                                     tr("No saved project is currently selected."));
            return;
        }
        clearManagedPath(QDir(proxyRoot).filePath(project),
                         tr("proxies for project '%1'").arg(project), true);
    });
    root->addWidget(proxyGroup);

    auto* preRenderGroup = new QGroupBox(tr("Pre-Render"), page);
    preRenderGroup->setObjectName(QStringLiteral("widgetPreRender"));
    auto* preRenderForm = new QFormLayout(preRenderGroup);
    preRenderForm->addRow(tr("Pre-render directory:"),
        makePathRow(preRenderGroup, &m_preRenderPath,
                    QStringLiteral("lineEditPreRenderDirectoryPath"),
                    QStringLiteral("toolButtonSelectPreRenderDirectoryPath")));
    m_previewMode = new QComboBox(preRenderGroup);
    m_previewMode->setObjectName(QStringLiteral("comboBoxPreviewMode"));
    m_previewMode->addItems({tr("Auto"), tr("Proxy"), tr("Full Resolution")});
    preRenderForm->addRow(tr("Preview mode:"), m_previewMode);
    auto* preRenderButtons = new QHBoxLayout;
    auto* deleteProjectPreRender = new QPushButton(tr("Delete Project Pre-Renders"), preRenderGroup);
    deleteProjectPreRender->setObjectName(QStringLiteral("toolButtonDeletePreRendersForProject"));
    auto* deleteAllPreRender = new QPushButton(tr("Delete All Pre-Renders"), preRenderGroup);
    deleteAllPreRender->setObjectName(QStringLiteral("toolButtonDeleteAllPreRenders"));
    preRenderButtons->addWidget(deleteProjectPreRender);
    preRenderButtons->addWidget(deleteAllPreRender);
    preRenderForm->addRow(preRenderButtons);
    connect(deleteAllPreRender, &QPushButton::clicked, this, [this] {
        clearManagedPath(m_preRenderPath->text(), tr("all pre-render files"));
    });
    connect(deleteProjectPreRender, &QPushButton::clicked, this, [this] {
        const QString preRenderRoot = m_preRenderPath->text().trimmed();
        if (preRenderRoot.isEmpty()) {
            QMessageBox::warning(this, tr("Delete Project Pre-Renders"),
                                 tr("Choose a pre-render directory first."));
            return;
        }
        const QString project = QFileInfo(optionSettings().value(
            QStringLiteral("general/lastProjectPath")).toString()).completeBaseName();
        if (project.isEmpty()) {
            QMessageBox::information(this, tr("Delete Project Pre-Renders"),
                                     tr("No saved project is currently selected."));
            return;
        }
        clearManagedPath(QDir(preRenderRoot).filePath(project),
                         tr("pre-renders for project '%1'").arg(project), true);
    });
    root->addWidget(preRenderGroup);

    m_preferIntegratedGpu = new QCheckBox(tr("Prefer integrated GPU for proxy generation"), page);
    m_preferIntegratedGpu->setObjectName(QStringLiteral("checkBoxPreferIntegratedGPU"));
    root->addWidget(m_preferIntegratedGpu);
    root->addStretch();
    return page;
}

QWidget* OptionsDialog::buildShortcutsPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(8, 8, 8, 8);

    m_shortcutSearch = new QLineEdit(page);
    m_shortcutSearch->setObjectName(QStringLiteral("lineEditShortcutSearch"));
    m_shortcutSearch->setPlaceholderText(tr("Search commands"));
    layout->addWidget(m_shortcutSearch);

    const int count = static_cast<int>(sizeof(kShortcutDefinitions) / sizeof(kShortcutDefinitions[0]));
    m_shortcutTable = new QTableWidget(count, 2, page);
    m_shortcutTable->setObjectName(QStringLiteral("tableShortcuts"));
    m_shortcutTable->setHorizontalHeaderLabels({tr("Command"), tr("Shortcut")});
    m_shortcutTable->verticalHeader()->setVisible(false);
    m_shortcutTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_shortcutTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_shortcutTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_shortcutEdits.clear();
    for (int row = 0; row < count; ++row) {
        const ShortcutDefinition& definition = kShortcutDefinitions[row];
        auto* command = new QTableWidgetItem(tr(definition.label));
        command->setData(Qt::UserRole, QLatin1String(definition.actionName));
        command->setFlags(command->flags() & ~Qt::ItemIsEditable);
        m_shortcutTable->setItem(row, 0, command);
        auto* edit = new QKeySequenceEdit(
            QKeySequence::fromString(QLatin1String(definition.defaultSequence),
                                     QKeySequence::PortableText), m_shortcutTable);
        edit->setObjectName(QStringLiteral("shortcut_%1").arg(
            QLatin1String(definition.actionName)));
        m_shortcutTable->setCellWidget(row, 1, edit);
        m_shortcutEdits.push_back(edit);
    }
    layout->addWidget(m_shortcutTable, 1);
    connect(m_shortcutSearch, &QLineEdit::textChanged, this, [this](const QString& text) {
        for (int row = 0; row < m_shortcutTable->rowCount(); ++row) {
            m_shortcutTable->setRowHidden(
                row, !m_shortcutTable->item(row, 0)->text().contains(text, Qt::CaseInsensitive));
        }
    });
    return page;
}

QWidget* OptionsDialog::buildAutoSavePage()
{
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);
    form->setContentsMargins(16, 16, 16, 16);
    form->setSpacing(8);

    m_autosaveEnabled = new QCheckBox(tr("Enable automatic backups"), page);
    m_autosaveEnabled->setObjectName(QStringLiteral("checkBoxAutoSaving"));
    m_autosaveEnabled->setStyleSheet(QStringLiteral("QCheckBox::indicator:checked { "
                                                    "background-color: %1; }")
                                         .arg(themeColors().focus.name()));
    form->addRow(m_autosaveEnabled);

    m_autosaveSeconds = new QSpinBox(page);
    m_autosaveSeconds->setObjectName(QStringLiteral("spinBoxSaveFrequency"));
    m_autosaveSeconds->setRange(30, 3600);
    m_autosaveSeconds->setSingleStep(30);
    m_autosaveSeconds->setSuffix(tr(" sec"));
    form->addRow(tr("Autosave every:"), m_autosaveSeconds);
    connect(m_autosaveEnabled, &QCheckBox::toggled,
            m_autosaveSeconds, &QWidget::setEnabled);

    form->addRow(makePageLabel(
        tr("Autosave writes to the project's folder beside the .vegfx file, "
           "kept separate from the manual Save As.")));
    return page;
}

void OptionsDialog::onCategoryChanged(int row)
{
    if (m_ui->stack && row >= 0 && row < m_ui->stack->count()) {
        m_ui->stack->setCurrentIndex(row);
    }
}

void OptionsDialog::onRestoreDefaults()
{
    const int idx = currentIndex();
    const QString name = categoryNames().value(idx);
    if (name == QStringLiteral("General")) {
        m_maxUndo->setValue(30);
        m_template->setCurrentIndex(0);
        m_shotDuration->setTime(QTime(0, 0, 30));
        m_editorDuration->setTime(QTime(0, 5, 0));
        m_planeDuration->setTime(QTime(0, 0, 30));
        m_waveforms->setCurrentIndex(0);
        m_includeLayout->setChecked(false);
        m_relativePaths->setChecked(false);
        m_closeMediaOnInactive->setChecked(false);
        m_playAudioOnScrub->setChecked(true);
        m_logWaveform->setChecked(true);
        m_analytics->setChecked(false);
    } else if (name == QStringLiteral("Interface")) {
        m_language->setCurrentIndex(0);
        m_theme->setCurrentIndex(0);
        m_useNativeMenuBar->setChecked(true);
        m_useNativeColorPicker->setChecked(true);
        m_enableHighDpi->setChecked(true);
        m_showMenuBarQuickActions->setChecked(true);
        m_wheelScrollMenus->setChecked(false);
        m_hideFullScreenPreview->setChecked(false);
    } else if (name == QStringLiteral("Display")) {
        m_thumbnailCacheMb->setValue(1024);
        // Reference defaults for its Viewer preferences: the checkerboard on,
        // the coordinates off, the viewer updating during playback.
        m_checkerboard->setChecked(true);
        m_showMouseCoordinates->setChecked(false);
        m_enablePlaybackUpdate->setChecked(true);
        m_showMotionPath->setChecked(false);
        m_motionPathFrames->setValue(30);
        m_checkerboard3d->setChecked(true);
        m_showFloorPlane->setChecked(true);
    } else if (name == QStringLiteral("Render")) {
        m_turboRender->setChecked(false);
        m_threadCount->setCurrentIndex(0);
        m_hardwareDecoding->setChecked(true);
        m_hardwareEncoding->setChecked(true);
        m_limitDecode8Bit->setChecked(false);
        m_modelTextureMaxSize->setValue(4096);
        m_shadowMapSize->setValue(2048);
        m_reflectionMapSize->setValue(1024);
        m_antialiasing->setCurrentIndex(2);
    } else if (name == QStringLiteral("Quality Profiles")) {
        m_playbackQuality->setCurrentIndex(0);
        m_pausedQuality->setCurrentIndex(0);
        m_playbackResolution->setCurrentIndex(1);
        m_pausedResolution->setCurrentIndex(1);
    } else if (name == QStringLiteral("Prompts & Warnings")) {
        for (int i = 0; i < m_promptChecks.size(); ++i) {
            m_promptChecks[i]->setChecked(kPromptDefinitions[i].defaultValue);
        }
    } else if (name == QStringLiteral("Labels")) {
        for (int i = 0; i < m_labelNames.size(); ++i) {
            m_labelNames[i]->setText(tr(kDefaultLabelNames[i]));
            setLabelColorButton(m_labelColors[i], QColor(QLatin1String(kDefaultLabelColors[i])));
        }
    } else if (name == QStringLiteral("Cache")) {
        m_cacheDbPath->setText(app::Settings().mediaCacheDbPath());
        m_cacheFilesPath->setText(app::Settings().mediaCacheFilesPath());
        m_daysToKeepCache->setValue(app::Settings::defaultDaysToKeepMediaCacheFiles());
        m_timelineCachePath->setText(defaultDataDirectory(QStringLiteral("TimelineCache")));
        m_timelineCacheDays->setValue(30);
        m_automaticRenderCache->setChecked(true);
        m_renderCacheDelay->setValue(2);
    } else if (name == QStringLiteral("Voiceover")) {
        m_voiceDevice->setCurrentIndex(0);
        m_voiceChannels->setCurrentIndex(0);
        m_voiceSampleRate->setCurrentIndex(1);
        m_voiceCountdown->setValue(3);
        m_voiceMuteOutput->setChecked(true);
    } else if (name == QStringLiteral("Proxies & Pre-Renders")) {
        m_proxyPath->setText(defaultDataDirectory(QStringLiteral("Proxies")));
        m_preRenderPath->setText(defaultDataDirectory(QStringLiteral("PreRenders")));
        m_proxyQuality->setCurrentIndex(1);
        m_previewMode->setCurrentIndex(0);
        m_preferIntegratedGpu->setChecked(false);
    } else if (name == QStringLiteral("Auto Save")) {
        m_autosaveEnabled->setChecked(true);
        m_autosaveSeconds->setValue(300);
    } else if (name == QStringLiteral("Shortcuts")) {
        for (int i = 0; i < m_shortcutEdits.size(); ++i) {
            m_shortcutEdits[i]->setKeySequence(QKeySequence::fromString(
                QLatin1String(kShortcutDefinitions[i].defaultSequence),
                QKeySequence::PortableText));
        }
    } else if (name == QStringLiteral("Export")) {
        m_exportDir->setText(QDir::toNativeSeparators(QDir(
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
                .filePath(QStringLiteral("OpenVegas Effects"))));
        m_snapshotDir->setText(QDir::toNativeSeparators(QDir::home().filePath(
            QStringLiteral("ExportSnapshots"))));
        m_timeFormat->setCurrentIndex(0);
        m_removeExt->setChecked(true);
        m_beepSpeaker->setChecked(true);
    }
}

bool OptionsDialog::saveSettings()
{
    QHash<QString, int> assigned;
    for (int i = 0; i < m_shortcutEdits.size(); ++i) {
        const QString sequence = m_shortcutEdits[i]->keySequence().toString(
            QKeySequence::PortableText);
        if (sequence.isEmpty()) continue;
        const auto previous = assigned.constFind(sequence);
        if (previous != assigned.cend()) {
            QMessageBox::warning(
                this, tr("Shortcut Conflict"),
                tr("The \"%1\" shortcut is already assigned to the \"%2\" command.")
                    .arg(sequence, tr(kShortcutDefinitions[*previous].label)));
            m_ui->categoryList->setCurrentRow(categoryNames().indexOf(
                QStringLiteral("Shortcuts")));
            m_shortcutEdits[i]->setFocus();
            return false;
        }
        assigned.insert(sequence, i);
    }

    QSettings s = optionSettings();
    s.setValue(kMaxUndo, m_maxUndo->value());
    s.setValue(kDefaultTemplate, m_template->currentText());
    s.setValue(QStringLiteral("Options/DefaultTemplateId"), m_template->currentData());
    s.setValue(kShotDuration, m_shotDuration->time().toString(QStringLiteral("hh:mm:ss.zzz")));
    s.setValue(kEditorDuration, m_editorDuration->time().toString(QStringLiteral("hh:mm:ss.zzz")));
    s.setValue(kPlaneDuration, m_planeDuration->time().toString(QStringLiteral("hh:mm:ss.zzz")));
    s.setValue(kAudioWaveforms, m_waveforms->currentText());
    s.setValue(kIncludeLayout, m_includeLayout->isChecked());
    s.setValue(kRelativePaths, m_relativePaths->isChecked());
    s.setValue(kCloseMediaInactive, m_closeMediaOnInactive->isChecked());
    s.setValue(kPlayAudioOnScrub, m_playAudioOnScrub->isChecked());
    s.setValue(kLogWaveform, m_logWaveform->isChecked());
    s.setValue(kAnalytics, m_analytics->isChecked());
    s.setValue(kUseNativeMenuBar, m_useNativeMenuBar->isChecked());
    s.setValue(kUseNativePicker, m_useNativeColorPicker->isChecked());
    s.setValue(kShowMenuBarQuick, m_showMenuBarQuickActions->isChecked());
    s.setValue(kWheelScrollMenus, m_wheelScrollMenus->isChecked());
    s.setValue(kHideFullScreen, m_hideFullScreenPreview->isChecked());
    s.setValue(kTheme, m_theme->currentData().toString());
    s.setValue(kLanguage, m_language->currentData().toString());
    app::Settings::setEnableHighDpiScaling(m_enableHighDpi->isChecked());
    s.setValue(kMediaCacheDb, QDir::toNativeSeparators(m_cacheDbPath->text().trimmed()));
    s.setValue(kMediaCacheFiles, QDir::toNativeSeparators(m_cacheFilesPath->text().trimmed()));
    s.setValue(kDaysToKeepCache, m_daysToKeepCache->value());
    s.setValue(kTimelineCache, QDir::toNativeSeparators(m_timelineCachePath->text().trimmed()));
    s.setValue(kTimelineCacheDays, m_timelineCacheDays->value());
    s.setValue(kAutomaticRenderCache, m_automaticRenderCache->isChecked());
    s.setValue(kRenderCacheDelay, m_renderCacheDelay->value());
    app::Settings settings;
    settings.setMediaCacheDbPath(m_cacheDbPath->text().trimmed());
    settings.setMediaCacheFilesPath(m_cacheFilesPath->text().trimmed());
    settings.setDaysToKeepMediaCacheFiles(m_daysToKeepCache->value());
    s.setValue(kThumbnailCacheMb, m_thumbnailCacheMb->value());
    settings.setThumbnailCacheSizeMb(m_thumbnailCacheMb->value());
    // Reference key "ShowCheckerboard2D" in the Options group, which the viewer
    // itself reads; this used to write an invented "CheckerboardBackground"
    // that nothing looked at.
    app::Settings::setShowCheckerboard2D(m_checkerboard->isChecked());
    app::Settings::setShowMotionPath(m_showMotionPath->isChecked());
    app::Settings::setMotionPathKeyFrames(m_motionPathFrames->value());
    app::Settings::setShowMouseCoordinates(m_showMouseCoordinates->isChecked());
    app::Settings::setEnablePlaybackUpdate(m_enablePlaybackUpdate->isChecked());
    s.setValue(QStringLiteral("Options/ShowCheckerboard3D"), m_checkerboard3d->isChecked());
    s.setValue(QStringLiteral("Options/ShowFloorPlane"), m_showFloorPlane->isChecked());
    s.setValue(kTurboRender, m_turboRender->isChecked());
    settings.setTurboRenderingEnabled(m_turboRender->isChecked());
    s.setValue(kThreadCount, m_threadCount->currentData().toInt());
    s.setValue(kHardwareDecoding, m_hardwareDecoding->isChecked());
    s.setValue(kHardwareEncoding, m_hardwareEncoding->isChecked());
    s.setValue(kLimitDecode8Bit, m_limitDecode8Bit->isChecked());
    s.setValue(kModelTextureMaxSize, m_modelTextureMaxSize->value());
    s.setValue(kShadowMapSize, m_shadowMapSize->value());
    s.setValue(kReflectionMapSize, m_reflectionMapSize->value());
    s.setValue(kAntialiasing, m_antialiasing->currentText());
    app::Settings::setPlaybackQualityProfile(m_playbackQuality->currentData().toString());
    app::Settings::setPausedQualityProfile(m_pausedQuality->currentData().toString());
    app::Settings::setPlaybackDownsampleMode(m_playbackResolution->currentData().toString());
    app::Settings::setPausedDownsampleMode(m_pausedResolution->currentData().toString());
    for (int i = 0; i < m_promptChecks.size(); ++i) {
        s.setValue(promptKey(kPromptDefinitions[i]), m_promptChecks[i]->isChecked());
    }
    for (int i = 0; i < m_labelNames.size(); ++i) {
        const QString base = QStringLiteral("Options/Labels/%1/").arg(i + 1);
        s.setValue(base + QStringLiteral("Name"), m_labelNames[i]->text().trimmed());
        s.setValue(base + QStringLiteral("Color"),
                   m_labelColors[i]->property("labelColor").toString());
    }
    s.setValue(kVoiceDevice, m_voiceDevice->currentData().toString());
    s.setValue(kVoiceChannels, m_voiceChannels->currentData().toInt());
    s.setValue(kVoiceSampleRate, m_voiceSampleRate->currentData().toInt());
    s.setValue(kVoiceCountdown, m_voiceCountdown->value());
    s.setValue(kVoiceMuteOutput, m_voiceMuteOutput->isChecked());
    s.setValue(kProxyPath, QDir::toNativeSeparators(m_proxyPath->text().trimmed()));
    s.setValue(kPreRenderPath, QDir::toNativeSeparators(m_preRenderPath->text().trimmed()));
    s.setValue(kProxyQuality, m_proxyQuality->currentText());
    s.setValue(kPreviewMode, m_previewMode->currentText());
    s.setValue(kPreferIntegratedGpu, m_preferIntegratedGpu->isChecked());
    s.setValue(kAutosaveEnabled, m_autosaveEnabled->isChecked());
    s.setValue(kAutosaveSeconds, m_autosaveSeconds->value());
    settings.setAutosaveIntervalSeconds(m_autosaveSeconds->value());
    for (int i = 0; i < m_shortcutEdits.size(); ++i) {
        s.setValue(shortcutKey(QLatin1String(kShortcutDefinitions[i].actionName)),
                   m_shortcutEdits[i]->keySequence().toString(QKeySequence::PortableText));
    }
    s.setValue(kExportDir, m_exportDir->text().trimmed());
    s.setValue(kSnapshotDir, m_snapshotDir->text().trimmed());
    settings.setSnapshotDirectory(m_snapshotDir->text().trimmed());
    settings.setLanguage(m_language->currentData().toString());
    s.setValue(kTimeFormat, m_timeFormat->currentText());
    s.setValue(kRemoveExt, m_removeExt->isChecked());
    s.setValue(kBeepSpeaker, m_beepSpeaker->isChecked());
    s.sync();
    return true;
}

void OptionsDialog::loadSettings()
{
    const QSettings s = optionSettings();
    m_maxUndo->setValue(s.value(kMaxUndo, 30).toInt());
    m_template->setCurrentIndex(qMax(0, m_template->findData(app::defaultProjectTemplateId(s))));
    const QTime t = QTime::fromString(
        s.value(kShotDuration, QStringLiteral("00:00:30.000")).toString(),
        QStringLiteral("hh:mm:ss.zzz"));
    if (t.isValid()) {
        m_shotDuration->setTime(t);
    }
    const auto loadTime = [&s](const char* key, const QTime& fallback) {
        const QTime value = QTime::fromString(
            s.value(key, fallback.toString(QStringLiteral("hh:mm:ss.zzz"))).toString(),
            QStringLiteral("hh:mm:ss.zzz"));
        return value.isValid() ? value : fallback;
    };
    m_editorDuration->setTime(loadTime(kEditorDuration, QTime(0, 5, 0)));
    m_planeDuration->setTime(loadTime(kPlaneDuration, QTime(0, 0, 30)));
    m_waveforms->setCurrentIndex(qMax(0, m_waveforms->findText(
        s.value(kAudioWaveforms).toString())));
    m_includeLayout->setChecked(s.value(kIncludeLayout, false).toBool());
    m_relativePaths->setChecked(s.value(kRelativePaths, false).toBool());
    m_closeMediaOnInactive->setChecked(s.value(kCloseMediaInactive, false).toBool());
    m_playAudioOnScrub->setChecked(s.value(kPlayAudioOnScrub, true).toBool());
    m_logWaveform->setChecked(s.value(kLogWaveform, true).toBool());
    m_analytics->setChecked(s.value(kAnalytics, false).toBool());
    m_useNativeMenuBar->setChecked(s.value(kUseNativeMenuBar, true).toBool());
    m_useNativeColorPicker->setChecked(s.value(kUseNativePicker, true).toBool());
    m_enableHighDpi->setChecked(app::Settings::enableHighDpiScaling());
    m_showMenuBarQuickActions->setChecked(s.value(kShowMenuBarQuick, true).toBool());
    m_wheelScrollMenus->setChecked(s.value(kWheelScrollMenus, false).toBool());
    m_hideFullScreenPreview->setChecked(s.value(kHideFullScreen, false).toBool());
    const int langIndex = m_language->findData(s.value(kLanguage).toString());
    m_language->setCurrentIndex(langIndex >= 0 ? langIndex : 0);
    const int themeIndex = m_theme->findData(s.value(kTheme, QStringLiteral("Dark")).toString());
    m_theme->setCurrentIndex(themeIndex >= 0 ? themeIndex : 0);
    // Cache paths fall back to the same defaults Settings seeds them with, so
    // an unconfigured install shows the real locations rather than blanks.
    app::Settings appSettings;
    m_cacheDbPath->setText(s.value(kMediaCacheDb, appSettings.mediaCacheDbPath()).toString());
    m_cacheFilesPath->setText(
        s.value(kMediaCacheFiles, appSettings.mediaCacheFilesPath()).toString());
    m_daysToKeepCache->setValue(
        s.value(kDaysToKeepCache,
                app::Settings::defaultDaysToKeepMediaCacheFiles()).toInt());
    m_timelineCachePath->setText(s.value(
        kTimelineCache, defaultDataDirectory(QStringLiteral("TimelineCache"))).toString());
    m_timelineCacheDays->setValue(s.value(kTimelineCacheDays, 30).toInt());
    m_automaticRenderCache->setChecked(s.value(kAutomaticRenderCache, true).toBool());
    m_renderCacheDelay->setValue(s.value(kRenderCacheDelay, 2).toInt());
    m_renderCacheDelay->setEnabled(m_automaticRenderCache->isChecked());
    const QString defaultExport = QDir::toNativeSeparators(QDir(
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
            .filePath(QStringLiteral("OpenVegas Effects")));
    m_exportDir->setText(s.value(kExportDir, defaultExport).toString());
    m_snapshotDir->setText(s.value(kSnapshotDir, appSettings.snapshotDirectory()).toString());
    m_timeFormat->setCurrentIndex(qMax(0, m_timeFormat->findText(
        s.value(kTimeFormat, QStringLiteral("Timecode")).toString())));
    m_removeExt->setChecked(s.value(kRemoveExt, true).toBool());
    m_beepSpeaker->setChecked(s.value(kBeepSpeaker, true).toBool());

    // Display / Render / Quality Profiles / Auto Save rows, seeded from the
    // same values the application's Settings class exposes so the checkboxes
    // and spins agree with what the app actually uses.
    m_thumbnailCacheMb->setValue(s.value(kThumbnailCacheMb,
                                         appSettings.thumbnailCacheSizeMb()).toInt());
    m_checkerboard->setChecked(app::Settings::showCheckerboard2D());
    m_checkerboard3d->setChecked(s.value(QStringLiteral("Options/ShowCheckerboard3D"), true).toBool());
    m_showMotionPath->setChecked(app::Settings::showMotionPath());
    m_motionPathFrames->setValue(app::Settings::motionPathKeyFrames());
    m_showMouseCoordinates->setChecked(app::Settings::showMouseCoordinates());
    m_enablePlaybackUpdate->setChecked(app::Settings::enablePlaybackUpdate());
    m_showFloorPlane->setChecked(s.value(QStringLiteral("Options/ShowFloorPlane"), true).toBool());
    m_motionPathFrames->setEnabled(m_showMotionPath->isChecked());
    m_turboRender->setChecked(s.value(kTurboRender,
                                      appSettings.turboRenderingEnabled()).toBool());
    const int threads = s.value(kThreadCount, 0).toInt();
    const int threadIdx = m_threadCount->findData(threads);
    m_threadCount->setCurrentIndex(threadIdx >= 0 ? threadIdx : 0);
    m_hardwareDecoding->setChecked(s.value(kHardwareDecoding, true).toBool());
    m_hardwareEncoding->setChecked(s.value(kHardwareEncoding, true).toBool());
    m_limitDecode8Bit->setChecked(s.value(kLimitDecode8Bit, false).toBool());
    m_modelTextureMaxSize->setValue(s.value(kModelTextureMaxSize, 4096).toInt());
    m_shadowMapSize->setValue(s.value(kShadowMapSize, 2048).toInt());
    m_reflectionMapSize->setValue(s.value(kReflectionMapSize, 1024).toInt());
    m_antialiasing->setCurrentIndex(qMax(0, m_antialiasing->findText(
        s.value(kAntialiasing, QStringLiteral("4x MSAA")).toString())));
    auto selectData = [](QComboBox* combo, const QVariant& value) {
        const int index = combo->findData(value);
        combo->setCurrentIndex(index >= 0 ? index : 0);
    };
    selectData(m_playbackQuality, app::Settings::playbackQualityProfile());
    selectData(m_pausedQuality, app::Settings::pausedQualityProfile());
    selectData(m_playbackResolution, app::Settings::playbackDownsampleMode());
    selectData(m_pausedResolution, app::Settings::pausedDownsampleMode());
    for (int i = 0; i < m_promptChecks.size(); ++i) {
        m_promptChecks[i]->setChecked(s.value(promptKey(kPromptDefinitions[i]),
                                             kPromptDefinitions[i].defaultValue).toBool());
    }
    for (int i = 0; i < m_labelNames.size(); ++i) {
        const QString base = QStringLiteral("Options/Labels/%1/").arg(i + 1);
        m_labelNames[i]->setText(s.value(base + QStringLiteral("Name"),
                                            tr(kDefaultLabelNames[i])).toString());
        setLabelColorButton(m_labelColors[i], QColor(s.value(
            base + QStringLiteral("Color"), QLatin1String(kDefaultLabelColors[i])).toString()));
    }
    selectData(m_voiceDevice, s.value(kVoiceDevice, QStringLiteral("default")));
    selectData(m_voiceChannels, s.value(kVoiceChannels, 1));
    selectData(m_voiceSampleRate, s.value(kVoiceSampleRate, 48000));
    m_voiceCountdown->setValue(s.value(kVoiceCountdown, 3).toInt());
    m_voiceMuteOutput->setChecked(s.value(kVoiceMuteOutput, true).toBool());
    m_proxyPath->setText(s.value(kProxyPath,
        defaultDataDirectory(QStringLiteral("Proxies"))).toString());
    m_preRenderPath->setText(s.value(kPreRenderPath,
        defaultDataDirectory(QStringLiteral("PreRenders"))).toString());
    m_proxyQuality->setCurrentIndex(qMax(0, m_proxyQuality->findText(
        s.value(kProxyQuality, QStringLiteral("Medium")).toString())));
    m_previewMode->setCurrentIndex(qMax(0, m_previewMode->findText(
        s.value(kPreviewMode, QStringLiteral("Auto")).toString())));
    m_preferIntegratedGpu->setChecked(s.value(kPreferIntegratedGpu, false).toBool());
    m_autosaveEnabled->setChecked(s.value(kAutosaveEnabled, true).toBool());
    m_autosaveSeconds->setValue(s.value(kAutosaveSeconds,
                                        appSettings.autosaveIntervalSeconds()).toInt());
    m_autosaveSeconds->setEnabled(m_autosaveEnabled->isChecked());
    for (int i = 0; i < m_shortcutEdits.size(); ++i) {
        m_shortcutEdits[i]->setKeySequence(QKeySequence::fromString(
            s.value(shortcutKey(QLatin1String(kShortcutDefinitions[i].actionName)),
                    QLatin1String(kShortcutDefinitions[i].defaultSequence)).toString(),
            QKeySequence::PortableText));
    }
}

void OptionsDialog::updateRestoreLabel()
{
    const int idx = currentIndex();
    if (m_ui->btnRestoreDefaults && idx >= 0 && idx < categoryNames().size()) {
        m_ui->btnRestoreDefaults->setText(
            tr("Restore '%1' Defaults").arg(categoryNames().at(idx)));
    }
}

int OptionsDialog::currentIndex() const { return m_ui->categoryList->currentRow(); }

} // namespace ui
} // namespace openvegas
