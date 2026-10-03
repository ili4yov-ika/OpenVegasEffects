#pragma once

#include <QMainWindow>

#include "ui/CompositionSettingsDialog.h"

class QDockWidget;
class QToolBar;
class QUndoStack;
class QProcess;
class QTemporaryDir;
#include <QHash>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <memory>
#include <functional>

#include "composition/Composition.h"
#include "composition/TextStyle.h"
#include "license/LicenseManager.h"
#include "media/AudioPlayer.h"
#include "media/MediaManager.h"
#include "media/VideoProbe.h"
#include "model3d/ModelImportSettings.h"
#include "plugin/PluginManager.h"
#include "render/RenderManager.h"
#include "render/ExportJob.h"
#include "ui/CameraRule.h"
#include "ui/EffectInspector.h"
#include "ui/EffectsPanel.h"
#include "ui/LayerPanel.h"
#include "ui/TrackPanel.h"
#include "ui/MediaPanel.h"
#include "ui/TextPanel.h"
#include "ui/TimelineWidget.h"
#include "ui/ViewerWidget.h"
#include "ui/Preview360VideoPanel.h"
#include "ui/ViewerTransportBar.h"
#include "ui/AudioMetersPanel.h"
#include "ui/HistoryPanel.h"
#include "ui/LibraryPanel.h"
#include "ui/TrimmerPanel.h"
#include "ui/ExportPanel.h"
#include "ui/LayoutPanel.h"
#include "ui/StartPanel.h"
#include "ui/LearnSidebar.h"

namespace openvegas {
namespace media {
class ProxyGenerator;
}
namespace app {
class AppMain;
}

namespace ui {

namespace Ui {
class MainWindow;
}

class OptionsDialog;
class ExportQueue;
class ExportQueueView;
class TextTransformOverlay;
class NativeCustomUiOverlay;
class NativeInstanceHost;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(app::AppMain* owner, QWidget* parent = nullptr);

    void bindModel(std::shared_ptr<composition::Composition> composition,
                   std::shared_ptr<media::MediaManager> mediaManager,
                   render::RenderManager* renderManager);

    void regenerateEffects(plugin::PluginManager* pluginManager);

    // Opens a project file, as used for the command line / shell file
    // association. Returns false when the file cannot be loaded.
    bool openProject(const QString& path);
    // At startup after a session that did not end normally: the Recovered
    // Projects dialog, when there are auto-saves to recover.
    void offerAutoSaveRecovery();

    // Re-reads the model into every panel. Public because the undo commands
    // call it after they change the composition.
    void refreshAfterModelChange();

    ~MainWindow() override;

private slots:
    void onEffectSelected(const plugin::EffectSpec& spec);
    // Appends the effect to the clip selected on the timeline.
    void onEffectActivated(const plugin::EffectSpec& spec);
    void onRenderRequested();
    void onMotionTrackingRequested(int layerIndex, int trackIndex);
    void onImportMedia();
    // Viewer manipulation of the selected layer: Orbit turns it about the X
    // and Y axes, Select drags its position. Both edit the model directly and
    // ask for a re-render, which is what makes the viewer a way of placing a
    // 3D layer rather than only of looking at one.
    void onLayerOrbited(const QPointF& delta);
    void onLayerMoved(const QPointF& delta);
    // Arrow-key nudge from the viewer: composition pixels already, so unlike a
    // drag it is not divided by the viewer's zoom.
    void onLayerNudged(const QPointF& delta);
    // Hands the viewer the selected layer's position track so its Options menu
    // item "Show Motion Path" has something to draw.
    void updateMotionPathOverlay();
    // The 360 Viewer tab is the Viewer itself in its 360 mode: the page with
    // the canvas, tools and transport moves into whichever of the two tabs
    // is being shown.
    void showViewerIn360(bool in360);
    void onPlayPause();
    void onStop();
    void onTransportTick();
    void onNewProject();
    void onOpenProject();
    bool onSaveProject();
    bool onSaveProjectAs();
    void onAddTextLayer(const QString& text, int fontSize);
    void createTextAt(const QPointF& canvasPosition);
    void editSelectedText();
    void setSelectedTextMode(composition::TextStyle::TextMode mode);
    void onAddNewLayer(composition::LayerKind kind = composition::LayerKind::Media);
    void makeCompositeShotFromSelection();
    // The project's composite shots (its CompositionAssets): Media > New >
    // Composite Shot, Delete (the reference's "Remove Asset", which takes the
    // layers nesting the shot with it) and "Set Primary Composite Shot".
    void newCompositeShot();
    void removeCompositeShot(const QString& shotId);
    void setPrimaryCompositeShot(const QString& shotId, bool primary);
    // File > Import > Composite Shot... (FUN_1407175e0) and Media's "Save
    // Composite Shot..." (FUN_1407137d0): shots from and to .vegfxcs files;
    // a .vegfx offers its shots too.
    void importCompositeShot();
    bool importCompositeShotsFrom(const QString& path);
    void saveCompositeShotToFile(const QString& shotId);
    // A Media panel row dropped on the timeline: a new layer above
    // `above` (0 is the top) starting at `seconds`.
    void addDroppedMediaLayer(const QString& filePath, int above, double seconds);
    void addDroppedShotLayer(const QString& shotId, int above, double seconds);
    // Both act on the layer selected in the timeline and go through the undo
    // stack, which is what makes Undo/Redo and the History panel mean anything.
    void deleteSelectedLayer();
    void duplicateSelectedLayer();
    void copySelection(bool cutAfterCopy = false);
    void pasteSelection();
    void pasteSelectionAttributes();
    void removeSelectionAttributes();
    void removeSelectionEffects();
    void sliceSelectionAtPlayhead();
    void rippleDeleteSelection();
    void onOpenInTrimmer();
    void onOptions();
    // Layout panel (reference TransformWidget / AlignmentWidget): the selected
    // layer's box in canvas pixels, mirrored, turned or typed into.
    void onLayoutBoundsEdited(const QRectF& bounds);
    void onLayoutSelectionBoundsEdited(const QVector<QRectF>& bounds);
    // Text panel (reference TextPanelWidget): formats the selected text layer.
    void onTextStyleEdited(const composition::TextStyle& style);
    void updateTextPanelSelection();
    void onLayoutMirror(Qt::Orientation orientation);
    void onLayoutRotate(int degrees);
    void updateLayoutPanelSelection();
    void onRecentProjectActivated(const QString& path);
private:
    void buildUi();
    void configureMenus();
    void addTransportShortcuts();
    void buildTransportToolbar();
    QAction* addToolBarAction(QToolBar* bar, const QString& iconName, const QString& label);
    void updatePlayIcon();
    void updateAutoSaveTimer();
    void writeAutoSave();
    // File > Recover Projects... and the post-crash offer at startup.
    void showRecoveredProjects();
    bool openRecoveredProject(const QString& autosaveFile, const QString& projectPath);
    // File > Project Settings... (or New Project Settings for File > New).
    void editProjectSettings(bool newProject);
    void storePlayheadInComposition();
    void wireSignals();
    void requestRenderFrame();
    void beginPlayback();
    void stopPlayback();
    // Picks the composition's first audio clip and hands it to the player.
    // Returns the clip's start on the timeline, or -1 when there is none.
    double prepareAudioSource();
    void refreshMediaAndInspector();
    // Decodes one pending video frame per tick, on this thread, and asks for a
    // re-render once it lands. One at a time on purpose: a decode opens the
    // file and seeks, which is long enough to be felt if several run back to
    // back in the event loop.
    void serviceVideoDecodeRequests();
    // Decoder held open for `filePath`, opening one if this is a new source.
    // Null when the file cannot be read.
    media::VideoDecoder* decoderFor(const QString& filePath, bool allowHardware = true);
    // Proxies (reference ProxyMediaMenu/ProxyMediaManager).
    QString proxyPathFor(const media::MediaAsset& asset) const;
    void setAssetProxyMode(const core::Identifier& assetId, media::ProxyMode mode);
    // Switches preview decoding between originals and ready proxies after a
    // change of playback state, Options or export.
    void updateProxyPreview(bool force = false);
    // Pre-renders of composite shots (reference AssetPreRenderMenu).
    QString preRenderRoot() const;
    void handlePreRenderRequest(int layerIndex, int clipIndex, bool make);
    bool doSaveProject(const QString& path);
    bool confirmDiscard();
    void setInPoint();
    void setOutPoint();
    void stepFrameBackward();
    void stepFrameForward();
    void stepFrames(int frames);
    void shuttle(double speedMultiplier);
    double frameSeconds() const;
    void applyPresetToSelection(const QString& library, const QString& preset);
    void addMediaClipToTimeline(const QString& filePath, int trimInFrame = -1,
                                int trimOutFrame = -1, bool insertEdit = false);
    // Asks (MediaMismatchPrompt) whether the composition should take the
    // frame size of a video clip that differs from it.
    void offerCompositionMatch(const media::MediaAsset& asset);
    // Timeline cache directory/retention and automatic render cache from
    // Options; `prune` removes expired frames (done once at startup).
    void applyTimelineCacheOptions(bool prune = false);
    void exportFrameToFile(const QString& path);
    // Reference "Export Frame": writes the shown frame into the snapshot
    // directory under a numbered name, without asking for a path.
    void exportSnapshot();
    void exportContents(const QString& path);
    // An export of any kind is running: preview then decodes the originals.
    bool isExporting() const;
    // The queue's media, pre-render and snapshot folders, from Options.
    void configureExportQueue();
    // Export panel "Add to Queue": the shot in front, its In/Out or contents.
    void queueContentsForExport(const QString& path);
    void importFiles(const QStringList& files);
    // Imports a model and puts a 3D layer on the timeline for it. Reached from
    // importFiles(), which is where every route into the project ends up - the
    // Import command, a drop on the Media panel, a drop on the window.
    bool addModelLayer(const QString& path, const model3d::ImportSettings& settings);
    // Opens an undo macro named `text` for layers about to be added; when one
    // of them needs a camera the shot lacks, asks first (CameraRule) and puts
    // the camera in the macro. False on Cancel, with no macro left open;
    // otherwise the caller pushes its layers and calls endMacro().
    bool pushLayersNeedingCamera(const QVector<composition::Layer>& layers,
                                 AddCameraReason reason, const QString& text);
    bool openProjectFile(const QString& path);
    // Reference reports missing plugins after a load ("These plugins could not
    // be found: "); an effect whose plugin is absent otherwise sits in the tree
    // doing nothing, with no hint why.
    void reportMissingPlugins();
    void noteRecentProject(const QString& path);
    // Bounding box of the selected layer in canvas pixels, for the Layout panel.
    QRectF selectedLayerBounds() const;
    QRectF layerBounds(int layerIndex) const;
    void editLayoutTransforms(const QString& title,
        const std::function<void(int, composition::LayerTransform&, int)>& edit);
    // The text effect on the selected clip, or null when it is not a text layer.
    composition::Effect* selectedTextEffect();
    void addTextLayer(const composition::TextStyle& style, const QPointF& canvasPosition);
    void applySelectedTextStyle(const composition::TextStyle& style, const QString& commandText);
    void showLearnSidebar();
    void showCenterTab(QWidget* page);
    void updateWindowTitle();
    void applyInterfacePreferences();
    void restoreWindowGeometry();
    void saveWindowGeometry();
    // Window > Workspaces (reference WorkspacesMenu, FUN_14039e2e0).
    void loadWorkspaces();
    void saveWorkspace();
    void deleteWorkspace();
    void resetWorkspace();
    void applyWorkspace(const QString& name);
    void activateComposition(const std::shared_ptr<composition::Composition>& composition);
    void rebuildCompositionTabs();
    void resetCompositionTabs();
    // The editor timeline's format (Project Settings), which Composite Shot
    // Properties' Match Timeline takes.
    CompositionSettingsDialog::Values editorTimelineFormat() const;
    void syncMatchFormat();
    // Lists the project's shots in the Media panel.
    void refreshCompositeShotList();
    void openCompositeShot(const std::shared_ptr<composition::Composition>& shot);
    // The root shot, one of the project's further shots, or one only a layer
    // still nests; null when the project has no shot with that id.
    std::shared_ptr<composition::Composition> projectShot(const QString& shotId) const;
    // Every shot of the project: the root, then its further shots.
    QVector<std::shared_ptr<composition::Composition>> allProjectShots() const;
    // After a shot was added or removed (or that was undone): tabs, panels
    // and the Media list follow.
    void projectShotsChanged();
    // Hands the project (its ID, settings, source document, editor timeline
    // and shot list) to `root`, which becomes the project's root shot.
    void adoptProjectRoot(const std::shared_ptr<composition::Composition>& root);
    std::function<void(const std::shared_ptr<composition::Composition>&)> rootAdopter();

protected:
    void closeEvent(QCloseEvent* event) override;

    Ui::MainWindow* m_ui = nullptr;
    media::AudioPlayer* m_audio = nullptr;
    double m_audioClipStart = -1.0;
    // Clip the timeline last selected; an effect is applied to this one.
    int m_selectedLayer = -1;
    // Layer count the Track and Layer panels last showed. A panel that adds a
    // layer beside its own edit (a camera for a 3D switch) leaves the others
    // to notice through the undo stack.
    qsizetype m_lastLayerCount = -1;
    bool m_lastShotIs3D = false;
    int m_selectedClip = -1;
    QVector<int> m_selectedLayers;
    app::AppMain* m_owner = nullptr;
    std::shared_ptr<composition::Composition> m_composition;
    std::shared_ptr<composition::Composition> m_rootComposition;
    QVector<std::shared_ptr<composition::Composition>> m_openCompositions;
    std::shared_ptr<media::MediaManager> m_mediaManager;
    render::RenderManager* m_renderManager = nullptr;

    EffectsPanel* m_effectsPanel = nullptr;
    MediaPanel* m_mediaPanel = nullptr;
    TextPanel* m_textPanel = nullptr;
    EffectInspector* m_controlsPanel = nullptr;
    LibraryPanel* m_libraryPanel = nullptr;
    HistoryPanel* m_historyPanel = nullptr;
    AudioMetersPanel* m_metersPanel = nullptr;
    TrimmerPanel* m_trimmerPanel = nullptr;
    ExportPanel* m_exportPanel = nullptr;
    LayerPanel* m_layerPanel = nullptr;
    TrackPanel* m_trackPanel = nullptr;
    LayoutPanel* m_layoutPanel = nullptr;
    QMenu* m_workspacesMenu = nullptr;
    QMenu* m_effectFavoritesMenu = nullptr;
    QMenu* m_effectRecentsMenu = nullptr;
    QString m_currentWorkspace;
    StartPanel* m_startPanel = nullptr;
    LearnSidebar* m_learnSidebar = nullptr;
    QDockWidget* m_viewerDock = nullptr;
    ViewerWidget* m_viewer = nullptr;
    QWidget* m_viewerPage = nullptr;
    // Shown in the Viewer tab while the page is in the 360 Viewer tab.
    QWidget* m_viewerPlaceholder = nullptr;
    std::unique_ptr<TextTransformOverlay> m_textOverlay;
    // The custom UI of the selected native effect (MotionTrack, BendGeometry).
    std::unique_ptr<NativeCustomUiOverlay> m_customUiOverlay;
    // Native modules on an instance of their own (MotionTrack): footage,
    // background analysis, the matrices the renderer reads.
    std::unique_ptr<NativeInstanceHost> m_instanceHost;
    Preview360VideoPanel* m_viewer360 = nullptr;
    ViewerTransportBar* m_transportBar = nullptr;
    TimelineWidget* m_timeline = nullptr;
    OptionsDialog* m_optionsDialog = nullptr;

    double m_inPoint = -1.0;
    double m_outPoint = -1.0;
    QAction* m_loopPlayback = nullptr;
    QAction* m_playAction = nullptr;

    QString m_currentFilePath;
    bool m_projectModified = false;
    // The auto-save a recovered project was opened from (title " - Recovered"
    // until saved) and this session's auto-saves, cleared by a manual save.
    QString m_recoveredFrom;
    QStringList m_autoSaveFiles;
    composition::Layer m_layerClipboard;
    composition::Clip m_clipClipboard;
    bool m_clipboardHasLayer = false;
    bool m_clipboardHasClip = false;
    QStringList m_recentEffectIds;

    // Undo history for model edits. The Edit menu, the toolbar's undo/redo and
    // the History panel are all views onto this one stack.
    QUndoStack* m_undoStack = nullptr;

    // One decoder per video file, kept alive between frames the way the
    // reference keeps an fxh::media::FXVideoFrameDecoder alive per source:
    // reopening the file for every picture is what made scrubbing crawl. Capped,
    // since each one holds a decoding pipeline open.
    QHash<QString, std::shared_ptr<media::VideoDecoder>> m_videoDecoders;
    // Most recently used last; the front is evicted once the cap is passed.
    QStringList m_videoDecoderOrder;

    QTimer m_playbackTimer;
    QTimer m_audioScrubTimer;
    // Options "Use automatic render cache" after "Automatic cache delay".
    QTimer m_autoRenderCacheTimer;
    bool m_autoRenderCache = false;
    media::ProxyGenerator* m_proxyGenerator = nullptr;
    bool m_previewUsesProxies = false;
    QTimer m_videoDecodeTimer;
    // Running pre-renders by folder, so a new request replaces the old one.
    QHash<QString, QPointer<render::RenderManager>> m_preRenderTasks;
    QTimer m_autoSaveTimer;
    bool m_playing = false;
    int m_renderedFrameIndex = 0;
    double m_playbackTime = 0.0;
    // Export Contents in progress (render/ExportJob).
    QPointer<render::ExportJob> m_exportJob;
    ExportQueue* m_exportQueue = nullptr;
    ExportQueueView* m_exportQueueView = nullptr;
};

} // namespace ui
} // namespace openvegas
