#include "ui/MainWindow.h"
#include "ui/AutoSave.h"
#include "ui/ProjectSettingsDialog.h"
#include "ui/RecoveredProjectsDialog.h"
#include "ui/ImportCompositionDialog.h"
#include "ui/CompositionSettingsDialog.h"
#include "ui/ViewerPanel.h"

#include "ui_MainWindow.h"

#include <QApplication>
#include <QPixmapCache>
#include <QCoreApplication>
#include <QDockWidget>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QIcon>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtGlobal>
#include <QtMath>

#include <QCloseEvent>
#include <QGuiApplication>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QScreen>
#include <QPushButton>
#include <QLabel>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QProcess>
#include <QProgressDialog>
#include <QTemporaryDir>
#include <QTextStream>
#include <QUrl>
#include <QUuid>

#include <algorithm>
#include <cstring>
#include <limits>

#include "app/AppMain.h"
#include "app/Settings.h"
#include "core/Identifier.h"
#include <QUndoCommand>
#include "app/ProjectDefaults.h"
#include <QUndoStack>

#include "core/Log.h"
#include "composition/MotionTracker.h"
#include "composition/Transition.h"
#include "render/AudioExport.h"
#include "render/VideoEncoder.h"
#include "render/ExportJob.h"
#include "ui/ExportQueue.h"
#include "ui/ExportQueueView.h"
#include "media/ProxyMedia.h"
#include "ui/EffectPlacement.h"
#include "ui/PromptMessage.h"
#include "media/VideoProbe.h"
#include "license/LicenseManager.h"
#include "media/ExrImage.h"
#include "project/VegfxSerializer.h"
#include "plugin/NativeEffectRender.h"
#include "ui/AboutDialog.h"
#include "ui/DockTitleBar.h"
#include "ui/EffectsPanel.h"
#include "model3d/ModelLoader.h"
#include "ui/Model3DSettingsDialog.h"
#include "ui/OptionsDialog.h"
#include "ui/Theme.h"
#include "ui/TextEditCommand.h"
#include "ui/LayoutTransformCommand.h"
#include "ui/TextSettingsDialog.h"
#include "ui/TextTransformOverlay.h"
#include "ui/NativeCustomUiOverlay.h"
#include "ui/NativeInstanceHost.h"
#include "ui/VoiceoverDialog.h"

namespace openvegas {
namespace ui {

namespace {

void notifyExportCompleted()
{
    const QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                             app::Settings::organizationName(),
                             app::Settings::applicationName());
    if (settings.value(QStringLiteral("Options/BeepOnCompletion"), true).toBool()) {
        QApplication::beep();
    }
}

// Undo commands over the layer stack. They own a copy of the layer, so redo
// restores exactly what was removed - effects, keyframes and all - rather than
// a freshly built one.
class AddLayerCommand : public QUndoCommand
{
public:
    // `index` is where the layer goes in the stack (0 is the top); -1 puts
    // it at the bottom.
    AddLayerCommand(MainWindow* window, composition::Composition* comp,
                    const composition::Layer& layer, const QString& text, int index = -1)
        : QUndoCommand(text)
        , m_window(window)
        , m_comp(comp)
        , m_layer(layer)
        , m_index(index < 0 || index > comp->layers().size() ? comp->layers().size() : index)
    {
    }

    void undo() override
    {
        m_comp->removeLayer(m_index);
        m_window->refreshAfterModelChange();
    }

    void redo() override
    {
        m_comp->insertLayer(m_index, m_layer);
        m_window->refreshAfterModelChange();
    }

private:
    MainWindow* m_window;
    composition::Composition* m_comp;
    composition::Layer m_layer;
    int m_index;
};

// One finished viewer drag of a layer's transform. Found by id, since the
// layer may have moved in the stack by the time Undo reaches it.
class LayerTransformCommand : public QUndoCommand
{
public:
    LayerTransformCommand(std::shared_ptr<composition::Composition> comp, core::Identifier layer,
                          composition::LayerTransform before, composition::LayerTransform after,
                          std::function<void()> changed, const QString& text)
        : QUndoCommand(text), m_comp(std::move(comp)), m_layer(std::move(layer)),
          m_before(std::move(before)), m_after(std::move(after)), m_changed(std::move(changed))
    {
    }
    void undo() override { apply(m_before); }
    void redo() override { apply(m_after); }

private:
    void apply(const composition::LayerTransform& transform)
    {
        if (!m_comp) return;
        for (int i = 0; i < m_comp->layers().size(); ++i) {
            if (m_comp->layers()[i].id != m_layer) continue;
            m_comp->layerRef(i).transform = transform;
            if (m_changed) m_changed();
            return;
        }
    }
    std::shared_ptr<composition::Composition> m_comp;
    core::Identifier m_layer;
    composition::LayerTransform m_before;
    composition::LayerTransform m_after;
    std::function<void()> m_changed;
};

class RemoveLayerCommand : public QUndoCommand
{
public:
    RemoveLayerCommand(MainWindow* window, composition::Composition* comp, int index,
                       const QString& text)
        : QUndoCommand(text)
        , m_window(window)
        , m_comp(comp)
        , m_layer(comp->layerCopy(index))
        , m_index(index)
    {
        for (const composition::Layer& layer : comp->layers()) {
            m_parentLinks.append(qMakePair(layer.id, layer.parentLayerId));
        }
    }

    void undo() override
    {
        m_comp->insertLayer(m_index, m_layer);
        // removeLayer promotes children to root. Restore every recorded link
        // when the parent comes back so structural Undo is lossless.
        for (int i = 0; i < m_comp->layers().size(); ++i) {
            composition::Layer& layer = m_comp->layerRef(i);
            for (const auto& link : m_parentLinks) {
                if (link.first == layer.id) { layer.parentLayerId = link.second; break; }
            }
        }
        m_window->refreshAfterModelChange();
    }

    void redo() override
    {
        m_comp->removeLayer(m_index);
        m_window->refreshAfterModelChange();
    }

private:
    MainWindow* m_window;
    composition::Composition* m_comp;
    composition::Layer m_layer;
    QVector<QPair<core::Identifier, core::Identifier>> m_parentLinks;
    int m_index;
};

} // namespace


namespace {
// Reference title format, from the screenshots: "<document>[*] - <app>",
// e.g. "Untitled Project* - VEGAS Effects", "ed_0001.vegfx* - VEGAS Effects".
const char kAppTitle[] = "OpenVegas Effects";

void giveBottomDockTheCorners(QMainWindow* window)
{
    // In the reference the Editor/Timeline row spans the full window width.
    // QMainWindow otherwise lets the side areas claim a bottom corner, which
    // stretches e.g. Layer to the status edge and prevents a bottom target
    // from appearing underneath it while docking.
    window->setCorner(Qt::BottomLeftCorner, Qt::BottomDockWidgetArea);
    window->setCorner(Qt::BottomRightCorner, Qt::BottomDockWidgetArea);
}

// Version tag for saveState()/restoreState(). Bump it whenever the set of
// dock/toolbar objects changes in a way that makes an older saved layout wrong:
// restoreState() then rejects the stale blob and the window falls back to the
// built-in arrangement. Version 4 moves Viewer, Trimmer, Export, Layer and
// 360 Viewer into one native dock-tab group.
constexpr int kLayoutStateVersion = 4;
const char kUntitledDocument[] = QT_TRANSLATE_NOOP("openvegas::ui::MainWindow", "Untitled Project");

const char kMediaPatterns[] =
    "*.mp4 *.mov *.avi *.mkv *.webm *.png *.jpg *.jpeg *.bmp *.tga *.exr *.dpx";

// Filter for the single Import command. The reference keeps a second command
// of its own for 3D models; here there is one Import, so its filter has to
// offer everything the project can hold - a user should not have to know which
// of two file dialogs a file belongs to before opening one.
//
// A function rather than a constant: the model patterns come from
// model3d::modelExtensions(), and the strings are translated, so neither can be
// built before there is an application.
QString importFileFilter()
{
    QStringList modelPatterns;
    const QStringList extensions = model3d::modelExtensions();
    modelPatterns.reserve(extensions.size());
    for (const QString& extension : extensions) {
        modelPatterns.append(QStringLiteral("*.%1").arg(extension));
    }
    const QString models = modelPatterns.join(QLatin1Char(' '));
    const QString media = QLatin1String(kMediaPatterns);

    QStringList filters;
    filters.append(QCoreApplication::translate("openvegas::ui::MainWindow",
                                               "All Supported Files (%1 %2)")
                       .arg(media, models));
    filters.append(QCoreApplication::translate("openvegas::ui::MainWindow", "Media Files (%1)")
                       .arg(media));
    // The per-format model entries come from the reference's own filter string
    // (1412d0000) rather than being restated here, so the list a user scrolls
    // reads exactly as the original's does.
    filters.append(model3d::modelFileFilter().split(QStringLiteral(";;")));
    filters.append(QCoreApplication::translate("openvegas::ui::MainWindow", "All Files (*)"));
    return filters.join(QStringLiteral(";;"));
}

int compositionFrameAt(const composition::Composition& composition, double seconds)
{
    const double fps = composition.fpsDenominator() > 0
        ? double(composition.fpsNumerator()) / composition.fpsDenominator() : 30.0;
    return qMax(0, qRound(seconds * fps));
}

// The text a layer shows at `time`, for the viewer's text frame: the clip
// under the playhead (the selected one when several overlap) and its text
// style resolved at that frame. No layer in the result when there is none.
TextTransformOverlay::Target textTargetAt(composition::Composition& comp, int layerIndex,
                                          int preferredClip, double time, int* clipIndex)
{
    TextTransformOverlay::Target target;
    if (layerIndex < 0 || layerIndex >= comp.layers().size()) return target;
    composition::Layer& layer = comp.layerRef(layerIndex);
    if (!layer.visible) return target;
    int shown = -1;
    for (int i = 0; i < layer.clips.size(); ++i) {
        const composition::Clip& clip = layer.clips.at(i);
        if (time >= clip.startSeconds && time < clip.startSeconds + clip.durationSeconds
            && (shown < 0 || i == preferredClip)) {
            shown = i;
        }
    }
    if (shown < 0) return target;
    const int frame = compositionFrameAt(comp, time);
    for (const composition::Effect& fx : layer.clips.at(shown).effects) {
        if (fx.pluginId.value() != QLatin1String("text")
            && fx.name.compare(QLatin1String("Text"), Qt::CaseInsensitive) != 0) {
            continue;
        }
        QStringList values = fx.parameterValues;
        for (int p = 0; p < values.size(); ++p) values[p] = fx.parameterAt(p, frame).toString();
        target.style = composition::textStyleFromParameters(values);
        target.layer = &layer;
        target.frame = frame;
        target.canvasSize = QSizeF(comp.displaySize());
        if (clipIndex) *clipIndex = shown;
        break;
    }
    return target;
}

void moveLayerAtFrame(composition::Layer& layer, const QPointF& delta, int frame)
{
    auto& transform = layer.transform;
    if (transform.positionXCurve.isEmpty() && transform.positionYCurve.isEmpty()) {
        transform.position += delta;
        return;
    }
    const QPointF current = transform.positionAt(frame);
    if (transform.positionXCurve.isEmpty())
        transform.positionXCurve.setDefaultValue(transform.position.x());
    if (transform.positionYCurve.isEmpty())
        transform.positionYCurve.setDefaultValue(transform.position.y());
    const auto xType = transform.positionXCurve.at(frame)
        ? transform.positionXCurve.at(frame)->temporal : composition::TemporalType::Linear;
    const auto yType = transform.positionYCurve.at(frame)
        ? transform.positionYCurve.at(frame)->temporal : composition::TemporalType::Linear;
    transform.positionXCurve.set(frame, current.x() + delta.x(), xType);
    transform.positionYCurve.set(frame, current.y() + delta.y(), yType);
}
} // namespace

MainWindow::MainWindow(app::AppMain* owner, QWidget* parent)
    : QMainWindow(parent)
    , m_ui(new Ui::MainWindow)
    , m_owner(owner)
{
    // Undo history has to exist before the menus are wired: Undo/Redo and the
    // History panel are all fed from it.
    m_undoStack = new QUndoStack(this);
    QSettings startupSettings(QSettings::IniFormat, QSettings::UserScope,
                              app::Settings::organizationName(),
                              app::Settings::applicationName());
    m_undoStack->setUndoLimit(qMax(0, startupSettings.value(
        QStringLiteral("Options/MaxUndo"), 30).toInt()));

    // The form supplies the menu bar and its actions. It also installs a
    // placeholder central widget; the native dock layout needs no central widget.
    m_ui->setupUi(this);
    delete takeCentralWidget();
    setWindowIcon(QApplication::windowIcon());
    updateWindowTitle();
    buildUi();
    // Restore the window from QSettings after the docks exist so the saved
    // layout state has real widgets to attach to.
    restoreWindowGeometry();
    // Apply after restoreState as well: old workspace blobs can carry a layout
    // whose side columns occupy the bottom corners.
    giveBottomDockTheCorners(this);
    installDockTabMenus(this);
    const auto refreshDockTabs = [this] {
        QTimer::singleShot(0, this, [this] { installDockTabMenus(this); });
    };
    connect(this, &QMainWindow::tabifiedDockWidgetActivated,
            this, [refreshDockTabs](QDockWidget*) { refreshDockTabs(); });
    const auto dockPanels = findChildren<QDockWidget*>(QString(), Qt::FindDirectChildrenOnly);
    for (QDockWidget* dock : dockPanels) {
        connect(dock, &QDockWidget::topLevelChanged,
                this, [refreshDockTabs](bool) { refreshDockTabs(); });
        connect(dock, &QDockWidget::dockLocationChanged,
                this, [refreshDockTabs](Qt::DockWidgetArea) { refreshDockTabs(); });
    }
    configureMenus();
    wireSignals();
    applyInterfacePreferences();
    installInterfacePreferenceFilter(qApp);

    m_audio = new media::AudioPlayer(this);
    connect(m_audio, &media::AudioPlayer::playbackError, this, [this](const QString& error) {
        statusBar()->showMessage(error, 10000);
    }, Qt::QueuedConnection);
    m_audioScrubTimer.setSingleShot(true);
    connect(&m_audioScrubTimer, &QTimer::timeout, this, [this] {
        if (!m_playing && m_audio) m_audio->stop();
    });
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state != Qt::ApplicationActive && app::Settings::optionSettings().value(
                QStringLiteral("Options/CloseMediaOnInactive"), false).toBool()) {
            stopPlayback();
            m_audioScrubTimer.stop();
            if (m_audio) m_audio->setSource(QString());
            m_videoDecoders.clear(); m_videoDecoderOrder.clear();
        }
    });
    connect(m_audio, &media::AudioPlayer::levelsChanged, this,
            [this](double left, double right) {
                if (m_transportBar) m_transportBar->setAudioLevels(left, right);
                if (m_metersPanel) {
                    m_metersPanel->setInputLevels(left, right);
                    m_metersPanel->setOutputLevels(left, right);
                }
            });

    m_playbackTimer.setInterval(80); // ~12.5 fps preview
    connect(&m_playbackTimer, &QTimer::timeout, this, &MainWindow::onTransportTick);
    connect(&m_autoRenderCacheTimer, &QTimer::timeout, this, [this] {
        if (!m_autoRenderCache || m_playbackTimer.isActive() || !m_renderManager
            || !m_composition || !m_viewer || m_renderManager->isCaching()) {
            return;
        }
        const double scale = m_viewer->renderScale(false);
        const QSize size(qMax(1, int(m_composition->width() * scale + 0.5)),
                         qMax(1, int(m_composition->height() * scale + 0.5)));
        m_renderManager->startPlaybackCache(m_playbackTime, size, m_viewer->renderEffects(false));
    });

    m_autoSaveTimer.setSingleShot(false);
    connect(&m_autoSaveTimer, &QTimer::timeout, this, &MainWindow::writeAutoSave);
    updateAutoSaveTimer();

    // Video decoding lives here rather than in the render worker so a slow
    // decode cannot stall compositing (with QMediaPlayer this was forced -
    // it wanted the GUI thread; libVLC does not care, and the split is kept
    // on its merits). The worker leaves requests in the media manager and
    // this drains them.
    // Short: each tick decodes at most one frame within its budget, so the
    // interval plus that decode is what sets how fast the picture follows the
    // playhead. Measured on 1080p h264 at about 30 ms a frame read straight
    // ahead and 40 for a random seek, so this lands near the source frame rate
    // while still handing the event loop a turn between frames.
    m_videoDecodeTimer.setInterval(8);
    connect(&m_videoDecodeTimer, &QTimer::timeout, this,
            &MainWindow::serviceVideoDecodeRequests);
    m_videoDecodeTimer.start();

    if (m_owner && m_owner->settings() && m_startPanel) {
        m_startPanel->setRecentProjects(m_owner->settings()->recentProjects());
    }
    if (m_owner && m_owner->settings() && m_learnSidebar
        && m_owner->settings()->learnSidebarIsOpen()) {
        m_learnSidebar->show();
    }
}

MainWindow::~MainWindow()
{
    // StartPanel is deliberately not parented to QMainWindow: restoreState()
    // treats every child QDockWidget as a restorable dock and would recreate
    // the obsolete second Timeline/Start row from older workspace blobs.
    delete m_startPanel;
    delete m_ui;
    // A module's custom UI ends before the GUI thread's native renderer (and
    // its GL context) is released while the application still exists.
    plugin::setNativeCustomUiRedrawHandler({});
    m_customUiOverlay.reset();
    m_instanceHost.reset();
    plugin::releaseNativeEffectThreadRenderer();
}

void MainWindow::restoreWindowGeometry()
{
    // Mirror of the reference's MainAppWindow::show(): keys live in QSettings
    // under the "MainWindow" group. Geometry is stored as QByteArray, the window
    // state separately as int (Qt::WindowMaximized etc.).
    QSettings settings;
    settings.beginGroup(QStringLiteral("MainWindow"));
    const QByteArray geometry = settings.value(QStringLiteral("Geometry")).toByteArray();
    const QByteArray dockState = settings.value(QStringLiteral("State")).toByteArray();
    const int windowStateFlags = settings.value(QStringLiteral("WindowState"), 0).toInt();
    settings.endGroup();

    const bool restored =
        !geometry.isEmpty() && restoreGeometry(geometry) && restoreState(dockState, kLayoutStateVersion);
    if (restored) {
        if (windowStateFlags & Qt::WindowMaximized) {
            setWindowState(windowState() | Qt::WindowMaximized);
        }
        return;
    }

    // First run (or the saved layout no longer matches a screen): the reference
    // centres a fresh window on the primary screen and shows it maximized.
    const QScreen* screen = QGuiApplication::primaryScreen();
    const QRect available =
        screen ? screen->availableGeometry() : QRect(0, 0, 1024, 728);
    resize(1400, qMin(900, available.height() - 32));
    move(available.center() - rect().center());
    setWindowState(windowState() | Qt::WindowMaximized);
}

void MainWindow::saveWindowGeometry()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("MainWindow"));
    settings.setValue(QStringLiteral("Geometry"), saveGeometry());
    settings.setValue(QStringLiteral("State"), saveState(kLayoutStateVersion));
    settings.setValue(QStringLiteral("WindowState"),
                       static_cast<int>(windowState()
                                        & (Qt::WindowMinimized | Qt::WindowMaximized
                                           | Qt::WindowFullScreen)));
    settings.setValue(QStringLiteral("NormalGeometry"), normalGeometry());
    settings.endGroup();
}

// Rebuilds Window > Workspaces: the saved workspaces as checkable entries,
// then the three commands the reference's WorkspacesMenu carries -
// "Save Workspace...", "Delete Workspace...", "Reset Workspace".
void MainWindow::loadWorkspaces()
{
    if (!m_workspacesMenu) {
        return;
    }
    QSettings settings;
    settings.beginGroup(QStringLiteral("Workspaces"));
    const QStringList names = settings.childGroups();
    settings.endGroup();

    QStringList list;
    list.append(tr("My Workspace"));
    for (const QString& n : names) {
        if (!n.isEmpty()) {
            list.append(n);
        }
    }
    if (!list.contains(m_currentWorkspace)) {
        m_currentWorkspace = tr("My Workspace");
    }

    m_workspacesMenu->clear();
    for (const QString& name : list) {
        QAction* action = m_workspacesMenu->addAction(name);
        action->setCheckable(true);
        action->setChecked(name == m_currentWorkspace);
        connect(action, &QAction::triggered, this, [this, name] { applyWorkspace(name); });
    }
    m_workspacesMenu->addSeparator();
    connect(m_workspacesMenu->addAction(tr("Save Workspace...")), &QAction::triggered, this,
            &MainWindow::saveWorkspace);
    connect(m_workspacesMenu->addAction(tr("Delete Workspace...")), &QAction::triggered, this,
            &MainWindow::deleteWorkspace);
    connect(m_workspacesMenu->addAction(tr("Reset Workspace")), &QAction::triggered, this,
            &MainWindow::resetWorkspace);
}

void MainWindow::saveWorkspace()
{
    bool ok = false;
    const QString name =
        QInputDialog::getText(this, tr("Save Workspace"),
                              tr("Workspace name:"), QLineEdit::Normal,
                              m_currentWorkspace, &ok);
    if (!ok || name.trimmed().isEmpty()) {
        return;
    }
    if (name.trimmed() == tr("My Workspace")) {
        // Reference: "You cannot replace the default workspaces. Please choose
        // another name." (1412d54b0)
        statusBar()->showMessage(
            tr("You cannot replace the default workspaces. Please choose another name."));
        return;
    }
    QSettings settings;
    settings.beginGroup(QStringLiteral("Workspaces"));
    settings.beginGroup(name.trimmed());
    settings.setValue(QStringLiteral("State"), saveState(kLayoutStateVersion));
    settings.endGroup();
    settings.endGroup();

    m_currentWorkspace = name.trimmed();
    loadWorkspaces();
    statusBar()->showMessage(tr("Workspace saved: %1").arg(m_currentWorkspace));
}

void MainWindow::deleteWorkspace()
{
    const QString name = m_currentWorkspace;
    if (name.isEmpty() || name == tr("My Workspace")) {
        statusBar()->showMessage(
            tr("My Workspace cannot be deleted; choose a saved workspace first"));
        return;
    }
    if (QMessageBox::question(
            this, tr("Delete Workspace"),
            tr("Are you sure you want to permanently remove this workspace?"))
        != QMessageBox::Yes) {
        return;
    }
    QSettings settings;
    settings.beginGroup(QStringLiteral("Workspaces"));
    settings.remove(name);
    settings.endGroup();

    m_currentWorkspace = tr("My Workspace");
    loadWorkspaces();
    statusBar()->showMessage(tr("Workspace deleted: %1").arg(name));
}

void MainWindow::resetWorkspace()
{
    m_currentWorkspace = tr("My Workspace");
    m_viewer->setLayout(ViewerWidget::ViewerLayout::Single);
    loadWorkspaces();
    statusBar()->showMessage(tr("Workspace reset"));
}

void MainWindow::applyWorkspace(const QString& name)
{
    m_currentWorkspace = name;
    if (name.isEmpty() || name == tr("My Workspace")) {
        loadWorkspaces();
        return;
    }
    QSettings settings;
    settings.beginGroup(QStringLiteral("Workspaces"));
    settings.beginGroup(name);
    const QByteArray state = settings.value(QStringLiteral("State")).toByteArray();
    settings.endGroup();
    settings.endGroup();
    if (!state.isEmpty()) {
        restoreState(state, kLayoutStateVersion);
    }
    giveBottomDockTheCorners(this);
    loadWorkspaces();
    statusBar()->showMessage(tr("Workspace: %1").arg(name));
}

void MainWindow::buildUi()
{
    setDockOptions(QMainWindow::AllowNestedDocks
                   | QMainWindow::AllowTabbedDocks
                   | QMainWindow::AnimatedDocks);
    giveBottomDockTheCorners(this);
    // Reference Effects screen layout (recovered Screen XML, panel types):
    //   Left  : Media (1) + Text (128)
    //   Center: Viewer (32)
    //   Right : Effects (4) + Controls (2) + Layout (2055) + Track (1024)
    //   Bottom: Editor/Trimmer timeline
    m_mediaPanel = new MediaPanel(this);
    m_mediaPanel->setUndoStack(m_undoStack);
    addDockWidget(Qt::LeftDockWidgetArea, m_mediaPanel);

    // Native dock tabs share one bottom row and retain Qt's drag/split targets.
    // No central widget: the middle dock group occupies the space between the
    // left and right columns, above the timeline.
    m_viewerDock = new QDockWidget(tr("Viewer"), this);
    m_viewerDock->setObjectName(QStringLiteral("ViewerDock"));
    auto* viewerPage = new ViewerPanel(m_viewerDock);
    m_viewerPage = viewerPage;
    m_viewer = viewerPage->viewer();
    m_viewer->setShowMouseCoordinates(app::Settings::showMouseCoordinates());
    m_transportBar = viewerPage->transportBar();
    m_viewerDock->setWidget(viewerPage);
    {
        // What the Viewer tab shows while the page is in the 360 Viewer tab
        // (both tabs can be on screen once one is floated).
        m_viewerPlaceholder = new QWidget(m_viewerDock);
        m_viewerPlaceholder->setObjectName(QStringLiteral("viewerIn360Placeholder"));
        auto* layout = new QVBoxLayout(m_viewerPlaceholder);
        layout->addStretch();
        auto* label = new QLabel(tr("The Viewer is open in the 360 Viewer."), m_viewerPlaceholder);
        label->setAlignment(Qt::AlignCenter);
        layout->addWidget(label);
        auto* back = new QPushButton(tr("Show the Viewer Here"), m_viewerPlaceholder);
        back->setObjectName(QStringLiteral("showViewerHere"));
        connect(back, &QPushButton::clicked, this, [this] { showViewerIn360(false); });
        layout->addWidget(back, 0, Qt::AlignHCenter);
        layout->addStretch();
        m_viewerPlaceholder->hide();
    }
    addDockWidget(Qt::LeftDockWidgetArea, m_viewerDock);
    splitDockWidget(m_mediaPanel, m_viewerDock, Qt::Horizontal);

    m_textPanel = new TextPanel(this);
    addDockWidget(Qt::LeftDockWidgetArea, m_textPanel);
    tabifyDockWidget(m_mediaPanel, m_textPanel);
    m_mediaPanel->raise();

    // Right column: Effects + Controls + Library + History tabbed.
    m_effectsPanel = new EffectsPanel(this);
    addDockWidget(Qt::RightDockWidgetArea, m_effectsPanel);

    m_controlsPanel = new EffectInspector(this);
    addDockWidget(Qt::RightDockWidgetArea, m_controlsPanel);
    tabifyDockWidget(m_effectsPanel, m_controlsPanel);
    m_controlsPanel->raise();

    // Layout (2055) and Track (1024) complete the reference panel group:
    // the screenshots show the tab order Effects | Controls | Layout | Track.
    m_layoutPanel = new LayoutPanel(this);
    addDockWidget(Qt::RightDockWidgetArea, m_layoutPanel);
    tabifyDockWidget(m_effectsPanel, m_layoutPanel);

    m_trackPanel = new TrackPanel(this);
    addDockWidget(Qt::RightDockWidgetArea, m_trackPanel);
    tabifyDockWidget(m_effectsPanel, m_trackPanel);

    // Layer is a distinct selection inspector in the reference.  Its runtime
    // widget is replaced when exactly one layer is selected.
    m_layerPanel = new LayerPanel(this);
    addDockWidget(Qt::RightDockWidgetArea, m_layerPanel);


    // Library (2059) and History (256) belong to the reference Edit screen;
    // they stay available but sit after the Effects-screen four.
    m_libraryPanel = new LibraryPanel(this);
    addDockWidget(Qt::RightDockWidgetArea, m_libraryPanel);
    tabifyDockWidget(m_effectsPanel, m_libraryPanel);

    m_historyPanel = new HistoryPanel(this);
    addDockWidget(Qt::RightDockWidgetArea, m_historyPanel);
    tabifyDockWidget(m_effectsPanel, m_historyPanel);

    // The 360 preview joins the Viewer dock group below.
    m_viewer360 = new Preview360VideoPanel(this);
    addDockWidget(Qt::RightDockWidgetArea, m_viewer360);

    // Picking a tab moves the one Viewer into it: the 360 Viewer tab shows it
    // in its 360 mode, the Viewer tab flat.
    // A tab switch shows the new tab before it hides the old one, so the move
    // is decided once both have settled; with both on screen (one floated)
    // the tab shown last wins.
    // (isVisible() stays true for a tab that is merely not current, so what
    // is on screen is followed through visibilityChanged.)
    struct TabsShown { bool viewer = true; bool viewer360 = false; bool last360 = false; };
    auto shown = std::make_shared<TabsShown>();
    const auto settle = [this, shown] {
        QTimer::singleShot(0, this, [this, shown] {
            if (shown->viewer && shown->viewer360) showViewerIn360(shown->last360);
            else if (shown->viewer360) showViewerIn360(true);
            else if (shown->viewer) showViewerIn360(false);
        });
    };
    connect(m_viewer360, &QDockWidget::visibilityChanged, this, [shown, settle](bool visible) {
        shown->viewer360 = visible;
        if (visible) shown->last360 = true;
        settle();
    });
    connect(m_viewerDock, &QDockWidget::visibilityChanged, this, [shown, settle](bool visible) {
        shown->viewer = visible;
        if (visible) shown->last360 = false;
        settle();
    });

    // Reference opens the Effects screen with Controls fronting the right
    // column (see screenshot 0.png); tabifying above leaves the last dock on
    // top, so raise it again here.
    m_controlsPanel->raise();

    QSettings librarySettings(QSettings::IniFormat, QSettings::UserScope,
                              app::Settings::organizationName(),
                              app::Settings::applicationName());
    const QString libraryRoot = QDir(QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation)).filePath(QStringLiteral("Library"));
    const QString libraryMedia = librarySettings.value(
        QStringLiteral("Options/LibraryMediaPath"),
        QDir(libraryRoot).filePath(QStringLiteral("Media"))).toString();
    const QString libraryTemplates = librarySettings.value(
        QStringLiteral("Options/LibraryTemplatePath"),
        QDir(libraryRoot).filePath(QStringLiteral("Templates"))).toString();
    m_libraryPanel->setLibraryPaths(libraryMedia, libraryTemplates);
    m_historyPanel->addEntry(tr("New Project"));
    // The panel is now a view of the undo stack rather than a write-only log:
    // every pushed command adds its row, and clicking a row walks the stack to
    // that point. Its undoRequested / redoRequested signals used to reach
    // nobody at all.
    connect(m_undoStack, &QUndoStack::indexChanged, this, [this](int index) {
        // Row 0 is "New Project", so stack step N lives on row N + 1.
        while (m_historyPanel->count() - 1 < m_undoStack->count()) {
            const int next = m_historyPanel->count() - 1;
            m_historyPanel->addEntry(m_undoStack->text(next));
        }
        m_historyPanel->setCurrentIndex(index);
        // A camera added with a 3D switch in the timeline, Controls or Layer
        // panel (or taken back by Undo) changes the stack under the others.
        if (m_composition && m_composition->layers().size() != m_lastLayerCount) {
            m_lastLayerCount = m_composition->layers().size();
            // The timeline rebuilds itself after its own edits and on the
            // other panels' change signals.
            if (m_trackPanel) m_trackPanel->refresh();
            if (m_layerPanel) m_layerPanel->refresh();
            requestRenderFrame();
        }
        // A shot that stops being 3D drops the multi-view layout, as the
        // reference's viewer does (FUN_1408e6df0, SetLayout(0)).
        const bool is3D = m_composition && compositionIs3D(*m_composition);
        if (m_lastShotIs3D && !is3D && m_viewer) m_viewer->setLayout(ViewerWidget::ViewerLayout::Single);
        m_lastShotIs3D = is3D;
    });
    connect(m_historyPanel, &HistoryPanel::undoRequested, m_undoStack, &QUndoStack::undo);
    connect(m_historyPanel, &HistoryPanel::redoRequested, m_undoStack, &QUndoStack::redo);
    connect(m_historyPanel, &HistoryPanel::clearRequested, this, [this] {
        m_undoStack->clear();
        m_historyPanel->clear();
        m_historyPanel->addEntry(tr("New Project"));
        m_historyPanel->setCurrentIndex(0);
    });
    connect(m_undoStack, &QUndoStack::canUndoChanged,
            m_historyPanel, &HistoryPanel::setCanUndo);
    connect(m_undoStack, &QUndoStack::canRedoChanged,
            m_historyPanel, &HistoryPanel::setCanRedo);

    auto* trimmerDock = new QDockWidget(tr("Trimmer"), this);
    trimmerDock->setObjectName(QStringLiteral("TrimmerDock"));
    m_trimmerPanel = new TrimmerPanel(trimmerDock);
    trimmerDock->setWidget(m_trimmerPanel);
    addDockWidget(Qt::LeftDockWidgetArea, trimmerDock);
    tabifyDockWidget(m_viewerDock, trimmerDock);

    auto* exportDock = new QDockWidget(tr("Export"), this);
    exportDock->setObjectName(QStringLiteral("ExportDock"));
    m_exportPanel = new ExportPanel(exportDock);
    exportDock->setWidget(m_exportPanel);
    // The export queue (ExportTaskManager): restored from its tasks file.
    m_exportQueue = new ExportQueue(this);
    m_exportQueue->setTasksFile(QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                                    .filePath(QStringLiteral("ExportTasks.xml")));
    m_exportQueue->load();
    m_exportQueueView = new ExportQueueView(m_exportQueue, m_exportPanel);
    m_exportPanel->setQueueWidget(m_exportQueueView);
    addDockWidget(Qt::LeftDockWidgetArea, exportDock);
    tabifyDockWidget(m_viewerDock, exportDock);
    tabifyDockWidget(m_viewerDock, m_layerPanel);
    tabifyDockWidget(m_viewerDock, m_viewer360);
    m_viewer360->show();
    m_viewerDock->raise();
    setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::South);

    // Bottom: Editor (timeline), as the reference Edit screen's Editor panel.
    m_timeline = new TimelineWidget(this);
    addDockWidget(Qt::BottomDockWidgetArea, m_timeline);
    resizeDocks({m_mediaPanel, m_viewerDock, m_effectsPanel}, {300, 900, 400}, Qt::Horizontal);
    resizeDocks({m_viewerDock, m_timeline}, {600, 350}, Qt::Vertical);

    // Meters (2051) belongs to the reference *Edit* screen, not the Effects
    // screen this window mirrors: the recovered Screen XML lists ten panels for
    // Effects (Media, Text, Viewer, Trimmer, Export, Effects, Controls, Layout,
    // Track, Start) and no Meters, while the Edit screen puts it in its own
    // narrow container beside the Editor (Stretch 0.0709 against the Editor's
    // 2.10). Kept available from the Window menu, hidden by default - visible
    // it just showed an empty sliver next to the timeline.
    m_metersPanel = new AudioMetersPanel(this);
    addDockWidget(Qt::BottomDockWidgetArea, m_metersPanel, Qt::Horizontal);
    m_metersPanel->hide();
    connect(m_metersPanel, &QDockWidget::visibilityChanged,
            m_transportBar, &ViewerTransportBar::setMetersVisible);

    // Learn Sidebar on the right-hand edge, its state remembered in
    // "learnSidebarIsOpen" as the reference does.
    m_learnSidebar = new LearnSidebar(this);
    addDockWidget(Qt::RightDockWidgetArea, m_learnSidebar);
    m_learnSidebar->hide();

    // Start and open compositions use the same bottom panel in the reference.
    // Keep StartPanel as the signal/state controller and place its form in the
    // Timeline page stack; a second QDockWidget would add a duplicate tab row.
    // Keep the controller outside QMainWindow's QObject tree. Its visible form
    // is immediately reparented into Timeline below; the controller itself is
    // deleted explicitly in ~MainWindow().
    m_startPanel = new StartPanel;
    m_timeline->setStartPage(m_startPanel->takeContentWidget());

    // The toolbar has to exist before restoreWindowGeometry() runs, which is
    // why it is built here with the docks rather than alongside the menus.
    // QMainWindow::restoreState() only places toolbars and docks that already
    // exist and silently drops the entries it cannot match; adding the toolbar
    // afterwards dropped it into a top area whose geometry had already been
    // computed without it, which is how it ended up drawn over File/Edit.
    buildTransportToolbar();

    // All panel windows share the reference title control.  Install it after
    // every dock has been constructed but before restoreState() replays the
    // saved arrangement.
    const auto docks = findChildren<QDockWidget*>(QString(), Qt::FindDirectChildrenOnly);
    for (QDockWidget* dock : docks) installDockTitleBar(dock);

    statusBar()->showMessage(tr("Ready"));
    statusBar()->setSizeGripEnabled(false);
    statusBar()->hide();
}

void MainWindow::configureMenus()
{
    // File, Edit, Effects, Export and Help come from ui/MainWindow.ui; only
    // their behaviour is wired here. The Window menu is still assembled in code
    // below because it is made of runtime objects -- the dock panels'
    // toggleViewAction()s and a viewer-layout group whose check marks track each
    // other -- which a Designer form has no way to express.

    // Standard sequences stay in code: QKeySequence resolves these per platform,
    // which a literal string in the form cannot do.
    m_ui->actionNew->setShortcut(QKeySequence::New);
    m_ui->actionOpen->setShortcut(QKeySequence::Open);
    m_ui->actionSave->setShortcut(QKeySequence::Save);
    m_ui->actionSaveAs->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_S));
    m_ui->actionExit->setShortcut(QKeySequence::Quit);
    m_ui->actionImport->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    // F2 is the reference Rename command.  It was accidentally assigned to
    // Show All Effects as well, making the shortcut ambiguous in Qt.
    m_ui->actionShowAllEffects->setShortcut(QKeySequence());
    m_ui->actionOptions->setMenuRole(QAction::PreferencesRole);
    m_ui->actionAbout->setMenuRole(QAction::AboutRole);
    m_ui->actionExit->setMenuRole(QAction::QuitRole);

    connect(m_ui->actionNew, &QAction::triggered, this, &MainWindow::onNewProject);
    connect(m_ui->actionOpen, &QAction::triggered, this, &MainWindow::onOpenProject);
    connect(m_ui->actionSave, &QAction::triggered, this, &MainWindow::onSaveProject);
    connect(m_ui->actionSaveAs, &QAction::triggered, this, &MainWindow::onSaveProjectAs);
    connect(m_ui->actionImport, &QAction::triggered, this, &MainWindow::onImportMedia);
    connect(m_ui->actionImportCompositeShot, &QAction::triggered, this, &MainWindow::importCompositeShot);
    connect(m_ui->actionExit, &QAction::triggered, this, &MainWindow::close);
    // File > Project Settings... is the project's Editor and Rendering
    // settings (ProjectSettingsDialog), not the composite shot's properties -
    // those stay on the timeline's cog.
    connect(m_ui->actionProjectSettings, &QAction::triggered, this,
            [this] { editProjectSettings(false); });
    connect(m_ui->actionRecordVoiceover, &QAction::triggered, this, [this] {
        if (!m_composition || !m_mediaManager) return;
        QString path = QFileDialog::getSaveFileName(this, tr("Record Voiceover"),
            QDir(app::Settings::optionSettings().value(QStringLiteral("Options/VoiceoverPath"),
                QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).toString())
                .filePath(QStringLiteral("Voiceover.wav")), tr("WAV audio (*.wav)"));
        if (path.isEmpty()) return;
        if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".wav");
        const double startTime = m_playbackTime;
        stopPlayback();
        VoiceoverDialog dialog(path, this);
        dialog.recordingStarted = [this] {
            if (m_audio) m_audio->setMuted(app::Settings::optionSettings().value(
                QStringLiteral("Options/Voiceover/MuteOutput"), true).toBool());
            beginPlayback();
        };
        dialog.recordingStopped = [this] {
            stopPlayback();
            if (m_audio) m_audio->setMuted(false);
        };
        const bool saved = dialog.exec() == QDialog::Accepted;
        m_playbackTime = startTime;
        m_timeline->setPlayheadPosition(startTime);
        if (saved) {
            app::Settings::optionSettings().setValue(QStringLiteral("Options/VoiceoverPath"), QFileInfo(path).absolutePath());
            const core::Result imported = m_mediaManager->importFile(path);
            if (imported.isFailure()) {
                QMessageBox::warning(this, tr("Record Voiceover"), imported.message());
            } else {
                const auto asset = m_mediaManager->assetByFilePath(path);
                composition::Layer layer;
                layer.name = asset.fileName();
                layer.kind = composition::LayerKind::Media;
                composition::Clip clip;
                clip.mediaId = asset.id(); clip.startSeconds = startTime;
                clip.durationSeconds = asset.durationSeconds();
                layer.clips.append(clip);
                m_undoStack->push(new AddLayerCommand(this, m_composition.get(), layer,
                                                       tr("Record Voiceover")));
                m_mediaPanel->refresh();
            }
        }
    });

    connect(m_ui->menuOpenRecent, &QMenu::aboutToShow, this, [this] {
        m_ui->menuOpenRecent->clear();
        const QStringList paths = m_owner && m_owner->settings()
            ? m_owner->settings()->recentProjects() : QStringList();
        for (auto it = paths.crbegin(); it != paths.crend(); ++it) {
            QAction* action = m_ui->menuOpenRecent->addAction(QFileInfo(*it).fileName());
            action->setToolTip(QDir::toNativeSeparators(*it));
            action->setEnabled(QFileInfo::exists(*it));
            connect(action, &QAction::triggered, this,
                    [this, path = *it] { onRecentProjectActivated(path); });
        }
        if (paths.isEmpty()) {
            QAction* empty = m_ui->menuOpenRecent->addAction(tr("No recent projects"));
            empty->setEnabled(false);
        }
    });
    // "Recover Projects..." (WindowHeaderWidget): the auto-saves in the
    // auto-save folder, in the reference's AutoSaveRecoveryDialog.
    m_ui->actionRecoveredSaves->setText(
        QCoreApplication::translate("WindowHeaderWidget", "Recover Projects..."));
    connect(m_ui->actionRecoveredSaves, &QAction::triggered, this, [this] {
        if (autosave::entries().isEmpty()) {
            const QString message = tr("No recoverable auto-saves were found.");
            QMessageBox::information(this,
                QCoreApplication::translate("biff::ui::common::AutoSaveRecoveryDialog", "Recovered Projects"),
                message);
            return;
        }
        showRecoveredProjects();
    });

    // Undo / Redo, on the one stack every model edit is pushed to. The actions
    // borrow their enabled state and their text from it, so "Undo Add Layer"
    // names the step it will take back.
    m_ui->actionUndo->setShortcut(QKeySequence::Undo);
    m_ui->actionRedo->setShortcut(QKeySequence::Redo);
    m_ui->actionUndo->setEnabled(false);
    m_ui->actionRedo->setEnabled(false);
    connect(m_ui->actionUndo, &QAction::triggered, m_undoStack, &QUndoStack::undo);
    connect(m_ui->actionRedo, &QAction::triggered, m_undoStack, &QUndoStack::redo);
    connect(m_undoStack, &QUndoStack::canUndoChanged, m_ui->actionUndo, &QAction::setEnabled);
    connect(m_undoStack, &QUndoStack::canRedoChanged, m_ui->actionRedo, &QAction::setEnabled);
    connect(m_undoStack, &QUndoStack::undoTextChanged, this, [this](const QString& text) {
        m_ui->actionUndo->setText(text.isEmpty() ? tr("Undo") : tr("Undo %1").arg(text));
    });
    connect(m_undoStack, &QUndoStack::redoTextChanged, this, [this](const QString& text) {
        m_ui->actionRedo->setText(text.isEmpty() ? tr("Redo") : tr("Redo %1").arg(text));
    });
    // Every push marks the document dirty; undoing back to the clean index
    // clears that again.
    connect(m_undoStack, &QUndoStack::cleanChanged, this, [this](bool clean) {
        m_projectModified = !clean;
        updateWindowTitle();
    });

    connect(m_ui->actionCut, &QAction::triggered, this, [this] { copySelection(true); });
    connect(m_ui->actionCopy, &QAction::triggered, this, [this] { copySelection(false); });
    connect(m_ui->actionPaste, &QAction::triggered, this, &MainWindow::pasteSelection);
    connect(m_ui->actionPasteAttributes, &QAction::triggered,
            this, &MainWindow::pasteSelectionAttributes);
    connect(m_ui->actionDelete, &QAction::triggered, this, &MainWindow::deleteSelectedLayer);
    connect(m_ui->actionDuplicate, &QAction::triggered, this, &MainWindow::duplicateSelectedLayer);
    connect(m_ui->actionSelectAll, &QAction::triggered, m_timeline, &TimelineWidget::selectAllRows);
    connect(m_ui->actionReset, &QAction::triggered, this, &MainWindow::removeSelectionAttributes);
    connect(m_ui->actionSlice, &QAction::triggered, this, &MainWindow::sliceSelectionAtPlayhead);
    connect(m_ui->actionRippleDelete, &QAction::triggered, this, &MainWindow::rippleDeleteSelection);
    connect(m_ui->actionRemoveAttributes, &QAction::triggered,
            this, &MainWindow::removeSelectionAttributes);
    connect(m_ui->actionRemoveEffects, &QAction::triggered,
            this, &MainWindow::removeSelectionEffects);

    auto* rename = new QAction(tr("Rename"), this);
    rename->setObjectName(QStringLiteral("actionRename"));
    rename->setShortcut(QKeySequence(Qt::Key_F2));
    connect(rename, &QAction::triggered, this, [this] {
        if (!m_composition || m_selectedLayer < 0
            || m_selectedLayer >= m_composition->layers().size()) return;
        const QString oldName = m_composition->layers().at(m_selectedLayer).name;
        bool accepted = false;
        const QString name = QInputDialog::getText(this, tr("Rename Layer"), tr("Name:"),
                                                   QLineEdit::Normal, oldName, &accepted).trimmed();
        if (!accepted || name.isEmpty() || name == oldName) return;
        m_composition->layerRef(m_selectedLayer).name = name;
        refreshAfterModelChange();
        m_projectModified = true;
        updateWindowTitle();
    });
    addAction(rename);

    auto* closePanel = new QAction(tr("Close Active Panel"), this);
    closePanel->setObjectName(QStringLiteral("actionCloseActivePanel"));
    closePanel->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_W));
    connect(closePanel, &QAction::triggered, this, [] {
        QWidget* widget = QApplication::focusWidget();
        while (widget) {
            if (auto* dock = qobject_cast<QDockWidget*>(widget)) {
                if (dock->features().testFlag(QDockWidget::DockWidgetClosable)) dock->close();
                return;
            }
            widget = widget->parentWidget();
        }
    });
    addAction(closePanel);

    connect(m_ui->actionShowAllEffects, &QAction::triggered, this,
            [this] {
                m_effectsPanel->show();
                m_effectsPanel->raise();
                m_effectsPanel->showAll();
            });
    connect(m_ui->actionBrowseEffects, &QAction::triggered, this,
            [this] {
                m_effectsPanel->show();
                m_effectsPanel->raise();
            });

    m_effectFavoritesMenu = m_ui->menuEffects->addMenu(tr("Favorites"));
    m_effectFavoritesMenu->setObjectName(QStringLiteral("menuEffectFavorites"));
    m_effectRecentsMenu = m_ui->menuEffects->addMenu(tr("Recents"));
    m_effectRecentsMenu->setObjectName(QStringLiteral("menuEffectRecents"));
    auto populateEffectsMenu = [this](QMenu* menu, const QStringList& ids) {
        menu->clear();
        plugin::PluginManager* plugins = m_owner ? m_owner->pluginManager() : nullptr;
        for (const QString& id : ids) {
            if (!plugins) break;
            const plugin::EffectSpec spec = plugins->spec(plugin::PluginId(id));
            if (!spec.id.isValid()) continue;
            QAction* action = menu->addAction(
                spec.displayName.isEmpty() ? spec.name : spec.displayName);
            connect(action, &QAction::triggered, this, [this, spec] { onEffectActivated(spec); });
        }
        if (menu->isEmpty()) {
            QAction* empty = menu->addAction(tr("None"));
            empty->setEnabled(false);
        }
    };
    connect(m_effectFavoritesMenu, &QMenu::aboutToShow, this,
            [this, populateEffectsMenu] {
                populateEffectsMenu(m_effectFavoritesMenu, m_effectsPanel->favourites());
            });
    connect(m_effectRecentsMenu, &QMenu::aboutToShow, this,
            [this, populateEffectsMenu] {
                populateEffectsMenu(m_effectRecentsMenu, m_recentEffectIds);
            });

    connect(m_ui->actionRenderFrame, &QAction::triggered, this, &MainWindow::onRenderRequested);
    connect(m_ui->actionExportSnapshot, &QAction::triggered, this, &MainWindow::exportSnapshot);
    connect(m_ui->actionExportProject, &QAction::triggered, this,
            [this] { showCenterTab(m_exportPanel); });

    connect(m_ui->actionOptions, &QAction::triggered, this, &MainWindow::onOptions);
    connect(m_ui->actionAbout, &QAction::triggered, this, [this] {
        AboutDialog dialog(this);
        dialog.exec();
    });
    connect(m_ui->actionOnlineHelp, &QAction::triggered, this, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral(
            "https://github.com/ili4yov-ika/OpenVegasEffects")));
    });

    connect(m_ui->actionReloadExternalStylesheet, &QAction::triggered,
            this, [] { applyTheme(qApp); });
    const QSettings debugSettings;
    m_ui->actionShowRenderTimings->setChecked(
        debugSettings.value(QStringLiteral("Debug/ShowRenderTimings"), false).toBool());
    m_ui->actionShowHardwareDecodingIndicator->setChecked(
        debugSettings.value(QStringLiteral("Debug/HardwareDecodingIndicator"), false).toBool());
    m_ui->actionPrintAnalytics->setChecked(
        debugSettings.value(QStringLiteral("Debug/PrintAnalytics"), false).toBool());
    m_ui->actionEnableEffectPresetCreation->setChecked(
        debugSettings.value(QStringLiteral("Debug/EffectPresetCreation"), false).toBool());
    connect(m_ui->actionShowRenderTimings, &QAction::toggled, this, [this](bool on) {
        QSettings().setValue(QStringLiteral("Debug/ShowRenderTimings"), on);
        statusBar()->showMessage(on ? tr("Render timings enabled") : tr("Render timings disabled"));
    });
    connect(m_ui->actionShowHardwareDecodingIndicator, &QAction::toggled,
            this, [this](bool on) {
                QSettings().setValue(QStringLiteral("Debug/HardwareDecodingIndicator"), on);
                statusBar()->showMessage(on ? tr("Hardware decoding indicator enabled")
                                            : tr("Hardware decoding indicator disabled"));
            });
    connect(m_ui->actionPrintAnalytics, &QAction::toggled, this, [](bool on) {
        QSettings().setValue(QStringLiteral("Debug/PrintAnalytics"), on);
        OV_LOG_INFO(QStringLiteral("Analytics logging %1").arg(on ? QStringLiteral("enabled")
                                                                   : QStringLiteral("disabled")));
    });
    connect(m_ui->actionEnableEffectPresetCreation, &QAction::toggled, this, [](bool on) {
        QSettings().setValue(QStringLiteral("Debug/EffectPresetCreation"), on);
    });
    connect(m_ui->actionExportFontList, &QAction::triggered, this, [this] {
        const QString path = QFileDialog::getSaveFileName(
            this, tr("Export Font List"), QStringLiteral("fonts.txt"), tr("Text files (*.txt)"));
        if (path.isEmpty()) return;
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::warning(this, tr("Export Font List"), file.errorString());
            return;
        }
        QTextStream stream(&file);
        for (const QString& family : QFontDatabase::families()) stream << family << Qt::endl;
        statusBar()->showMessage(tr("Exported font list to %1").arg(path));
    });
    m_ui->menuDebug->menuAction()->setVisible(qEnvironmentVariableIsSet("OPENVEGAS_DEBUG_MENU"));

    const QHash<QString, QString> trackedEvents{
        {QStringLiteral("actionNew"), QStringLiteral("new project requested")},
        {QStringLiteral("actionOpen"), QStringLiteral("open project requested")},
        {QStringLiteral("actionSave"), QStringLiteral("save project requested")},
        {QStringLiteral("actionSaveAs"), QStringLiteral("save as project requested")},
        {QStringLiteral("actionSlice"), QStringLiteral("sliceSelected()")},
        {QStringLiteral("actionSelectAll"), QStringLiteral("selectAll()")},
        {QStringLiteral("actionCut"), QStringLiteral("cut()")},
        {QStringLiteral("actionCopy"), QStringLiteral("copy()")},
        {QStringLiteral("actionPaste"), QStringLiteral("paste()")},
        {QStringLiteral("actionDuplicate"), QStringLiteral("duplicate()")},
        {QStringLiteral("actionDelete"), QStringLiteral("remove()")},
        {QStringLiteral("actionRippleDelete"), QStringLiteral("rippleDelete()")},
        {QStringLiteral("actionPasteAttributes"), QStringLiteral("pasteAttributes()")},
        {QStringLiteral("actionRemoveAttributes"), QStringLiteral("removeAttributes()")},
        {QStringLiteral("actionRemoveEffects"), QStringLiteral("removeEffects()")},
    };
    const QStringList trackedActions{
        QStringLiteral("actionNew"), QStringLiteral("actionOpen"),
        QStringLiteral("actionRecoveredSaves"), QStringLiteral("actionSave"),
        QStringLiteral("actionSaveAs"), QStringLiteral("actionProjectSettings"),
        QStringLiteral("actionOptions"), QStringLiteral("actionExit"),
        QStringLiteral("actionUndo"), QStringLiteral("actionRedo"),
        QStringLiteral("actionSlice"), QStringLiteral("actionSelectAll"),
        QStringLiteral("actionCut"), QStringLiteral("actionCopy"),
        QStringLiteral("actionPaste"), QStringLiteral("actionDuplicate"),
        QStringLiteral("actionDelete"), QStringLiteral("actionRippleDelete"),
        QStringLiteral("actionPasteAttributes"), QStringLiteral("actionRemoveAttributes"),
        QStringLiteral("actionRemoveEffects"), QStringLiteral("actionAbout"),
        QStringLiteral("actionOnlineHelp"), QStringLiteral("actionReloadExternalStylesheet"),
        QStringLiteral("actionShowRenderTimings"), QStringLiteral("actionExportFontList"),
        QStringLiteral("actionShowHardwareDecodingIndicator"), QStringLiteral("actionPrintAnalytics"),
        QStringLiteral("actionEnableEffectPresetCreation")};
    for (const QString& name : trackedActions) {
        if (QAction* action = findChild<QAction*>(name)) {
            action->setProperty("track-action", true);
            if (trackedEvents.contains(name)) {
                action->setProperty("tracked-action-event-name", trackedEvents.value(name));
            }
            connect(action, &QAction::triggered, this, [action, name] {
                if (QSettings().value(QStringLiteral("Debug/PrintAnalytics"), false).toBool())
                    OV_LOG_INFO(QStringLiteral("UI event: %1 (%2)").arg(name,
                        action->property("tracked-action-event-name").toString()));
            });
        }
    }
    m_ui->menuOpenRecent->setProperty("track-menu", true);
    m_ui->menuOpenRecent->setProperty("tracked-menu-name", QStringLiteral("open recent"));
    m_ui->menuOpenRecent->setProperty("tracked-menu-parent-name", QStringLiteral("file"));
    m_ui->menuEffects->setProperty("track-menu", true);
    m_ui->menuEffects->setProperty("tracked-menu-name", QStringLiteral("effects"));

    // Window menu -- toggle dock panels + viewer workspaces/layouts/zoom
    // (reference menuWindow + WorkspacesMenu) + full screen. It belongs between
    // Effects and Export, so it is inserted ahead of the form's Export menu.
    auto* windowMenu = new QMenu(tr("&Window"), this);
    windowMenu->setObjectName(QStringLiteral("menuWindow"));
    windowMenu->setProperty("track-menu", true);
    windowMenu->setProperty("tracked-menu-name", QStringLiteral("window"));
    menuBar()->insertMenu(m_ui->menuExport->menuAction(), windowMenu);

    QAction* layoutSingle = windowMenu->addAction(tr("1-View Layout"));
    layoutSingle->setCheckable(true);
    layoutSingle->setChecked(true);
    connect(layoutSingle, &QAction::triggered, this,
            [this] { m_viewer->setLayout(ViewerWidget::ViewerLayout::Single); });
    QAction* layoutRow = windowMenu->addAction(tr("Row (2-View) Layout"));
    layoutRow->setCheckable(true);
    connect(layoutRow, &QAction::triggered, this,
            [this] { m_viewer->setLayout(ViewerWidget::ViewerLayout::Row); });
    QAction* layoutTwoOverOne = windowMenu->addAction(tr("2+1 Layout"));
    layoutTwoOverOne->setCheckable(true);
    connect(layoutTwoOverOne, &QAction::triggered, this,
            [this] { m_viewer->setLayout(ViewerWidget::ViewerLayout::TwoOverOne); });
    QAction* layoutFour = windowMenu->addAction(tr("Four-View Layout"));
    layoutFour->setCheckable(true);
    connect(layoutFour, &QAction::triggered, this,
            [this] { m_viewer->setLayout(ViewerWidget::ViewerLayout::Four); });
    QAction* layoutRowFour = windowMenu->addAction(tr("Four-In-A-Row Layout"));
    layoutRowFour->setCheckable(true);
    connect(layoutRowFour, &QAction::triggered, this,
            [this] { m_viewer->setLayout(ViewerWidget::ViewerLayout::RowFour); });
    connect(m_viewer, &ViewerWidget::layoutChanged, this,
            [this, layoutSingle, layoutRow, layoutTwoOverOne, layoutFour, layoutRowFour](
                ViewerWidget::ViewerLayout layout) {
                layoutSingle->setChecked(layout == ViewerWidget::ViewerLayout::Single);
                layoutRow->setChecked(layout == ViewerWidget::ViewerLayout::Row);
                layoutTwoOverOne->setChecked(layout == ViewerWidget::ViewerLayout::TwoOverOne);
                layoutFour->setChecked(layout == ViewerWidget::ViewerLayout::Four);
                layoutRowFour->setChecked(layout == ViewerWidget::ViewerLayout::RowFour);
            });

    windowMenu->addSeparator();

    // Reference Window > Workspaces (WorkspacesMenu, FUN_14039e2e0): the list
    // of saved workspaces and the three commands. They used to sit on the
    // Layout panel, which is not where the reference keeps them - that panel
    // holds the transform and alignment widgets and nothing else.
    m_workspacesMenu = windowMenu->addMenu(tr("Workspaces"));
    m_workspacesMenu->setObjectName(QStringLiteral("WorkspacesMenu"));
    connect(m_workspacesMenu, &QMenu::aboutToShow, this, &MainWindow::loadWorkspaces);

    QAction* zoomFitAction = windowMenu->addAction(tr("Zoom to Fit"));
    zoomFitAction->setShortcut(QKeySequence(Qt::Key_QuoteLeft)); // id 40
    connect(zoomFitAction, &QAction::triggered, this, [this] { m_viewer->zoomToFit(); });
    QAction* zoomInAction = windowMenu->addAction(tr("Zoom In"));
    zoomInAction->setShortcut(QKeySequence(Qt::Key_Equal)); // id 41
    connect(zoomInAction, &QAction::triggered, this, [this] { m_viewer->zoomIn(); });
    QAction* zoomOutAction = windowMenu->addAction(tr("Zoom Out"));
    zoomOutAction->setShortcut(QKeySequence(Qt::Key_Minus)); // id 42
    connect(zoomOutAction, &QAction::triggered, this, [this] { m_viewer->zoomOut(); });

    QAction* checkerboard = windowMenu->addAction(tr("Checkerboard Background"));
    checkerboard->setCheckable(true);
    checkerboard->setChecked(m_viewer->checkerboardEnabled());
    connect(checkerboard, &QAction::toggled, this, [this](bool on) {
        m_viewer->setCheckerboardEnabled(on);
    });
    connect(m_viewer, &ViewerWidget::checkerboardChanged, checkerboard, &QAction::setChecked);

    windowMenu->addSeparator();
    windowMenu->addAction(m_mediaPanel->toggleViewAction());
    windowMenu->addAction(m_textPanel->toggleViewAction());
    windowMenu->addAction(m_effectsPanel->toggleViewAction());
    windowMenu->addAction(m_controlsPanel->toggleViewAction());
    windowMenu->addAction(m_libraryPanel->toggleViewAction());
    windowMenu->addAction(m_historyPanel->toggleViewAction());
    windowMenu->addAction(m_metersPanel->toggleViewAction());
    windowMenu->addAction(m_timeline->toggleViewAction());
    windowMenu->addAction(m_viewerDock->toggleViewAction());
    QAction* showTrimmer = windowMenu->addAction(tr("Trimmer"));
    connect(showTrimmer, &QAction::triggered, this,
            [this] { showCenterTab(m_trimmerPanel); });
    QAction* showExport = windowMenu->addAction(tr("Export Panel"));
    connect(showExport, &QAction::triggered, this,
            [this] { showCenterTab(m_exportPanel); });
    windowMenu->addAction(m_trackPanel->toggleViewAction());
    windowMenu->addAction(m_layerPanel->toggleViewAction());
    windowMenu->addAction(m_viewer360->toggleViewAction());
    windowMenu->addAction(m_layoutPanel->toggleViewAction());
    QAction* showStart = windowMenu->addAction(tr("Start"));
    connect(showStart, &QAction::triggered, this, [this] {
        m_timeline->show();
        m_timeline->raise();
        m_timeline->showStartPage();
    });
    // Reference command "Toggle Learn Sidebar" (0x1412d0908).
    QAction* learnAction = m_learnSidebar->toggleViewAction();
    learnAction->setText(tr("Toggle Learn Sidebar"));
    windowMenu->addAction(learnAction);
    connect(m_learnSidebar, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (m_owner && m_owner->settings()) {
            m_owner->settings()->setLearnSidebarIsOpen(visible);
        }
    });
    windowMenu->addSeparator();
    QAction* openInTrimmer = windowMenu->addAction(tr("Open in Trimmer"));
    openInTrimmer->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_T)); // reference: Ctrl+T
    connect(openInTrimmer, &QAction::triggered, this, &MainWindow::onOpenInTrimmer);
    // Reference command 5026. It shows the frame alone on a screen of its own
    // (biff::ui::common::FullScreenPreviewWidget), which is not the same thing
    // as full-screening the editor - what this entry used to do. The viewer
    // owns the action; the menu borrows it, so Ctrl+Shift+F has one owner.
    if (m_viewer && m_viewer->fullScreenPreviewAction()) {
        windowMenu->addAction(m_viewer->fullScreenPreviewAction());
    }

    addTransportShortcuts();
}

void MainWindow::addTransportShortcuts()
{
    // Reference command set (see RE_VegasEffects.md / shortcuts XML):
    // transport, frame steps, 10-frame jumps, in/out, shuttle, zoom.
    auto* play = new QAction(this);
    play->setObjectName(QStringLiteral("transportPlay"));
    play->setShortcut(QKeySequence(Qt::Key_Space)); // id 30
    connect(play, &QAction::triggered, this, &MainWindow::onPlayPause);
    addAction(play);

    auto* loop = new QAction(this);
    loop->setObjectName(QStringLiteral("transportLoop"));
    loop->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_L)); // id 31
    loop->setCheckable(true);
    m_loopPlayback = loop;
    connect(loop, &QAction::toggled, this, [this](bool on) {
        // The viewer button and this shortcut are two faces of one toggle.
        if (m_transportBar) {
            m_transportBar->setLooping(on);
        }
        statusBar()->showMessage(on ? tr("Loop playback: on") : tr("Loop playback: off"));
    });
    addAction(loop);

    auto* home = new QAction(this);
    home->setObjectName(QStringLiteral("transportStart"));
    home->setShortcut(QKeySequence(Qt::Key_Home)); // id 26
    connect(home, &QAction::triggered, this, &MainWindow::onStop);
    addAction(home);

    auto* end = new QAction(this);
    end->setObjectName(QStringLiteral("transportEnd"));
    end->setShortcut(QKeySequence(Qt::Key_End)); // id 27
    connect(end, &QAction::triggered, this, [this] {
        if (m_composition) {
            m_playbackTime = m_composition->durationSeconds();
            m_timeline->setPlayheadPosition(m_playbackTime);
            if (m_controlsPanel) {
                m_controlsPanel->setCurrentTime(m_playbackTime);
            }
            requestRenderFrame();
        }
    });
    addAction(end);

    auto* inPoint = new QAction(this);
    inPoint->setObjectName(QStringLiteral("transportSetIn"));
    inPoint->setShortcut(QKeySequence(Qt::Key_I)); // id 20
    connect(inPoint, &QAction::triggered, this, [this] {
        if (m_trimmerPanel && m_trimmerPanel->isVisible()
            && !m_trimmerPanel->visibleRegion().isEmpty()) {
            m_trimmerPanel->setTrimInPoint();
        } else {
            setInPoint();
        }
    });
    addAction(inPoint);

    auto* outPoint = new QAction(this);
    outPoint->setObjectName(QStringLiteral("transportSetOut"));
    outPoint->setShortcut(QKeySequence(Qt::Key_O)); // id 21
    connect(outPoint, &QAction::triggered, this, [this] {
        if (m_trimmerPanel && m_trimmerPanel->isVisible()
            && !m_trimmerPanel->visibleRegion().isEmpty()) {
            m_trimmerPanel->setTrimOutPoint();
        } else {
            setOutPoint();
        }
    });
    addAction(outPoint);

    auto* gotoIn = new QAction(this);
    gotoIn->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_I)); // id 28
    connect(gotoIn, &QAction::triggered, this, [this] {
        if (m_inPoint >= 0.0) {
            m_playbackTime = m_inPoint;
            m_timeline->setPlayheadPosition(m_playbackTime);
            if (m_controlsPanel) {
                m_controlsPanel->setCurrentTime(m_playbackTime);
            }
            requestRenderFrame();
        }
    });
    addAction(gotoIn);

    auto* gotoOut = new QAction(this);
    gotoOut->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_O)); // id 29
    connect(gotoOut, &QAction::triggered, this, [this] {
        if (m_outPoint >= 0.0) {
            m_playbackTime = m_outPoint;
            m_timeline->setPlayheadPosition(m_playbackTime);
            if (m_controlsPanel) {
                m_controlsPanel->setCurrentTime(m_playbackTime);
            }
            requestRenderFrame();
        }
    });
    addAction(gotoOut);

    auto* prevFrame = new QAction(this);
    prevFrame->setObjectName(QStringLiteral("transportPreviousFrame"));
    prevFrame->setShortcut(QKeySequence(Qt::Key_Comma)); // id 22
    connect(prevFrame, &QAction::triggered, this, &MainWindow::stepFrameBackward);
    addAction(prevFrame);
    auto* prevFrameCtl = new QAction(this);
    prevFrameCtl->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Left));
    connect(prevFrameCtl, &QAction::triggered, this, &MainWindow::stepFrameBackward);
    addAction(prevFrameCtl);

    auto* nextFrame = new QAction(this);
    nextFrame->setObjectName(QStringLiteral("transportNextFrame"));
    nextFrame->setShortcut(QKeySequence(Qt::Key_Period)); // id 23
    connect(nextFrame, &QAction::triggered, this, &MainWindow::stepFrameForward);
    addAction(nextFrame);
    auto* nextFrameCtl = new QAction(this);
    nextFrameCtl->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Right));
    connect(nextFrameCtl, &QAction::triggered, this, &MainWindow::stepFrameForward);
    addAction(nextFrameCtl);

    auto* back10 = new QAction(this);
    back10->setObjectName(QStringLiteral("transportJumpBack10"));
    back10->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Comma)); // id 24
    connect(back10, &QAction::triggered, this, [this] { stepFrames(-10); });
    addAction(back10);
    auto* back10Ctl = new QAction(this);
    back10Ctl->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Left));
    connect(back10Ctl, &QAction::triggered, this, [this] { stepFrames(-10); });
    addAction(back10Ctl);
    auto* fwd10 = new QAction(this);
    fwd10->setObjectName(QStringLiteral("transportJumpForward10"));
    fwd10->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Period)); // id 25
    connect(fwd10, &QAction::triggered, this, [this] { stepFrames(10); });
    addAction(fwd10);
    auto* fwd10Ctl = new QAction(this);
    fwd10Ctl->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Right));
    connect(fwd10Ctl, &QAction::triggered, this, [this] { stepFrames(10); });
    addAction(fwd10Ctl);

    // Zoom in / out / fit are bound once, on the Window menu actions in
    // configureMenus(). They used to be duplicated here as bare window actions
    // with the same keys, and two actions sharing a shortcut on one widget make
    // it ambiguous - Qt then fires neither, so zooming by keyboard did nothing.

    // New layer commands, with the reference's own defaults (Ctrl+Alt+N opens
    // the menu; each kind has its own key).
    struct LayerShortcut
    {
        composition::LayerKind kind;
        int key;
    };
    const LayerShortcut layerKeys[] = {
        {composition::LayerKind::Plane,  Qt::Key_A},
        {composition::LayerKind::Camera, Qt::Key_C},
        {composition::LayerKind::Light,  Qt::Key_L},
        {composition::LayerKind::Grade,  Qt::Key_G},
        {composition::LayerKind::Text,   Qt::Key_T},
        {composition::LayerKind::Point,  Qt::Key_P},
    };
    for (const LayerShortcut& def : layerKeys) {
        auto* action = new QAction(this);
        action->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | def.key));
        connect(action, &QAction::triggered, this,
                [this, kind = def.kind] { onAddNewLayer(kind); });
        addAction(action);
    }
    // Keyframe navigation, with the reference's defaults. The timeline already
    // had goToPreviousKeyFrame / goToNextKeyFrame / toggleKeyFrameAtPlayhead;
    // none of them was reachable from the keyboard.
    auto* prevKey = new QAction(this);
    prevKey->setObjectName(QStringLiteral("timelinePreviousKey"));
    prevKey->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Comma));
    connect(prevKey, &QAction::triggered, this, [this] {
        if (m_timeline) {
            m_timeline->goToPreviousKeyFrame();
        }
    });
    addAction(prevKey);
    auto* nextKey = new QAction(this);
    nextKey->setObjectName(QStringLiteral("timelineNextKey"));
    nextKey->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Period));
    connect(nextKey, &QAction::triggered, this, [this] {
        if (m_timeline) {
            m_timeline->goToNextKeyFrame();
        }
    });
    addAction(nextKey);

    auto* openLayerMenu = new QAction(this);
    openLayerMenu->setObjectName(QStringLiteral("timelineNewLayerMenu"));
    openLayerMenu->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_N));
    connect(openLayerMenu, &QAction::triggered, this, [this] {
        if (m_timeline) {
            m_timeline->openNewLayerMenu();
        }
    });
    addAction(openLayerMenu);

    auto* jKey = new QAction(this);
    jKey->setObjectName(QStringLiteral("transportShuttleBackward"));
    jKey->setShortcut(QKeySequence(Qt::Key_J)); // id 32
    connect(jKey, &QAction::triggered, this, [this] { shuttle(-2.0); });
    addAction(jKey);
    auto* kKey = new QAction(this);
    kKey->setObjectName(QStringLiteral("transportShuttleStop"));
    kKey->setShortcut(QKeySequence(Qt::Key_K)); // id 34
    connect(kKey, &QAction::triggered, this, [this] { shuttle(0.0); });
    addAction(kKey);
    auto* lKey = new QAction(this);
    lKey->setObjectName(QStringLiteral("transportShuttleForward"));
    lKey->setShortcut(QKeySequence(Qt::Key_L)); // id 33
    connect(lKey, &QAction::triggered, this, [this] { shuttle(2.0); });
    addAction(lKey);

    OptionsDialog::applyShortcuts(this);
}

// Toolbar buttons show an icon, like the reference toolbar, and keep the label
// as the tooltip. If an icon fails to load - Qt's SVG image plugin missing, say
// - the action falls back to its text so the button never ends up blank.
QAction* MainWindow::addToolBarAction(QToolBar* bar, const QString& iconName,
                                      const QString& label)
{
    const QIcon icon(QStringLiteral(":/icons/%1.svg").arg(iconName));
    // isNull() is false as soon as the resource exists, even when nothing can
    // rasterise it, so test that a pixmap actually comes out.
    const bool usable = !icon.isNull()
                        && !icon.pixmap(QSize(20, 20)).isNull();
    QAction* action = usable ? bar->addAction(icon, QString()) : bar->addAction(label);
    action->setToolTip(label);
    action->setObjectName(QStringLiteral("toolButton") + iconName);
    return action;
}

void MainWindow::buildTransportToolbar()
{
    // The reference puts transport on the Viewer itself (see the Viewer panel:
    // scrubber row, then go-to-start / step / play beneath the frame), and
    // keeps the window toolbar for file and edit commands. This bar therefore
    // carries the same file/edit quick actions as screenshot 1.png.
    QToolBar* main = addToolBar(QStringLiteral("Main"));
    main->setObjectName(QStringLiteral("widgetQuickActions"));
    main->setMovable(false);
    main->setIconSize(QSize(16, 16));
    main->setFloatable(false);
    main->setFixedHeight(28);
    main->setStyleSheet(QStringLiteral(
        "QToolBar { spacing: 3px; padding: 0px; }"
        "QToolBar QToolButton { padding: 2px; min-width: 18px; min-height: 18px; }"));

    // One-time correction of the old hidden-by-default toolbar. Subsequent
    // changes in Options continue to control visibility.
    QSettings preferences;
    if (preferences.value(QStringLiteral("Options/QuickActionsLayoutVersion"), 0).toInt() < 1) {
        preferences.setValue(QStringLiteral("Options/ShowMenuBarQuickActions"), true);
        preferences.setValue(QStringLiteral("Options/QuickActionsLayoutVersion"), 1);
    }
    main->setToolButtonStyle(Qt::ToolButtonIconOnly);

    auto* logo = new QToolButton(main);
    logo->setObjectName(QStringLiteral("toolButtonLogo"));
    logo->setIcon(QIcon(QStringLiteral(":/icons/logo.png")));
    logo->setToolTip(tr("Start"));
    logo->setAutoRaise(true);
    connect(logo, &QToolButton::clicked, this, [this] {
        m_timeline->show();
        m_timeline->raise();
        m_timeline->showStartPage();
    });
    main->addWidget(logo);

    m_ui->actionOpen->setIcon(QIcon(QStringLiteral(":/icons/open.svg")));
    m_ui->actionSave->setIcon(QIcon(QStringLiteral(":/icons/save.svg")));
    m_ui->actionOptions->setIcon(QIcon(QStringLiteral(":/icons/settings.svg")));
    m_ui->actionCut->setIcon(QIcon(QStringLiteral(":/icons/cut.svg")));
    m_ui->actionCopy->setIcon(QIcon(QStringLiteral(":/icons/copy.svg")));
    m_ui->actionPaste->setIcon(QIcon(QStringLiteral(":/icons/paste.svg")));
    const QList<QPair<QAction*, QString>> quickActions{
        {m_ui->actionOpen, QStringLiteral("toolButtonOpen")},
        {m_ui->actionSave, QStringLiteral("toolButtonSave")},
        {m_ui->actionOptions, QStringLiteral("toolButtonOptions")},
        {m_ui->actionCut, QStringLiteral("toolButtonCut")},
        {m_ui->actionCopy, QStringLiteral("toolButtonCopy")},
        {m_ui->actionPaste, QStringLiteral("toolButtonPaste")},
        {m_ui->actionUndo, QStringLiteral("toolButtonUndo")},
        {m_ui->actionRedo, QStringLiteral("toolButtonRedo")}};
    for (const auto& item : quickActions) main->addAction(item.first);
    for (const auto& item : quickActions) {
        if (QWidget* button = main->widgetForAction(item.first)) button->setObjectName(item.second);
    }
    if (QWidget* undoWidget = main->widgetForAction(m_ui->actionUndo)) {
        undoWidget->setToolTip(tr("Undo"));
    }
    if (QWidget* redoWidget = main->widgetForAction(m_ui->actionRedo)) {
        redoWidget->setToolTip(tr("Redo"));
    }
    m_ui->actionUndo->setIcon(QIcon(QStringLiteral(":/icons/undo.svg")));
    m_ui->actionRedo->setIcon(QIcon(QStringLiteral(":/icons/redo.svg")));
}

// The play/pause glyph lives on the viewer's transport bar.
void MainWindow::updatePlayIcon()
{
    if (m_transportBar) {
        m_transportBar->setPlaying(m_playing);
    }
    if (m_viewer) {
        // The viewer's quality button shows the resolution that is actually in
        // force, and which of its two that is depends on the transport.
        m_viewer->setPlaying(m_playing);
    }
}

void MainWindow::wireSignals()
{
    if (m_learnSidebar) {
        connect(m_learnSidebar, &LearnSidebar::tutorialsRequested, this, [this] {
            m_ui->actionOnlineHelp->trigger();
        });
        connect(m_learnSidebar, &LearnSidebar::commandRequested,
                this, [this](const QString& name) {
            if (name == QLatin1String("Create New Composite Shot")) {
                onNewProject();
            } else if (name == QLatin1String("Import Media")) {
                onImportMedia();
            } else {
                statusBar()->showMessage(tr("Unknown command: %1").arg(name));
            }
        });
    }

    if (m_layoutPanel) {
        // The Layout panel edits the selected layer's box, mirrors it and
        // turns it by quarter turns - the reference's TransformWidget and
        // AlignmentWidget.
        connect(m_layoutPanel, &LayoutPanel::boundsEdited, this, &MainWindow::onLayoutBoundsEdited);
        connect(m_layoutPanel, &LayoutPanel::selectionBoundsEdited,
                this, &MainWindow::onLayoutSelectionBoundsEdited);
        connect(m_layoutPanel, &LayoutPanel::mirrorRequested, this, &MainWindow::onLayoutMirror);
        connect(m_layoutPanel, &LayoutPanel::rotateRequested, this, &MainWindow::onLayoutRotate);
        loadWorkspaces();
    }

    if (m_startPanel) {
        connect(m_startPanel, &StartPanel::newCompositeShotRequested,
                this, &MainWindow::onNewProject);
        connect(m_startPanel, &StartPanel::newCompositeShotsFromMediaRequested,
                this, &MainWindow::onImportMedia);
        connect(m_startPanel, &StartPanel::importFileRequested,
                this, &MainWindow::onImportMedia);
        connect(m_startPanel, &StartPanel::newProjectRequested,
                this, &MainWindow::onNewProject);
        connect(m_startPanel, &StartPanel::openProjectRequested,
                this, &MainWindow::onOpenProject);
        connect(m_startPanel, &StartPanel::editScreenRequested, this, [this] {
            if (m_timeline) {
                m_timeline->show();
                m_timeline->raise();
                m_timeline->showTimelinePage();
            }
        });
        connect(m_startPanel, &StartPanel::recentProjectActivated,
                this, &MainWindow::onRecentProjectActivated);
        connect(m_startPanel, &StartPanel::clearRecentsRequested, this, [this] {
            if (m_owner && m_owner->settings()) {
                m_owner->settings()->setRecentProjects(QStringList());
            }
        });
        connect(m_startPanel, &StartPanel::tutorialsRequested,
                this, &MainWindow::showLearnSidebar);
    }

    if (m_transportBar) {
        connect(m_transportBar, &ViewerTransportBar::snapshotRequested,
                this, &MainWindow::exportSnapshot);
        connect(m_transportBar, &ViewerTransportBar::metersToggled, this, [this](bool visible) {
            m_metersPanel->setVisible(visible);
            if (visible) m_metersPanel->raise();
        });
        connect(m_transportBar, &ViewerTransportBar::loopToggled, this, [this](bool on) {
            if (m_loopPlayback && m_loopPlayback->isChecked() != on) {
                m_loopPlayback->setChecked(on);   // drives the shared handler
            }
        });
        connect(m_transportBar, &ViewerTransportBar::playPauseRequested, this, &MainWindow::onPlayPause);
        connect(m_transportBar, &ViewerTransportBar::stopRequested, this, &MainWindow::onStop);
        connect(m_transportBar, &ViewerTransportBar::stepBackRequested, this, &MainWindow::stepFrameBackward);
        connect(m_transportBar, &ViewerTransportBar::stepForwardRequested, this, &MainWindow::stepFrameForward);
        connect(m_transportBar, &ViewerTransportBar::setInPointRequested, this, &MainWindow::setInPoint);
        connect(m_transportBar, &ViewerTransportBar::setOutPointRequested, this, &MainWindow::setOutPoint);
        connect(m_transportBar, &ViewerTransportBar::scrubRequested, this,
                [this](double t) {
                    m_playbackTime = t;
                    if (m_playing && m_audio) m_audio->seek(t);
                    m_timeline->setPlayheadPosition(t);
                    if (m_controlsPanel) {
                        m_controlsPanel->setCurrentTime(t);
                    }
                    if (m_viewer) {
                        m_viewer->setTimecode(t);
                    }
                    requestRenderFrame();
                });
        connect(m_transportBar, &ViewerTransportBar::durationChangeRequested,
                m_timeline, &TimelineWidget::setCompositionDuration);
    }

    connect(m_effectsPanel, &EffectsPanel::effectSelected, this, &MainWindow::onEffectSelected);
    connect(m_effectsPanel, &EffectsPanel::effectActivated, this, &MainWindow::onEffectActivated);

    connect(m_timeline, &TimelineWidget::motionTrackingRequested,
            this, &MainWindow::onMotionTrackingRequested);
    connect(m_controlsPanel, &EffectInspector::motionTrackingRequested,
            this, &MainWindow::onMotionTrackingRequested);
    connect(m_controlsPanel, &EffectInspector::effectParameterEdited, this,
            [this](int layerIndex, int clipIndex, int effectIndex, int parameterIndex) {
        if (!m_instanceHost || !m_composition || layerIndex < 0
            || layerIndex >= m_composition->layers().size()) {
            return;
        }
        m_instanceHost->propertyChanged({m_composition->layers().at(layerIndex).id, clipIndex,
                                         effectIndex},
                                        parameterIndex, m_playbackTime);
    });
    m_timeline->setUndoStack(m_undoStack);
    m_controlsPanel->setUndoStack(m_undoStack);
    connect(m_timeline, &TimelineWidget::preRenderRequested, this, [this] {
        if (!m_renderManager || !m_composition) return;
        if (m_renderManager->isCaching()) { m_renderManager->cancelPlaybackCache(); return; }
        const double scale = m_viewer->renderScale(true);
        const QSize size(qMax(1, qRound(m_composition->width() * scale)), qMax(1, qRound(m_composition->height() * scale)));
        m_renderManager->startPlaybackCache(m_playbackTime, size, m_viewer->renderEffects(true));
    });
    connect(m_timeline, &TimelineWidget::compositionPropertiesChanged, this, [this] {
        if (!m_composition) return;
        if (m_renderManager) m_renderManager->cancelPlaybackCache();
        m_playbackTime = m_timeline->playhead();
        m_viewer->setProjectSize(m_composition->displaySize());
        m_viewer->setFrameRate(m_composition->fpsNumerator(), m_composition->fpsDenominator());
        m_trimmerPanel->setFrameRate(
            m_composition->fpsDenominator() > 0
                ? double(m_composition->fpsNumerator()) / m_composition->fpsDenominator() : 30.0);
        if (m_transportBar) {
            m_transportBar->setFrameRate(m_composition->fpsNumerator(), m_composition->fpsDenominator());
            m_transportBar->setDuration(m_composition->durationSeconds());
            m_transportBar->setTimecode(m_playbackTime);
        }
        m_projectModified = true; updateWindowTitle();
        m_controlsPanel->setCurrentTime(m_playbackTime);
        rebuildCompositionTabs();
        refreshCompositeShotList();
        requestRenderFrame();
    });
    connect(m_timeline, &TimelineWidget::exportRequested, this,
            [this] { showCenterTab(m_exportPanel); });
    connect(m_timeline, &TimelineWidget::valueGraphToggled, this, [this](bool on) {
        statusBar()->showMessage(on ? tr("Value Graph shown") : tr("Value Graph hidden"));
    });
    connect(m_timeline, &TimelineWidget::keyFramesChanged, this, [this] {
        m_projectModified = true;
        updateWindowTitle();
        m_controlsPanel->refresh();
        updateTextPanelSelection();
        updateLayoutPanelSelection();
        m_timeline->update();
        requestRenderFrame();
    });
    connect(m_timeline, &TimelineWidget::timeScrubbed, this, [this](double t) {
        m_playbackTime = t;
        m_controlsPanel->setCurrentTime(t);
        if (m_playing && m_audio) m_audio->seek(t);
        if (!m_playing) {
            requestRenderFrame();
            if (m_audio && app::Settings::optionSettings().value(QStringLiteral("Options/PlayAudioOnScrub"), true).toBool()) {
                const double clipStart = prepareAudioSource();
                if (clipStart >= 0.0) {
                    m_audio->play(t - clipStart);
                    m_audioScrubTimer.start(100);
                }
            }
        }
    });
    connect(m_timeline, &TimelineWidget::clipSelected, this, [this](int layerIndex, int clipIndex) {
        m_selectedLayer = layerIndex;
        m_selectedClip = clipIndex;
        const int inspectorClip = clipIndex < 0 && m_composition && layerIndex >= 0
            && layerIndex < m_composition->layers().size() && m_composition->layers()[layerIndex].clips.size() == 1 ? 0 : clipIndex;
        m_controlsPanel->setSelection(layerIndex, inspectorClip);
        m_trackPanel->setSelectedLayer(layerIndex);
        m_layerPanel->setSelection(layerIndex);
        // The motion path and the Layout panel both follow the selection, so
        // they are rebuilt here and not only when a frame is rendered.
        updateMotionPathOverlay();
        updateLayoutPanelSelection();
        updateTextPanelSelection();
    });
    connect(m_timeline, &TimelineWidget::layersSelected, this,
            [this](const QVector<int>& layers) {
        m_selectedLayers = layers;
        updateLayoutPanelSelection();
    });
    connect(m_timeline, &TimelineWidget::newLayerRequested, this, &MainWindow::onAddNewLayer);
    connect(m_timeline, &TimelineWidget::makeCompositeShotRequested,
            this, &MainWindow::makeCompositeShotFromSelection);
    connect(m_timeline, &TimelineWidget::shotPreRenderRequested,
            this, &MainWindow::handlePreRenderRequest);
    connect(m_timeline, &TimelineWidget::mediaDropped, this, &MainWindow::addDroppedMediaLayer);
    connect(m_timeline, &TimelineWidget::compositeShotDropped, this, &MainWindow::addDroppedShotLayer);
    connect(m_timeline, &TimelineWidget::compositionTabActivated, this, [this](int index) {
        if (index >= 0 && index < m_openCompositions.size())
            activateComposition(m_openCompositions.at(index));
    });
    connect(m_timeline, &TimelineWidget::compositionTabCloseRequested, this, [this](int index) {
        // The last tab stays bound - the panels always show a shot - and the
        // timeline goes back to its start page instead.
        if (m_openCompositions.size() <= 1) {
            m_timeline->showStartPage();
            return;
        }
        if (index < 0 || index >= m_openCompositions.size()) return;
        const bool closingActive = m_openCompositions.at(index) == m_composition;
        m_openCompositions.removeAt(index);
        if (closingActive) activateComposition(m_openCompositions.at(qMax(0, index - 1)));
        else rebuildCompositionTabs();
    });
    // The frame round the selected text: the text shown at the playhead, read
    // fresh for every event so it follows selection, time and edits.
    m_textOverlay = std::make_unique<TextTransformOverlay>(
        [this] {
            return m_composition
                ? textTargetAt(*m_composition, m_selectedLayer, m_selectedClip, m_playbackTime, nullptr)
                : TextTransformOverlay::Target();
        },
        [this] {
            m_projectModified = true;
            updateWindowTitle();
            requestRenderFrame();
        },
        [this](const composition::LayerTransform& before, const composition::LayerTransform& after,
               const QString& title) {
            if (!m_composition || m_selectedLayer < 0
                || m_selectedLayer >= m_composition->layers().size()) {
                return;
            }
            m_undoStack->push(new LayerTransformCommand(
                m_composition, m_composition->layers().at(m_selectedLayer).id, before, after,
                [this] {
                    m_projectModified = true;
                    updateWindowTitle();
                    m_timeline->refreshKeyFrames();
                    m_controlsPanel->refresh();
                    requestRenderFrame();
                }, title));
        },
        [this](const QPointF& canvasPos) {
            // The topmost text under the pointer becomes the selection, as a
            // click on it in the timeline would make it.
            if (!m_composition) return false;
            for (int i = 0; i < m_composition->layers().size(); ++i) {
                int clip = -1;
                const auto target = textTargetAt(*m_composition, i, -1, m_playbackTime, &clip);
                if (!target.layer
                    || !TextTransformOverlay::quadFor(target).containsPoint(canvasPos,
                                                                            Qt::OddEvenFill)) {
                    continue;
                }
                if (i == m_selectedLayer) return false;
                m_timeline->selectLayer(i);
                m_selectedLayer = i;
                m_selectedClip = clip;
                m_selectedLayers = {i};
                m_layerPanel->setSelection(i);
                m_trackPanel->setSelectedLayer(i);
                m_controlsPanel->setSelection(i, clip);
                updateMotionPathOverlay();
                updateLayoutPanelSelection();
                updateTextPanelSelection();
                return true;
            }
            return false;
        });
    m_viewer->addOverlay(m_textOverlay.get());
    m_instanceHost = std::make_unique<NativeInstanceHost>(
        [this] { return m_composition; }, [this] { return m_mediaManager; });
    connect(m_instanceHost.get(), &NativeInstanceHost::effectChanged, this, [this] {
        // The module's status, its data or the layer's motion changed.
        m_projectModified = true;
        updateWindowTitle();
        if (m_controlsPanel) m_controlsPanel->refreshValues();
        if (m_viewer) m_viewer->update();
        requestRenderFrame();
    });
    // The selected clip's first effect with viewer custom UI (MotionTrack's
    // feature picker, BendGeometry's handles), on top of the text frame.
    m_customUiOverlay = std::make_unique<NativeCustomUiOverlay>([this] {
        NativeCustomUiOverlay::Target target;
        if (!m_composition || m_selectedLayer < 0
            || m_selectedLayer >= m_composition->layers().size()) {
            return target;
        }
        const composition::Layer& layer = m_composition->layers().at(m_selectedLayer);
        if (!layer.visible) return target;
        int shown = -1;
        for (int i = 0; i < layer.clips.size(); ++i) {
            const composition::Clip& clip = layer.clips.at(i);
            if (m_playbackTime >= clip.startSeconds
                && m_playbackTime < clip.startSeconds + clip.durationSeconds
                && (shown < 0 || i == m_selectedClip)) {
                shown = i;
            }
        }
        if (shown < 0) return target;
        const composition::Clip& clip = layer.clips.at(shown);
        for (int e = 0; e < clip.effects.size(); ++e) {
            const composition::Effect& fx = clip.effects.at(e);
            if (!fx.enabled || !plugin::nativeEffectHasCustomUi(fx.pluginId)) continue;
            // The instance's view: MotionTrack's is its footage, placed on
            // the canvas as that layer is.
            const NativeInstanceHost::Ref ref {layer.id, shown, e};
            if (!m_instanceHost->describe(ref, m_playbackTime, &target.effectId, &target.values,
                                          &target.view)) {
                continue;
            }
            m_instanceHost->prepare(ref);
            target.instanceKey = target.view.instanceKey;
            target.resultHandler = [host = m_instanceHost.get(), ref](
                                       const plugin::NativeCustomUiResult& result) {
                host->handleResult(ref, result);
            };
            break;
        }
        return target;
    });
    m_viewer->addOverlay(m_customUiOverlay.get());
    plugin::setNativeCustomUiRedrawHandler([viewer = QPointer<ViewerWidget>(m_viewer)] {
        if (viewer) viewer->update();
    });
    connect(m_viewer, &ViewerWidget::layerOrbited, this, &MainWindow::onLayerOrbited);
    connect(m_viewer, &ViewerWidget::layerMoved, this, &MainWindow::onLayerMoved);
    connect(m_viewer, &ViewerWidget::layerNudged, this, &MainWindow::onLayerNudged);
    connect(m_viewer, &ViewerWidget::textCreationRequested, this, &MainWindow::createTextAt);
    connect(m_viewer, &ViewerWidget::textEditRequested, this, &MainWindow::editSelectedText);
    connect(m_viewer, &ViewerWidget::maskCreationRequested, this,
            [this](int tool, const QRectF& bounds) {
        if (!m_composition || m_selectedLayer < 0
            || m_selectedLayer >= m_composition->layers().size()) return;
        auto shape = composition::MaskShape::Rectangle;
        switch (static_cast<ViewerWidget::ViewerTool>(tool)) {
        case ViewerWidget::ViewerTool::RoundedRect: shape = composition::MaskShape::RoundedRectangle; break;
        case ViewerWidget::ViewerTool::Ellipse: shape = composition::MaskShape::Ellipse; break;
        case ViewerWidget::ViewerTool::Polygon: shape = composition::MaskShape::Polygon; break;
        case ViewerWidget::ViewerTool::Star: shape = composition::MaskShape::Star; break;
        default: break;
        }
        m_timeline->addMaskToLayer(m_selectedLayer, shape, bounds);
        requestRenderFrame();
    });
    connect(m_viewer, &ViewerWidget::freehandMaskCreationRequested, this,
            [this](const QVector<QPointF>& points) {
        if (!m_composition || m_selectedLayer < 0
            || m_selectedLayer >= m_composition->layers().size()) return;
        m_timeline->addFreehandMaskToLayer(m_selectedLayer, points);
        requestRenderFrame();
    });
    connect(m_viewer, &ViewerWidget::textContextMenuRequested, this,
            [this](const QPoint& globalPosition) {
        composition::Effect* effect = selectedTextEffect();
        if (!effect) return;
        const composition::TextStyle style =
            composition::textStyleFromParameters(effect->parameterValues);
        QMenu menu(this);
        QAction* properties = menu.addAction(tr("Text Properties"));
        QAction* convert = menu.addAction(style.textMode == composition::TextStyle::TextMode::Point
            ? tr("Convert to Paragraph Text") : tr("Convert to Point Text"));
        QAction* chosen = menu.exec(globalPosition);
        if (chosen == properties) editSelectedText();
        else if (chosen == convert) setSelectedTextMode(
            style.textMode == composition::TextStyle::TextMode::Point
                ? composition::TextStyle::TextMode::Paragraph
                : composition::TextStyle::TextMode::Point);
    });
    // A new preview resolution or quality profile only shows once a frame has
    // been rendered under it.
    connect(m_viewer, &ViewerWidget::renderQualityChanged, this, &MainWindow::requestRenderFrame);
    connect(m_viewer, &ViewerWidget::showMotionPathChanged, this,
            [this](bool) { updateMotionPathOverlay(); });
    if (m_renderManager && m_transportBar) {
        connect(m_renderManager, &render::RenderManager::stateChanged, m_transportBar,
                [this](render::RenderState state) {
                    m_transportBar->setRendering(state == render::RenderState::Rendering);
                });
    }
    connect(m_timeline, &TimelineWidget::searchFilterChanged, this,
            [this](const QString& filter) {
                if (m_timeline && m_trackPanel) {
                    m_timeline->setProperty("filter", filter);
                    m_trackPanel->applyFilter(filter);
                }
            });

    // Real-time preview: editing effect parameters re-renders the current frame.
    connect(m_controlsPanel, &EffectInspector::effectParamsChanged, this, [this] {
        m_timeline->refreshKeyFrames();
        updateTextPanelSelection();
        requestRenderFrame();
    });
    connect(m_controlsPanel, &EffectInspector::keyFramesChanged, this, [this] {
        m_projectModified = true;
        updateWindowTitle();
        if (m_timeline) {
            m_timeline->refreshKeyFrames();
        }
    });

    // Drop media files onto the Project Media panel to import them.
    connect(m_mediaPanel, &MediaPanel::importRequested, this, &MainWindow::importFiles);
    connect(m_mediaPanel, &MediaPanel::importCommandRequested, this, &MainWindow::onImportMedia);
    connect(m_mediaPanel, &MediaPanel::mediaMetadataModified, this, [this] {
        m_projectModified = true;
        updateWindowTitle();
        // Rebuild audio with the new stream/interpretation settings as well.
        if (m_playing && m_audio) {
            prepareAudioSource();
            m_audio->play(m_playbackTime);
        }
        m_timeline->update();
        requestRenderFrame();
    });
    // Media > New > Composite Shot makes an empty shot of the project, as the
    // reference's does; Convert to Composite Shot (Ctrl+M) is the timeline's.
    connect(m_mediaPanel, &MediaPanel::newCompositeShotRequested,
            this, &MainWindow::newCompositeShot);
    connect(m_mediaPanel, &MediaPanel::compositeShotActivated, this,
            [this](const QString& id) { openCompositeShot(projectShot(id)); });
    connect(m_mediaPanel, &MediaPanel::compositeShotPropertiesRequested, this, [this](const QString& id) {
        const auto shot = projectShot(id);
        if (!shot) return;
        openCompositeShot(shot);
        m_timeline->editCompositionProperties();
    });
    connect(m_mediaPanel, &MediaPanel::compositeShotRemoveRequested,
            this, &MainWindow::removeCompositeShot);
    connect(m_mediaPanel, &MediaPanel::primaryCompositeShotRequested,
            this, &MainWindow::setPrimaryCompositeShot);
    connect(m_mediaPanel, &MediaPanel::compositeShotSaveRequested,
            this, &MainWindow::saveCompositeShotToFile);

    // Removing a media asset refreshes the timeline and preview.
    connect(m_mediaPanel, &MediaPanel::mediaRemoved, this, [this](const QString&) {
        m_projectModified = true;
        updateWindowTitle();
        m_timeline->update();
        requestRenderFrame();
    });

    connect(m_textPanel, &TextPanel::styleEdited, this, &MainWindow::onTextStyleEdited);

    connect(m_libraryPanel, &LibraryPanel::presetActivated, this, &MainWindow::applyPresetToSelection);

    // Trimmer: open the activated media asset, and Insert/Overlay clips.
    connect(m_mediaPanel, &MediaPanel::mediaActivated, this, [this](const QString& filePath) {
        if (m_mediaManager) {
            m_trimmerPanel->openAsset(m_mediaManager->assetByFilePath(filePath));
            showCenterTab(m_trimmerPanel);
        }
    });
    connect(m_trimmerPanel, &TrimmerPanel::insertRequested, this,
            [this](const QString& filePath, int inFrame, int outFrame) {
                addMediaClipToTimeline(filePath, inFrame, outFrame, true);
            });
    connect(m_trimmerPanel, &TrimmerPanel::overlayRequested, this,
            [this](const QString& filePath, int inFrame, int outFrame) {
                addMediaClipToTimeline(filePath, inFrame, outFrame, false);
            });
    connect(m_trimmerPanel, &TrimmerPanel::trimRangeChanged, this,
            [this](const QString&, int, int) {
                m_projectModified = true;
                updateWindowTitle();
            });

    // Export: render the current frame to the requested path.
    connect(m_exportPanel, &ExportPanel::exportFrameRequested, this,
            [this](const QString& path) { exportFrameToFile(path); });
    connect(m_exportPanel, &ExportPanel::exportContentsRequested,
            this, &MainWindow::exportContents);
    connect(m_exportPanel, &ExportPanel::addToQueueRequested,
            this, &MainWindow::queueContentsForExport);
    connect(m_exportQueue, &ExportQueue::runningChanged, this, [this](bool running) {
        configureExportQueue();
        updateProxyPreview();
        statusBar()->showMessage(running
            ? QCoreApplication::translate("biff::ui::exporter::ExportPanelWidget", "Starting...")
            : QCoreApplication::translate("biff::ui::exporter::ExportPanelWidget", "Inactive"), 3000);
    });
    connect(m_exportQueue, &ExportQueue::queueFinished, this, [] { notifyExportCompleted(); });
}

void MainWindow::bindModel(std::shared_ptr<composition::Composition> composition,
                           std::shared_ptr<media::MediaManager> mediaManager,
                           render::RenderManager* renderManager)
{
    m_composition = std::move(composition);
    m_rootComposition = m_composition;
    m_openCompositions = {m_rootComposition};
    m_mediaManager = std::move(mediaManager);
    m_renderManager = renderManager;
    applyTimelineCacheOptions(true);
    if (m_renderManager) {
        connect(m_renderManager, &render::RenderManager::playbackCacheProgress, this, [this](int n, int total) {
            statusBar()->showMessage(tr("Playback cache: %1 / %2 frames (256 MiB budget)").arg(n).arg(total));
        });
        connect(m_renderManager, &render::RenderManager::playbackCacheFinished, this, [this] {
            statusBar()->showMessage(tr("Playback cache ready: %1 frames").arg(m_renderManager->cachedFrameCount()), 5000);
        });
        connect(m_renderManager, &render::RenderManager::playbackCacheFailed, this, [this](const QString& reason) { statusBar()->showMessage(reason, 8000); });
    }
    m_timeline->setComposition(m_composition);
    m_timeline->setMediaManager(m_mediaManager);
    m_controlsPanel->setMediaManager(m_mediaManager);
    m_mediaPanel->setMediaManager(m_mediaManager.get());
    m_controlsPanel->setComposition(m_composition);
    m_trimmerPanel->setMediaManager(m_mediaManager.get());
    m_trackPanel->bindModel(m_composition);
    m_layerPanel->bindModel(m_composition);
    m_layerPanel->setUndoStack(m_undoStack);
    m_layerPanel->setMediaManager(m_mediaManager);
    connect(m_trackPanel, &TrackPanel::newLayerRequested, this, &MainWindow::onAddNewLayer);
    connect(m_trackPanel, &TrackPanel::layerSelected, this, [this](int layerIndex) {
        m_timeline->selectLayer(layerIndex);
        m_selectedLayer = layerIndex;
        m_selectedClip = -1;
        m_selectedLayers = {layerIndex};
        m_layerPanel->setSelection(layerIndex);
        m_controlsPanel->setSelection(layerIndex, -1);
        updateMotionPathOverlay();
        updateLayoutPanelSelection();
        updateTextPanelSelection();
    });
    connect(m_mediaPanel, &MediaPanel::proxyModeRequested, this,
            &MainWindow::setAssetProxyMode, Qt::UniqueConnection);
    connect(m_mediaPanel, &MediaPanel::relinkRequested, this,
            [this](const QString& oldPath, const QString& newPath) {
        const media::MediaAsset oldAsset = m_mediaManager->assetByFilePath(oldPath);
        if (!oldAsset.id().isValid() || m_mediaManager->importFile(newPath).isFailure()) return;
        const media::MediaAsset newAsset = m_mediaManager->assetByFilePath(newPath);
        if (newAsset.id() == oldAsset.id()) return;
        if (auto* relinked = m_mediaManager->assetByIdForEdit(newAsset.id())) {
            relinked->setLabelColor(oldAsset.labelColor());
            relinked->setTrimInPoint(oldAsset.trimInPoint());
            relinked->setTrimOutPoint(oldAsset.trimOutPoint());
            relinked->setAudioStreamIndex(oldAsset.audioStreamIndex());
        }
        for (int layerIndex = 0; layerIndex < m_composition->layers().size(); ++layerIndex) {
            composition::Layer& layer = m_composition->layerRef(layerIndex);
            for (composition::Clip& clip : layer.clips)
                if (clip.mediaId == oldAsset.id()) clip.mediaId = newAsset.id();
        }
        m_mediaManager->removeAsset(oldAsset.id());
        m_mediaPanel->refresh();
        m_timeline->refreshKeyFrames();
        if (m_playing) {
            m_audio->stop();
            beginPlayback();
        }
        m_projectModified = true;
        updateWindowTitle();
        requestRenderFrame();
    });
    connect(m_trackPanel, &TrackPanel::layersModified, this, [this] {
        m_projectModified = true;
        updateWindowTitle();
        m_timeline->refreshKeyFrames();
        m_layerPanel->refresh();
        requestRenderFrame();
    });
    connect(m_layerPanel, &LayerPanel::layersModified, this, [this] {
        m_projectModified = true;
        updateWindowTitle();
        m_trackPanel->refresh();
        m_timeline->refreshKeyFrames();
        m_controlsPanel->refresh();
        updateLayoutPanelSelection();
        requestRenderFrame();
    });
    if (m_composition) {
        const double trimmerFps = m_composition->fpsDenominator() > 0
            ? double(m_composition->fpsNumerator()) / m_composition->fpsDenominator() : 30.0;
        m_trimmerPanel->setFrameRate(trimmerFps);
        m_viewer->setProjectSize(m_composition->displaySize());
        m_viewer->setFrameRate(m_composition->fpsNumerator(), m_composition->fpsDenominator());
        if (m_transportBar) {
            m_transportBar->setFrameRate(m_composition->fpsNumerator(), m_composition->fpsDenominator());
            m_transportBar->setDuration(m_composition->durationSeconds());
            m_transportBar->setTimecode(m_playbackTime);
        }
    }
    updateWindowTitle();

    if (m_renderManager && m_composition) {
        connect(m_renderManager, &render::RenderManager::frameReady, this,
                [this](int frameIndex, QByteArray rgba, QSize size) {
                    Q_UNUSED(frameIndex);
                    if (size.isEmpty()
                        || rgba.size() < qsizetype(size.width()) * size.height() * 4) {
                        return;
                    }
                    // The size comes from the renderer, not from the
                    // composition: at 1/2 or 1/4 preview resolution the two are
                    // no longer the same.
                    QImage frame(reinterpret_cast<const uchar*>(rgba.constData()), size.width(),
                                 size.height(), size.width() * 4, QImage::Format_RGBA8888);
                    const QImage ownedFrame = frame.copy();
                    m_viewer->setFrame(ownedFrame);
                    // Hosting the Viewer, the 360 tab shows the frame through it.
                    if (m_viewer360->isVisible() && !m_viewer360->hostsViewer())
                        m_viewer360->setFrame(ownedFrame);
                });
    }
}

void MainWindow::regenerateEffects(plugin::PluginManager* pluginManager)
{
    if (!pluginManager) {
        return;
    }

    QVector<plugin::EffectSpec> specs;
    const QVector<plugin::PluginId> ids = pluginManager->allPluginIds();
    specs.reserve(ids.size());
    for (const plugin::PluginId& id : ids) {
        specs.push_back(pluginManager->spec(id));
    }
    m_effectsPanel->setEffects(specs);
    // The timeline's layer tree resolves effect parameter names through the
    // same registry.
    if (m_timeline) {
        m_timeline->setPluginManager(pluginManager);
    }
    m_controlsPanel->setPluginManager(pluginManager);
    m_controlsPanel->setMediaManager(m_mediaManager);
    refreshMediaAndInspector();
}

void MainWindow::onEffectSelected(const plugin::EffectSpec& spec)
{
    statusBar()->showMessage(tr("Selected effect: %1 (%2)")
                                 .arg(spec.displayName, spec.id.value()));
}

void MainWindow::onEffectActivated(const plugin::EffectSpec& spec)
{
    if (!m_composition) {
        return;
    }
    composition::Clip* clip = m_composition->clipAt(m_selectedLayer, m_selectedClip);
    if (!clip) {
        statusBar()->showMessage(tr("Select a clip on the timeline first"));
        return;
    }

    // Some native Geometry/Behavior modules can be read and listed before
    // their execution ABI is available. Adding one would leave a row that does
    // nothing, so make that explicit to the user.
    if (!spec.renderable) {
        const QString name = spec.displayName.isEmpty() ? spec.name : spec.displayName;
        const QMessageBox::StandardButton answer = QMessageBox::question(
            this, tr("Add %1?").arg(name),
            tr("%1\n\nIt can still be added to the clip and saved with the project, and it will "
               "come back when this build can run it. Add it now?")
                .arg(spec.unavailableReason),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            statusBar()->showMessage(tr("%1 was not added").arg(name));
            return;
        }
    }

    // Seeded with the spec's defaults; a transition goes on the clip edge
    // nearer the playhead. Through the timeline, so it can be undone.
    const int effectIndex = m_timeline
        ? m_timeline->addEffect(m_selectedLayer, m_selectedClip, spec, m_playbackTime)
        : ui::addEffectToClip(*clip, spec, m_playbackTime);
    if (effectIndex < 0) {
        statusBar()->showMessage(tr("The layer is locked"));
        return;
    }
    clip = m_composition->clipAt(m_selectedLayer, m_selectedClip);
    const composition::Effect effect = clip->effects.at(effectIndex);
    m_recentEffectIds.removeAll(spec.id.value());
    m_recentEffectIds.prepend(spec.id.value());
    while (m_recentEffectIds.size() > 10) m_recentEffectIds.removeLast();

    m_projectModified = true;
    updateWindowTitle();
    m_controlsPanel->setSelection(m_selectedLayer, m_selectedClip);
    m_controlsPanel->raise();
    if (m_timeline) {
        // Rebuild the lane tree so the new effect row appears, without the
        // playhead reset that refreshAfterModelChange() does.
        m_timeline->setComposition(m_composition);
        m_timeline->update();
    }
    requestRenderFrame();
    statusBar()->showMessage(tr("Applied %1").arg(effect.name));
}

void MainWindow::requestRenderFrame()
{
    if (!m_renderManager || !m_composition) {
        return;
    }
    // Tracked layers whose effects moved or whose data an undo put back get
    // their matrices again before the frame is drawn.
    if (m_instanceHost) m_instanceHost->sync();
    updateProxyPreview();
    m_renderManager->setPreRenderDirectory(preRenderRoot());
    m_viewer->setTimecode(m_playbackTime);
    m_layerPanel->setCurrentTime(m_playbackTime);
    double cameraFov = 39.6;
    for (const auto& layer : m_composition->layers()) {
        if (layer.kind == composition::LayerKind::Camera && layer.visible) {
            cameraFov = layer.cameraFieldOfView; break;
        }
    }
    m_viewer360->videoWidget()->setCameraFieldOfView(cameraFov);

    // Preview resolution and quality profile from the viewer's own quality
    // button (reference toolButtonPlaybackQuality), which keeps a separate
    // setting for playing and for paused - the point of the pair being that a
    // project can preview at 1/2 while it runs and at Full once it stops.
    const bool playing = m_playbackTimer.isActive();
    const double scale = m_viewer->renderScale(playing);
    const QSize renderSize(qMax(1, int(m_composition->width() * scale + 0.5)),
                           qMax(1, int(m_composition->height() * scale + 0.5)));
    const bool withEffects = m_viewer->renderEffects(playing);
    m_renderManager->requestFrame(m_renderedFrameIndex++, m_playbackTime, renderSize, withEffects);
    if (m_autoRenderCache && !playing) {
        m_autoRenderCacheTimer.start(); // restarted by every new paused frame
    }
    updateMotionPathOverlay();
    updateLayoutPanelSelection();
}

void MainWindow::onRenderRequested()
{
    statusBar()->showMessage(tr("Render requested"));
    OV_LOG_INFO(QStringLiteral("Render requested from timeline"));
    requestRenderFrame();
}

void MainWindow::onImportMedia()
{
    const QStringList files =
        QFileDialog::getOpenFileNames(this, tr("Import"), QString(), importFileFilter());
    if (files.isEmpty()) {
        return;
    }
    importFiles(files);
}

void MainWindow::onLayerOrbited(const QPointF& delta)
{
    if (!m_composition || m_selectedLayer < 0
        || m_selectedLayer >= m_composition->layers().size()) {
        return;
    }
    composition::Layer& layer = m_composition->layerRef(m_selectedLayer);
    if (layer.dimension != composition::LayerDimension::ThreeD) {
        statusBar()->showMessage(tr("Orbit works on a 3D layer"));
        return;
    }
    // Dragging right turns the layer about its Y axis and dragging down about
    // its X, at a quarter of a degree per pixel - a full turn in a drag across
    // a 1440-pixel viewer.
    const double perPixel = 0.25;
    layer.transform.rotationYDegrees += delta.x() * perPixel;
    layer.transform.rotationXDegrees += delta.y() * perPixel;
    m_projectModified = true;
    updateWindowTitle();
    m_timeline->refreshKeyFrames();
    m_controlsPanel->refresh();
    requestRenderFrame();
}

void MainWindow::onLayerMoved(const QPointF& delta)
{
    if (!m_composition || m_selectedLayer < 0
        || m_selectedLayer >= m_composition->layers().size()) {
        return;
    }
    // Fit is not 100% on most windows. Convert through the actual image rect,
    // so a drag follows the pointer at Fit, 25%, 100% and 400% alike.
    const QPointF canvasDelta = m_viewer ? m_viewer->canvasDeltaForViewDelta(delta) : delta;
    const QPointF modelDelta(canvasDelta.x(), -canvasDelta.y());
    QVector<int> layers = m_selectedLayers;
    if (layers.isEmpty() || !layers.contains(m_selectedLayer)) layers = {m_selectedLayer};
    const int frame = compositionFrameAt(*m_composition, m_playbackTime);
    for (int index : layers) {
        if (index < 0 || index >= m_composition->layers().size()) continue;
        composition::Layer& layer = m_composition->layerRef(index);
        if (!layer.locked) moveLayerAtFrame(layer, modelDelta, frame);
    }
    m_projectModified = true;
    updateWindowTitle();
    m_timeline->refreshKeyFrames();
    m_controlsPanel->refresh();
    requestRenderFrame();
}

void MainWindow::onLayerNudged(const QPointF& delta)
{
    if (!m_composition || m_selectedLayer < 0
        || m_selectedLayer >= m_composition->layers().size()) {
        return;
    }
    // A pixel here is a pixel of the frame, not of the widget: the reference
    // names these commands "Move position left by 1 pixel", and the number they
    // move by does not change when the viewer is zoomed. Y is already in the
    // composition's sense - up is positive - because the viewer sends it that
    // way for the arrow keys.
    QVector<int> layers = m_selectedLayers;
    if (layers.isEmpty() || !layers.contains(m_selectedLayer)) layers = {m_selectedLayer};
    const int frame = compositionFrameAt(*m_composition, m_playbackTime);
    for (int index : layers) {
        if (index < 0 || index >= m_composition->layers().size()) continue;
        composition::Layer& layer = m_composition->layerRef(index);
        if (!layer.locked) moveLayerAtFrame(layer, delta, frame);
    }
    m_projectModified = true;
    updateWindowTitle();
    m_timeline->refreshKeyFrames();
    m_controlsPanel->refresh();
    requestRenderFrame();
}

void MainWindow::showViewerIn360(bool in360)
{
    if (!m_viewerPage || !m_viewer360 || !m_viewerDock) return;
    if (in360 == m_viewer360->hostsViewer()) return;
    const bool hadFocus = m_viewer && m_viewer->hasFocus();
    if (in360) {
        m_viewerDock->setWidget(m_viewerPlaceholder);
        m_viewerPlaceholder->show();
        m_viewer360->hostViewer(m_viewerPage);
        m_viewer->setSphericalView(m_viewer360->view());
    } else {
        QWidget* page = m_viewer360->releaseViewer();
        if (!page) return;
        m_viewerPlaceholder->hide();
        m_viewerDock->setWidget(page);
        page->show();
        m_viewer->setSphericalView(nullptr);
        if (!m_viewerDock->isVisible()) m_viewerDock->raise();
    }
    if (hadFocus) m_viewer->setFocus();
    // The 360 mode looks at the same frame; a repaint is all it takes, but a
    // fresh render keeps the frame in step if the page was hidden meanwhile.
    requestRenderFrame();
}

void MainWindow::updateMotionPathOverlay()
{
    if (!m_viewer) {
        return;
    }
    if (!m_viewer->showMotionPath() || !m_composition || m_selectedLayer < 0
        || m_selectedLayer >= m_composition->layers().size()) {
        m_viewer->setMotionPath({}, {});
        return;
    }

    const composition::Layer& layer = m_composition->layers().at(m_selectedLayer);
    const composition::LayerTransform& transform = layer.transform;
    // Nothing moves, nothing to draw - a straight layer would otherwise get a
    // path that is a single point.
    if (transform.positionXCurve.isEmpty() && transform.positionYCurve.isEmpty()) {
        m_viewer->setMotionPath({}, {});
        return;
    }

    // The renderer places a layer at (W/2 + x, H/2 - y); the path is drawn in
    // that same canvas space so the viewer only has to scale it to the view.
    const double halfWidth = m_composition->displaySize().width() / 2.0;
    const double halfHeight = m_composition->displaySize().height() / 2.0;
    const auto toCanvas = [&](const QPointF& position) {
        return QPointF(halfWidth + position.x(), halfHeight - position.y());
    };

    // Only as many keys as the reference's own MotionPathKeyFrames allows, so a
    // long animation does not bury the frame under its own path.
    const int maxKeys = app::Settings::motionPathKeyFrames();
    QVector<int> keyFrames = transform.positionXCurve.locations();
    for (int frame : transform.positionYCurve.locations()) {
        if (!keyFrames.contains(frame)) {
            keyFrames.append(frame);
        }
    }
    std::sort(keyFrames.begin(), keyFrames.end());
    if (keyFrames.size() > maxKeys) {
        keyFrames = keyFrames.mid(0, maxKeys);
    }
    if (keyFrames.size() < 2) {
        m_viewer->setMotionPath({}, {});
        return;
    }

    QVector<QPointF> keys;
    keys.reserve(keyFrames.size());
    for (int frame : keyFrames) {
        keys.append(toCanvas(transform.positionAt(frame)));
    }

    // Sampled between the first and last key so curved interpolation shows as a
    // curve rather than as the straight lines between its keys.
    QVector<QPointF> path;
    const int first = keyFrames.first();
    const int last = keyFrames.last();
    const int steps = qBound(2, (last - first) + 1, 600);
    path.reserve(steps);
    for (int i = 0; i < steps; ++i) {
        const int frame = first + (last - first) * i / (steps - 1);
        path.append(toCanvas(transform.positionAt(frame)));
    }

    m_viewer->setMotionPath(path, keys);
}

bool MainWindow::addModelLayer(const QString& path, const model3d::ImportSettings& settings)
{
    if (!m_composition || !m_mediaManager) {
        return false;
    }
    const core::Result result = m_mediaManager->importModel(path, settings);
    if (result.isFailure()) {
        QMessageBox::warning(this, tr("Import 3D Model"), result.message());
        return false;
    }
    const media::MediaAsset asset = m_mediaManager->assetByFilePath(path);
    if (!asset.isValid()) {
        QMessageBox::warning(this, tr("Import 3D Model"),
                             tr("The 3D model could not be imported."));
        return false;
    }

    // A model layer is a scene object: it is born in 3D, because there is
    // nothing for it to be in the composition's plane.
    composition::Layer layer;
    layer.name = asset.fileName();
    layer.kind = composition::LayerKind::Model3D;
    layer.dimension = composition::LayerDimension::ThreeD;
    layer.modelAssetId = asset.id();
    layer.visible = true;
    layer.blendMode = QStringLiteral("None");
    layer.opacity = 1.0;

    const QString text = tr("Import 3D model '%1'").arg(asset.fileName());
    // The model stays imported in Media on Cancel; only its layer is not made.
    if (!pushLayersNeedingCamera({layer}, AddCameraReason::CreateLayer, text)) {
        m_mediaPanel->refresh();
        return false;
    }
    m_undoStack->push(new AddLayerCommand(this, m_composition.get(), layer, text));
    m_undoStack->endMacro();
    m_mediaPanel->refresh();
    statusBar()->showMessage(tr("Imported %1").arg(asset.fileName()));
    return true;
}

void MainWindow::importFiles(const QStringList& files)
{
    if (files.isEmpty() || !m_mediaManager || !m_composition) {
        return;
    }

    double cursor = 0.0;
    int imported = 0;
    int models = 0;
    // Reference ImageSequenceHelper (FUN_140385190, ShowImageSequenceImportDialog):
    // when a still belongs to a numbered run, one question for the whole
    // import decides between sequence videos and individual images.
    QHash<QString, QStringList> sequenceOf;
    for (const QString& file : files) {
        const QStringList run = media::findImageSequence(file);
        if (run.size() > 1) sequenceOf.insert(file, run);
    }
    bool asSequences = false;
    if (!sequenceOf.isEmpty()) {
        asSequences = showPrompt(
            this, QStringLiteral("ImageSequenceImportPrompt"), QMessageBox::Question,
            tr("Import"),
            tr("It looks like some of the files to import are from image sequences.\n\n"
               "Do you want to import sequences as videos, or import individual images?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No, [](QMessageBox& box) {
                box.button(QMessageBox::Yes)->setText(tr("Import Sequence Videos"));
                box.button(QMessageBox::No)->setText(tr("Import Images"));
            }) == QMessageBox::Yes;
    }
    QSet<QString> importedSequences;
    const double sequenceRate = m_composition->fpsDenominator() > 0
        ? double(m_composition->fpsNumerator()) / m_composition->fpsDenominator() : 30.0;
    for (const QString& file : files) {
        if (asSequences && sequenceOf.contains(file)) {
            const QStringList& run = sequenceOf[file];
            if (importedSequences.contains(run.first())) continue; // one clip per run
            importedSequences.insert(run.first());
            QString sequencePath;
            if (m_mediaManager->importImageSequence(run, sequenceRate, &sequencePath).isFailure()) {
                continue;
            }
            ++imported;
            const media::MediaAsset asset = m_mediaManager->assetByFilePath(sequencePath);
            offerCompositionMatch(asset);
            m_composition->addClip(QStringLiteral("V1"), asset.id(), cursor,
                                   asset.durationSeconds());
            cursor += asset.durationSeconds();
            continue;
        }
        // A 3D model is not footage: it has no duration to lay on a track and
        // its geometry has to be read through the model importer. Sending it
        // down the same route as a clip produced an asset with nothing behind
        // it and a three-second block on V1.
        if (model3d::modelExtensions().contains(QFileInfo(file).suffix().toLower())) {
            // Settings first, the way the reference's Model3DImport does it:
            // unit, axes and normals are decided before the geometry lands in
            // the project. This used to happen only for the File menu's own 3D
            // entry, so a model that arrived by drop was silently taken with
            // the defaults; now every model gets the dialog however it comes in.
            Model3DSettingsDialog dialog(file, model3d::ImportSettings(), this);
            if (dialog.exec() != QDialog::Accepted) {
                continue;
            }
            if (!dialog.meshIsValid()) {
                QMessageBox::warning(this, tr("Import"),
                                     tr("The 3D model could not be imported: %1")
                                         .arg(QFileInfo(file).fileName()));
                continue;
            }
            if (addModelLayer(file, dialog.settings())) {
                ++models;
            }
            continue;
        }
        const core::Result r = m_mediaManager->importFile(file);
        if (r.isFailure()) {
            OV_LOG_WARN(QStringLiteral("Import failed for %1: %2").arg(file, r.message()));
            continue;
        }
        ++imported;
        const media::MediaAsset asset = m_mediaManager->assetByFilePath(file);
        offerCompositionMatch(asset);
        double duration = asset.durationSeconds();
        if (duration <= 0.0) {
            duration = asset.kind() == media::MediaKind::Image ? 5.0 : 3.0;
        }
        m_composition->addClip(QStringLiteral("V1"), asset.id(), cursor, duration);
        cursor += duration;
    }

    m_timeline->setComposition(m_composition);
    m_timeline->update();
    m_mediaPanel->refresh();
    m_projectModified = true;
    updateWindowTitle();
    // Models are counted apart from footage: they land as layers rather than as
    // clips, so "3 media files" would be a lie about what just happened.
    const QString summary =
        models > 0 ? tr("Imported %1 media file(s) and %2 3D model(s)").arg(imported).arg(models)
                   : tr("Imported %1 media file(s)").arg(imported);
    if (m_historyPanel) {
        m_historyPanel->addEntry(summary);
    }
    statusBar()->showMessage(summary);
    requestRenderFrame();
}

void MainWindow::onAddTextLayer(const QString& text, int fontSize)
{
    if (!m_composition || text.isEmpty()) {
        return;
    }
    QString name = text;
    if (name.length() > 24) {
        name = name.left(24) + QStringLiteral("...");
    }
    composition::Layer layer;
    layer.name = name;
    layer.kind = composition::LayerKind::Text;
    layer.blendMode = QStringLiteral("None");
    layer.opacity = 1.0;

    composition::Clip clip;
    clip.mediaId = core::Identifier(QStringLiteral("media:text"));
    clip.startSeconds = m_playbackTime;
    clip.durationSeconds = qMin(5.0, m_composition->durationSeconds() - m_playbackTime);
    if (clip.durationSeconds <= 0.0) {
        clip.startSeconds = 0.0;
        clip.durationSeconds = m_composition->durationSeconds();
    }
    composition::Effect textEffect;
    textEffect.name = QStringLiteral("Text");
    textEffect.pluginId = core::Identifier(QStringLiteral("text"));
    // The full parameter list from the start, so the Text panel has every field
    // to edit rather than two and a row of blanks.
    composition::TextStyle style;
    style.text = text;
    style.fontSize = fontSize;
    textEffect.parameterValues = composition::textStyleToParameters(style);
    clip.effects.push_back(textEffect);
    layer.clips.push_back(clip);

    m_undoStack->push(new AddLayerCommand(this, m_composition.get(), layer,
                                         tr("Add text layer '%1'").arg(name)));
    statusBar()->showMessage(tr("Added text layer '%1'").arg(name));
    requestRenderFrame();
}

void MainWindow::addTextLayer(const composition::TextStyle& style, const QPointF& canvasPosition)
{
    if (!m_composition || style.text.trimmed().isEmpty()) return;
    QString name = style.text.simplified();
    if (name.size() > 32) name = name.left(29) + QStringLiteral("...");
    composition::Layer layer;
    layer.name = name;
    layer.kind = composition::LayerKind::Text;
    layer.blendMode = QStringLiteral("None");
    layer.opacity = 1.0;
    layer.transform.position = QPointF(canvasPosition.x() - m_composition->displaySize().width() / 2.0,
                                       m_composition->displaySize().height() / 2.0 - canvasPosition.y());
    composition::Clip clip;
    clip.mediaId = core::Identifier(QStringLiteral("media:text"));
    clip.startSeconds = m_playbackTime;
    clip.durationSeconds = qMin(5.0, m_composition->durationSeconds() - m_playbackTime);
    if (clip.durationSeconds <= 0.0) {
        clip.startSeconds = 0.0;
        clip.durationSeconds = m_composition->durationSeconds();
    }
    composition::Effect effect;
    effect.name = QStringLiteral("Text");
    effect.pluginId = core::Identifier(QStringLiteral("text"));
    effect.parameterValues = composition::textStyleToParameters(style);
    clip.effects.append(effect);
    layer.clips.append(clip);
    m_undoStack->push(new AddLayerCommand(this, m_composition.get(), layer,
                                         tr("Add text layer '%1'").arg(name)));
    statusBar()->showMessage(tr("Added text layer '%1'").arg(name));
}

void MainWindow::onMotionTrackingRequested(int layerIndex, int trackIndex)
{
    if (!m_composition || !m_mediaManager || layerIndex < 0
        || layerIndex >= m_composition->layers().size()) return;
    const composition::Layer& layer = m_composition->layers().at(layerIndex);
    if (trackIndex < 0 || trackIndex >= layer.motionTracks.size() || layer.clips.isEmpty()) return;

    const composition::Clip* sourceClip = nullptr;
    media::MediaAsset asset;
    for (const composition::Clip& clip : layer.clips) {
        const media::MediaAsset candidate = m_mediaManager->assetById(clip.mediaId);
        if (candidate.isValid() && candidate.kind() == media::MediaKind::Video) {
            sourceClip = &clip;
            asset = candidate;
            break;
        }
    }
    if (!sourceClip) {
        statusBar()->showMessage(tr("Motion tracking needs a video clip"), 5000);
        return;
    }
    media::VideoDecoder* decoder = decoderFor(asset.filePath(), asset.hardwareDecoding());
    if (!decoder || !decoder->isValid()) {
        statusBar()->showMessage(tr("Could not open the video for motion tracking"), 5000);
        return;
    }

    const double fps = m_composition->fpsDenominator() > 0
        ? double(m_composition->fpsNumerator()) / m_composition->fpsDenominator() : 30.0;
    const int firstFrame = qMax(0, qCeil(sourceClip->startSeconds * fps));
    const int lastFrame = qMax(firstFrame, qFloor(sourceClip->endSeconds() * fps - 1e-6));
    const composition::MotionTrack settings = layer.motionTracks.at(trackIndex);
    const QSize frameSize = decoder->frameSize();
    const int radius = qBound(2, settings.sampleRadius,
        qMax(2, qMin(frameSize.width(), frameSize.height()) / 2 - 1));
    QPointF point(qBound(double(radius), settings.point.x(), double(frameSize.width() - radius - 1)),
                  qBound(double(radius), settings.point.y(), double(frameSize.height() - radius - 1)));
    const auto sourceTime = [sourceClip, fps](int frame) {
        return sourceClip->sourceStartSeconds
            + (frame / fps - sourceClip->startSeconds) * qMax(0.0001, sourceClip->speed);
    };

    QProgressDialog progress(tr("Analyzing motion…"), tr("Cancel"), firstFrame, lastFrame, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    composition::KeyFrameList xCurve(point.x());
    composition::KeyFrameList yCurve(point.y());
    QImage previous = decoder->frameAt(sourceTime(firstFrame));
    if (previous.isNull()) {
        statusBar()->showMessage(tr("Could not decode the first tracking frame"), 5000);
        return;
    }
    for (int frame = firstFrame; frame <= lastFrame; ++frame) {
        if (progress.wasCanceled()) {
            statusBar()->showMessage(tr("Motion tracking cancelled"), 3000);
            return;
        }
        if (frame > firstFrame) {
            const QImage current = decoder->frameAt(sourceTime(frame));
            if (current.isNull()) {
                statusBar()->showMessage(tr("Motion tracking stopped at frame %1").arg(frame), 5000);
                break;
            }
            point = composition::MotionTracker::trackStep(previous, current, point,
                settings.sampleRadius, settings.searchRadius);
            previous = current;
        }
        composition::KeyFrame xKey;
        xKey.frame = frame;
        xKey.value = point.x();
        xKey.temporal = composition::TemporalType::Linear;
        composition::KeyFrame yKey = xKey;
        yKey.value = point.y();
        xCurve.add(xKey);
        yCurve.add(yKey);
        progress.setValue(frame);
        if ((frame - firstFrame) % 4 == 0) QCoreApplication::processEvents();
    }
    m_timeline->setMotionTrackData(layerIndex, trackIndex, xCurve, yCurve);
    statusBar()->showMessage(tr("Motion track analyzed: %1 frames").arg(xCurve.count()), 5000);
}

void MainWindow::createTextAt(const QPointF& canvasPosition)
{
    TextSettingsDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) addTextLayer(dialog.style(), canvasPosition);
}

void MainWindow::applySelectedTextStyle(const composition::TextStyle& style,
                                        const QString& commandText)
{
    composition::Effect* effect = selectedTextEffect();
    if (!effect || !m_composition) return;
    const int clipIndex = m_selectedClip < 0 ? 0 : m_selectedClip;
    composition::Clip* clip = m_composition->clipAt(m_selectedLayer, clipIndex);
    if (!clip) return;
    const int effectIndex = int(effect - clip->effects.data());
    const QStringList before = effect->parameterValues;
    const QStringList after = composition::textStyleToParameters(style);
    if (before == after) return;
    m_undoStack->push(new TextEditCommand(m_composition, m_selectedLayer, clipIndex,
        effectIndex, before, after, [this] {
            updateTextPanelSelection();
            if (m_controlsPanel) m_controlsPanel->refresh();
            m_projectModified = true;
            updateWindowTitle();
            requestRenderFrame();
        }, commandText));
}

void MainWindow::editSelectedText()
{
    composition::Effect* effect = selectedTextEffect();
    if (!effect) return;
    TextSettingsDialog dialog(this);
    dialog.setStyle(composition::textStyleFromParameters(effect->parameterValues));
    if (dialog.exec() == QDialog::Accepted)
        applySelectedTextStyle(dialog.style(), tr("Edit Text"));
}

void MainWindow::setSelectedTextMode(composition::TextStyle::TextMode mode)
{
    composition::Effect* effect = selectedTextEffect();
    if (!effect) return;
    composition::TextStyle style = composition::textStyleFromParameters(effect->parameterValues);
    style.textMode = mode;
    applySelectedTextStyle(style, mode == composition::TextStyle::TextMode::Point
        ? tr("Convert to Point Text") : tr("Convert to Paragraph Text"));
}

// --- Text panel ---------------------------------------------------------
//
// The reference's Text panel formats the selected text layer, so it has to
// follow the selection and write straight back into the layer's text effect.

composition::Effect* MainWindow::selectedTextEffect()
{
    if (!m_composition) {
        return nullptr;
    }
    composition::Clip* clip = m_composition->clipAt(m_selectedLayer, m_selectedClip);
    // Selecting the layer header reports clipIndex == -1. A text layer with
    // one clip must still be formattable without expanding its tree first.
    if (!clip && m_selectedClip < 0 && m_selectedLayer >= 0
        && m_selectedLayer < m_composition->layers().size()) {
        composition::Layer& layer = m_composition->layerRef(m_selectedLayer);
        if (layer.clips.size() == 1) {
            clip = &layer.clips[0];
        }
    }
    if (!clip) {
        return nullptr;
    }
    for (composition::Effect& fx : clip->effects) {
        if (fx.pluginId.value() == QLatin1String("text")
            || fx.name.compare(QLatin1String("Text"), Qt::CaseInsensitive) == 0) {
            return &fx;
        }
    }
    return nullptr;
}

void MainWindow::updateTextPanelSelection()
{
    if (!m_textPanel) {
        return;
    }
    if (composition::Effect* fx = selectedTextEffect()) {
        m_textPanel->setStyle(composition::textStyleFromParameters(fx->parameterValues));
    } else {
        m_textPanel->clearSelection();
    }
}

void MainWindow::onTextStyleEdited(const composition::TextStyle& style)
{
    composition::Effect* fx = selectedTextEffect();
    if (!fx) {
        return;
    }
    // The string itself is not edited here - the panel formats, it does not
    // type - so whatever the layer already says is kept.
    composition::TextStyle merged = style;
    merged.text = composition::textStyleFromParameters(fx->parameterValues).text;
    const QStringList before = fx->parameterValues;
    const QStringList after = composition::textStyleToParameters(merged);
    if (before == after) return;
    const int clipIndex = m_selectedClip < 0 ? 0 : m_selectedClip;
    const auto* clip = m_composition->clipAt(m_selectedLayer, clipIndex);
    if (!clip) return;
    const int effectIndex = int(fx - clip->effects.constData());
    m_undoStack->push(new TextEditCommand(m_composition, m_selectedLayer, clipIndex,
        effectIndex, before, after, [this] {
            // During the initial redo the panel already has this value. Avoid
            // rebuilding a live spinbox or outline row under the user's cursor.
            if (auto* selected = selectedTextEffect()) {
                const auto style = composition::textStyleFromParameters(selected->parameterValues);
                if (composition::textStyleToParameters(style)
                    != composition::textStyleToParameters(m_textPanel->style()))
                    m_textPanel->setStyle(style);
            } else {
                m_textPanel->clearSelection();
            }
            m_projectModified = true;
            updateWindowTitle();
            if (m_controlsPanel) m_controlsPanel->refresh();
            requestRenderFrame();
        }));
}

void MainWindow::onAddNewLayer(composition::LayerKind kind)
{
    if (!m_composition) {
        return;
    }
    if (kind == composition::LayerKind::Text) {
        TextSettingsDialog dialog(this);
        if (dialog.exec() == QDialog::Accepted)
            addTextLayer(dialog.style(), QPointF(m_composition->displaySize().width() / 2.0,
                                                 m_composition->displaySize().height() / 2.0));
        return;
    }
    const int next = m_composition->layers().size() + 1;
    // Named after the kind, as the reference does ("1. New Point [Point]" in
    // its own layer tree), so the tree says what the layer is.
    const QString kindName =
        QCoreApplication::translate("LayerKind", composition::layerKindName(kind));
    const QString name = kind == composition::LayerKind::Media
                             ? tr("Layer %1").arg(next)
                             : tr("New %1 %2").arg(kindName).arg(next);
    // Built here, then handed to the undo stack: the command owns the finished
    // layer, so redo puts back exactly this one.
    composition::Layer layer;
    if (kind == composition::LayerKind::Camera) {
        // Placed where the default view looks from, as the camera a 3D
        // switch adds is: at the origin it would sit inside the layers.
        layer = newCameraLayer(*m_composition);
    }
    layer.name = name;
    layer.kind = kind;
    layer.visible = true;
    layer.blendMode = QStringLiteral("None");
    layer.opacity = 1.0;
    // A light only exists in a 3D shot: without a camera the reference asks
    // to add one first (FUN_1405a8740) and creates nothing on Cancel.
    if (kind == composition::LayerKind::Light) layer.dimension = composition::LayerDimension::ThreeD;
    if (!pushLayersNeedingCamera({layer}, AddCameraReason::CreateLayer, tr("Add layer '%1'").arg(name))) {
        return;
    }

    // Grade and Plane are picture-producing layers with no media behind them.
    // Effects hang off clips in this model, so they get one spanning the whole
    // composition - that is also what gives them a range on the timeline.
    if (kind == composition::LayerKind::Grade || kind == composition::LayerKind::Plane) {
        composition::Clip clip;
        clip.startSeconds = 0.0;
        clip.durationSeconds = kind == composition::LayerKind::Plane
            ? qMin(m_composition->durationSeconds(), app::Settings::planeDefaultDurationSeconds())
            : m_composition->durationSeconds();
        layer.clips.push_back(clip);
    }

    m_undoStack->push(new AddLayerCommand(this, m_composition.get(), layer,
                                          tr("Add layer '%1'").arg(name)));
    m_undoStack->endMacro();
    statusBar()->showMessage(tr("Added layer '%1'").arg(name));
}

bool MainWindow::pushLayersNeedingCamera(const QVector<composition::Layer>& layers,
                                         AddCameraReason reason, const QString& text)
{
    // Opens the undo macro the caller's own AddLayerCommand(s) and its
    // endMacro() close, so an added camera and the layers it is for are one
    // step of History.
    const bool needsCamera = std::any_of(layers.cbegin(), layers.cend(), layerNeedsCamera);
    if (needsCamera && !compositionIs3D(*m_composition)) {
        if (!confirmAddCamera(this, reason)) return false;
        m_undoStack->beginMacro(text);
        const composition::Layer camera = newCameraLayer(*m_composition);
        m_undoStack->push(new AddLayerCommand(
            this, m_composition.get(), camera,
            QCoreApplication::translate("QObject", "Insert Layer")));
        return true;
    }
    m_undoStack->beginMacro(text);
    return true;
}

void MainWindow::makeCompositeShotFromSelection()
{
    if (!m_composition) return;
    QVector<int> selected = m_selectedLayers;
    if (selected.isEmpty() && m_selectedLayer >= 0) selected.append(m_selectedLayer);
    std::sort(selected.begin(), selected.end());
    selected.erase(std::unique(selected.begin(), selected.end()), selected.end());
    selected.erase(std::remove_if(selected.begin(), selected.end(), [this](int index) {
        return index < 0 || index >= m_composition->layers().size();
    }), selected.end());
    if (selected.isEmpty()) {
        statusBar()->showMessage(tr("Select one or more layers first"));
        return;
    }

    auto child = std::make_shared<composition::Composition>();
    child->setName(tr("Composite Shot %1").arg(m_openCompositions.size()));
    child->setSize(m_composition->width(), m_composition->height());
    child->setPixelAspect(m_composition->pixelAspect(), m_composition->customPixelAspect());
    child->setFrameRate(m_composition->fpsNumerator(), m_composition->fpsDenominator());

    QVector<composition::Layer> childLayers;
    double firstTime = std::numeric_limits<double>::max();
    double lastTime = 0.0;
    for (int index : selected) {
        const composition::Layer copy = m_composition->layerCopy(index);
        childLayers.append(copy);
        for (const composition::Clip& clip : copy.clips) {
            firstTime = qMin(firstTime, clip.startSeconds);
            lastTime = qMax(lastTime, clip.endSeconds());
        }
    }
    if (firstTime == std::numeric_limits<double>::max()) firstTime = 0.0;
    for (composition::Layer& layer : childLayers)
        for (composition::Clip& clip : layer.clips) clip.startSeconds -= firstTime;
    const double frameDuration = double(qMax(1, child->fpsDenominator()))
        / qMax(1, child->fpsNumerator());
    const double childDuration = qMax(frameDuration, lastTime - firstTime);
    child->setDurationSeconds(childDuration);
    child->setLayers(childLayers);

    QVector<composition::Layer> parentLayers = m_composition->layers();
    for (int i = selected.size() - 1; i >= 0; --i) parentLayers.removeAt(selected.at(i));
    composition::Layer nestedLayer;
    nestedLayer.name = child->name();
    nestedLayer.kind = composition::LayerKind::Media;
    composition::Clip nestedClip;
    nestedClip.mediaId = core::Identifier(QStringLiteral("composition:") + child->id().value());
    nestedClip.nestedCompositionId = child->id();
    nestedClip.nestedComposition = child;
    nestedClip.startSeconds = firstTime;
    nestedClip.durationSeconds = childDuration;
    nestedLayer.clips.append(nestedClip);
    parentLayers.insert(selected.first(), nestedLayer);
    m_composition->setLayers(parentLayers);

    if (m_rootComposition) m_rootComposition->addCompositeShot(child);
    m_openCompositions.append(child);
    m_projectModified = true;
    updateWindowTitle();
    activateComposition(child);
    statusBar()->showMessage(tr("Created %1 from %2 layer(s)")
                                 .arg(child->name()).arg(selected.size()));
}

void MainWindow::deleteSelectedLayer()
{
    if (!m_composition || m_selectedLayer < 0
        || m_selectedLayer >= m_composition->layers().size()) {
        statusBar()->showMessage(tr("Select a layer on the timeline first"));
        return;
    }
    composition::Layer& layer = m_composition->layerRef(m_selectedLayer);
    const QString name = layer.name;
    if (layer.locked) {
        statusBar()->showMessage(tr("Unlock the layer before deleting it")); return;
    }
    if (m_selectedClip >= 0 && m_selectedClip < layer.clips.size()) {
        layer.clips.removeAt(m_selectedClip);
        m_selectedClip = -1;
        m_controlsPanel->setSelection(m_selectedLayer, -1);
        m_timeline->setComposition(m_composition);
        m_trackPanel->refresh();
        m_projectModified = true;
        updateWindowTitle();
        requestRenderFrame();
        statusBar()->showMessage(tr("Deleted selected clip"));
        return;
    }
    // Taking the last camera out of a 3D shot turns it 2D (FUN_140593b70):
    // asked first, then lights go and 3D layers become 2D in the same step.
    const bool lastCamera = removesLastCamera(*m_composition, {m_selectedLayer});
    if (lastCamera && !confirmRemoveLastCamera(this)) return;
    if (lastCamera) m_undoStack->beginMacro(
        QCoreApplication::translate("CompositionTools", "Remove Layer(s)"));
    m_undoStack->push(new RemoveLayerCommand(this, m_composition.get(), m_selectedLayer,
                                             tr("Delete layer '%1'").arg(name)));
    if (lastCamera) {
        const auto before = m_composition->layers();
        if (convertTo2D(*m_composition)) {
            m_undoStack->push(new LayerStackCommand(
                m_composition, before, m_composition->layers(),
                QCoreApplication::translate("CompositionTools", "Remove Layer(s)"),
                [this] { refreshAfterModelChange(); }));
            refreshAfterModelChange();
        }
        m_undoStack->endMacro();
    }
    m_selectedLayer = -1;
    m_selectedClip = -1;
    m_trackPanel->setSelectedLayer(-1);
    m_layerPanel->setSelection(-1);
    m_controlsPanel->setSelection(-1, -1);
    statusBar()->showMessage(tr("Deleted layer '%1'").arg(name));
}

void MainWindow::duplicateSelectedLayer()
{
    if (!m_composition || m_selectedLayer < 0
        || m_selectedLayer >= m_composition->layers().size()) {
        statusBar()->showMessage(tr("Select a layer on the timeline first"));
        return;
    }
    composition::Layer& source = m_composition->layerRef(m_selectedLayer);
    if (m_selectedClip >= 0 && m_selectedClip < source.clips.size()) {
        composition::Clip copy = source.clips.at(m_selectedClip);
        copy.startSeconds += copy.durationSeconds;
        source.clips.insert(m_selectedClip + 1, copy);
        m_selectedClip += 1;
        m_timeline->setComposition(m_composition);
        m_controlsPanel->setSelection(m_selectedLayer, m_selectedClip);
        m_projectModified = true;
        updateWindowTitle();
        requestRenderFrame();
        statusBar()->showMessage(tr("Duplicated selected clip"));
        return;
    }
    composition::Layer copy = source;
    copy.id = core::Identifier(QUuid::createUuid().toString(QUuid::WithoutBraces));
    copy.name = tr("%1 copy").arg(copy.name);
    const QString text = tr("Duplicate layer '%1'").arg(copy.name);
    if (!pushLayersNeedingCamera({copy}, AddCameraReason::PasteLayers, text)) return;
    m_undoStack->push(new AddLayerCommand(this, m_composition.get(), copy, text));
    m_undoStack->endMacro();
    statusBar()->showMessage(tr("Duplicated layer '%1'").arg(copy.name));
}

void MainWindow::copySelection(bool cutAfterCopy)
{
    if (!m_composition || m_selectedLayer < 0
        || m_selectedLayer >= m_composition->layers().size()) {
        statusBar()->showMessage(tr("Select a layer or clip first"));
        return;
    }
    const composition::Layer& layer = m_composition->layers().at(m_selectedLayer);
    if (m_selectedClip >= 0 && m_selectedClip < layer.clips.size()) {
        m_clipClipboard = layer.clips.at(m_selectedClip);
        m_clipboardHasClip = true;
        m_clipboardHasLayer = false;
        statusBar()->showMessage(tr("Copied selected clip"));
    } else {
        m_layerClipboard = layer;
        m_clipboardHasLayer = true;
        m_clipboardHasClip = false;
        statusBar()->showMessage(tr("Copied layer '%1'").arg(layer.name));
    }
    if (cutAfterCopy) deleteSelectedLayer();
}

void MainWindow::pasteSelection()
{
    if (!m_composition) return;
    if (m_clipboardHasClip) {
        QString layerName = QStringLiteral("V1");
        if (m_selectedLayer >= 0 && m_selectedLayer < m_composition->layers().size())
            layerName = m_composition->layers().at(m_selectedLayer).name;
        composition::Clip* pasted = m_composition->addClip(
            layerName, m_clipClipboard.mediaId, m_playbackTime, m_clipClipboard.durationSeconds);
        if (pasted) {
            pasted->sourceStartSeconds = m_clipClipboard.sourceStartSeconds;
            pasted->audioLevel = m_clipClipboard.audioLevel;
            pasted->speed = m_clipClipboard.speed;
            pasted->effects = m_clipClipboard.effects;
        }
        refreshAfterModelChange();
        m_projectModified = true;
        updateWindowTitle();
        statusBar()->showMessage(tr("Pasted clip at playhead"));
        return;
    }
    if (m_clipboardHasLayer) {
        composition::Layer copy = m_layerClipboard;
        copy.id = core::Identifier(QUuid::createUuid().toString(QUuid::WithoutBraces));
        copy.parentLayerId = core::Identifier();
        copy.name = tr("%1 copy").arg(copy.name);
        const QString text = tr("Paste layer '%1'").arg(copy.name);
        if (!pushLayersNeedingCamera({copy}, AddCameraReason::PasteLayers, text)) return;
        m_undoStack->push(new AddLayerCommand(this, m_composition.get(), copy, text));
        m_undoStack->endMacro();
        statusBar()->showMessage(tr("Pasted layer '%1'").arg(copy.name));
        return;
    }
    statusBar()->showMessage(tr("Clipboard is empty"));
}

void MainWindow::pasteSelectionAttributes()
{
    if (!m_composition || m_selectedLayer < 0
        || m_selectedLayer >= m_composition->layers().size()) return;
    composition::Layer& layer = m_composition->layerRef(m_selectedLayer);
    bool addCamera = false;
    if (m_clipboardHasClip && m_selectedClip >= 0 && m_selectedClip < layer.clips.size()) {
        composition::Clip& clip = layer.clips[m_selectedClip];
        clip.audioLevel = m_clipClipboard.audioLevel;
        clip.speed = m_clipClipboard.speed;
        clip.effects = m_clipClipboard.effects;
    } else if (m_clipboardHasLayer) {
        // Taking 3D over in a shot without a camera asks for one first; on
        // Cancel the layer keeps its dimension and the rest is pasted.
        bool takeDimension = true;
        if (m_layerClipboard.dimension == composition::LayerDimension::ThreeD
            && layer.dimension != composition::LayerDimension::ThreeD
            && !compositionIs3D(*m_composition)) {
            addCamera = confirmAddCamera(this, AddCameraReason::SetDimension);
            takeDimension = addCamera;
        }
        if (takeDimension) layer.dimension = m_layerClipboard.dimension;
        layer.blendMode = m_layerClipboard.blendMode;
        layer.opacity = m_layerClipboard.opacity;
        layer.transform = m_layerClipboard.transform;
        layer.labelColor = m_layerClipboard.labelColor;
    } else {
        statusBar()->showMessage(tr("No compatible attributes in the clipboard"));
        return;
    }
    // Appended last: `layer` refers into the stack this insertion may move.
    if (addCamera) m_composition->insertLayer(m_composition->layers().size(),
                                              newCameraLayer(*m_composition));
    m_projectModified = true;
    updateWindowTitle();
    refreshAfterModelChange();
    statusBar()->showMessage(tr("Pasted attributes"));
}

void MainWindow::removeSelectionAttributes()
{
    if (!m_composition || m_selectedLayer < 0
        || m_selectedLayer >= m_composition->layers().size()) return;
    composition::Layer& layer = m_composition->layerRef(m_selectedLayer);
    if (m_selectedClip >= 0 && m_selectedClip < layer.clips.size()) {
        composition::Clip& clip = layer.clips[m_selectedClip];
        clip.audioLevel = 0.0;
        clip.speed = 1.0;
    } else {
        layer.dimension = composition::LayerDimension::TwoD;
        layer.blendMode = QStringLiteral("None");
        layer.opacity = 1.0;
        layer.transform = composition::LayerTransform();
    }
    m_projectModified = true;
    updateWindowTitle();
    refreshAfterModelChange();
    statusBar()->showMessage(tr("Removed attributes"));
}

void MainWindow::removeSelectionEffects()
{
    if (!m_composition || m_selectedLayer < 0
        || m_selectedLayer >= m_composition->layers().size()) return;
    composition::Layer& layer = m_composition->layerRef(m_selectedLayer);
    if (m_selectedClip >= 0 && m_selectedClip < layer.clips.size()) {
        layer.clips[m_selectedClip].effects.clear();
    } else {
        for (composition::Clip& clip : layer.clips) clip.effects.clear();
    }
    m_projectModified = true;
    updateWindowTitle();
    refreshAfterModelChange();
    statusBar()->showMessage(tr("Removed effects"));
}

void MainWindow::sliceSelectionAtPlayhead()
{
    composition::Clip* clip = m_composition
        ? m_composition->clipAt(m_selectedLayer, m_selectedClip) : nullptr;
    if (!clip || m_playbackTime <= clip->startSeconds || m_playbackTime >= clip->endSeconds()) {
        statusBar()->showMessage(tr("Place the playhead inside the selected clip"));
        return;
    }
    composition::Layer& layer = m_composition->layerRef(m_selectedLayer);
    const double leftDuration = m_playbackTime - clip->startSeconds;
    composition::Clip right = *clip;
    right.startSeconds = m_playbackTime;
    right.durationSeconds -= leftDuration;
    right.sourceStartSeconds += leftDuration * clip->speed;
    clip->durationSeconds = leftDuration;
    layer.clips.insert(m_selectedClip + 1, right);
    m_timeline->setComposition(m_composition);
    rebuildCompositionTabs();
    m_projectModified = true;
    updateWindowTitle();
    requestRenderFrame();
    statusBar()->showMessage(tr("Sliced selected clip"));
}

void MainWindow::rippleDeleteSelection()
{
    if (!m_composition || m_selectedLayer < 0
        || m_selectedLayer >= m_composition->layers().size()) return;
    composition::Layer& layer = m_composition->layerRef(m_selectedLayer);
    if (m_selectedClip < 0 || m_selectedClip >= layer.clips.size()) {
        statusBar()->showMessage(tr("Select a clip to ripple delete"));
        return;
    }
    const composition::Clip removed = layer.clips.at(m_selectedClip);
    layer.clips.removeAt(m_selectedClip);
    for (composition::Clip& clip : layer.clips) {
        if (clip.startSeconds >= removed.endSeconds()) clip.startSeconds -= removed.durationSeconds;
    }
    m_selectedClip = -1;
    m_timeline->setComposition(m_composition);
    m_controlsPanel->setSelection(m_selectedLayer, -1);
    m_projectModified = true;
    updateWindowTitle();
    requestRenderFrame();
    statusBar()->showMessage(tr("Ripple deleted selected clip"));
}

void MainWindow::onOpenInTrimmer()
{
    if (!m_mediaPanel) {
        return;
    }
    const QString filePath = m_mediaPanel->selectedFilePath();
    if (filePath.isEmpty()) {
        statusBar()->showMessage(tr("Select a media asset first"));
        return;
    }
    if (m_mediaManager) {
        m_trimmerPanel->openAsset(m_mediaManager->assetByFilePath(filePath));
    }
    showCenterTab(m_trimmerPanel);
}

void MainWindow::onOptions()
{
    if (!m_optionsDialog) {
        m_optionsDialog = new OptionsDialog(this);
        // Viewer preferences take effect when the dialog closes, so the
        // widgets that honour them re-read once rather than polling.
        connect(m_optionsDialog, &QDialog::finished, this, [this] {
            if (m_viewer) {
                m_viewer->setShowMouseCoordinates(app::Settings::showMouseCoordinates());
                // The reference drives these from two places - its Preferences
                // Viewer page and the viewer's own Options menu.
                m_viewer->setCheckerboardEnabled(app::Settings::showCheckerboard2D());
                m_viewer->setShowMotionPath(app::Settings::showMotionPath());
                m_viewer->setPlaybackQuality(ViewerWidget::qualityFromLabel(
                    app::Settings::playbackQualityProfile()));
                m_viewer->setPausedQuality(ViewerWidget::qualityFromLabel(
                    app::Settings::pausedQualityProfile()));
                m_viewer->setPlaybackResolution(ViewerWidget::downsampleFromLabel(
                    app::Settings::playbackDownsampleMode()));
                m_viewer->setPausedResolution(ViewerWidget::downsampleFromLabel(
                    app::Settings::pausedDownsampleMode()));
                updateMotionPathOverlay();
            }
            const QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                                     app::Settings::organizationName(),
                                     app::Settings::applicationName());
            if (settings.value(QStringLiteral("Options/Theme"),
                               QStringLiteral("Dark")).toString() == QLatin1String("System")) {
                qApp->setStyleSheet(QString());
            } else {
                applyTheme(qApp);
            }
            OptionsDialog::applyShortcuts(this);
            applyInterfacePreferences();
            // Reopen decoders so changed codec/hardware options take effect.
            m_videoDecoders.clear(); m_videoDecoderOrder.clear();
            if (m_mediaManager) m_mediaManager->clearVideoFrames();
            applyTimelineCacheOptions();
            requestRenderFrame();
            updateAutoSaveTimer();
            if (m_exportPanel) {
                m_exportPanel->setExportDirectory(settings.value(
                    QStringLiteral("Options/ExportDirectory")).toString());
            }
            if (m_exportQueueView) m_exportQueueView->setTimeFormat(exportTimeFormatFromSettings());
        });
    }
    m_optionsDialog->show();
    m_optionsDialog->raise();
    m_optionsDialog->activateWindow();
}

void MainWindow::applyInterfacePreferences()
{
    QPixmapCache::setCacheLimit(qMax(0, app::Settings().thumbnailCacheSizeMb()) * 1024);
    const QSettings settings = app::Settings::optionSettings();
    menuBar()->setNativeMenuBar(settings.value(QStringLiteral("Options/UseNativeMenuBar"), true).toBool());
    if (auto* toolbar = findChild<QToolBar*>(QStringLiteral("widgetQuickActions")))
        toolbar->setVisible(settings.value(QStringLiteral("Options/ShowMenuBarQuickActions"), true).toBool());
}

void MainWindow::updateAutoSaveTimer()
{
    // Options > Auto Save: on/off and the frequency in minutes.
    if (!autosave::enabled()) {
        m_autoSaveTimer.stop();
        return;
    }
    m_autoSaveTimer.start(autosave::frequencyMinutes() * 60 * 1000);
}

void MainWindow::writeAutoSave()
{
    // Only while there are changes since the last manual save, each time as
    // a new file in the auto-save folder - never over the project itself.
    if (!m_projectModified || !m_rootComposition || !m_mediaManager) {
        return;
    }
    const QString backup = autosave::nextFile(m_currentFilePath);
    project::ProjectSaveOptions options;
    options.isAutoSave = true;
    options.autoSaveOf = m_currentFilePath;   // absolute media paths: it lives elsewhere
    const QSettings settings = app::Settings::optionSettings();
    if (settings.value(QStringLiteral("Options/IncludeScreenLayout"), false).toBool())
        options.screenLayout = saveState(kLayoutStateVersion);
    storePlayheadInComposition();
    const core::Result result = project::VegfxSerializer::saveToFile(
        backup, *m_rootComposition, *m_mediaManager, options);
    if (result.isSuccess()) m_autoSaveFiles.append(backup);
    statusBar()->showMessage(result.isSuccess()
        ? tr("Auto-saved %1").arg(QDir::toNativeSeparators(backup))
        : tr("Auto-save failed: %1").arg(result.message()), 5000);
}

void MainWindow::showRecoveredProjects()
{
    RecoveredProjectsDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) return;
    if (m_projectModified && !confirmDiscard()) return;
    openRecoveredProject(dialog.selected().file, dialog.selected().projectPath);
}

bool MainWindow::openRecoveredProject(const QString& autosaveFile, const QString& projectPath)
{
    if (autosaveFile.isEmpty() || !openProjectFile(autosaveFile)) {
        QMessageBox::warning(this, tr("Open Project"),
                             QCoreApplication::translate("biff::ui::MainAppWindow",
                                                         "The recovered project cannot be opened."));
        return false;
    }
    // It belongs to the project it was taken from: Save goes back there (or
    // asks for a place when that project was never saved), and the auto-save
    // goes once it has.
    m_currentFilePath = projectPath;
    m_recoveredFrom = autosaveFile;
    m_projectModified = true;
    updateWindowTitle();
    return true;
}

void MainWindow::offerAutoSaveRecovery()
{
    if (autosave::entries().isEmpty()) return;
    showRecoveredProjects();
}

void MainWindow::applyTimelineCacheOptions(bool prune)
{
    // Options > Cache > Timeline Cache. The playback cache keeps its frames
    // in this directory too; at startup frames unused for longer than the
    // retention are removed, like the media cache's own pruning.
    const QSettings settings = app::Settings::optionSettings();
    const QString directory = settings.value(
        QStringLiteral("Options/TimelineCache"),
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
            .filePath(QStringLiteral("TimelineCache"))).toString().trimmed();
    if (m_renderManager) {
        m_renderManager->setDiskCacheDirectory(QDir::fromNativeSeparators(directory));
        if (prune) {
            const int removed = m_renderManager->diskCache().prune(
                settings.value(QStringLiteral("Options/TimelineCacheDays"), 30).toInt());
            if (removed > 0) {
                OV_LOG_INFO(QStringLiteral("Timeline cache: removed %1 expired frame(s)").arg(removed));
            }
        }
    }
    // Automatic render cache: once the timeline has been idle for the delay,
    // cache from the playhead onwards.
    m_autoRenderCache = settings.value(QStringLiteral("Options/UseAutomaticRenderCache"), false).toBool();
    m_autoRenderCacheTimer.setSingleShot(true);
    m_autoRenderCacheTimer.setInterval(
        qMax(0, settings.value(QStringLiteral("Options/RenderCacheDelay"), 2).toInt()) * 1000);
    if (!m_autoRenderCache) m_autoRenderCacheTimer.stop();
}

void MainWindow::offerCompositionMatch(const media::MediaAsset& asset)
{
    // Reference SequenceTools prompt (FUN_140804440, ShowMatchClipDialog,
    // "remember choice"). MediaAsset carries no frame rate here, so only the
    // frame size is compared; the reference also matches the rate.
    if (!m_composition || asset.kind() != media::MediaKind::Video) return;
    const QSize size = asset.frameSize();
    if (!size.isValid() || size.isEmpty()
        || size == QSize(m_composition->width(), m_composition->height())) {
        return;
    }
    const auto answer = showPrompt(
        this, QStringLiteral("MediaMismatchPrompt"), QMessageBox::Question,
        tr("Composite Shot Settings"),
        tr("The composite shot settings differ to this clip you are adding (%1 x %2).\n\n"
           "Do you want to change the composite shot's settings to match the clip?")
            .arg(size.width()).arg(size.height()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (answer == QMessageBox::Yes) {
        m_composition->setSize(size.width(), size.height());
        m_projectModified = true;
        statusBar()->showMessage(tr("Composite shot resized to %1 x %2")
                                     .arg(size.width()).arg(size.height()));
    }
}

void MainWindow::addMediaClipToTimeline(const QString& filePath, int trimInFrame,
                                        int trimOutFrame, bool insertEdit)
{
    if (filePath.isEmpty() || !m_mediaManager || !m_composition) {
        return;
    }
    const core::Result r = m_mediaManager->importFile(filePath);
    if (r.isFailure()) {
        OV_LOG_WARN(QStringLiteral("Add clip: import failed for %1: %2").arg(filePath, r.message()));
        statusBar()->showMessage(tr("Could not open %1").arg(filePath));
        return;
    }
    const media::MediaAsset asset = m_mediaManager->assetByFilePath(filePath);
    offerCompositionMatch(asset);
    const double fps = m_composition->fpsDenominator() > 0
        ? double(m_composition->fpsNumerator()) / m_composition->fpsDenominator() : 30.0;
    const int fullFrameCount = qMax(1, qRound(asset.durationSeconds() * fps));
    const int sourceIn = trimInFrame >= 0
        ? qBound(0, trimInFrame, qMax(0, fullFrameCount - 1)) : 0;
    const int sourceOut = trimOutFrame > sourceIn
        ? qBound(sourceIn + 1, trimOutFrame, fullFrameCount) : fullFrameCount;
    double duration = (sourceOut - sourceIn) / fps;
    if (duration <= 0.0) {
        duration = asset.kind() == media::MediaKind::Image ? 5.0 : 3.0;
    }
    if (insertEdit) {
        if (composition::Layer* layer = m_composition->layer(QStringLiteral("V1"))) {
            for (composition::Clip& clip : layer->clips) {
                if (clip.startSeconds >= m_playbackTime) {
                    clip.startSeconds += duration;
                }
            }
        }
    }
    composition::Clip* clip = m_composition->addClip(
        QStringLiteral("V1"), asset.id(), m_playbackTime, duration);
    if (clip) {
        clip->sourceStartSeconds = sourceIn / fps;
    }
    double latestEnd = m_composition->durationSeconds();
    for (const composition::Layer& layer : m_composition->layers()) {
        for (const composition::Clip& candidate : layer.clips) {
            latestEnd = qMax(latestEnd, candidate.endSeconds());
        }
    }
    m_composition->setDurationSeconds(latestEnd);
    m_timeline->setComposition(m_composition);
    m_timeline->update();
    m_mediaPanel->refresh();
    m_projectModified = true;
    updateWindowTitle();
    if (m_historyPanel) {
        m_historyPanel->addEntry((insertEdit ? tr("Insert clip from %1")
                                             : tr("Overlay clip from %1")).arg(filePath));
    }
    statusBar()->showMessage((insertEdit ? tr("Inserted clip from %1")
                                         : tr("Overlaid clip from %1")).arg(filePath));
    requestRenderFrame();
}

void MainWindow::exportFrameToFile(const QString& path)
{
    if (path.isEmpty() || !m_viewer) {
        return;
    }
    const QImage frame = m_viewer->frame();
    if (frame.isNull()) {
        statusBar()->showMessage(tr("No frame available to export"));
        return;
    }

    // Qt ships no EXR image plugin, so the Export panel's "OpenEXR (.exr)"
    // preset has to go through OpenEXR directly.
    if (QFileInfo(path).suffix().compare(QLatin1String("exr"), Qt::CaseInsensitive) == 0) {
        QString error;
        if (!media::writeExr(frame, path, &error)) {
            statusBar()->showMessage(tr("EXR export failed: %1").arg(error));
            QMessageBox::warning(this, QStringLiteral("Export Frame"), error);
            return;
        }
    } else if (!frame.save(path)) {
        statusBar()->showMessage(tr("Export failed: %1").arg(path));
        return;
    }
    statusBar()->showMessage(tr("Exported frame to %1").arg(path));
    if (m_historyPanel) {
        m_historyPanel->addEntry(tr("Export frame to %1").arg(path));
    }
    notifyExportCompleted();
}

bool MainWindow::isExporting() const
{
    return (m_exportJob && m_exportJob->isRunning()) || (m_exportQueue && m_exportQueue->isRunning());
}

void MainWindow::configureExportQueue()
{
    if (!m_exportQueue) return;
    m_exportQueue->setMediaManager(m_mediaManager);
    m_exportQueue->setPreRenderDirectory(preRenderRoot());
    m_exportQueue->setHardwareEncoding(app::Settings::optionSettings()
                                           .value(QStringLiteral("Options/UseHardwareEncoding"), true).toBool());
    if (m_owner && m_owner->settings())
        m_exportQueue->setSnapshotDirectory(m_owner->settings()->snapshotDirectory());
}

void MainWindow::queueContentsForExport(const QString& path)
{
    if (!m_composition || !m_rootComposition || !m_mediaManager || !m_exportQueue || path.isEmpty()) return;
    configureExportQueue();
    // The open tabs and playheads go into the snapshot as they are.
    storePlayheadInComposition();
    const double fps = m_composition->fpsDenominator() > 0
        ? double(m_composition->fpsNumerator()) / m_composition->fpsDenominator() : 30.0;
    const int first = m_inPoint >= 0.0 ? qRound(m_inPoint * fps) : 0;
    const double end = m_outPoint > m_inPoint ? m_outPoint : m_composition->durationSeconds();
    const int last = qMax(first, qCeil(end * fps) - 1);
    const QString id = m_exportQueue->addTask(*m_rootComposition, *m_composition, *m_mediaManager,
                                              m_exportPanel->currentPreset(), path, first, last);
    const QString added = tr("Added %1 to the export queue").arg(m_composition->name());
    statusBar()->showMessage(id.isEmpty()
        ? QCoreApplication::translate("biff::ui::exporter::ExportTask", "The project snapshot could not be created.")
        : added);
}

void MainWindow::exportContents(const QString& path)
{
    if (!m_composition || path.isEmpty() || (m_exportJob && m_exportJob->isRunning())) return;
    const double fps = m_composition->fpsDenominator() > 0
        ? double(m_composition->fpsNumerator()) / m_composition->fpsDenominator() : 30.0;
    render::ExportJob::Request request;
    request.composition = m_composition;
    request.media = m_mediaManager;
    request.outputPath = path;
    request.firstFrame = m_inPoint >= 0.0 ? qRound(m_inPoint * fps) : 0;
    const double end = m_outPoint > m_inPoint ? m_outPoint : m_composition->durationSeconds();
    request.lastFrame = qMax(request.firstFrame, qCeil(end * fps) - 1);
    request.preRenderDirectory = preRenderRoot();
    request.hardwareEncoding = app::Settings::optionSettings()
                                   .value(QStringLiteral("Options/UseHardwareEncoding"), true).toBool();
    auto* job = new render::ExportJob(request, this);
    m_exportJob = job;
    connect(job, &render::ExportJob::progress, this, [this](int done, int total) {
        statusBar()->showMessage(tr("Exporting frame %1 of %2").arg(qMin(done + 1, total)).arg(total));
    });
    connect(job, &render::ExportJob::encoding, this,
            [this] { statusBar()->showMessage(tr("Encoding video...")); });
    connect(job, &render::ExportJob::finished, this, [this, job, path](bool ok, const QString& message) {
        statusBar()->showMessage(message);
        if (ok && m_historyPanel)
            m_historyPanel->addEntry(tr("Export contents to %1").arg(QDir::toNativeSeparators(path)));
        if (ok) notifyExportCompleted();
        job->deleteLater();
        updateProxyPreview();
    });
    updateProxyPreview();   // export reads the originals
    job->start();
}

void MainWindow::exportSnapshot()
{
    if (!m_owner || !m_owner->settings()) {
        return;
    }
    const QString dir = m_owner->settings()->snapshotDirectory();
    if (!QDir().mkpath(dir)) {
        statusBar()->showMessage(tr("Cannot create the snapshot folder: %1").arg(dir));
        return;
    }

    // Numbered after the document, the way the reference names its snapshots,
    // and never overwriting: the button takes no path, so a collision would
    // silently destroy an earlier grab.
    const QString base = m_currentFilePath.isEmpty()
                             ? QStringLiteral("Untitled")
                             : QFileInfo(m_currentFilePath).completeBaseName();
    QString path;
    for (int i = 1; i < 10000; ++i) {
        const QString candidate =
            QDir(dir).filePath(QStringLiteral("%1_%2.png").arg(base).arg(i, 4, 10, QLatin1Char('0')));
        if (!QFileInfo::exists(candidate)) {
            path = candidate;
            break;
        }
    }
    if (path.isEmpty()) {
        statusBar()->showMessage(tr("Snapshot folder is full: %1").arg(dir));
        return;
    }
    exportFrameToFile(path);
}

void MainWindow::applyPresetToSelection(const QString& library, const QString& preset)
{
    if (!m_composition) {
        return;
    }
    statusBar()->showMessage(tr("Applied %1 preset '%2'").arg(library, preset));
    if (m_historyPanel) {
        m_historyPanel->addEntry(tr("Apply %1: %2").arg(library, preset));
    }
    requestRenderFrame();
}

void MainWindow::refreshAfterModelChange()
{
    stopPlayback();
    syncMatchFormat();
    if (!m_composition || !m_mediaManager) {
        return;
    }
    m_timeline->setComposition(m_composition);
    m_timeline->update();
    m_trackPanel->refresh();
    m_layerPanel->setSelection(m_selectedLayer);
    // Loading a project repopulates the media manager, so the Media panel and
    // the Controls inspector have to be rebuilt too - otherwise the timeline
    // shows the project's clips while Media still reports "0 item(s)".
    refreshMediaAndInspector();
    updateTextPanelSelection();
    updateWindowTitle();
    m_playbackTime = 0.0;
    m_timeline->setPlayheadPosition(0.0);
    // VegfxSerializer has already restored the composition's frame rate and
    // duration at this point.  Keep the viewer transport in sync as well:
    // otherwise it retains the ten-second range of the blank startup project.
    m_viewer->setProjectSize(m_composition->displaySize());
    m_viewer->setFrameRate(m_composition->fpsNumerator(),
                           m_composition->fpsDenominator());
    m_viewer->setTimecode(m_playbackTime);
    m_trimmerPanel->setFrameRate(double(m_composition->fpsNumerator())
                                 / qMax(1, m_composition->fpsDenominator()));
    if (m_transportBar) {
        m_transportBar->setFrameRate(m_composition->fpsNumerator(),
                                     m_composition->fpsDenominator());
        m_transportBar->setDuration(m_composition->durationSeconds());
        m_transportBar->setTimecode(m_playbackTime);
    }
    if (m_controlsPanel) {
        m_controlsPanel->setCurrentTime(0.0);
    }
    requestRenderFrame();
}

bool MainWindow::onSaveProject()
{
    if (m_currentFilePath.isEmpty()) {
        return onSaveProjectAs();
    }
    return doSaveProject(m_currentFilePath);
}

// The folder the Open and Save As dialogs start in: the last one a project
// was opened from or saved to, as the reference keeps its RecentFolders.
static QString projectDialogFolder(const QString& currentFile)
{
    if (!currentFile.isEmpty()) return QFileInfo(currentFile).absolutePath();
    const QString last = app::Settings::optionSettings()
                             .value(QStringLiteral("Project/RecentFolder")).toString();
    return !last.isEmpty() && QFileInfo(last).isDir()
        ? last : QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
}

static void rememberProjectFolder(const QString& file)
{
    QSettings settings = app::Settings::optionSettings();
    settings.setValue(QStringLiteral("Project/RecentFolder"), QFileInfo(file).absolutePath());
}

static QString projectFileFilter()
{
    return QCoreApplication::translate("biff::ui::BiffFileFilter", "VEGAS Effects Projects (*.vegfx)");
}

bool MainWindow::onSaveProjectAs()
{
    if (!m_rootComposition || !m_mediaManager) {
        return false;
    }
    const QString name = m_currentFilePath.isEmpty()
        ? (m_rootComposition->name().isEmpty() ? tr(kUntitledDocument) : m_rootComposition->name())
              + QStringLiteral(".vegfx")
        : QFileInfo(m_currentFilePath).fileName();
    QString path = QFileDialog::getSaveFileName(
        this, tr("Save Project"), QDir(projectDialogFolder(m_currentFilePath)).filePath(name),
        projectFileFilter());
    if (path.isEmpty()) {
        return false;
    }
    if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".vegfx");
    return doSaveProject(path);
}

bool MainWindow::doSaveProject(const QString& path)
{
    project::ProjectSaveOptions options;
    const QSettings settings = app::Settings::optionSettings();
    options.useRelativePaths = settings.value(QStringLiteral("Options/UseRelativePaths"), false).toBool();
    if (settings.value(QStringLiteral("Options/IncludeScreenLayout"), false).toBool())
        options.screenLayout = saveState(kLayoutStateVersion);
    storePlayheadInComposition();
    const core::Result r = project::VegfxSerializer::saveToFile(
        path, *m_rootComposition, *m_mediaManager, options);
    if (r.isFailure()) {
        QMessageBox::warning(this, tr("Save Project"), r.message());
        return false;
    }
    m_currentFilePath = path;
    m_projectModified = false;
    // The undo stack's clean state is what tells later edits apart from the
    // saved project; without it an edit after a save went unnoticed.
    m_undoStack->setClean();
    // A manual save supersedes the auto-saves taken since the last one: this
    // session's (an untitled project's included), any earlier ones of this
    // project, and the auto-save it was recovered from.
    for (const QString& file : std::as_const(m_autoSaveFiles)) QFile::remove(file);
    m_autoSaveFiles.clear();
    if (!m_recoveredFrom.isEmpty()) QFile::remove(m_recoveredFrom);
    m_recoveredFrom.clear();
    autosave::clearFor(path);
    const QFileInfo projectFile(path);
    QFile::remove(projectFile.dir().filePath(
        projectFile.completeBaseName() + QStringLiteral(".autosave.vegfx")));
    rememberProjectFolder(path);
    updateWindowTitle();
    noteRecentProject(path);
    statusBar()->showMessage(tr("Saved %1").arg(QDir::toNativeSeparators(path)));
    return true;
}

void MainWindow::storePlayheadInComposition()
{
    // CompositionAsset/<CTI>: the shot reopens where it was left.
    if (!m_composition) return;
    const double fps = double(m_composition->fpsNumerator()) / qMax(1, m_composition->fpsDenominator());
    m_composition->setCurrentFrame(qRound64(m_playbackTime * fps));
    // <OpenCompositeShots>: the tabs, and the one in front.
    if (m_rootComposition) {
        QStringList open;
        for (const auto& shot : m_openCompositions)
            if (shot) open.append(shot->id().value());
        m_rootComposition->setOpenShots(open, m_composition->id().value());
    }
}

std::shared_ptr<composition::Composition> MainWindow::projectShot(const QString& id) const
{
    if (!m_rootComposition || id.isEmpty()) return nullptr;
    if (m_rootComposition->id().value() == id) return m_rootComposition;
    if (auto shot = m_rootComposition->compositeShot(core::Identifier(id))) return shot;
    // A shot only a layer still nests.
    QSet<const composition::Composition*> seen;
    std::function<std::shared_ptr<composition::Composition>(const composition::Composition&)> find =
        [&](const composition::Composition& comp) -> std::shared_ptr<composition::Composition> {
            if (seen.contains(&comp)) return nullptr;
            seen.insert(&comp);
            for (const composition::Layer& layer : comp.layers()) {
                for (const composition::Clip& clip : layer.clips) {
                    if (!clip.nestedComposition) continue;
                    if (clip.nestedComposition->id().value() == id) return clip.nestedComposition;
                    if (auto found = find(*clip.nestedComposition)) return found;
                }
            }
            return nullptr;
        };
    if (auto found = find(*m_rootComposition)) return found;
    for (const auto& shot : m_rootComposition->compositeShots()) {
        if (!shot) continue;
        if (auto found = find(*shot)) return found;
    }
    return nullptr;
}

void MainWindow::openCompositeShot(const std::shared_ptr<composition::Composition>& shot)
{
    if (!shot) return;
    if (!m_openCompositions.contains(shot)) m_openCompositions.append(shot);
    if (m_timeline) m_timeline->showTimelinePage();
    if (shot != m_composition) activateComposition(shot);
    else rebuildCompositionTabs();
}

QVector<std::shared_ptr<composition::Composition>> MainWindow::allProjectShots() const
{
    QVector<std::shared_ptr<composition::Composition>> shots;
    if (!m_rootComposition) return shots;
    shots.append(m_rootComposition);
    for (const auto& shot : m_rootComposition->compositeShots())
        if (shot && !shots.contains(shot)) shots.append(shot);
    return shots;
}

void MainWindow::refreshCompositeShotList()
{
    if (!m_mediaPanel) return;
    QVector<MediaPanel::CompositeShotEntry> entries;
    for (const auto& shot : allProjectShots()) {
        MediaPanel::CompositeShotEntry entry;
        entry.id = shot->id().value();
        entry.name = shot->name();
        entry.size = QSize(shot->width(), shot->height());
        entry.frameRate = double(shot->fpsNumerator()) / qMax(1, shot->fpsDenominator());
        entry.durationSeconds = shot->durationSeconds();
        entry.primary = shot->isPrimary();
        entry.open = m_openCompositions.contains(shot);
        entries.append(entry);
    }
    m_mediaPanel->setCompositeShots(entries);
}

namespace {
// The project's shot list, every shot's layers and the primary flags, taken
// whole: adding or removing a shot - and with it the layers that nest it -
// is one History entry that puts all of it back.
struct ProjectShotsState
{
    // The shot that holds the project (Composition::compositeShots and the
    // project's own data); deleting it hands the project to another shot.
    std::shared_ptr<composition::Composition> root;
    QVector<std::shared_ptr<composition::Composition>> shots;
    QVector<std::shared_ptr<composition::Composition>> touched;
    QVector<QVector<composition::Layer>> layers;
    QVector<bool> primary;
};

ProjectShotsState captureProjectShots(const std::shared_ptr<composition::Composition>& root,
                                      const QVector<std::shared_ptr<composition::Composition>>& extra = {})
{
    ProjectShotsState state;
    state.root = root;
    state.shots = root->compositeShots();
    state.touched.append(root);
    for (const auto& shot : state.shots + extra)
        if (shot && !state.touched.contains(shot)) state.touched.append(shot);
    for (const auto& shot : state.touched) {
        state.layers.append(shot->layers());
        state.primary.append(shot->isPrimary());
    }
    return state;
}

class ProjectShotsCommand : public QUndoCommand
{
public:
    using AdoptRoot = std::function<void(const std::shared_ptr<composition::Composition>&)>;
    ProjectShotsCommand(AdoptRoot adoptRoot, ProjectShotsState before,
                        ProjectShotsState after, const QString& text, std::function<void()> notify)
        : QUndoCommand(text), m_adoptRoot(std::move(adoptRoot)), m_before(std::move(before)),
          m_after(std::move(after)), m_notify(std::move(notify)) {}
    void undo() override { apply(m_before); }
    void redo() override { apply(m_after); }

private:
    void apply(const ProjectShotsState& state)
    {
        m_adoptRoot(state.root);
        state.root->setCompositeShots(state.shots);
        for (int i = 0; i < state.touched.size(); ++i) {
            state.touched.at(i)->setLayers(state.layers.at(i));
            state.touched.at(i)->setPrimary(state.primary.at(i));
        }
        m_notify();
    }
    AdoptRoot m_adoptRoot;
    ProjectShotsState m_before, m_after;
    std::function<void()> m_notify;
};
} // namespace

CompositionSettingsDialog::Values MainWindow::editorTimelineFormat() const
{
    CompositionSettingsDialog::Values format;
    if (!m_rootComposition) return format;
    format = CompositionSettingsDialog::fromComposition(*m_rootComposition);
    const composition::EditorSequence& sequence = m_rootComposition->editorSequence();
    if (sequence.width > 0 && sequence.height > 0) {
        format.width = sequence.width;
        format.height = sequence.height;
    }
    if (sequence.fps > 0.0)
        composition::Composition::frameRateFraction(sequence.fps, &format.fpsNumerator, &format.fpsDenominator);
    return format;
}

void MainWindow::syncMatchFormat()
{
    if (!m_timeline || !m_rootComposition) return;
    const CompositionSettingsDialog::Values format = editorTimelineFormat();
    m_timeline->setMatchFormat(format.width, format.height, format.fpsNumerator, format.fpsDenominator);
}

void MainWindow::newCompositeShot()
{
    if (!m_rootComposition) return;
    // A new shot takes the size and rate of the shot in front and the
    // Options' default duration, under the reference's "Composite Shot %1".
    auto shot = std::make_shared<composition::Composition>();
    const auto& from = m_composition ? *m_composition : *m_rootComposition;
    shot->setSize(from.width(), from.height());
    shot->setFrameRate(from.fpsNumerator(), from.fpsDenominator());
    shot->setPixelAspect(from.pixelAspect(), from.customPixelAspect());
    shot->setAudioSampleRate(from.audioSampleRate());
    shot->setDurationSeconds(app::Settings::compositeShotDefaultDurationSeconds());
    QStringList names;
    for (const auto& existing : allProjectShots()) names.append(existing->name());
    int number = allProjectShots().size() + 1;
    while (names.contains(tr("Composite Shot %1").arg(number))) ++number;
    shot->setName(tr("Composite Shot %1").arg(number));
    // As in the reference, the new shot's properties are asked for first;
    // Cancel makes none.
    {
        CompositionSettingsDialog dialog(CompositionSettingsDialog::fromComposition(*shot),
                                         editorTimelineFormat(), this);
        dialog.setWindowTitle(tr("New Composite Shot"));
        if (dialog.exec() != QDialog::Accepted) return;
        CompositionSettingsDialog::apply(dialog.values(), *shot);
    }

    const ProjectShotsState before = captureProjectShots(m_rootComposition);
    m_rootComposition->addCompositeShot(shot);
    const ProjectShotsState after = captureProjectShots(m_rootComposition);
    m_rootComposition->setCompositeShots(before.shots);
    const QPointer<MainWindow> self(this);
    m_undoStack->push(new ProjectShotsCommand(rootAdopter(), before, after,
                                              tr("New Composite Shot"), [self] {
        if (self) self->projectShotsChanged();
    }));
    openCompositeShot(shot);
    statusBar()->showMessage(tr("Created %1").arg(shot->name()));
}

void MainWindow::removeCompositeShot(const QString& shotId)
{
    const auto shot = projectShot(shotId);
    if (!shot || !m_rootComposition) return;
    // The project lives on its root shot: deleting that one hands the
    // project to the next shot, and the last one stays.
    const bool deletingRoot = shot == m_rootComposition;
    if (deletingRoot && m_rootComposition->compositeShots().isEmpty()) {
        statusBar()->showMessage(tr("%1 is the project's only composite shot and cannot be deleted")
                                     .arg(shot->name()));
        return;
    }
    // The reference's RemoveAssetCmd (FUN_1403ce320): the asset goes with
    // every layer that is an instance of it.
    int instances = 0;
    for (const auto& owner : allProjectShots())
        for (const composition::Layer& layer : owner->layers())
            for (const composition::Clip& clip : layer.clips)
                if (clip.nestedComposition == shot) { ++instances; break; }
    if (instances > 0
        && QMessageBox::question(this, tr("Delete Composite Shot"),
                                 tr("%1 is used by %n layer(s) in other composite shots. "
                                    "Deleting it removes those layers too.\n\nContinue?", nullptr, instances)
                                     .arg(shot->name()),
                                 QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
               != QMessageBox::Yes)
        return;

    const ProjectShotsState before = captureProjectShots(m_rootComposition);
    ProjectShotsState after = before;
    if (deletingRoot) after.root = before.shots.first();
    after.shots.clear();
    for (const auto& kept : before.shots)
        if (kept != shot && kept != after.root) after.shots.append(kept);
    for (int i = 0; i < after.touched.size(); ++i) {
        if (after.touched.at(i) == shot) continue;
        QVector<composition::Layer>& layers = after.layers[i];
        layers.erase(std::remove_if(layers.begin(), layers.end(), [&](const composition::Layer& layer) {
            return std::any_of(layer.clips.cbegin(), layer.clips.cend(), [&](const composition::Clip& clip) {
                return clip.nestedComposition == shot;
            });
        }), layers.end());
    }
    const QPointer<MainWindow> self(this);
    m_undoStack->push(new ProjectShotsCommand(rootAdopter(), before, after, tr("Remove Asset"), [self] {
        if (self) self->projectShotsChanged();
    }));
    statusBar()->showMessage(tr("Deleted %1").arg(shot->name()));
}

void MainWindow::setPrimaryCompositeShot(const QString& shotId, bool primary)
{
    const auto shot = projectShot(shotId);
    if (!shot || !m_rootComposition || shot->isPrimary() == primary) return;
    // FUN_1407582d0: the shot that was primary stops being so first.
    const ProjectShotsState before = captureProjectShots(m_rootComposition, {shot});
    if (primary)
        for (const auto& other : before.touched) other->setPrimary(false);
    shot->setPrimary(primary);
    const ProjectShotsState after = captureProjectShots(m_rootComposition, {shot});
    for (int i = 0; i < before.touched.size(); ++i) before.touched.at(i)->setPrimary(before.primary.at(i));
    const QPointer<MainWindow> self(this);
    m_undoStack->push(new ProjectShotsCommand(rootAdopter(), before, after,
                                              tr("Set Primary Composite Shot"), [self] {
        if (self) self->projectShotsChanged();
    }));
}

void MainWindow::importCompositeShot()
{
    if (!m_rootComposition || !m_mediaManager) return;
    // FUN_1407175e0: the folder the last import came from (RecentFolders /
    // AddCompShot) and the reference's own filter.
    QSettings settings = app::Settings::optionSettings();
    QString folder = settings.value(QStringLiteral("RecentFolders/AddCompShot")).toString();
    if (folder.isEmpty() || !QFileInfo(folder).isDir())
        folder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import Composite Shot"), folder,
        QCoreApplication::translate("biff::ui::BiffFileFilter",
                                    "Composite Shots (*.vegfxcs *.vegfx);;"
                                    "VEGAS Effects Composite Shots (*.vegfxcs);;"
                                    "VEGAS Effects Projects (*.vegfx)"));
    if (path.isEmpty()) return;
    settings.setValue(QStringLiteral("RecentFolders/AddCompShot"), QFileInfo(path).absolutePath());
    importCompositeShotsFrom(path);
}

bool MainWindow::importCompositeShotsFrom(const QString& path)
{
    QVector<project::CompositeShotInfo> offered;
    core::Result result = project::VegfxSerializer::listCompositeShots(path, &offered);
    if (result.isFailure()) {
        QMessageBox::critical(this, tr("Import Composite Shot"), result.message());
        return false;
    }
    if (offered.isEmpty()) {
        QMessageBox::warning(this, tr("Import Composite Shot"),
                             tr("No composite shots can be imported from this file."));
        return false;
    }
    // One shot comes in as it is; from several, the reference asks which.
    QStringList ids;
    if (offered.size() == 1) {
        ids.append(offered.first().id);
    } else {
        ImportCompositionDialog dialog(QFileInfo(path).fileName(), offered, this);
        if (dialog.exec() != QDialog::Accepted) return false;
        ids = dialog.selectedIds();
    }
    QSet<QString> taken;
    for (const auto& shot : allProjectShots()) taken.insert(shot->id().value());
    QVector<std::shared_ptr<composition::Composition>> shots;
    result = project::VegfxSerializer::importCompositeShots(path, ids, taken, m_mediaManager.get(), &shots);
    if (result.isFailure()) {
        QMessageBox::critical(this, tr("Import Composite Shot"), result.message());
        return false;
    }
    // The shots join the project as one History entry; the media they
    // brought stays in Media after an Undo, like any other import.
    const ProjectShotsState before = captureProjectShots(m_rootComposition);
    for (const auto& shot : shots) m_rootComposition->addCompositeShot(shot);
    const ProjectShotsState after = captureProjectShots(m_rootComposition);
    m_rootComposition->setCompositeShots(before.shots);
    const QPointer<MainWindow> self(this);
    m_undoStack->push(new ProjectShotsCommand(rootAdopter(), before, after,
                                              tr("Import Composite Shot"), [self] {
        if (self) self->projectShotsChanged();
    }));
    refreshMediaAndInspector();
    statusBar()->showMessage(tr("Imported %n composite shot(s) from %1", nullptr, int(shots.size()))
                                 .arg(QDir::toNativeSeparators(path)));
    return true;
}

void MainWindow::saveCompositeShotToFile(const QString& shotId)
{
    const auto shot = projectShot(shotId);
    if (!shot || !m_mediaManager) return;
    // FUN_1407137d0 refuses a shot that nests another before asking where.
    for (const composition::Layer& layer : shot->layers()) {
        for (const composition::Clip& clip : layer.clips) {
            if (clip.nestedComposition || clip.nestedCompositionId.isValid()) {
                QMessageBox::critical(this, tr("Save Composite Shot"),
                                      tr("This composite shot cannot be saved because it contains "
                                         "one or more embedded composite shots."));
                return;
            }
        }
    }
    QString path = QFileDialog::getSaveFileName(
        this, tr("Save Composite Shot"),
        QDir(projectDialogFolder(m_currentFilePath)).filePath(shot->name() + QStringLiteral(".vegfxcs")),
        QCoreApplication::translate("biff::ui::BiffFileFilter", "VEGAS Effects Composite Shots (*.vegfxcs)"));
    if (path.isEmpty()) return;
    if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".vegfxcs");
    project::ProjectSaveOptions options;
    options.useRelativePaths = app::Settings::optionSettings()
                                   .value(QStringLiteral("Options/UseRelativePaths"), false).toBool();
    const core::Result result = project::VegfxSerializer::saveCompositeShot(path, *shot, *m_mediaManager, options);
    if (result.isFailure()) {
        QMessageBox::critical(this, tr("Save Composite Shot"),
                              tr("The composite shot could not be saved to %1")
                                  .arg(QDir::toNativeSeparators(path)));
        return;
    }
    statusBar()->showMessage(tr("Saved %1 to %2").arg(shot->name(), QDir::toNativeSeparators(path)));
}

void MainWindow::addDroppedMediaLayer(const QString& filePath, int above, double seconds)
{
    if (!m_composition || !m_mediaManager || filePath.isEmpty()) return;
    if (!m_mediaManager->assetByFilePath(filePath).isValid()
        && m_mediaManager->importFile(filePath).isFailure()) {
        statusBar()->showMessage(tr("Could not open %1").arg(filePath));
        return;
    }
    const media::MediaAsset asset = m_mediaManager->assetByFilePath(filePath);
    if (asset.kind() == media::MediaKind::Model3D) {
        statusBar()->showMessage(tr("Use Import to place a 3D model"));
        return;
    }
    offerCompositionMatch(asset);
    // The asset's own range: its Trimmer in/out points when it has them,
    // else all of it; a still lasts to the end of the shot.
    const double fps = m_composition->fpsDenominator() > 0
        ? double(m_composition->fpsNumerator()) / m_composition->fpsDenominator() : 30.0;
    composition::Clip clip;
    clip.mediaId = asset.id();
    clip.startSeconds = seconds;
    double length = asset.durationSeconds();
    if (asset.trimOutPoint() > asset.trimInPoint()) {
        clip.sourceStartSeconds = asset.trimInPoint() / fps;
        length = (asset.trimOutPoint() - asset.trimInPoint()) / fps;
    }
    if (length <= 0.0) length = qMax(1.0 / fps, m_composition->durationSeconds() - seconds);
    clip.durationSeconds = length;
    composition::Layer layer;
    layer.name = asset.fileName();
    layer.kind = composition::LayerKind::Media;
    layer.clips.append(clip);
    const QString text = tr("Add layer '%1'").arg(layer.name);
    m_undoStack->push(new AddLayerCommand(this, m_composition.get(), layer, text, qMax(0, above)));
    statusBar()->showMessage(tr("Added layer '%1'").arg(layer.name));
}

void MainWindow::addDroppedShotLayer(const QString& shotId, int above, double seconds)
{
    const auto shot = projectShot(shotId);
    if (!m_composition || !shot) return;
    // A shot cannot show itself, directly or through the shots it nests.
    std::function<bool(const composition::Composition&)> contains = [&](const composition::Composition& outer) {
        for (const composition::Layer& layer : outer.layers())
            for (const composition::Clip& clip : layer.clips)
                if (clip.nestedComposition
                    && (clip.nestedComposition == m_composition || contains(*clip.nestedComposition)))
                    return true;
        return false;
    };
    if (shot == m_composition || contains(*shot)) {
        statusBar()->showMessage(tr("%1 cannot be placed inside itself").arg(shot->name()));
        return;
    }
    composition::Clip clip;
    clip.mediaId = core::Identifier(QStringLiteral("composition:") + shot->id().value());
    clip.nestedCompositionId = shot->id();
    clip.nestedComposition = shot;
    clip.startSeconds = seconds;
    clip.durationSeconds = shot->durationSeconds();
    composition::Layer layer;
    layer.name = shot->name();
    layer.kind = composition::LayerKind::Media;
    layer.clips.append(clip);
    const QString text = tr("Add layer '%1'").arg(layer.name);
    m_undoStack->push(new AddLayerCommand(this, m_composition.get(), layer, text, qMax(0, above)));
    statusBar()->showMessage(tr("Added layer '%1'").arg(layer.name));
}

std::function<void(const std::shared_ptr<composition::Composition>&)> MainWindow::rootAdopter()
{
    const QPointer<MainWindow> self(this);
    return [self](const std::shared_ptr<composition::Composition>& root) {
        if (self) self->adoptProjectRoot(root);
    };
}

void MainWindow::adoptProjectRoot(const std::shared_ptr<composition::Composition>& root)
{
    if (!root || root == m_rootComposition || !m_rootComposition) return;
    const std::shared_ptr<composition::Composition> previous = m_rootComposition;
    // The project's own data moves with it; the shot left behind is a
    // plain shot again (or gone, until an Undo brings it back).
    root->setProjectId(previous->projectId());
    root->projectSettings() = previous->projectSettings();
    root->setNativeSource(previous->nativeSource());
    root->editorSequence() = previous->editorSequence();
    root->setOpenShots(previous->openShotIds(), previous->activeShotId());
    previous->setCompositeShots({});
    previous->setNativeSource(nullptr);
    m_rootComposition = root;
}

void MainWindow::projectShotsChanged()
{
    // Tabs of shots that are gone close; the panels move to the root when
    // the shot in front went.
    const QVector<std::shared_ptr<composition::Composition>> shots = allProjectShots();
    const auto inProject = [&](const std::shared_ptr<composition::Composition>& shot) {
        return shots.contains(shot) || projectShot(shot->id().value()) == shot;
    };
    const bool frontGone = m_composition && !inProject(m_composition);
    m_openCompositions.erase(std::remove_if(m_openCompositions.begin(), m_openCompositions.end(),
                                            [&](const auto& shot) { return !shot || !inProject(shot); }),
                             m_openCompositions.end());
    if (m_openCompositions.isEmpty()) m_openCompositions.append(m_rootComposition);
    if (frontGone) {
        activateComposition(m_openCompositions.first());
    } else {
        m_timeline->setComposition(m_composition);
        if (m_trackPanel) m_trackPanel->refresh();
        if (m_layerPanel) m_layerPanel->refresh();
        rebuildCompositionTabs();
        refreshCompositeShotList();
        requestRenderFrame();
    }
    m_projectModified = true;
    updateWindowTitle();
}

void MainWindow::onNewProject()
{
    if (m_projectModified && !confirmDiscard()) {
        return;
    }
    if (!m_composition || !m_mediaManager) {
        return;
    }
    stopPlayback();
    if (m_rootComposition != m_composition) activateComposition(m_rootComposition);
    m_undoStack->clear();
    m_rootComposition->clear();
    m_rootComposition->setName(QStringLiteral("Untitled"));
    app::applyNewProjectDefaults(*m_rootComposition);
    m_openCompositions = {m_rootComposition};
    m_mediaManager->clear();
    // Nothing left to read frames from; let the open files go.
    m_videoDecoders.clear();
    m_videoDecoderOrder.clear();
    m_currentFilePath.clear();
    m_recoveredFrom.clear();
    m_autoSaveFiles.clear();
    m_projectModified = false;
    rebuildCompositionTabs();
    refreshAfterModelChange();
    if (m_timeline) m_timeline->showTimelinePage();
    // "Prompt me for the project settings to use before creating a new
    // project": the reference's New Project Settings (FUN_1402b3b30).
    const QSettings newProjectSettings(QSettings::IniFormat, QSettings::UserScope,
                                       app::Settings::organizationName(), app::Settings::applicationName());
    if (newProjectSettings.value(QStringLiteral("Options/Prompts/ShowProjectSettings"), true).toBool())
        editProjectSettings(true);
}

void MainWindow::editProjectSettings(bool newProject)
{
    if (!m_rootComposition) return;
    ProjectSettingsDialog dialog(ProjectSettingsDialog::fromComposition(*m_rootComposition),
                                 newProject, this);
    if (dialog.exec() != QDialog::Accepted) return;
    const ProjectSettingsDialog::Values before = ProjectSettingsDialog::fromComposition(*m_rootComposition);
    const ProjectSettingsDialog::Values after = dialog.values();
    ProjectSettingsDialog::apply(after, *m_rootComposition);
    syncMatchFormat();
    if (after.render != before.render || after.width != before.width || after.height != before.height
        || !qFuzzyCompare(after.fps, before.fps) || after.sampleRate != before.sampleRate
        || !qFuzzyCompare(after.durationSeconds, before.durationSeconds)) {
        // A new project's settings are part of creating it, not an edit.
        if (!newProject) {
            m_projectModified = true;
            updateWindowTitle();
        }
        requestRenderFrame();
    }
}

void MainWindow::onOpenProject()
{
    if (m_projectModified && !confirmDiscard()) {
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open Project"), projectDialogFolder(m_currentFilePath), projectFileFilter());
    if (path.isEmpty()) {
        return;
    }
    rememberProjectFolder(path);
    openProjectFile(path);
}

bool MainWindow::openProject(const QString& path)
{
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        statusBar()->showMessage(tr("Could not open %1").arg(path));
        return false;
    }
    return openProjectFile(path);
}

bool MainWindow::openProjectFile(const QString& path)
{
    stopPlayback();
    // The decoders belong to the outgoing project's media.
    m_videoDecoders.clear();
    m_videoDecoderOrder.clear();
    if (m_rootComposition != m_composition) activateComposition(m_rootComposition);
    QByteArray screenLayout;
    const core::Result r = project::VegfxSerializer::loadFromFile(
        path, m_rootComposition.get(), m_mediaManager.get(), &screenLayout);
    if (r.isFailure()) {
        QMessageBox::warning(this, tr("Open Project"), r.message());
        return false;
    }
    m_currentFilePath = path;
    m_recoveredFrom.clear();
    m_autoSaveFiles.clear();
    m_projectModified = false;
    m_composition = m_rootComposition;
    m_undoStack->clear();
    if (m_renderManager) m_renderManager->setComposition(m_composition);
    resetCompositionTabs();
    refreshAfterModelChange();
    if (m_timeline) m_timeline->showTimelinePage();
    // The shot that was in front when the project was saved.
    if (const auto active = projectShot(m_rootComposition->activeShotId());
        active && active != m_composition && m_openCompositions.contains(active))
        activateComposition(active);
    // Back where the shot's playhead was when it was saved (<CTI>).
    {
        const double fps = double(m_composition->fpsNumerator()) / qMax(1, m_composition->fpsDenominator());
        const double seconds = qBound(0.0, m_composition->currentFrame() / qMax(1.0, fps),
                                      m_composition->durationSeconds());
        m_playbackTime = seconds;
        m_timeline->setPlayheadPosition(seconds);
        if (m_controlsPanel) m_controlsPanel->setCurrentTime(seconds);
        m_viewer->setTimecode(seconds);
        requestRenderFrame();
    }
    noteRecentProject(path);
    statusBar()->showMessage(tr("Opened %1").arg(QDir::toNativeSeparators(path)));
    if (!screenLayout.isEmpty()) {
        restoreState(screenLayout, kLayoutStateVersion);
        giveBottomDockTheCorners(this);
        installDockTabMenus(this);
        applyInterfacePreferences();
    }
    QStringList offlineMedia;
    for (const auto& asset : m_mediaManager->assets()) {
        if (!QFileInfo::exists(asset.sourcePath())) offlineMedia.append(asset.sourcePath());
    }
    if (!offlineMedia.isEmpty()) {
        QMessageBox::warning(this, tr("Missing Media"),
            tr("These project files could not be found:\n%1\n\n"
               "In the Media panel, right-click each missing file and choose "
               "Relink Media... to locate it. Audio from missing files cannot play.")
                .arg(offlineMedia.join(QLatin1Char('\n'))));
    }
    reportMissingPlugins();
    return true;
}

void MainWindow::reportMissingPlugins()
{
    if (!m_composition || !m_owner || !m_owner->pluginManager()) {
        return;
    }
    plugin::PluginManager* plugins = m_owner->pluginManager();

    // Collected in encounter order and de-duplicated, so a project using one
    // absent effect on twenty clips reports it once.
    QStringList missing;
    for (const composition::Layer& layer : m_composition->layers()) {
        for (const composition::Clip& clip : layer.clips) {
            for (const composition::Effect& effect : clip.effects) {
                const QString id = effect.pluginId.value();
                if (id.isEmpty() || missing.contains(id)) {
                    continue;
                }
                // The built-in text effect is registered like any other, so it
                // resolves normally; the guard stays only for projects written
                // before it had a spec.
                if (id == QLatin1String("text")) {
                    continue;
                }
                if (!plugins->spec(effect.pluginId).id.isValid()) {
                    missing.append(id);
                }
            }
        }
    }
    if (missing.isEmpty()) {
        return;
    }

    // Newlines spelled as QChar(0x0A): the source stays free of escapes, which
    // is what this file already does for the star glyphs.
    const QString newline = QString(QChar(0x0A));
    const QString list = missing.join(newline);
    OV_LOG_WARN(QStringLiteral("Missing plugins for %1: %2")
                    .arg(QFileInfo(m_currentFilePath).fileName(),
                         missing.join(QLatin1String(", "))));
    statusBar()->showMessage(
        tr("%n plugin(s) used by this project are missing", "", missing.size()));
    QMessageBox::warning(this, tr("Open Project"),
                         tr("These plugins could not be found:") + newline + list
                             + newline + newline
                             + tr("Effects that use them will have no result."));
}

void MainWindow::onRecentProjectActivated(const QString& path)
{
    if (m_projectModified && !confirmDiscard()) {
        return;
    }
    if (!QFileInfo::exists(path)) {
        QMessageBox::warning(this, tr("Open Project"),
                             tr("The project file no longer exists:\n%1").arg(QDir::toNativeSeparators(path)));
        return;
    }
    openProjectFile(path);
}

// Mirrors the reference "RecentProjects" list feeding listViewRecentProjects.
void MainWindow::noteRecentProject(const QString& path)
{
    if (!m_startPanel) {
        return;
    }
    m_startPanel->addRecentProject(path);
    if (m_owner && m_owner->settings()) {
        m_owner->settings()->setRecentProjects(m_startPanel->recentProjects());
    }
}

// Reference window title: the document name first, an asterisk while the
// project has unsaved changes, then the application name. A saved project shows
// its file name with extension; a new one shows "Untitled Project".
void MainWindow::updateWindowTitle()
{
    if (m_exportPanel && m_composition) m_exportPanel->setOutputName(m_composition->name());
    QString document = m_currentFilePath.isEmpty()
                           ? tr(kUntitledDocument)
                           : QFileInfo(m_currentFilePath).fileName();
    // A project brought back from an auto-save says so until it is saved.
    if (!m_recoveredFrom.isEmpty()) {
        document = m_currentFilePath.isEmpty()
            ? QCoreApplication::translate("biff::ui::MainAppWindow", "Recovered Untitled Project")
            : document + QCoreApplication::translate("biff::ui::MainAppWindow", " - Recovered");
    }
    // The Save icon is dark while there is nothing new to save, and shows
    // where the project lives when hovered (Quick Start 2.5).
    if (m_ui && m_ui->actionSave) {
        m_ui->actionSave->setEnabled(m_projectModified || !m_recoveredFrom.isEmpty());
        m_ui->actionSave->setToolTip(m_currentFilePath.isEmpty()
            ? tr("Save Project")
            : tr("Save Project (%1)").arg(QDir::toNativeSeparators(m_currentFilePath)));
    }
    // The reference carries the modified marker as Qt's "[*]" placeholder - its
    // string pool holds both "Untitled Project" and "Untitled Project[*]" - and
    // lets setWindowModified() expand it. Following suit means the marker also
    // reaches the taskbar entry and the platform close-confirmation instead of
    // only the caption, which is all the hand-rolled asterisk ever reached.
    setWindowTitle(tr("%1[*] - %2").arg(document, QString::fromLatin1(kAppTitle)));
    setWindowModified(m_projectModified);
}

// Activate a page even after its dock has been moved or detached.
void MainWindow::showCenterTab(QWidget* page)
{
    if (!page) return;
    if (auto* dock = qobject_cast<QDockWidget*>(page->parentWidget())) {
        dock->show();
        dock->raise();
    }
}

void MainWindow::showLearnSidebar()
{
    if (m_learnSidebar) {
        m_learnSidebar->show();
        m_learnSidebar->raise();
    }
}

// --- Layout panel -------------------------------------------------------
//
// The panel works in canvas pixels with the origin at the top left, which is
// the space the renderer composites in: a layer is drawn centred on
// (W/2 + position.x, H/2 - position.y) and scaled by its scale percentage. The
// two conversions below are that formula and its inverse, and everything the
// panel does - typing a number, mirroring, aligning - goes through them.

QRectF MainWindow::selectedLayerBounds() const
{
    return layerBounds(m_selectedLayer);
}

QRectF MainWindow::layerBounds(int layerIndex) const
{
    if (!m_composition || layerIndex < 0
        || layerIndex >= m_composition->layers().size()) {
        return QRectF();
    }
    const composition::Layer& layer = m_composition->layers().at(layerIndex);
    const double fps = m_composition->fpsDenominator() > 0
                           ? double(m_composition->fpsNumerator()) / m_composition->fpsDenominator()
                           : 30.0;
    const int frame = int(m_playbackTime * fps + 0.5);
    const QPointF scale = layer.transform.scaleAt(frame);
    const QPointF position = layer.transform.positionAt(frame);
    // The size is taken from the magnitude: a mirrored layer has a negative
    // scale, and a box with a negative width is not a box.
    const double w = m_composition->displaySize().width() * qAbs(scale.x()) / 100.0;
    const double h = m_composition->displaySize().height() * qAbs(scale.y()) / 100.0;
    const QPointF centre(m_composition->displaySize().width() / 2.0 + position.x(),
                         m_composition->displaySize().height() / 2.0 - position.y());
    return QRectF(centre.x() - w / 2.0, centre.y() - h / 2.0, w, h);
}

void MainWindow::updateLayoutPanelSelection()
{
    if (!m_layoutPanel) {
        return;
    }
    if (m_composition) {
        m_layoutPanel->setFrameRect(QRectF(0, 0, m_composition->displaySize().width(), m_composition->displaySize().height()));
    }
    QVector<int> selected = m_selectedLayers;
    if (selected.isEmpty() && m_selectedLayer >= 0) selected = {m_selectedLayer};
    QVector<QRectF> bounds;
    for (int layerIndex : selected) {
        const QRectF item = layerBounds(layerIndex);
        if (!item.isEmpty()) bounds.append(item);
    }
    if (bounds.isEmpty()) {
        m_layoutPanel->clearSelection();
    } else {
        m_layoutPanel->setSelectionBounds(bounds);
    }
}

void MainWindow::onLayoutBoundsEdited(const QRectF& bounds)
{
    onLayoutSelectionBoundsEdited({bounds});
}

void MainWindow::activateComposition(
    const std::shared_ptr<composition::Composition>& composition)
{
    if (!composition) return;
    stopPlayback();
    // Each shot keeps its own playhead (<CTI>), as the reference's tabs do.
    if (m_composition && m_composition != composition) storePlayheadInComposition();
    if (m_renderManager) {
        m_renderManager->cancelPlaybackCache();
        m_renderManager->setComposition(composition);
    }
    m_composition = composition;
    m_selectedLayer = -1;
    m_selectedClip = -1;
    m_selectedLayers.clear();
    {
        const double fps = double(m_composition->fpsNumerator()) / qMax(1, m_composition->fpsDenominator());
        m_playbackTime = qBound(0.0, m_composition->currentFrame() / qMax(1.0, fps),
                                m_composition->durationSeconds());
    }
    m_timeline->setComposition(m_composition);
    m_controlsPanel->setComposition(m_composition);
    m_controlsPanel->setSelection(-1, -1);
    m_trackPanel->bindModel(m_composition);
    m_trackPanel->setSelectedLayer(-1);
    m_layerPanel->bindModel(m_composition);
    m_layerPanel->setUndoStack(m_undoStack);
    m_layerPanel->setMediaManager(m_mediaManager);
    m_layerPanel->setSelection(-1);
    m_viewer->setProjectSize(m_composition->displaySize());
    m_viewer->setFrameRate(m_composition->fpsNumerator(), m_composition->fpsDenominator());
    m_trimmerPanel->setFrameRate(double(m_composition->fpsNumerator())
                                 / qMax(1, m_composition->fpsDenominator()));
    if (m_transportBar) {
        m_transportBar->setFrameRate(m_composition->fpsNumerator(),
                                     m_composition->fpsDenominator());
        m_transportBar->setDuration(m_composition->durationSeconds());
        m_transportBar->setTimecode(m_playbackTime);
    }
    rebuildCompositionTabs();
    m_timeline->setPlayheadPosition(m_playbackTime);
    if (m_controlsPanel) m_controlsPanel->setCurrentTime(m_playbackTime);
    m_viewer->setTimecode(m_playbackTime);
    updateTextPanelSelection();
    updateLayoutPanelSelection();
    refreshCompositeShotList();
    requestRenderFrame();
}

void MainWindow::rebuildCompositionTabs()
{
    if (!m_timeline) return;
    QStringList names;
    int current = -1;
    for (int i = 0; i < m_openCompositions.size(); ++i) {
        const auto& comp = m_openCompositions.at(i);
        names.append(comp ? comp->name() : QString());
        if (comp == m_composition) current = i;
    }
    m_timeline->setCompositionTabs(names, current);
}

void MainWindow::resetCompositionTabs()
{
    m_openCompositions.clear();
    if (!m_rootComposition) return;
    // The tabs the project was saved with (<OpenCompositeShots>).
    for (const QString& id : m_rootComposition->openShotIds()) {
        const auto shot = projectShot(id);
        if (shot && !m_openCompositions.contains(shot)) m_openCompositions.append(shot);
    }
    if (m_openCompositions.isEmpty()) m_openCompositions.append(m_rootComposition);
    rebuildCompositionTabs();
}

void MainWindow::editLayoutTransforms(
    const QString& title,
    const std::function<void(int, composition::LayerTransform&, int)>& edit)
{
    if (!m_composition) return;
    QVector<int> selected = m_selectedLayers;
    if (selected.isEmpty() && m_selectedLayer >= 0) selected = {m_selectedLayer};
    const double fps = double(m_composition->fpsNumerator()) / qMax(1, m_composition->fpsDenominator());
    const int frame = qRound(m_playbackTime * fps);
    QVector<LayoutTransformChange> changes;
    for (int index : selected) {
        if (index < 0 || index >= m_composition->layers().size()) continue;
        const auto& layer = m_composition->layers()[index];
        if (layer.locked) continue;
        LayoutTransformChange change{layer.id, layer.transform, layer.transform};
        edit(index, change.after, frame);
        changes.append(std::move(change));
    }
    if (changes.isEmpty()) return;
    m_undoStack->push(new LayoutTransformCommand(m_composition, std::move(changes), [this] {
        m_projectModified = true;
        updateWindowTitle();
        m_timeline->refreshKeyFrames();
        m_controlsPanel->refresh();
        updateLayoutPanelSelection();
        requestRenderFrame();
    }, title));
}

void MainWindow::onLayoutSelectionBoundsEdited(const QVector<QRectF>& bounds)
{
    if (!m_composition) return;
    QVector<int> selected = m_selectedLayers;
    if (selected.isEmpty() && m_selectedLayer >= 0) selected = {m_selectedLayer};
    if (selected.size() != bounds.size()) return;
    for (const auto& box : bounds) if (box.isEmpty()) return;
    const QSizeF size = m_composition->displaySize();
    editLayoutTransforms(m_layoutPanel->windowTitle(), [selected, bounds, size](int index,
                           composition::LayerTransform& transform, int frame) {
        const QRectF box = bounds.at(selected.indexOf(index));
        const QPointF scale = transform.scaleAt(frame);
        const QPointF resized(100.0 * box.width() / size.width() * (scale.x() < 0 ? -1 : 1),
                              100.0 * box.height() / size.height() * (scale.y() < 0 ? -1 : 1));
        writeLayoutPoint(transform.scaleXCurve, transform.scaleYCurve, frame,
                         resized, transform.scalePercent);
        writeLayoutPoint(transform.positionXCurve, transform.positionYCurve, frame,
                         QPointF(box.center().x() - size.width() / 2.0,
                                 size.height() / 2.0 - box.center().y()), transform.position);
    });
}

void MainWindow::onLayoutMirror(Qt::Orientation orientation)
{
    const QString title = orientation == Qt::Vertical ? tr("Mirror Vertical") : tr("Mirror Horizontal");
    editLayoutTransforms(title, [orientation](int, composition::LayerTransform& transform, int frame) {
        QPointF scale = transform.scaleAt(frame);
        // Vertical is the mirror axis: exchange left/right (negate X).
        if (orientation == Qt::Vertical) scale.setX(-scale.x());
        else scale.setY(-scale.y());
        writeLayoutPoint(transform.scaleXCurve, transform.scaleYCurve, frame,
                         scale, transform.scalePercent);
    });
}

void MainWindow::onLayoutRotate(int degrees)
{
    const QString title = degrees > 0 ? tr("Rotate 90 Degrees Clockwise")
                                      : tr("Rotate 90 Degrees Counter Clockwise");
    editLayoutTransforms(title, [degrees](int, composition::LayerTransform& transform, int frame) {
        writeLayoutValue(transform.rotationCurve, frame, transform.rotationAt(frame) + degrees,
                         transform.rotationDegrees);
    });
}

bool MainWindow::confirmDiscard()
{
    const QMessageBox::StandardButton btn = QMessageBox::question(
        this, QStringLiteral("Unsaved Changes"),
        QStringLiteral("The current project has unsaved changes. Discard them?"),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    return btn == QMessageBox::Yes;
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (m_projectModified && !confirmDiscard()) {
        event->ignore();
        return;
    }
    if (m_renderManager) {
        m_renderManager->cancelAll();
    }
    // Persist window geometry/dock layout before shutdown (QSettings "MainWindow").
    saveWindowGeometry();
    QMainWindow::closeEvent(event);
}

double MainWindow::prepareAudioSource()
{
    m_audioClipStart = -1.0;
    if (!m_audio || !m_composition || !m_mediaManager) {
        return -1.0;
    }

    QVector<media::AudioClip> sources;
    QVector<media::AudioTransition> transitions;
    QSet<const composition::Composition*> visited;
    // Linear gain by timeline second of everything a shot plays: the levels
    // of the layers and clips it is nested through. `animated` stays false
    // while all of them are constant, so plain clips keep one gain value.
    struct Gain {
        std::function<double(double)> at;
        bool animated = false;
    };
    std::function<void(const composition::Composition&, double, double, double, double, const Gain&)> collect;
    collect = [&](const composition::Composition& shot, double origin, double rate,
                  double firstAllowed, double lastAllowed, const Gain& outer) {
        if (visited.contains(&shot)) return;
        visited.insert(&shot);
        const double shotFps = shot.fpsDenominator() > 0
            ? double(shot.fpsNumerator()) / shot.fpsDenominator() : 30.0;
        for (const auto& layer : shot.layers()) {
            if (!layer.visible || layer.muted) continue;
            // Audio transitions play both clips through their window, so the
            // clips they join are extended into their handles here.
            const auto windows = composition::transitionWindows(
                layer, 0.5 / shotFps, [](const composition::Effect& effect) {
                    return plugin::isAudioTransition(effect.pluginId);
                });
            QHash<int, int> sourceOfClip;
            for (int clipIndex = 0; clipIndex < layer.clips.size(); ++clipIndex) {
                const auto& clip = layer.clips.at(clipIndex);
                const double speed = clip.speed > 0 ? clip.speed : 1;
                double clipStart = clip.startSeconds, clipEnd = clip.endSeconds();
                if (!clip.nestedComposition) {
                    for (const auto& window : windows) {
                        if (window.toClip == clipIndex) clipStart = qMin(clipStart, window.start);
                        if (window.fromClip == clipIndex) clipEnd = qMax(clipEnd, window.end);
                    }
                }
                double first = qMax(firstAllowed, origin + clipStart / rate);
                const double last = qMin(lastAllowed, origin + clipEnd / rate);
                if (last <= first) continue;
                // Audio > Level of the layer (keys in this shot's frames)
                // plus the clip's own level, on top of the outer shots'.
                Gain gain;
                gain.animated = outer.animated || !layer.transform.audioLevelCurve.isEmpty();
                gain.at = [outerAt = outer.at, staticDb = layer.transform.audioLevel,
                           curve = layer.transform.audioLevelCurve, clipDb = clip.audioLevel,
                           origin, rate, shotFps](double t) {
                    const double layerDb = composition::audioLevelAtSeconds(
                        staticDb, curve, (t - origin) * rate, shotFps);
                    return outerAt(t) * qPow(10.0, (layerDb + clipDb) / 20.0);
                };
                if (clip.nestedComposition) {
                    const double childOrigin = origin + clip.startSeconds / rate
                        - clip.sourceStartSeconds / (rate * speed);
                    collect(*clip.nestedComposition, childOrigin, rate * speed, first, last, gain);
                    continue;
                }
                const auto asset = m_mediaManager->assetById(clip.mediaId);
                if (asset.kind() != media::MediaKind::Audio && asset.kind() != media::MediaKind::Video) continue;
                if (asset.filePath().isEmpty() || asset.isImageSequence()) continue; // stills are silent
                double source = clip.sourceStartSeconds
                    + (first - origin - clip.startSeconds / rate) * rate * speed;
                if (source < 0.0) {
                    // No material before the in-point: the handle is silence.
                    first -= source / (rate * speed);
                    source = 0.0;
                    if (last <= first) continue;
                }
                media::AudioClip audioClip {asset.filePath(), first, last,
                                            source, speed * rate,
                                            gain.animated ? 1.0 : gain.at(first)};
                audioClip.audioStreamIndex = asset.audioStreamIndex();
                if (gain.animated) {
                    // 10 ms samples; the mixer interpolates between them.
                    constexpr double step = 0.01;
                    const qsizetype count = qsizetype(std::ceil((last - first) / step)) + 1;
                    audioClip.envelope.reserve(count);
                    for (qsizetype n = 0; n < count; ++n)
                        audioClip.envelope.append(float(gain.at(first + double(n) * step)));
                    audioClip.envelopeStart = first;
                    audioClip.envelopeStep = step;
                }
                const plugin::PluginManager* plugins =
                    m_owner ? m_owner->pluginManager() : nullptr;
                if (plugins) {
                    const int parameterFrame = qRound((first - origin) * rate * shotFps);
                    for (const composition::Effect& effect : clip.effects) {
                        if (!effect.enabled
                            || !plugin::nativeAudioEffectRenderingVerified(effect.pluginId)) {
                            continue;
                        }
                        const plugin::EffectSpec effectSpec = plugins->spec(effect.pluginId);
                        media::NativeAudioModule module;
                        module.pluginId = effect.pluginId;
                        module.instanceKey = QUuid::createUuid().toString(
                            QUuid::WithoutBraces);
                        module.sourceEffect = effect;
                        module.shotOrigin = origin;
                        module.shotRate = rate;
                        module.shotFps = shotFps;
                        module.layerStart = origin + clip.startSeconds / rate;
                        module.layerDuration = (clip.endSeconds() - clip.startSeconds) / rate;
                        for (int parameter = 0;
                             parameter < effectSpec.parameters.size(); ++parameter) {
                            const QVariant value = effect.parameterAt(parameter, parameterFrame);
                            module.parameters.append(value.isValid()
                                ? value.toString()
                                : effectSpec.parameters.at(parameter).defaultValue);
                        }
                        audioClip.nativeEffects.append(std::move(module));
                    }
                }
                sourceOfClip.insert(clipIndex, sources.size());
                sources.append(std::move(audioClip));
            }
            for (const auto& window : windows) {
                const composition::Effect& effect =
                    layer.clips.at(window.ownerClip).effects.at(window.effectIndex);
                media::AudioTransition transition;
                transition.fromClip = sourceOfClip.value(window.fromClip, -1);
                transition.toClip = sourceOfClip.value(window.toClip, -1);
                transition.start = origin + window.start / rate;
                transition.end = origin + window.end / rate;
                transition.cut = origin + window.cut / rate;
                transition.pluginId = effect.pluginId;
                const int frame = qRound(window.start * shotFps);
                for (int i = 0; i < effect.parameterValues.size(); ++i) {
                    transition.parameters.append(effect.parameterAt(i, frame).toString());
                }
                if (transition.end > firstAllowed && transition.start < lastAllowed) {
                    transitions.append(transition);
                }
            }
        }
        visited.remove(&shot);
    };
    collect(*m_composition, 0, 1, 0, m_composition->durationSeconds(),
            Gain{[](double) { return 1.0; }, false});
    m_audio->setClips(sources, m_composition->durationSeconds(), transitions);
    m_audioClipStart = sources.isEmpty() ? -1 : 0;
    return m_audioClipStart;
}

void MainWindow::beginPlayback()
{
    m_audioScrubTimer.stop();
    m_playing = true;
    updatePlayIcon();
    m_playbackTimer.start();
    // Start the audio at the same point on the timeline the playhead is at.
    const double clipStart = prepareAudioSource();
    if (m_audio && clipStart >= 0.0) {
        m_audio->play(m_playbackTime - clipStart);
    }
    if (m_transportBar) {
        m_transportBar->setPlaying(true);
    }
    statusBar()->showMessage(tr("Playing"));
}

void MainWindow::stopPlayback()
{
    m_playing = false;
    updatePlayIcon();
    m_playbackTimer.stop();
    if (m_audio) {
        m_audio->stop();
    }
    if (m_transportBar) {
        m_transportBar->setPlaying(false);
    }
    statusBar()->showMessage(tr("Stopped"));
}

void MainWindow::onPlayPause()
{
    if (m_playing) {
        m_playbackTimer.stop();
        m_playing = false;
        updatePlayIcon();
        if (m_audio) {
            m_audio->pause();
        }
        statusBar()->showMessage(tr("Paused"));
    } else {
        beginPlayback();
    }
}

void MainWindow::onStop()
{
    stopPlayback();
    m_playbackTime = 0.0;
    m_timeline->setPlayheadPosition(0.0);
    if (m_controlsPanel) {
        m_controlsPanel->setCurrentTime(0.0);
    }
    requestRenderFrame();
}

void MainWindow::onTransportTick()
{
    if (!m_composition) {
        stopPlayback();
        return;
    }

    if (m_audio && m_audio->isPlaying()) m_playbackTime = m_audio->position();
    else m_playbackTime += static_cast<double>(m_playbackTimer.interval()) / 1000.0;

    const bool looping = m_loopPlayback && m_loopPlayback->isChecked();
    double endTime = m_composition->durationSeconds();
    double startTime = 0.0;
    if (looping && m_inPoint >= 0.0) {
        startTime = m_inPoint;
    }
    if (looping && m_outPoint >= 0.0) {
        endTime = m_outPoint;
    }
    if (m_playbackTime >= endTime) {
        if (!looping) {
            // Without the toggle the transport has to stop at the end. It used
            // to wrap regardless, so playback ran forever and the loop flag
            // only decided whether in/out bounded the range.
            m_playbackTime = endTime;
            stopPlayback();
            m_timeline->setPlayheadPosition(m_playbackTime);
            if (m_transportBar) {
                m_transportBar->setTimecode(m_playbackTime);
            }
            requestRenderFrame();
            return;
        }
        m_playbackTime = startTime;
        if (m_audio) m_audio->seek(startTime);
    }
    if (m_playbackTime < startTime) {
        m_playbackTime = startTime;
    }
    m_timeline->setPlayheadPosition(m_playbackTime);
    if (m_controlsPanel) {
        m_controlsPanel->setCurrentTime(m_playbackTime);
    }
    if (m_viewer) {
        m_viewer->setTimecode(m_playbackTime);
    }
    if (m_transportBar) {
        m_transportBar->setTimecode(m_playbackTime);
    }
    // Reference "Update the viewer during playback" (checkBoxEnablePlaybackUpdate).
    // With it off the transport still runs and the readouts still move; only the
    // picture is left alone, which is what makes playback smooth on a project
    // the renderer cannot keep up with.
    if (app::Settings::enablePlaybackUpdate()) {
        requestRenderFrame();
    }
}

namespace {
// Open video files kept at once. Each decoder holds a pipeline and its frame
// buffers, so this trades memory for scrub speed; four covers a handful of
// layers stacked at the same point on the timeline.
constexpr int kMaxOpenVideoDecoders = 4;

// How long one timer tick may spend decoding. Kept well under the tick period
// so the event loop still gets its turn.
constexpr int kVideoDecodeBudgetMs = 25;
} // namespace

QString MainWindow::proxyPathFor(const media::MediaAsset& asset) const
{
    const QSettings settings = app::Settings::optionSettings();
    const QString root = settings.value(
        QStringLiteral("Options/ProxyDirectoryPath"),
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
            .filePath(QStringLiteral("Proxies"))).toString().trimmed();
    const QString project = QFileInfo(m_currentFilePath).completeBaseName();
    return media::proxyFilePath(QDir::fromNativeSeparators(root), project, asset.filePath(),
                                asset.proxyMode());
}

void MainWindow::setAssetProxyMode(const core::Identifier& assetId, media::ProxyMode mode)
{
    media::MediaAsset* asset = m_mediaManager ? m_mediaManager->assetByIdForEdit(assetId) : nullptr;
    if (!asset || asset->proxyMode() == mode) return;
    asset->setProxyMode(mode);
    m_projectModified = true;
    updateWindowTitle();
    m_mediaPanel->refresh();
    if (mode != media::ProxyMode::None) {
        const QString path = proxyPathFor(*asset);
        if (!QFileInfo::exists(path)) {
            if (!m_proxyGenerator) {
                m_proxyGenerator = new media::ProxyGenerator(this);
                connect(m_proxyGenerator, &media::ProxyGenerator::proxyFinished, this,
                        [this](const QString& source, const QString&, bool ok,
                               const QString& error) {
                    statusBar()->showMessage(ok
                        ? tr("Proxy ready: %1").arg(QFileInfo(source).fileName())
                        : tr("Proxy failed for %1: %2").arg(QFileInfo(source).fileName(), error));
                    // Frames decoded from the original until now give way.
                    updateProxyPreview(true);
                    requestRenderFrame();
                });
            }
            const QSettings settings = app::Settings::optionSettings();
            const QString quality = settings.value(QStringLiteral("Options/ProxyQuality"),
                                                   QStringLiteral("Medium")).toString();
            // "Prefer integrated GPU for proxy generation": Intel Quick Sync
            // when it really encodes here, libx264 otherwise.
            const bool integrated = settings.value(QStringLiteral("Options/PreferIntegratedGPU"),
                                                   false).toBool();
            const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
            const QString encoder = integrated && render::h264EncoderWorks(ffmpeg, QStringLiteral("h264_qsv"))
                ? QStringLiteral("h264_qsv") : QStringLiteral("libx264");
            m_proxyGenerator->enqueue(asset->filePath(), path, mode, quality, encoder);
            statusBar()->showMessage(tr("The proxy is being prepared."));
        }
    }
    updateProxyPreview(true);
    requestRenderFrame();
}

QString MainWindow::preRenderRoot() const
{
    // Options/PreRenderDirectoryPath/<project>, the folder "Delete Project
    // Pre-Renders" removes.
    const QString root = app::Settings::optionSettings().value(
        QStringLiteral("Options/PreRenderDirectoryPath"),
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
            .filePath(QStringLiteral("PreRenders"))).toString().trimmed();
    const QString project = QFileInfo(m_currentFilePath).completeBaseName();
    return QDir(QDir::fromNativeSeparators(root))
        .filePath(project.isEmpty() ? QStringLiteral("Untitled") : project);
}

void MainWindow::handlePreRenderRequest(int layerIndex, int clipIndex, bool make)
{
    const composition::Clip* clip = m_composition ? m_composition->clipAt(layerIndex, clipIndex)
                                                  : nullptr;
    if (!clip || !clip->nestedComposition) return;
    const std::shared_ptr<composition::Composition> shot = clip->nestedComposition;
    const QString folder = render::preRenderFolder(preRenderRoot(), *shot);
    if (folder.isEmpty()) return;
    if (QPointer<render::RenderManager> running = m_preRenderTasks.take(folder)) {
        running->disconnect(this);
        running->cancelAll();
        running->deleteLater();
    }
    // A new pre-render replaces whatever the folder held for older states.
    QDir(folder).removeRecursively();
    if (!make) {
        statusBar()->showMessage(tr("Removed the pre-render of %1").arg(shot->name()));
        requestRenderFrame();
        return;
    }
    if (!QDir().mkpath(folder)) {
        statusBar()->showMessage(tr("Could not create the pre-render directory."));
        return;
    }
    // The shot is rendered once over transparency, at its own size, into the
    // pre-render folder; RenderWorker then reads those frames for every clip
    // of the shot while its state still matches.
    auto* task = new render::RenderManager(this);
    task->setComposition(shot);
    task->setMediaManager(m_mediaManager);
    task->setPreRenderDirectory(preRenderRoot());
    task->setDiskCacheDirectory(folder);
    const QString name = shot->name();
    // A cache run that gives up emits Finished before Failed, so only a run
    // whose progress reached the end counts as a finished pre-render.
    const double fps = double(shot->fpsNumerator()) / qMax(1, shot->fpsDenominator());
    auto incomplete = std::make_shared<bool>(qRound(shot->durationSeconds() * fps) > 0);
    connect(task, &render::RenderManager::playbackCacheProgress, this,
            [this, name, incomplete](int done, int total) {
        *incomplete = done < total;
        statusBar()->showMessage(tr("Pre-rendering %1: %2 / %3 frames").arg(name).arg(done).arg(total));
    });
    connect(task, &render::RenderManager::playbackCacheFailed, this,
            [this, task, folder](const QString& reason) {
        task->disconnect(this);
        m_preRenderTasks.remove(folder);
        statusBar()->showMessage(reason, 8000);
        QTimer::singleShot(0, task, [task] { task->cancelAll(); task->deleteLater(); });
    });
    connect(task, &render::RenderManager::playbackCacheFinished, this,
            [this, task, folder, incomplete] {
        if (*incomplete) return; // Failed follows
        task->disconnect(this);
        m_preRenderTasks.remove(folder);
        QTimer::singleShot(0, task, [task] { task->cancelAll(); task->deleteLater(); });
        statusBar()->showMessage(tr("The media has been pre-rendered."));
        requestRenderFrame();
    });
    m_preRenderTasks.insert(folder, task);
    task->startPlaybackCache(0.0, QSize(qMax(1, shot->width()), qMax(1, shot->height())),
                             true, true);
    statusBar()->showMessage(tr("The pre-render is being prepared."));
}

void MainWindow::updateProxyPreview(bool force)
{
    // Options > Proxy & Pre-Renders "Preview mode": Auto uses proxies while
    // playing, Proxy always, Full Resolution never. Export always reads the
    // originals.
    const QString mode = app::Settings::optionSettings()
        .value(QStringLiteral("Options/PreviewMode"), QStringLiteral("Auto")).toString();
    const bool exporting = isExporting();
    // Only a ready proxy changes what preview shows; without one the frames
    // (and the render cache that keeps them) stay shared with full resolution.
    bool anyProxyReady = false;
    if (m_mediaManager) {
        for (const media::MediaAsset& asset : m_mediaManager->assets()) {
            if (asset.proxyMode() == media::ProxyMode::None) continue;
            const QString proxy = proxyPathFor(asset);
            if (QFileInfo::exists(proxy) && !(m_proxyGenerator && m_proxyGenerator->isPending(proxy))) {
                anyProxyReady = true;
                break;
            }
        }
    }
    const bool use = !exporting && anyProxyReady
        && (mode == QLatin1String("Proxy")
            || (mode == QLatin1String("Auto") && m_playbackTimer.isActive()));
    if (use == m_previewUsesProxies && !force) return;
    m_previewUsesProxies = use;
    // Video frames and rendered frames of the other variant must not be reused.
    if (m_mediaManager) m_mediaManager->clearVideoFrames();
    if (m_renderManager) m_renderManager->setMediaVariant(use ? QStringLiteral("proxy") : QString());
}

media::VideoDecoder* MainWindow::decoderFor(const QString& filePath, bool allowHardware)
{
    if (filePath.isEmpty()) {
        return nullptr;
    }
    // A file kept off the GPU decoder has a decoder of its own.
    const QString key = allowHardware ? filePath : filePath + QStringLiteral("|software");
    auto it = m_videoDecoders.find(key);
    if (it == m_videoDecoders.end()) {
        auto decoder = std::make_shared<media::VideoDecoder>(filePath, 4000, allowHardware);
        it = m_videoDecoders.insert(key, decoder);
    }
    // Touch: the list is ordered least-recently-used first.
    m_videoDecoderOrder.removeAll(key);
    m_videoDecoderOrder.append(key);
    while (m_videoDecoderOrder.size() > kMaxOpenVideoDecoders) {
        m_videoDecoders.remove(m_videoDecoderOrder.takeFirst());
    }
    return it->get();
}

void MainWindow::serviceVideoDecodeRequests()
{
    if (!m_mediaManager) {
        return;
    }

    // Several frames per tick, but only while there is time left in the budget:
    // decoding runs on this thread, so overrunning it would be felt as a stall
    // in the interface. A warm decoder answers a seek quickly enough that a
    // batch usually fits, which is what lets the picture keep up with a drag.
    QElapsedTimer budget;
    budget.start();
    bool decodedAny = false;
    core::Identifier id;
    int sourceMilliseconds = 0;
    while (budget.elapsed() < kVideoDecodeBudgetMs
           && m_mediaManager->takeVideoRequest(&id, &sourceMilliseconds)) {
        const media::MediaAsset asset = m_mediaManager->assetById(id);
        if (!asset.isValid()) {
            continue;
        }
        // Requests carry the position in the source itself.
        const double seconds = sourceMilliseconds / 1000.0;
        // A ready proxy stands in for the original while previewing; its
        // frames are scaled back to the original size so layer transforms,
        // masks and effects see the same geometry.
        QString decodePath = asset.filePath();
        if (m_previewUsesProxies && asset.proxyMode() != media::ProxyMode::None) {
            const QString proxy = proxyPathFor(asset);
            if (QFileInfo::exists(proxy)
                && !(m_proxyGenerator && m_proxyGenerator->isPending(proxy))) {
                decodePath = proxy;
            }
        }
        media::VideoDecoder* decoder = decoderFor(decodePath, asset.hardwareDecoding());
        QImage frame = decoder ? decoder->frameAt(seconds) : QImage();
        if (decodePath != asset.filePath() && !frame.isNull() && asset.frameSize().isValid()
            && frame.size() != asset.frameSize()) {
            frame = frame.scaled(asset.frameSize(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        }
        // Stored even when null: that records the attempt, so an unreadable
        // file is not queued again on every render.
        m_mediaManager->putVideoFrame(id, sourceMilliseconds, frame);
        decodedAny = decodedAny || !frame.isNull();
    }

    if (decodedAny) {
        requestRenderFrame();
    }
}

void MainWindow::refreshMediaAndInspector()
{
    m_mediaPanel->refresh();
    refreshCompositeShotList();
    if (m_trimmerPanel && m_mediaManager) {
        m_trimmerPanel->setMediaManager(m_mediaManager.get());
        if (m_composition) {
            m_trimmerPanel->setFrameRate(
                m_composition->fpsDenominator() > 0
                    ? double(m_composition->fpsNumerator()) / m_composition->fpsDenominator() : 30.0);
        }
    }
    m_controlsPanel->refresh();
}

double MainWindow::frameSeconds() const
{
    if (m_composition && m_composition->fpsNumerator() > 0) {
        return static_cast<double>(m_composition->fpsDenominator()) / m_composition->fpsNumerator();
    }
    return 1.0 / 30.0;
}

void MainWindow::setInPoint()
{
    m_inPoint = m_playbackTime;
    statusBar()->showMessage(tr("In point: %1s").arg(m_inPoint, 0, 'f', 3));
}

void MainWindow::setOutPoint()
{
    m_outPoint = m_playbackTime;
    statusBar()->showMessage(tr("Out point: %1s").arg(m_outPoint, 0, 'f', 3));
}

void MainWindow::stepFrameBackward()
{
    m_playbackTime = qMax(0.0, m_playbackTime - frameSeconds());
    m_timeline->setPlayheadPosition(m_playbackTime);
    if (m_controlsPanel) {
        m_controlsPanel->setCurrentTime(m_playbackTime);
    }
    requestRenderFrame();
}

void MainWindow::stepFrameForward()
{
    if (m_composition) {
        m_playbackTime = qMin(m_composition->durationSeconds(), m_playbackTime + frameSeconds());
    } else {
        m_playbackTime += frameSeconds();
    }
    m_timeline->setPlayheadPosition(m_playbackTime);
    if (m_controlsPanel) {
        m_controlsPanel->setCurrentTime(m_playbackTime);
    }
    requestRenderFrame();
}

void MainWindow::stepFrames(int frames)
{
    m_playbackTime += frames * frameSeconds();
    if (m_composition) {
        m_playbackTime = qBound(0.0, m_playbackTime, m_composition->durationSeconds());
    } else {
        m_playbackTime = qMax(0.0, m_playbackTime);
    }
    m_timeline->setPlayheadPosition(m_playbackTime);
    if (m_controlsPanel) {
        m_controlsPanel->setCurrentTime(m_playbackTime);
    }
    requestRenderFrame();
}

void MainWindow::shuttle(double speedMultiplier)
{
    if (speedMultiplier == 0.0) {
        stopPlayback();
        statusBar()->showMessage(tr("Shuttle: paused"));
        return;
    }
    beginPlayback();
    m_playbackTimer.setInterval(static_cast<int>(80.0 / qAbs(speedMultiplier)));
    statusBar()->showMessage(tr("Shuttle: x%1").arg(speedMultiplier, 0, 'f', 1));
}

} // namespace ui
} // namespace openvegas
