# Panel windows moved into *.ui forms

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

This document describes the Qt Designer forms of the panels and how they connect to the
working classes. All the forms listed are part of the CMake and qmake builds; the panel
constructors call `setupUi()`, while dynamic lists, menus, painting and signals stay in
C++. `ui/panels/Export.ui` is kept as a reference copy: the working Export panel uses the
form that was already connected, `ui/ExportPanel.ui`.

## 1. What was studied

| Source | What it gave |
|--------|--------------|
| `src/ui/MainWindow.cpp` (`buildUi`) | The shared dock group Viewer / Trimmer / Export / Layer / 360 Viewer; `viewerPage` → canvas + inline transport bar |
| `src/ui/ViewerWidget.{h,cpp}` | The canvas itself (`graphicsViewViewer`), the layout manager (Single/Row/TwoOverOne/Four/RowFour), the tool strip (`viewer-tool-strip`, buttons), `createViewOptionsBar()` → `toolButtonView`, `toolButtonOption`, `toolButtonPlaybackQuality` |
| `src/ui/ViewerTransportBar.{h,cpp}` | The `ViewerPlaybackWidget` naming, the `progressBarRender` progress, the scrub row (`spinBoxCurrentTime`, `sliderCurrentFrame`, the editable `spinBoxFrameCount`/Timeline Duration with Undo/Redo), the button row (cluster order), `addTrailingWidget(...)` for the view options |
| `src/ui/ViewScaleButton.{h,cpp}` | The zoom ladder (Fit, 12%…200%): "−", the `viewScaleLabel` label, "+"; the objectName overridden to `toolButtonZoom` |
| `resources/icons.qrc` | The set of available icons `/icons/*` |
| `ui/ExportPanel.ui`, `ui/MainWindow.ui` | The project's .ui style: `<class>` = owner class, root widget name, explicit margins/spacing, `<string notr="true">` for text that is not translated |

## 2. What was moved

`Viewer.ui` owns the layout of the central tab and uses two promoted working widgets:

```
ViewerPanel (QWidget, form of class openvegas::ui::ViewerPanel)
└── rootLayout (QVBoxLayout, margins 0, spacing 0)
    ├── graphicsViewViewer (ViewerWidget; min 320×180)
    └── ViewerPlaybackWidget (ViewerTransportBar)
```

The form class is `openvegas::ui::ViewerPanel`. The nested `ViewerWidget` and
`ViewerTransportBar` keep their own reference object names, tooltips, special styles
and working connections. `ViewerPanel` takes both pointers from the form and appends
the Viewer options bar to the end of `ViewerTransportBar`.

## 3. What stays in code (dynamic parts)

These parts were **deliberately not** moved into .ui — they are not a static tree:

- **The canvas** — the promoted `ViewerWidget`; the frame, motion path, timecode and
  mouse coordinates are painted in its `paintEvent`.
- **The tool strip** — its 24 px width and left position are set in code
  (`toolStripRect`); the 22×22 px buttons are aligned on a common axis with one-pixel
  margins, and a local style isolates them from the general `QToolButton` padding; the
  cursor shortcuts (V/H/T/R/E/F/B), the shape flyout on `toolButtonMaskShape`
  (Rec/RRect/Ellipse/Polygon/Star) and the drop-down menus.
- **The button menus** of transport/options: "View" (layouts), "Options" (Show Motion
  Path, Background Color, Checkerboard, Color Channels, Full Screen Preview,
  Scale/Fit/In/Out), Quality (4 profiles × playback/paused), the zoom ladder.
- **Icons** — set in code (`makeButton`) with a text fallback when an SVG does not
  rasterize; the forms do not reference the resources, so they do not pull in
  `icons.qrc`.
- **Signals/slots** — all the wiring (`scrubRequested`, `playPauseRequested`, …).

## 4. How they are connected

1. `ViewerWidget` and `ViewerTransportBar` are declared in the form's `<customwidgets>`.
2. `ViewerPanel` calls `setupUi()` and hands the working pointers to `MainWindow`.
3. `CMAKE_AUTOUIC_SEARCH_PATHS` includes `ui/panels`; the forms are also listed in
   `OpenVegasEffects.pro`.
4. The Timeline uses the promoted `TimelineTree`, `TimelineCanvas` and
   `TimelineValueGraphView`; Trimmer, Preview360 and Meters replace only the painted
   placeholders with working custom widgets after `setupUi()`.

## 5. Observations

- `audioMeterIcon` — a compact painted two-channel LED indicator without a label. It
  gets the master RMS from `AudioPlayer`, sits before Options and opens the full Audio
  Meters panel on a click.
- `toolButtonZoom` — a composite widget: in code `ViewScaleButton` first gets its own
  name `ViewScaleButton`, then it is overridden to `toolButtonZoom`. The form keeps the
  final (reference) name so that themes and tests do not break.
- `sliderCurrentFrame` is disabled in code while `duration == 0` (it is enabled in
  `setDuration`). In the form `enabled=false` matches the initial state of a new
  project.
- The button order in the left and center clusters is the reference constructor's order
  (Export Frame → Loop → In → Out → Meters | First → Prev → Next → Play), not invented
  anew.

---

# 6. The other MainWindow panels

All the other dock panels and tabs of the main window use similar forms. The form
class is the class owning the tree (`openvegas::ui::…`), the widget names are the
**reference object names** (themes and tests look them up), and the dynamic parts
(paintEvent, popups, lists, icons) are described in comments over a static skeleton.
For **Export** a spec copy (`Export.ui`) was created in `ui/panels/`, but the working
implementation stays on the existing connected form `ui/ExportPanel.ui` (see 6.15).

The dock tree in `src/ui/MainWindow.cpp`, screen "Effects":

- **Left:** Media (`mediaPanel`) + Text (`textPanel`, both in the left dock)
- **Center:** the shared dock group Viewer / Trimmer / Export / Layer / 360 Viewer
- **Right:** Controls / Effects / Layout / Track / Library / History + Learn (a native
  dock, hidden)
- **Bottom:** Timeline (`timelinePanel`) with the shared Start/compositions strip +
  AudioMeters (`AudioMetersPanel`, hidden by default). The Start form is embedded as
  the Timeline's second page, so there is no separate system dock strip.

| # | Source | Form | Root widget / objectName |
|---|--------|------|--------------------------|
| 6.1 | `MediaPanel.cpp` | `Media.ui` | `MediaPanel` / `media-panel` (QWidget) |
| 6.2 | `TextPanel.cpp` | `Text.ui` | `TextPanel` / `textPanel` (QWidget) |
| 6.3 | `EffectsPanel.cpp` | `Effects.ui` | `EffectsPanel` / `effectsPanel` (QWidget) |
| 6.4 | `EffectInspector.cpp` | `Controls.ui` | `EffectInspector` / `controlsPanel` (QDockWidget) |
| 6.5 | `LayoutPanel.cpp` | `Layout.ui` | `LayoutPanel` / `Layout` (QDockWidget) |
| 6.6 | `TrackPanel.cpp` | `Track.ui` | `TrackPanel` / `trackPanel` (QDockWidget) |
| 6.7 | `LayerPanel.cpp` | `Layer.ui` | `LayerPanel` / `layerPanel` (QDockWidget) |
| 6.8 | `HistoryPanel.cpp` | `History.ui` | `HistoryPanel` / `History` (QDockWidget) |
| 6.9 | `LibraryPanel.cpp` | `Library.ui` | `LibraryPanel` / `libraryPanel` (QDockWidget) |
| 6.10 | `Preview360VideoPanel.cpp` | `Preview360.ui` | `Preview360VideoPanel` (QDockWidget) |
| 6.11 | `AudioMetersPanel.cpp` | `AudioMeters.ui` | `AudioMetersPanel` / `AudioMetersWidget` (QDockWidget) |
| 6.12 | `StartPanel.cpp` | `Start.ui` | `StartPanel` / `StartPanelWidget` (QDockWidget) |
| 6.13 | `TrimmerPanel.cpp` | `Trimmer.ui` | `TrimmerPanel` / `trimmer-panel` (QWidget) |
| 6.14 | `TimelineWidget.cpp` | `Timeline.ui` | `TimelineWidget` / `timelinePanel` (QDockWidget) |
| 6.15 | `ExportPanel.{h,cpp}` + `ui/ExportPanel.ui` | `Export.ui` (spec) | `ExportPanel` / `export-panel` (QWidget) |

## 6.1 Media — `Media.ui`

`media-panel` follows the vertical order of the reference screenshots: a top row with
`toolButtonImport` / `MediaPanelNewFolderButton` and two view buttons,
`media-panel-search` ("Search in Project Media"), a separate row with
`mediaArrangeButton` / `mediaGroupButton`, the `QListWidget listView` (drag and drop) and
a bottom row with `MediaPanelFolderButton` / `MediaPanelCompositeShotButton` /
`MediaPanelTrashButton` and the `media-panel-count` counter. All the sort, group and
view menus are connected; both folder commands work, and Composite Shot uses the shared,
tested path that creates a nested composition from the selected layers. The context
menu and drag and drop stay in code.

## 6.2 Text — `Text.ui`

`textPanel` has three groups: `groupBoxCharacter` (font, `spinBoxFontSize`, a color
button with `QColorDialog`, bold/italic/underline/strikethrough, stroke distance, blur,
direction and spread, `strokeColor`, `strokeWidth`, the `tracking`/`leading` metrics),
`groupBoxParagraph` (alignment `toolButtonAlignLeft/Center/Right`, vertical alignment,
the `left/right/top/bottom` margin spin boxes) and `groupBoxBackground`
(`checkBoxBackground`, color, radius). The form is connected through `setupUi()`. The
code assigns the extracted SVGs to all toggle buttons, adds a horizontal scrub gesture
to the designer spin boxes, creates the extra strokes and color picker rows; the root
layout can shrink to the dock's width without a hidden horizontal scroll range.

## 6.3 Effects — `Effects.ui`

`effectsPanel` — the `effects-panel-search` "Search field", the "show all types" check
box `effects-panel-show-all`, a `QTreeWidget` (2 columns, headers hidden:
effects/transitions) and `effects-panel-count`. The tree is filled from
`EffectsPanel::EffectsListModel`; showing `toolButtonOptions` in the search row is code.

## 6.4 Controls (EffectInspector) — `Controls.ui`

The QDockWidget `controlsPanel` with the root widget `ControlsPanel` (a spacer from the
inspector; clicking the pin hides the panel). Inside: `controlsSelectionTitle` +
`controlsPin`; `controlsCurrentTime` (a clickable timecode) + a clock icon;
`controlsSearch` + `controlsCollapseAll` + `controlsExpandAll`; `controlsTree` — a
`QTreeWidget` (2 columns, hidden header, indentation 14). The inspector builds and
removes the property rows of the selected items on the fly — the form holds only the
skeleton.

## 6.5 Layout — `Layout.ui`

The QDockWidget `LayoutPanel`: the form is connected to `LayoutPanel.cpp`. Inside the
scroll area — `TransformWidget`: two mirror buttons, a gap, two 90° rotations, a 3×3
pivot with the original icons, X/Y/Width/Height and the size link. Then
`AlignmentWidget`: the Alignment title, Selection/Timeline, six alignments and six
distributions with a gap between the triples. The original PNG states and @2x variants
are in `resources/icons/layout`. All selected unlocked layers are edited in one Undo
step; animated properties change at the current frame and keep their keys. The exact
limits of the recovery and the Ghidra addresses: `MARKDOWN/RE_wnd_Layout.md`.

## 6.6 Track — `Track.ui`

The QDockWidget `trackPanel`: `widgetLayerHeader` (a QFrame with the buttons
`toolButtonNewLayer`, `toolButtonMoveLayerUp`, `toolButtonMoveLayerDown`,
`toolButtonRemoveLayer`) and `composition-layers-header` — a QTreeWidget with the
columns Name / Visible / Muted / Blend / Opacity (PosX/PosY in the options). The form
holds a static header grid; the layer rows are built by a separate class.

## 6.7 Layer — `Layer.ui`

The QDockWidget `layerPanel` → `stackedWidget`: `pageLayer` with `labelBreadCrumbTrail`,
a separate `layerPreview` view and the `layerPropertiesWidget` form (rows: `layerType`
read-only, `layerName`, `layerVisible`, `layerMuted`, `layerLocked`, `layerBlendMode`,
`layerOpacity`, `layerDimension`, `layerParent`, `layerLabelColor`, `layerPlaneColor`,
`layerCameraFov`, `layerID`) and `pageNoLayer` (`dummyWidget` — "no layer selected").
The growing form's properties and the changes on selection are code. `layerPreview`
shows the layer's source and its masks without its effects, transform and opacity, as
the reference's `RenderLayerViewJob` does (`MARKDOWN/RE_wnd_Layer.md`); a text layer has
no preview.

## 6.8 History — `History.ui`

The QDockWidget `History` → `HistoryDockWidget` (root): a tool bar
`toolButtonUndo`/`toolButtonRedo`/`toolButtonClear` + `listViewHistory` (recent actions,
a click reverts). The history lives in `MainWindow`; the form is a skeleton.

## 6.9 Library — `Library.ui`

The QDockWidget `libraryPanel` → root `LibraryPanel`: `libraryStatus` (a QLabel
"Library"), `toolButtonReloadLibrary`, `libraryList` (QListWidget). Filling it from the
folders is code.

## 6.10 Preview360 — `Preview360.ui`

The QDockWidget `Preview360VideoPanel` → `Preview360VideoPanelWidget` (the `360-viewer`
wrapper): a tool bar with `comboBoxCurrentView` (7 directions: Front/Back/Left/Right/
Top/Bottom/Custom), `toolButtonProperties` opens the Yaw/Pitch/Roll/FOV, camera and wrap
editor; `glWidget` is `Viewer360View`, which draws the frame through the reference's
EnvironmentMapViewer module and, without it, with its own perspective projection on the
CPU (`MARKDOWN/RE_wnd_Viewer360.md`). Rendering and interaction are code.

## 6.11 AudioMeters — `AudioMeters.ui`

The QDockWidget `AudioMetersPanel` → `AudioMetersWidget`: `AudioMetersHoldPeaks` (a
QCheckBox "Hold Peaks", from QSettings), `toolButtonResetPeaks`, the `InputLevels` /
`OutputLevels` columns (LED columns, `AudioMeterDisplay`, min 32×90; colors
`Theme/meterBackgroundColor` #111111, hold peak #eeeeee). The columns are painted in
paintEvent; the form has placeholder slots. The levels come from the shared PCM
observer in `AudioPlayer`: `MainWindow` updates both columns and the inline Viewer
indicator from one `levelsChanged` signal.

## 6.12 Start — `Start.ui`

The QDockWidget `StartPanel` → `StartPanelWidget`: the action buttons "New Composite
Shot" (`toolButtonNewComp`), "New Composite Shots From Footage"
(`toolButtonNewCompsFromMedia`), "Import File" (`AddMedia`), "Edit Screen"
(`toolButtonEditScreen`) — all minHeight 46, text only; on the right
`ProjectSideBarWidget` with "Jump back in" (`toolButtonNew`, `toolButtonOpen`,
`toolButtonClearRecents`) and `listViewRecentProjects`; a learning strip below. The
recent list is code.

## 6.13 Trimmer — `Trimmer.ui`

The `trimmer-panel` widget inside the `TrimmerDock` dock of the shared Viewer group
(QWidget, drops enabled). The "trimmer-toolbar-top" tool bar: `trimmerSetIn`/
`trimmerSetOut`, `trimmerPosition` (timecode), `trimmerInsert`, `trimmerOverlay`; the
`trimmer-frame` frame (class `TrimmerWidget`, draw strip); "trimmer-toolbar-bottom":
`TrimmerSlider` (0..1000), `trimmerRange`; `trimmerSourceList` (QListWidget, maxHeight
90). The canvas is paintEvent; the form has a placeholder.

## 6.14 Timeline — `Timeline.ui`

The QDockWidget `timelinePanel` → `TimelinePanelWidget` (dark QSS #272727).
`timelineToolStrip` (23 px, the Select/Hand/Slice/Rate Stretch tools + a stretch menu
for Slip/Slide/Ripple/Roll/TrackSelect + `timelineSnap` + `timelineSettings`). The
header: `timelineClock`, `timelineTimecode`, `timelineNewLayer`,
`toolButtonMakeCompositeShot`, the keyframe bar (`timelinePreviousKeyframe`,
`timelineToggleKeyframe`, `timelineNextKeyframe`, `timelineKeyFrameType*` for all
interpolations), `timelineValueGraph`, `timelineGraphAutoZoom`, `timelineRenderCache`,
`timelineExport`. The body: a `QSplitter` → `leftColumn` (the `timelineSearch` row +
`timelineLayerTree` + `timelineZoomOut/Slider/In`) and `stackedWidgetTimelines` (tree
canvas / value graph); on the right `timelineVScroll` (mirrored). The outer
`timelinePanelPages` switches Editor/Start, and a single 25 px footer holds
`timelineStartTab` and `timelineCompositionTabs` side by side, then the panel menu on
the right. The native QDockWidget frame and the QStatusBar are hidden so that no second
row appears under the footer. The layer tree, the canvas and the value graph are the
promoted `TimelineTree`, `TimelineCanvas` and `TimelineValueGraphView`; the row
structure and `timelinePreset_*` / `timelineTrackEnabled_*` / `timelineMask*` are
generated in code.

## 6.15 Export — `Export.ui` (spec copy)

The `export-panel` widget inside the `ExportDock` dock of the shared Viewer group.
**Important:** the working form already exists and **is connected** to the build —
`ui/ExportPanel.ui`; the `ExportPanel` class builds it through `Ui::ExportPanel`
(`setupUi`), and the constructor wraps it in an outer widget with `objectName
"export-panel"` and zero margins. The rest of the dynamics is in `ExportPanel.cpp`:
choosing the directory, restoring `Options/ExportDirectory` from QSettings, writing the
frame path to `plainTextProgress`, the `exportFrameRequested` /
`exportContentsRequested` signals.

`ui/panels/Export.ui` is **a specification, not connected and not loaded**, created for
consistency with the other forms. Its root is a `QDockWidget`
(`objectName "export-panel"`), as in the other panel windows, so that the frame has the
standard dock buttons: **undocking into a full window**
(`QDockWidget::DockWidgetFloatable`) and **closing** (`DockWidgetClosable`; together with
`DockWidgetMovable` in the `features` property). Inside is the `ExportPanel` body widget
with a tree matching the connected form one to one by reference object names:
`lineEditExportDirectory`, `toolButtonExportDirectory` ("…"), `comboBoxPreset`
(H.264 (.mp4) / PNG / JPEG / OpenEXR / ProRes), `pushButtonExportFrame` ("Export
Frame"), `pushButtonExportContents` ("Export Contents"), `plainTextProgress`
(read-only). A rule for the future: keep both forms in sync by objectName (tests look up
the connected form's names), or delete the spec if it drifts away, so as not to
duplicate reference names. Note that in the working code ExportPanel is a QWidget
inside the working `ExportDock`: moving, detaching and closing are provided by the
QDockWidget shell in MainWindow, and the body form stays the same.

## 7. General observations for the panels

- **Reference object names** in all forms match the constructors one to one,
  including suffixed ones (for example `timelineKeyFrameType` + the keyframe icon name).
- **Custom painting** (`TimelineCanvas`, `TimelineValueGraphView`, `TrimmerWidget`,
  `AudioMeterDisplay`, the 360 canvas, the checkerboard) is `paintEvent`; in the forms
  these widgets are either promoted to the working classes (Timeline, Viewer) or
  replaced by them after `setupUi()` (Trimmer, Preview360, AudioMeters).
- **Icons** are set in code with a text fallback (`makeButton`); the forms hold no
  icons and do not reference the resources (`icons.qrc` is not pulled into the forms).
- **Dynamic parts** (filling lists, menus, drag and drop, signals/slots, syncing with
  the selection) stay in code — the forms describe only the static skeleton.
- **Build:** `uic` compiles the 15 connected forms (`panels/*.ui` except `Export.ui`;
  AUTOUIC in CMake, `FORMS` in qmake); `Export.ui` is a specification and is not built.
- `timeline_regression.cpp` and the other tests look up the objectNames of layouts,
  buttons and tool strip buttons — keep these names when editing the forms.

## 8. Default layout (27.09.2026)

The central tab row: **Viewer / Trimmer / Export / Layer / 360 Viewer**. All five
panels are in one native dock group with the tabs at the bottom. Layer and 360 Viewer
are available right away; the Viewer tab is active. Media/Text stay on the left,
Effects/Controls/Layout/Track/Library/History on the right, and the Timeline below all
groups. QMainWindow works without a separate centralWidget; this keeps the native drag,
split/tab targets, float/dock and the panel menus. The layout state has version 4:
outdated saves do not restore the former arrangement on the right.

The `widgetQuickActions` bar under the main menu is visible by default:
Logo/Open/Save/Options/Cut/Copy/Paste/Undo/Redo. `Options/QuickActionsLayoutVersion=1`
fixes the former hiding once; after that the `ShowMenuBarQuickActions` choice applies
without a reset. The `timelineGraphAutoZoom` button uses IconOnly and keeps its tooltip,
accessible name and existing toggled handler.
