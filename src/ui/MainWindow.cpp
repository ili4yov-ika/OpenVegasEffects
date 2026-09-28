#include "ui/MainWindow.h"
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
#include "ui/TextSettingsDialog.h"
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

bool fileHasAudioStream(const QString& ffprobe, const QString& path)
{
    if (ffprobe.isEmpty() || path.isEmpty()) return false;
    QProcess process;
    process.start(ffprobe, {QStringLiteral("-v"), QStringLiteral("error"),
        QStringLiteral("-select_streams"), QStringLiteral("a:0"),
        QStringLiteral("-show_entries"), QStringLiteral("stream=index"),
        QStringLiteral("-of"), QStringLiteral("csv=p=0"), path});
    if (!process.waitForFinished(3000)) { process.kill(); return false; }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0
        && !process.readAllStandardOutput().trimmed().isEmpty();
}

// Undo commands over the layer stack. They own a copy of the layer, so redo
// restores exactly what was removed - effects, keyframes and all - rather than
// a freshly built one.
class AddLayerCommand : public QUndoCommand
{
public:
    AddLayerCommand(MainWindow* window, composition::Composition* comp,
                    const composition::Layer& layer, const QString& text)
        : QUndoCommand(text)
        , m_window(window)
        , m_comp(comp)
        , m_layer(layer)
        , m_index(comp->layers().size())
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
#ifdef OPENVEGAS_HAVE_WEBENGINE
    if (m_owner && m_owner->settings() && m_learnSidebar
        && m_owner->settings()->learnSidebarIsOpen()) {
        m_learnSidebar->show();
    }
#endif
}

MainWindow::~MainWindow()
{
    // StartPanel is deliberately not parented to QMainWindow: restoreState()
    // treats every child QDockWidget as a restorable dock and would recreate
    // the obsolete second Timeline/Start row from older workspace blobs.
    delete m_startPanel;
    delete m_ui;
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
    m_viewer = viewerPage->viewer();
    m_viewer->setShowMouseCoordinates(app::Settings::showMouseCoordinates());
    m_transportBar = viewerPage->transportBar();
    m_viewerDock->setWidget(viewerPage);
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

    connect(m_viewer360, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (visible && m_viewer && !m_viewer->frame().isNull())
            m_viewer360->setFrame(m_viewer->frame());
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

#ifdef OPENVEGAS_HAVE_WEBENGINE
    // Learn Sidebar: the reference renders it through Qt WebEngine on the
    // right-hand edge and remembers its state in "learnSidebarIsOpen".
    m_learnSidebar = new LearnSidebar(this);
    addDockWidget(Qt::RightDockWidgetArea, m_learnSidebar);
    m_learnSidebar->hide();
#endif

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
    connect(m_ui->actionExit, &QAction::triggered, this, &MainWindow::close);
    connect(m_ui->actionProjectSettings, &QAction::triggered,
            m_timeline, &TimelineWidget::editCompositionProperties);
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
    connect(m_ui->actionRecoveredSaves, &QAction::triggered, this, [this] {
        QStringList candidates;
        QStringList projects = m_owner && m_owner->settings()
            ? m_owner->settings()->recentProjects() : QStringList();
        if (!m_currentFilePath.isEmpty()) {
            projects.prepend(m_currentFilePath);
        }
        for (const QString& projectPath : projects) {
            const QFileInfo info(projectPath);
            const QString autosave = info.dir().filePath(
                info.completeBaseName() + QStringLiteral(".autosave.vegfx"));
            if (QFileInfo::exists(autosave) && !candidates.contains(autosave)) {
                candidates.append(autosave);
            }
        }
        if (candidates.isEmpty()) {
            QMessageBox::information(this, tr("Recovered Saves"),
                                     tr("No recoverable auto-saves were found."));
            return;
        }
        bool ok = false;
        const QString selected = QInputDialog::getItem(
            this, tr("Recovered Saves"), tr("Open auto-save:"), candidates, 0, false, &ok);
        if (ok && !selected.isEmpty() && (!m_projectModified || confirmDiscard())) {
            openProjectFile(selected);
        }
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
            "https://www.vegascreativesoftware.com/us/support/")));
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
#ifdef OPENVEGAS_HAVE_WEBENGINE
    // Reference command "Toggle Learn Sidebar" (0x1412d0908).
    QAction* learnAction = m_learnSidebar->toggleViewAction();
    learnAction->setText(tr("Toggle Learn Sidebar"));
    windowMenu->addAction(learnAction);
    connect(m_learnSidebar, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (m_owner && m_owner->settings()) {
            m_owner->settings()->setLearnSidebarIsOpen(visible);
        }
    });
#endif
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
#ifdef OPENVEGAS_HAVE_WEBENGINE
    if (m_learnSidebar) {
        // Page -> host calls arriving over Qt WebChannel.
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
#endif

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
        m_viewer->setProjectSize(QSize(m_composition->width(), m_composition->height()));
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
    connect(m_timeline, &TimelineWidget::compositionTabActivated, this, [this](int index) {
        if (index >= 0 && index < m_openCompositions.size())
            activateComposition(m_openCompositions.at(index));
    });
    connect(m_timeline, &TimelineWidget::compositionTabCloseRequested, this, [this](int index) {
        if (index == 0) {
            m_timeline->showStartPage();
            return;
        }
        if (index < 0 || index >= m_openCompositions.size()) return;
        const bool closingActive = m_openCompositions.at(index) == m_composition;
        m_openCompositions.removeAt(index);
        if (closingActive) activateComposition(m_openCompositions.at(qMax(0, index - 1)));
        else rebuildCompositionTabs();
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
    });
    connect(m_mediaPanel, &MediaPanel::newCompositeShotRequested,
            this, &MainWindow::makeCompositeShotFromSelection);

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
        m_viewer->setProjectSize(QSize(m_composition->width(), m_composition->height()));
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
                    if (m_viewer360->isVisible()) m_viewer360->setFrame(ownedFrame);
                    consumeExportFrame(frameIndex, rgba, size);
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

    composition::Effect effect;
    effect.pluginId = spec.id;
    effect.name = spec.displayName.isEmpty() ? spec.name : spec.displayName;
    // Seed every parameter from the spec so the inspector has something to
    // edit and the effect renders with its documented defaults.
    for (const plugin::EffectParameterSpec& param : spec.parameters) {
        effect.parameterValues.push_back(param.defaultValue);
    }
    clip->effects.push_back(effect);
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
    const double halfWidth = m_composition->width() / 2.0;
    const double halfHeight = m_composition->height() / 2.0;
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

    m_undoStack->push(new AddLayerCommand(this, m_composition.get(), layer,
                                          tr("Import 3D model '%1'").arg(asset.fileName())));
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
    for (const QString& file : files) {
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
    layer.transform.position = QPointF(canvasPosition.x() - m_composition->width() / 2.0,
                                       m_composition->height() / 2.0 - canvasPosition.y());
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
    media::VideoDecoder* decoder = decoderFor(asset.filePath());
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
            addTextLayer(dialog.style(), QPointF(m_composition->width() / 2.0,
                                                 m_composition->height() / 2.0));
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
    layer.name = name;
    layer.kind = kind;
    layer.visible = true;
    layer.blendMode = QStringLiteral("None");
    layer.opacity = 1.0;

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
    statusBar()->showMessage(tr("Added layer '%1'").arg(name));
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
    m_undoStack->push(new RemoveLayerCommand(this, m_composition.get(), m_selectedLayer,
                                             tr("Delete layer '%1'").arg(name)));
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
    m_undoStack->push(new AddLayerCommand(this, m_composition.get(), copy,
                                          tr("Duplicate layer '%1'").arg(copy.name)));
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
        m_undoStack->push(new AddLayerCommand(this, m_composition.get(), copy,
                                              tr("Paste layer '%1'").arg(copy.name)));
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
    if (m_clipboardHasClip && m_selectedClip >= 0 && m_selectedClip < layer.clips.size()) {
        composition::Clip& clip = layer.clips[m_selectedClip];
        clip.audioLevel = m_clipClipboard.audioLevel;
        clip.speed = m_clipClipboard.speed;
        clip.effects = m_clipClipboard.effects;
    } else if (m_clipboardHasLayer) {
        layer.dimension = m_layerClipboard.dimension;
        layer.blendMode = m_layerClipboard.blendMode;
        layer.opacity = m_layerClipboard.opacity;
        layer.transform = m_layerClipboard.transform;
        layer.labelColor = m_layerClipboard.labelColor;
    } else {
        statusBar()->showMessage(tr("No compatible attributes in the clipboard"));
        return;
    }
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
            requestRenderFrame();
            updateAutoSaveTimer();
            if (m_exportPanel) {
                m_exportPanel->setExportDirectory(settings.value(
                    QStringLiteral("Options/ExportDirectory")).toString());
            }
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
    const QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                             app::Settings::organizationName(),
                             app::Settings::applicationName());
    if (!settings.value(QStringLiteral("Options/AutoSaveEnabled"), true).toBool()) {
        m_autoSaveTimer.stop();
        return;
    }
    const int seconds = qBound(30, settings.value(
        QStringLiteral("Options/AutoSaveIntervalSeconds"), 300).toInt(), 3600);
    m_autoSaveTimer.start(seconds * 1000);
}

void MainWindow::writeAutoSave()
{
    if (!m_projectModified || m_currentFilePath.isEmpty() || !m_rootComposition || !m_mediaManager) {
        return;
    }
    const QFileInfo projectFile(m_currentFilePath);
    const QString backup = projectFile.dir().filePath(
        projectFile.completeBaseName() + QStringLiteral(".autosave.vegfx"));
    project::ProjectSaveOptions options;
    const QSettings settings = app::Settings::optionSettings();
    options.useRelativePaths = settings.value(QStringLiteral("Options/UseRelativePaths"), false).toBool();
    if (settings.value(QStringLiteral("Options/IncludeScreenLayout"), false).toBool())
        options.screenLayout = saveState(kLayoutStateVersion);
    const core::Result result = project::VegfxSerializer::saveToFile(
        backup, *m_rootComposition, *m_mediaManager, options);
    statusBar()->showMessage(result.isSuccess()
        ? tr("Auto-saved %1").arg(QDir::toNativeSeparators(backup))
        : tr("Auto-save failed: %1").arg(result.message()), 5000);
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

void MainWindow::exportContents(const QString& path)
{
    if (!m_composition || !m_renderManager || path.isEmpty() || m_exportFrame >= 0) return;
    const double fps = m_composition->fpsDenominator() > 0
        ? double(m_composition->fpsNumerator()) / m_composition->fpsDenominator() : 30.0;
    m_exportFirstFrame = m_inPoint >= 0.0 ? qRound(m_inPoint * fps) : 0;
    const double end = m_outPoint > m_inPoint ? m_outPoint : m_composition->durationSeconds();
    m_exportLastFrame = qMax(m_exportFirstFrame, qCeil(end * fps) - 1);
    m_exportTemp = std::make_unique<QTemporaryDir>();
    if (!m_exportTemp->isValid() || !QDir().mkpath(QFileInfo(path).absolutePath())) {
        m_exportTemp.reset();
        statusBar()->showMessage(tr("Cannot create export directory"));
        return;
    }
    m_exportTarget = path;
    m_exportFrame = m_exportFirstFrame;
    m_exportPrimed = false;
    statusBar()->showMessage(tr("Exporting frame %1 of %2")
        .arg(1).arg(m_exportLastFrame - m_exportFirstFrame + 1));
    requestNextExportFrame();
}

void MainWindow::requestNextExportFrame()
{
    if (m_exportFrame < 0 || !m_renderManager || !m_composition) return;
    const double fps = m_composition->fpsDenominator() > 0
        ? double(m_composition->fpsNumerator()) / m_composition->fpsDenominator() : 30.0;
    m_renderManager->requestFrame(m_exportFrame, m_exportFrame / fps,
                                  QSize(m_composition->width(), m_composition->height()), true);
}

void MainWindow::consumeExportFrame(int frameIndex, const QByteArray& rgba, const QSize& size)
{
    if (m_exportFrame < 0 || frameIndex != m_exportFrame || !m_exportTemp
        || size != QSize(m_composition->width(), m_composition->height())) return;
    // The first pass primes on-demand video decoders. The GUI decode timer gets
    // a turn before the same frame is requested again and written.
    if (!m_exportPrimed) {
        m_exportPrimed = true;
        QTimer::singleShot(100, this, &MainWindow::requestNextExportFrame);
        return;
    }
    QImage image(reinterpret_cast<const uchar*>(rgba.constData()), size.width(), size.height(),
                 size.width() * 4, QImage::Format_RGBA8888);
    const QString framePath = QDir(m_exportTemp->path()).filePath(
        QStringLiteral("frame-%1.png").arg(m_exportFrame, 8, 10, QLatin1Char('0')));
    if (!image.copy().save(framePath)) {
        statusBar()->showMessage(tr("Could not write export frame %1").arg(m_exportFrame));
        m_exportFrame = -1;
        m_exportTemp.reset();
        return;
    }
    if (m_exportFrame >= m_exportLastFrame) {
        finishVideoExport();
        return;
    }
    ++m_exportFrame;
    m_exportPrimed = false;
    statusBar()->showMessage(tr("Exporting frame %1 of %2")
        .arg(m_exportFrame - m_exportFirstFrame + 1)
        .arg(m_exportLastFrame - m_exportFirstFrame + 1));
    requestNextExportFrame();
}

void MainWindow::finishVideoExport()
{
    const QString suffix = QFileInfo(m_exportTarget).suffix().toLower();
    if (suffix != QLatin1String("mp4") && suffix != QLatin1String("mov")) {
        const QFileInfo target(m_exportTarget);
        for (int frame = m_exportFirstFrame; frame <= m_exportLastFrame; ++frame) {
            const QString source = QDir(m_exportTemp->path()).filePath(
                QStringLiteral("frame-%1.png").arg(frame, 8, 10, QLatin1Char('0')));
            const QString output = target.dir().filePath(QStringLiteral("%1-%2.%3")
                .arg(target.completeBaseName()).arg(frame - m_exportFirstFrame + 1, 8, 10,
                                                     QLatin1Char('0')).arg(suffix));
            QImage image(source);
            QString error;
            const bool ok = suffix == QLatin1String("exr")
                ? media::writeExr(image, output, &error) : image.save(output);
            if (!ok) {
                statusBar()->showMessage(tr("Image sequence export failed: %1").arg(output));
                m_exportFrame = -1; m_exportTemp.reset(); return;
            }
        }
        statusBar()->showMessage(tr("Exported image sequence to %1").arg(target.absolutePath()));
        if (m_historyPanel) m_historyPanel->addEntry(
            tr("Export contents to %1").arg(target.absolutePath()));
        notifyExportCompleted();
        m_exportFrame = -1; m_exportTemp.reset();
        return;
    }
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty()) {
        statusBar()->showMessage(tr("FFmpeg was not found; choose an image-sequence preset"));
        m_exportFrame = -1; m_exportTemp.reset();
        return;
    }
    const double fps = m_composition->fpsDenominator() > 0
        ? double(m_composition->fpsNumerator()) / m_composition->fpsDenominator() : 30.0;
    QStringList arguments{QStringLiteral("-y"), QStringLiteral("-framerate"),
                          QString::number(fps, 'f', 6), QStringLiteral("-start_number"),
                          QString::number(m_exportFirstFrame), QStringLiteral("-i"),
                          QDir(m_exportTemp->path()).filePath(QStringLiteral("frame-%08d.png"))};
    struct AudioInput {
        QString path;
        double source = 0;
        double sourceDuration = 0;
        double delay = 0;
        double speed = 1;
        double gain = 1;
        bool rawPcm = false;
    };
    QVector<AudioInput> audioInputs;
    const QString ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
    const double exportStart = m_exportFirstFrame / fps;
    const double exportDuration = (m_exportLastFrame - m_exportFirstFrame + 1) / fps;
    const double exportEnd = exportStart + exportDuration;
    if (m_mediaManager) {
        for (const composition::Layer& layer : m_composition->layers()) {
            if (!layer.visible || layer.muted) continue;
            for (const composition::Clip& clip : layer.clips) {
                const media::MediaAsset asset = m_mediaManager->assetById(clip.mediaId);
                if (!asset.isValid() || !fileHasAudioStream(ffprobe, asset.filePath())) continue;
                const double first = qMax(exportStart, clip.startSeconds);
                const double last = qMin(exportEnd, clip.endSeconds());
                if (last <= first) continue;
                const double speed = clip.speed > 0.0 ? clip.speed : 1.0;
                AudioInput input {asset.filePath(),
                    clip.sourceStartSeconds + (first - clip.startSeconds) * speed,
                    (last - first) * speed, first - exportStart, speed,
                    qPow(10.0, clip.audioLevel / 20.0), false};

                // Native audio modules consume interleaved PCM16. Decode and
                // retime only clips that actually contain such effects; all
                // other clips keep the direct FFmpeg input/filter path.
                QVector<const composition::Effect*> nativeAudioEffects;
                plugin::PluginManager* plugins = m_owner ? m_owner->pluginManager() : nullptr;
                if (plugins) {
                    for (const composition::Effect& effect : clip.effects) {
                        if (effect.enabled
                            && plugin::nativeAudioEffectRenderingVerified(effect.pluginId)) {
                            nativeAudioEffects.append(&effect);
                        }
                    }
                }
                if (!nativeAudioEffects.isEmpty()) {
                    const QString rawPath = QDir(m_exportTemp->path()).filePath(
                        QStringLiteral("audio-native-%1.pcm").arg(audioInputs.size()));
                    QProcess decode;
                    QStringList decodeArguments {
                        QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"),
                        QStringLiteral("-ss"), QString::number(input.source, 'f', 6),
                        QStringLiteral("-t"), QString::number(input.sourceDuration, 'f', 6),
                        QStringLiteral("-i"), input.path, QStringLiteral("-vn"),
                        QStringLiteral("-ac"), QStringLiteral("2"),
                        QStringLiteral("-ar"), QStringLiteral("48000"),
                        QStringLiteral("-af"),
                        QStringLiteral("atempo=%1").arg(input.speed, 0, 'f', 6),
                        QStringLiteral("-f"), QStringLiteral("s16le"), rawPath
                    };
                    decode.start(ffmpeg, decodeArguments);
                    if (!decode.waitForFinished(-1)
                        || decode.exitStatus() != QProcess::NormalExit
                        || decode.exitCode() != 0) {
                        statusBar()->showMessage(tr("Native audio decode failed: %1")
                                                     .arg(QString::fromUtf8(
                                                         decode.readAllStandardError()).trimmed()));
                        m_exportFrame = -1;
                        m_exportTemp.reset();
                        return;
                    }
                    QFile rawFile(rawPath);
                    if (!rawFile.open(QIODevice::ReadOnly)) {
                        statusBar()->showMessage(tr("Could not read decoded audio: %1")
                                                     .arg(rawPath));
                        m_exportFrame = -1;
                        m_exportTemp.reset();
                        return;
                    }
                    const QByteArray bytes = rawFile.readAll();
                    rawFile.close();
                    QVector<qint16> samples(bytes.size() / int(sizeof(qint16)));
                    if (!samples.isEmpty()) {
                        std::memcpy(samples.data(), bytes.constData(),
                                    size_t(samples.size()) * sizeof(qint16));
                    }
                    const int parameterFrame = qRound(first * fps);
                    for (const composition::Effect* effect : nativeAudioEffects) {
                        const plugin::EffectSpec effectSpec = plugins->spec(effect->pluginId);
                        QStringList values;
                        values.reserve(effectSpec.parameters.size());
                        for (int parameter = 0;
                             parameter < effectSpec.parameters.size(); ++parameter) {
                            const QVariant value = effect->parameterAt(parameter, parameterFrame);
                            values.append(value.isValid()
                                              ? value.toString()
                                              : effectSpec.parameters.at(parameter).defaultValue);
                        }
                        if (!plugin::applyNativeAudioEffect(
                                samples, 2, 48000,
                                qRound64((first - exportStart) * 48000.0),
                                effect->pluginId, values)) {
                            statusBar()->showMessage(
                                tr("Native audio effect failed: %1").arg(effect->name));
                            plugin::releaseNativeEffectThreadRenderer();
                            m_exportFrame = -1;
                            m_exportTemp.reset();
                            return;
                        }
                    }
                    plugin::releaseNativeEffectThreadRenderer();
                    if (!rawFile.open(QIODevice::WriteOnly | QIODevice::Truncate)
                        || rawFile.write(reinterpret_cast<const char*>(samples.constData()),
                                         qint64(samples.size()) * sizeof(qint16))
                               != qint64(samples.size()) * sizeof(qint16)) {
                        statusBar()->showMessage(tr("Could not write processed audio: %1")
                                                     .arg(rawPath));
                        m_exportFrame = -1;
                        m_exportTemp.reset();
                        return;
                    }
                    rawFile.close();
                    input.path = rawPath;
                    input.source = 0.0;
                    input.sourceDuration = (last - first);
                    input.speed = 1.0;
                    input.rawPcm = true;
                }
                audioInputs.append(input);
            }
        }
    }
    for (const auto& input : audioInputs) {
        if (input.rawPcm) {
            arguments << QStringLiteral("-f") << QStringLiteral("s16le")
                      << QStringLiteral("-ar") << QStringLiteral("48000")
                      << QStringLiteral("-ac") << QStringLiteral("2")
                      << QStringLiteral("-t")
                      << QString::number(input.sourceDuration, 'f', 6)
                      << QStringLiteral("-i") << input.path;
        } else {
            arguments << QStringLiteral("-ss") << QString::number(input.source, 'f', 6)
                      << QStringLiteral("-t") << QString::number(input.sourceDuration, 'f', 6)
                      << QStringLiteral("-i") << input.path;
        }
    }

    QStringList audioFilters;
    QStringList audioLabels;
    for (int i = 0; i < audioInputs.size(); ++i) {
        const auto& input = audioInputs.at(i);
        const QString label = QStringLiteral("a%1").arg(i);
        audioLabels << QStringLiteral("[%1]").arg(label);
        audioFilters << QStringLiteral("[%1:a]asetpts=PTS-STARTPTS,atempo=%2,volume=%3,adelay=%4:all=1[%5]")
            .arg(i + 1).arg(input.speed, 0, 'f', 6).arg(input.gain, 0, 'f', 6)
            .arg(qRound64(input.delay * 1000.0)).arg(label);
    }
    if (!audioInputs.isEmpty()) {
        audioFilters << QStringLiteral("%1amix=inputs=%2:normalize=0:dropout_transition=0[aout]")
            .arg(audioLabels.join(QString())).arg(audioInputs.size());
        arguments << QStringLiteral("-filter_complex") << audioFilters.join(QLatin1Char(';'))
                  << QStringLiteral("-map") << QStringLiteral("0:v:0")
                  << QStringLiteral("-map") << QStringLiteral("[aout]");
    }
    if (suffix == QLatin1String("mov"))
        arguments << QStringLiteral("-c:v") << QStringLiteral("prores_ks")
                  << QStringLiteral("-profile:v") << QStringLiteral("3")
                  << QStringLiteral("-pix_fmt") << QStringLiteral("yuv422p10le");
    else
        arguments << QStringLiteral("-c:v") << QStringLiteral("libx264")
                  << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p")
                  << QStringLiteral("-movflags") << QStringLiteral("+faststart");
    if (!audioInputs.isEmpty())
        arguments << QStringLiteral("-c:a")
                  << (suffix == QLatin1String("mov") ? QStringLiteral("pcm_s16le") : QStringLiteral("aac"));
    arguments << QStringLiteral("-t") << QString::number(exportDuration, 'f', 6);
    arguments << m_exportTarget;
    QProcess* exportProcess = new QProcess(this);
    m_exportProcess = exportProcess;
    connect(exportProcess, &QProcess::finished, this,
            [this, exportProcess](int exitCode, QProcess::ExitStatus status) {
        const bool ok = status == QProcess::NormalExit && exitCode == 0;
        statusBar()->showMessage(ok ? tr("Exported video to %1").arg(m_exportTarget)
                                    : tr("FFmpeg export failed"));
        if (ok && m_historyPanel) m_historyPanel->addEntry(
            tr("Export contents to %1").arg(m_exportTarget));
        if (ok) notifyExportCompleted();
        exportProcess->deleteLater();
        if (m_exportProcess == exportProcess) m_exportProcess = nullptr;
        m_exportFrame = -1; m_exportTemp.reset();
    });
    connect(exportProcess, &QProcess::errorOccurred, this, [this, exportProcess](QProcess::ProcessError) {
        if (m_exportProcess != exportProcess) return;
        statusBar()->showMessage(tr("Could not start FFmpeg: %1").arg(exportProcess->errorString()));
        exportProcess->deleteLater(); m_exportProcess = nullptr;
        m_exportFrame = -1; m_exportTemp.reset();
    });
    statusBar()->showMessage(tr("Encoding video..."));
    exportProcess->start(ffmpeg, arguments);
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
    m_viewer->setProjectSize(QSize(m_composition->width(), m_composition->height()));
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

bool MainWindow::onSaveProjectAs()
{
    if (!m_rootComposition || !m_mediaManager) {
        return false;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Save Project"), m_rootComposition->name() + QStringLiteral(".vegfx"),
        QStringLiteral("VEGAS Effects Project (*.vegfx)"));
    if (path.isEmpty()) {
        return false;
    }
    return doSaveProject(path);
}

bool MainWindow::doSaveProject(const QString& path)
{
    project::ProjectSaveOptions options;
    const QSettings settings = app::Settings::optionSettings();
    options.useRelativePaths = settings.value(QStringLiteral("Options/UseRelativePaths"), false).toBool();
    if (settings.value(QStringLiteral("Options/IncludeScreenLayout"), false).toBool())
        options.screenLayout = saveState(kLayoutStateVersion);
    const core::Result r = project::VegfxSerializer::saveToFile(
        path, *m_rootComposition, *m_mediaManager, options);
    if (r.isFailure()) {
        QMessageBox::warning(this, QStringLiteral("Save Project"), r.message());
        return false;
    }
    m_currentFilePath = path;
    m_projectModified = false;
    const QFileInfo projectFile(path);
    QFile::remove(projectFile.dir().filePath(
        projectFile.completeBaseName() + QStringLiteral(".autosave.vegfx")));
    updateWindowTitle();
    noteRecentProject(path);
    statusBar()->showMessage(tr("Saved %1").arg(path));
    return true;
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
    m_projectModified = false;
    rebuildCompositionTabs();
    refreshAfterModelChange();
    if (m_timeline) m_timeline->showTimelinePage();
    const QSettings newProjectSettings(QSettings::IniFormat, QSettings::UserScope,
                                       app::Settings::organizationName(), app::Settings::applicationName());
    if (newProjectSettings.value(QStringLiteral("Options/Prompts/ShowProjectSettings"), true).toBool())
        m_timeline->editCompositionProperties();
}

void MainWindow::onOpenProject()
{
    if (m_projectModified && !confirmDiscard()) {
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Open Project"), QString(),
        QStringLiteral("VEGAS Effects Project (*.vegfx)"));
    if (path.isEmpty()) {
        return;
    }
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
        QMessageBox::warning(this, QStringLiteral("Open Project"), r.message());
        return false;
    }
    m_currentFilePath = path;
    m_projectModified = false;
    m_composition = m_rootComposition;
    m_undoStack->clear();
    if (m_renderManager) m_renderManager->setComposition(m_composition);
    resetCompositionTabs();
    refreshAfterModelChange();
    if (m_timeline) m_timeline->showTimelinePage();
    noteRecentProject(path);
    statusBar()->showMessage(tr("Opened %1").arg(path));
    if (!screenLayout.isEmpty()) {
        restoreState(screenLayout, kLayoutStateVersion);
        giveBottomDockTheCorners(this);
        installDockTabMenus(this);
        applyInterfacePreferences();
    }
    QStringList offlineMedia;
    for (const auto& asset : m_mediaManager->assets()) {
        if (!QFileInfo::exists(asset.filePath())) offlineMedia.append(asset.filePath());
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
        QMessageBox::warning(this, QStringLiteral("Open Project"),
                             QStringLiteral("The project file no longer exists:\n%1").arg(path));
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
    const QString document = m_currentFilePath.isEmpty()
                                 ? tr(kUntitledDocument)
                                 : QFileInfo(m_currentFilePath).fileName();
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
#ifdef OPENVEGAS_HAVE_WEBENGINE
    if (m_learnSidebar) {
        m_learnSidebar->show();
        m_learnSidebar->raise();
        return;
    }
#endif
    statusBar()->showMessage(
        tr("The Learn sidebar needs a Qt WebEngine build"));
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
    const double w = m_composition->width() * qAbs(scale.x()) / 100.0;
    const double h = m_composition->height() * qAbs(scale.y()) / 100.0;
    const QPointF centre(m_composition->width() / 2.0 + position.x(),
                         m_composition->height() / 2.0 - position.y());
    return QRectF(centre.x() - w / 2.0, centre.y() - h / 2.0, w, h);
}

void MainWindow::updateLayoutPanelSelection()
{
    if (!m_layoutPanel) {
        return;
    }
    if (m_composition) {
        m_layoutPanel->setFrameRect(QRectF(0, 0, m_composition->width(), m_composition->height()));
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
    if (!m_composition || bounds.isEmpty() || m_selectedLayer < 0
        || m_selectedLayer >= m_composition->layers().size()) {
        return;
    }
    composition::Layer& layer = m_composition->layerRef(m_selectedLayer);
    const double sx = 100.0 * bounds.width() / m_composition->width();
    const double sy = 100.0 * bounds.height() / m_composition->height();
    // The sign is the layer's own: resizing a mirrored layer must not unmirror
    // it, and the panel only ever reports a positive box.
    const double signX = layer.transform.scalePercent.x() < 0.0 ? -1.0 : 1.0;
    const double signY = layer.transform.scalePercent.y() < 0.0 ? -1.0 : 1.0;
    layer.transform.scalePercent = QPointF(sx * signX, sy * signY);
    layer.transform.position =
        QPointF(bounds.center().x() - m_composition->width() / 2.0,
                m_composition->height() / 2.0 - bounds.center().y());
    m_projectModified = true;
    updateWindowTitle();
    m_timeline->refreshKeyFrames();
    m_controlsPanel->refresh();
    requestRenderFrame();
}

void MainWindow::activateComposition(
    const std::shared_ptr<composition::Composition>& composition)
{
    if (!composition) return;
    stopPlayback();
    if (m_renderManager) {
        m_renderManager->cancelPlaybackCache();
        m_renderManager->setComposition(composition);
    }
    m_composition = composition;
    m_selectedLayer = -1;
    m_selectedClip = -1;
    m_selectedLayers.clear();
    m_playbackTime = 0.0;
    m_timeline->setComposition(m_composition);
    m_controlsPanel->setComposition(m_composition);
    m_controlsPanel->setSelection(-1, -1);
    m_trackPanel->bindModel(m_composition);
    m_trackPanel->setSelectedLayer(-1);
    m_layerPanel->bindModel(m_composition);
    m_layerPanel->setUndoStack(m_undoStack);
    m_layerPanel->setMediaManager(m_mediaManager);
    m_layerPanel->setSelection(-1);
    m_viewer->setProjectSize(QSize(m_composition->width(), m_composition->height()));
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
    updateTextPanelSelection();
    updateLayoutPanelSelection();
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
    QSet<QString> visited;
    std::function<void(const std::shared_ptr<composition::Composition>&)> visit =
        [&](const std::shared_ptr<composition::Composition>& comp) {
            if (!comp || visited.contains(comp->id().value())) return;
            visited.insert(comp->id().value());
            m_openCompositions.append(comp);
            for (const composition::Layer& layer : comp->layers())
                for (const composition::Clip& clip : layer.clips)
                    if (clip.nestedComposition) visit(clip.nestedComposition);
        };
    visit(m_rootComposition);
    rebuildCompositionTabs();
}

void MainWindow::applyLayerBounds(int layerIndex, const QRectF& bounds)
{
    if (!m_composition || bounds.isEmpty() || layerIndex < 0
        || layerIndex >= m_composition->layers().size()) return;
    composition::Layer& layer = m_composition->layerRef(layerIndex);
    const double sx = 100.0 * bounds.width() / m_composition->width();
    const double sy = 100.0 * bounds.height() / m_composition->height();
    const double signX = layer.transform.scalePercent.x() < 0.0 ? -1.0 : 1.0;
    const double signY = layer.transform.scalePercent.y() < 0.0 ? -1.0 : 1.0;
    layer.transform.scalePercent = QPointF(sx * signX, sy * signY);
    layer.transform.position = QPointF(bounds.center().x() - m_composition->width() / 2.0,
                                       m_composition->height() / 2.0 - bounds.center().y());
}

void MainWindow::onLayoutSelectionBoundsEdited(const QVector<QRectF>& bounds)
{
    QVector<int> selected = m_selectedLayers;
    if (selected.isEmpty() && m_selectedLayer >= 0) selected = {m_selectedLayer};
    if (selected.size() != bounds.size()) return;
    for (int i = 0; i < selected.size(); ++i) applyLayerBounds(selected.at(i), bounds.at(i));
    m_projectModified = true;
    updateWindowTitle();
    m_timeline->refreshKeyFrames();
    m_controlsPanel->refresh();
    updateLayoutPanelSelection();
    requestRenderFrame();
}

void MainWindow::onLayoutMirror(Qt::Orientation orientation)
{
    if (!m_composition || m_selectedLayer < 0
        || m_selectedLayer >= m_composition->layers().size()) {
        statusBar()->showMessage(tr("Select a layer first"));
        return;
    }
    // Mirroring is a negative scale on one axis, which is how the renderer
    // already draws a flipped layer - there is no separate flip flag.
    composition::Layer& layer = m_composition->layerRef(m_selectedLayer);
    QPointF scale = layer.transform.scalePercent;
    if (orientation == Qt::Horizontal) {
        scale.setX(-scale.x());
    } else {
        scale.setY(-scale.y());
    }
    layer.transform.scalePercent = scale;
    m_projectModified = true;
    updateWindowTitle();
    m_timeline->refreshKeyFrames();
    m_controlsPanel->refresh();
    requestRenderFrame();
    statusBar()->showMessage(orientation == Qt::Horizontal ? tr("Mirror Horizontal")
                                                           : tr("Mirror Vertical"));
}

void MainWindow::onLayoutRotate(int degrees)
{
    if (!m_composition || m_selectedLayer < 0
        || m_selectedLayer >= m_composition->layers().size()) {
        statusBar()->showMessage(tr("Select a layer first"));
        return;
    }
    composition::Layer& layer = m_composition->layerRef(m_selectedLayer);
    layer.transform.rotationDegrees += degrees;
    m_projectModified = true;
    updateWindowTitle();
    m_timeline->refreshKeyFrames();
    m_controlsPanel->refresh();
    requestRenderFrame();
    statusBar()->showMessage(degrees > 0 ? tr("Rotate 90 Degrees Clockwise")
                                         : tr("Rotate 90 Degrees Counter Clockwise"));
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
#ifdef OPENVEGAS_HAVE_WEBENGINE
    // Drop the web view while the QWebEngineProfile is still alive.
    if (m_learnSidebar) {
        m_learnSidebar->shutdown();
    }
#endif
    QMainWindow::closeEvent(event);
}

double MainWindow::prepareAudioSource()
{
    m_audioClipStart = -1.0;
    if (!m_audio || !m_composition || !m_mediaManager) {
        return -1.0;
    }

    QVector<media::AudioClip> sources;
    QSet<const composition::Composition*> visited;
    std::function<void(const composition::Composition&, double, double, double, double, double)> collect;
    collect = [&](const composition::Composition& shot, double origin, double rate,
                  double firstAllowed, double lastAllowed, double gain) {
        if (visited.contains(&shot)) return;
        visited.insert(&shot);
        for (const auto& layer : shot.layers()) {
            if (!layer.visible || layer.muted) continue;
            for (const auto& clip : layer.clips) {
                const double speed = clip.speed > 0 ? clip.speed : 1;
                const double first = qMax(firstAllowed, origin + clip.startSeconds / rate);
                const double last = qMin(lastAllowed, origin + clip.endSeconds() / rate);
                if (last <= first) continue;
                const double level = gain * qPow(10.0, clip.audioLevel / 20.0);
                if (clip.nestedComposition) {
                    const double childOrigin = origin + clip.startSeconds / rate
                        - clip.sourceStartSeconds / (rate * speed);
                    collect(*clip.nestedComposition, childOrigin, rate * speed, first, last, level);
                    continue;
                }
                const auto asset = m_mediaManager->assetById(clip.mediaId);
                if (asset.kind() != media::MediaKind::Audio && asset.kind() != media::MediaKind::Video) continue;
                if (asset.filePath().isEmpty()) continue;
                const double source = clip.sourceStartSeconds
                    + (first - origin - clip.startSeconds / rate) * rate * speed;
                media::AudioClip audioClip {asset.filePath(), first, last,
                                            source, speed * rate, level};
                const plugin::PluginManager* plugins =
                    m_owner ? m_owner->pluginManager() : nullptr;
                if (plugins) {
                    const double shotFps = shot.fpsDenominator() > 0
                        ? double(shot.fpsNumerator()) / shot.fpsDenominator() : 30.0;
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
                sources.append(std::move(audioClip));
            }
        }
        visited.remove(&shot);
    };
    collect(*m_composition, 0, 1, 0, m_composition->durationSeconds(), 1);
    m_audio->setClips(sources, m_composition->durationSeconds());
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

media::VideoDecoder* MainWindow::decoderFor(const QString& filePath)
{
    if (filePath.isEmpty()) {
        return nullptr;
    }
    auto it = m_videoDecoders.find(filePath);
    if (it == m_videoDecoders.end()) {
        auto decoder = std::make_shared<media::VideoDecoder>(filePath);
        it = m_videoDecoders.insert(filePath, decoder);
    }
    // Touch: the list is ordered least-recently-used first.
    m_videoDecoderOrder.removeAll(filePath);
    m_videoDecoderOrder.append(filePath);
    while (m_videoDecoderOrder.size() > kMaxOpenVideoDecoders) {
        m_videoDecoders.remove(m_videoDecoderOrder.takeFirst());
    }
    return it->get();
}

void MainWindow::serviceVideoDecodeRequests()
{
    if (!m_mediaManager || !m_composition) {
        return;
    }

    // The worker quantised the time by the composition's frame rate, so the
    // same divisor turns a key back into a position in the file.
    const int den = m_composition->fpsDenominator() > 0 ? m_composition->fpsDenominator() : 1;
    const double fps = static_cast<double>(m_composition->fpsNumerator()) / den;

    // Several frames per tick, but only while there is time left in the budget:
    // decoding runs on this thread, so overrunning it would be felt as a stall
    // in the interface. A warm decoder answers a seek quickly enough that a
    // batch usually fits, which is what lets the picture keep up with a drag.
    QElapsedTimer budget;
    budget.start();
    bool decodedAny = false;
    core::Identifier id;
    int sourceFrame = 0;
    while (budget.elapsed() < kVideoDecodeBudgetMs && m_mediaManager->takeVideoRequest(&id, &sourceFrame)) {
        const media::MediaAsset asset = m_mediaManager->assetById(id);
        if (!asset.isValid()) {
            continue;
        }
        const double seconds = fps > 0.0 ? sourceFrame / fps : 0.0;
        media::VideoDecoder* decoder = decoderFor(asset.filePath());
        const QImage frame = decoder ? decoder->frameAt(seconds) : QImage();
        // Stored even when null: that records the attempt, so an unreadable
        // file is not queued again on every render.
        m_mediaManager->putVideoFrame(id, sourceFrame, frame);
        decodedAny = decodedAny || !frame.isNull();
    }

    if (decodedAny) {
        requestRenderFrame();
    }
}

void MainWindow::refreshMediaAndInspector()
{
    m_mediaPanel->refresh();
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
