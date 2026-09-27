#pragma once

#include <QDialog>

#include <QKeySequence>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QListWidget;
class QKeySequenceEdit;
class QTimeEdit;
class QSpinBox;
class QStackedWidget;
class QLabel;
class QPushButton;
class QTableWidget;
class QWidget;

namespace openvegas {
namespace ui {

namespace Ui {
class OptionsDialog;
}

// Mirrors the reference "Options" modal dialog (see SAMPLES/screenshots/3.md):
// a left category list with a per-category page on the right, a footer note
// about settings marked with '*', and Cancel / Restore '<Category>' Defaults /
// OK buttons. Persists its state to QSettings under keys in kOptionsPrefix.
class OptionsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit OptionsDialog(QWidget* parent = nullptr);
    ~OptionsDialog() override;

    // Category list (order matches the reference screenshot).
    static const QStringList& categoryNames();

    // Applies the editable shortcuts stored by the Shortcuts page to actions
    // below root. Actions are addressed by stable objectName values.
    static void applyShortcuts(QWidget* root);

protected:
    void reject() override;

private slots:
    void onCategoryChanged(int row);
    void onRestoreDefaults();

private:
    QWidget* buildGeneralPage();
    QWidget* buildInterfacePage();
    QWidget* buildDisplayPage();
    QWidget* buildRenderPage();
    QWidget* buildQualityProfilesPage();
    QWidget* buildPromptsPage();
    QWidget* buildLabelsPage();
    QWidget* buildAutoSavePage();
    QWidget* buildExportPage();
    QWidget* buildCachePage();
    QWidget* buildVoiceoverPage();
    QWidget* buildProxiesPage();
    QWidget* buildShortcutsPage();
    void browseDirectory(QLineEdit* target);
    void browseFile(QLineEdit* target, const QString& filter);
    void clearManagedPath(const QString& path, const QString& description,
                          bool removeRoot = false);

    bool saveSettings();
    void loadSettings();
    void updateRestoreLabel();
    int currentIndex() const;

    Ui::OptionsDialog* m_ui = nullptr;

    // General page controls.
    QSpinBox*  m_maxUndo = nullptr;
    QComboBox* m_template = nullptr;
    QTimeEdit* m_shotDuration = nullptr;
    QComboBox* m_waveforms = nullptr;
    QCheckBox* m_includeLayout = nullptr;
    QCheckBox* m_relativePaths = nullptr;
    QCheckBox* m_closeMediaOnInactive = nullptr;
    QCheckBox* m_playAudioOnScrub = nullptr;
    QCheckBox* m_logWaveform = nullptr;
    QCheckBox* m_analytics = nullptr;
    QTimeEdit* m_editorDuration = nullptr;
    QTimeEdit* m_planeDuration = nullptr;

    // Interface page controls.
    QComboBox* m_language = nullptr;
    QComboBox* m_theme = nullptr;
    QCheckBox* m_useNativeMenuBar = nullptr;
    QCheckBox* m_useNativeColorPicker = nullptr;
    QCheckBox* m_enableHighDpi = nullptr;
    QCheckBox* m_showMenuBarQuickActions = nullptr;
    QCheckBox* m_wheelScrollMenus = nullptr;
    QCheckBox* m_hideFullScreenPreview = nullptr;

    // Display page controls.
    QSpinBox* m_thumbnailCacheMb = nullptr;
    QCheckBox* m_checkerboard = nullptr;
    QCheckBox* m_checkerboard3d = nullptr;
    QCheckBox* m_showMotionPath = nullptr;
    QSpinBox* m_motionPathFrames = nullptr;
    QCheckBox* m_showMouseCoordinates = nullptr;
    QCheckBox* m_enablePlaybackUpdate = nullptr;
    QCheckBox* m_showFloorPlane = nullptr;

    // Render page controls.
    QCheckBox* m_turboRender = nullptr;
    QComboBox* m_threadCount = nullptr;
    QCheckBox* m_hardwareDecoding = nullptr;
    QCheckBox* m_hardwareEncoding = nullptr;
    QCheckBox* m_limitDecode8Bit = nullptr;
    QSpinBox* m_modelTextureMaxSize = nullptr;
    QSpinBox* m_shadowMapSize = nullptr;
    QSpinBox* m_reflectionMapSize = nullptr;
    QComboBox* m_antialiasing = nullptr;

    // Quality Profiles page controls.
    QComboBox* m_playbackQuality = nullptr;
    QComboBox* m_pausedQuality = nullptr;
    QComboBox* m_playbackResolution = nullptr;
    QComboBox* m_pausedResolution = nullptr;

    // Prompts & Warnings and Labels pages are data-driven.
    QVector<QCheckBox*> m_promptChecks;
    QVector<QLineEdit*> m_labelNames;
    QVector<QPushButton*> m_labelColors;

    // Auto Save page controls.
    QSpinBox* m_autosaveSeconds = nullptr;
    QCheckBox* m_autosaveEnabled = nullptr;

    // Cache page controls (reference keys Options/MediaCacheDB,
    // Options/MediaCacheFiles, Options/DaysToKeepMediaCacheFiles).
    QLineEdit* m_cacheDbPath = nullptr;
    QLineEdit* m_cacheFilesPath = nullptr;
    QSpinBox*  m_daysToKeepCache = nullptr;
    QLineEdit* m_timelineCachePath = nullptr;
    QSpinBox* m_timelineCacheDays = nullptr;
    QCheckBox* m_automaticRenderCache = nullptr;
    QSpinBox* m_renderCacheDelay = nullptr;

    // Voiceover page controls.
    QComboBox* m_voiceDevice = nullptr;
    QComboBox* m_voiceChannels = nullptr;
    QComboBox* m_voiceSampleRate = nullptr;
    QSpinBox* m_voiceCountdown = nullptr;
    QCheckBox* m_voiceMuteOutput = nullptr;

    // Proxy / pre-render page controls.
    QLineEdit* m_proxyPath = nullptr;
    QLineEdit* m_preRenderPath = nullptr;
    QComboBox* m_proxyQuality = nullptr;
    QComboBox* m_previewMode = nullptr;
    QCheckBox* m_preferIntegratedGpu = nullptr;

    // Shortcut editor.
    QLineEdit* m_shortcutSearch = nullptr;
    QTableWidget* m_shortcutTable = nullptr;
    QVector<QKeySequenceEdit*> m_shortcutEdits;

    // Export page controls.
    QLineEdit* m_exportDir = nullptr;
    QLineEdit* m_snapshotDir = nullptr;
    QComboBox* m_timeFormat = nullptr;
    QCheckBox* m_removeExt = nullptr;
    QCheckBox* m_beepSpeaker = nullptr;
};

} // namespace ui
} // namespace openvegas
