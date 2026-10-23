# 面板窗口迁移到 *.ui 表单

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

本文档介绍各面板的 Qt Designer 表单，以及它们与实际工作类之间的联系。这里列出的所有表单
都已纳入 CMake 和 qmake 构建；面板的构造函数调用 `setupUi()`，而动态列表、菜单、绘制和
信号仍保留在 C++ 中。`ui/panels/Export.ui` 作为参考副本保留：实际使用的 Export 面板采用的
是早已接入的表单 `ui/ExportPanel.ui`。

## 1. 研究了什么

| 源文件 | 得到的信息 |
|--------|------------|
| `src/ui/MainWindow.cpp`（`buildUi`） | 共享停靠组 Viewer / Trimmer / Export / Layer / 360 Viewer；`viewerPage` → 画布 + 内嵌传输栏 |
| `src/ui/ViewerWidget.{h,cpp}` | 画布本身（`graphicsViewViewer`）、布局管理器（Single/Row/TwoOverOne/Four/RowFour）、工具条（`viewer-tool-strip`、按钮）、`createViewOptionsBar()` → `toolButtonView`、`toolButtonOption`、`toolButtonPlaybackQuality` |
| `src/ui/ViewerTransportBar.{h,cpp}` | `ViewerPlaybackWidget` 的命名、进度条 `progressBarRender`、拖动行（`spinBoxCurrentTime`、`sliderCurrentFrame`、可编辑且支持撤销／重做的 `spinBoxFrameCount`／Timeline Duration）、按钮行（分组顺序）、用于视图选项的 `addTrailingWidget(...)` |
| `src/ui/ViewScaleButton.{h,cpp}` | 缩放档位（Fit、12%…200%）：“−”、标签 `viewScaleLabel`、“+”；objectName 被覆盖为 `toolButtonZoom` |
| `resources/icons.qrc` | 可用图标 `/icons/*` 的集合 |
| `ui/ExportPanel.ui`、`ui/MainWindow.ui` | 项目 .ui 的写法：`<class>` = 所属类、根控件名、显式的 margins/spacing、不翻译的文本使用 `<string notr="true">` |

## 2. 迁移了什么

`Viewer.ui` 负责中央标签页的布局，并使用两个提升（promote）的工作控件：

```
ViewerPanel（QWidget，类 openvegas::ui::ViewerPanel 的表单）
└── rootLayout（QVBoxLayout，margins 0，spacing 0）
    ├── graphicsViewViewer（ViewerWidget；最小 320×180）
    └── ViewerPlaybackWidget（ViewerTransportBar）
```

表单类是 `openvegas::ui::ViewerPanel`。内部的 `ViewerWidget` 和 `ViewerTransportBar` 保留
各自的参考对象名、工具提示、专用样式和实际连接。`ViewerPanel` 从表单中取得这两个指针，
并把 Viewer 的选项栏追加到 `ViewerTransportBar` 的末尾。

## 3. 留在代码中的部分（动态部分）

以下部分**有意没有**迁移到 .ui —— 它们不是静态的控件树：

- **画布** —— 提升后的 `ViewerWidget`；帧、运动路径、时间码和鼠标坐标都在它的
  `paintEvent` 中绘制。
- **工具条** —— 24 px 的宽度和左侧位置由代码设置（`toolStripRect`）；22×22 px 的按钮以
  1 像素边距对齐到同一条轴线上，局部样式使它们不受通用 `QToolButton` 边距影响；光标快捷键
  （V/H/T/R/E/F/B）、`toolButtonMaskShape` 上的形状弹出菜单（Rec/RRect/Ellipse/Polygon/
  Star）和下拉菜单也都在代码中。
- **传输／选项按钮的菜单**：“View”（布局）、“Options”（Show Motion Path、Background
  Color、Checkerboard、Color Channels、Full Screen Preview、Scale/Fit/In/Out）、Quality
  （4 个配置 × 播放／暂停）、缩放档位。
- **图标** —— 在代码中设置（`makeButton`），SVG 无法栅格化时回退为文本；表单不引用资源，
  因此不会引入 `icons.qrc`。
- **信号／槽** —— 全部连接（`scrubRequested`、`playPauseRequested` 等）。

## 4. 如何接入

1. `ViewerWidget` 和 `ViewerTransportBar` 在表单的 `<customwidgets>` 中声明。
2. `ViewerPanel` 调用 `setupUi()`，并把实际的指针交给 `MainWindow`。
3. `CMAKE_AUTOUIC_SEARCH_PATHS` 包含 `ui/panels`；这些表单也列在
   `OpenVegasEffects.pro` 中。
4. Timeline 使用提升后的 `TimelineTree`、`TimelineCanvas` 和 `TimelineValueGraphView`；
   Trimmer、Preview360 和 Meters 只在 `setupUi()` 之后把绘制用的占位控件替换为实际的自定义
   控件。

## 5. 观察

- `audioMeterIcon` —— 一个没有标签、由绘制实现的紧凑型双声道 LED 指示器。它从
  `AudioPlayer` 获取主输出的 RMS，位于 Options 之前，单击可打开完整的 Audio Meters 面板。
- `toolButtonZoom` —— 复合控件：在代码中 `ViewScaleButton` 先使用自己的名字
  `ViewScaleButton`，随后被覆盖为 `toolButtonZoom`。表单中保留最终的（参考）名称，以免
  主题和测试失效。
- `sliderCurrentFrame` 在 `duration == 0` 时由代码禁用（在 `setDuration` 中启用）。表单
  中的 `enabled=false` 对应新项目的初始状态。
- 左侧和中间分组的按钮顺序是参考程序构造函数中的顺序（Export Frame → Loop → In → Out →
  Meters | First → Prev → Next → Play），并非重新设计。

---

# 6. MainWindow 的其他面板

主窗口的其他所有停靠面板和标签页都使用类似的表单。表单类是拥有该控件树的类
（`openvegas::ui::…`），控件名称是**参考对象名**（主题和测试按名称查找），动态部分
（paintEvent、弹出菜单、列表、图标）以注释形式写在静态骨架之上。**Export** 在
`ui/panels/` 中创建了一个规格副本（`Export.ui`），但实际实现仍使用已接入的表单
`ui/ExportPanel.ui`（见 6.15）。

`src/ui/MainWindow.cpp` 中的停靠树，“Effects”界面：

- **左侧：** Media（`mediaPanel`）+ Text（`textPanel`，都在左侧停靠区）
- **中央：** 共享停靠组 Viewer / Trimmer / Export / Layer / 360 Viewer
- **右侧：** Controls / Effects / Layout / Track / Library / History + Learn（原生停靠
  面板，隐藏）
- **底部：** Timeline（`timelinePanel`），带有共享的 Start／合成条 + AudioMeters
  （`AudioMetersPanel`，默认隐藏）。Start 表单作为 Timeline 的第二页嵌入，因此没有单独的
  系统停靠条。

| # | 源文件 | 表单 | 根控件 / objectName |
|---|--------|------|---------------------|
| 6.1 | `MediaPanel.cpp` | `Media.ui` | `MediaPanel` / `media-panel`（QWidget） |
| 6.2 | `TextPanel.cpp` | `Text.ui` | `TextPanel` / `textPanel`（QWidget） |
| 6.3 | `EffectsPanel.cpp` | `Effects.ui` | `EffectsPanel` / `effectsPanel`（QWidget） |
| 6.4 | `EffectInspector.cpp` | `Controls.ui` | `EffectInspector` / `controlsPanel`（QDockWidget） |
| 6.5 | `LayoutPanel.cpp` | `Layout.ui` | `LayoutPanel` / `Layout`（QDockWidget） |
| 6.6 | `TrackPanel.cpp` | `Track.ui` | `TrackPanel` / `trackPanel`（QDockWidget） |
| 6.7 | `LayerPanel.cpp` | `Layer.ui` | `LayerPanel` / `layerPanel`（QDockWidget） |
| 6.8 | `HistoryPanel.cpp` | `History.ui` | `HistoryPanel` / `History`（QDockWidget） |
| 6.9 | `LibraryPanel.cpp` | `Library.ui` | `LibraryPanel` / `libraryPanel`（QDockWidget） |
| 6.10 | `Preview360VideoPanel.cpp` | `Preview360.ui` | `Preview360VideoPanel`（QDockWidget） |
| 6.11 | `AudioMetersPanel.cpp` | `AudioMeters.ui` | `AudioMetersPanel` / `AudioMetersWidget`（QDockWidget） |
| 6.12 | `StartPanel.cpp` | `Start.ui` | `StartPanel` / `StartPanelWidget`（QDockWidget） |
| 6.13 | `TrimmerPanel.cpp` | `Trimmer.ui` | `TrimmerPanel` / `trimmer-panel`（QWidget） |
| 6.14 | `TimelineWidget.cpp` | `Timeline.ui` | `TimelineWidget` / `timelinePanel`（QDockWidget） |
| 6.15 | `ExportPanel.{h,cpp}` + `ui/ExportPanel.ui` | `Export.ui`（规格） | `ExportPanel` / `export-panel`（QWidget） |

## 6.1 Media —— `Media.ui`

`media-panel` 按参考截图的纵向顺序排列：顶部一行是 `toolButtonImport` /
`MediaPanelNewFolderButton` 和两个视图按钮，然后是 `media-panel-search`（“Search in
Project Media”），单独一行的 `mediaArrangeButton` / `mediaGroupButton`，
`QListWidget listView`（支持拖放），底部一行是 `MediaPanelFolderButton` /
`MediaPanelCompositeShotButton` / `MediaPanelTrashButton` 以及计数器
`media-panel-count`。所有排序、分组和视图菜单都已接入；两个新建文件夹命令都可用，
Composite Shot 使用经过测试的共享流程，从所选图层创建嵌套合成。上下文菜单和拖放仍在代码中。

## 6.2 Text —— `Text.ui`

`textPanel` 有三个分组：`groupBoxCharacter`（字体、`spinBoxFontSize`、使用 `QColorDialog`
的颜色按钮、粗体／斜体／下划线／删除线、描边的距离、模糊、方向和扩展、`strokeColor`、
`strokeWidth`、度量 `tracking`/`leading`），`groupBoxParagraph`（对齐
`toolButtonAlignLeft/Center/Right`、垂直对齐、`left/right/top/bottom` 边距微调框），
`groupBoxBackground`（`checkBoxBackground`、颜色、半径）。表单通过 `setupUi()` 接入。代码
为所有切换按钮指定提取出的 SVG，为设计器中的微调框添加水平拖动手势，并创建额外的描边和
取色行；根布局可以收缩到停靠区宽度，而不会产生隐藏的水平滚动范围。

## 6.3 Effects —— `Effects.ui`

`effectsPanel` —— `effects-panel-search`“Search field”、“show all types”复选框
`effects-panel-show-all`、`QTreeWidget`（2 列，隐藏表头：特效／转场）和
`effects-panel-count`。树由 `EffectsPanel::EffectsListModel` 填充；搜索行中
`toolButtonOptions` 的显示由代码负责。

## 6.4 Controls（EffectInspector）—— `Controls.ui`

QDockWidget `controlsPanel`，根控件为 `ControlsPanel`（来自检查器的占位；单击图钉会隐藏
面板）。内部：`controlsSelectionTitle` + `controlsPin`；`controlsCurrentTime`（可点击的
时间码）+ 时钟图标；`controlsSearch` + `controlsCollapseAll` + `controlsExpandAll`；
`controlsTree` —— `QTreeWidget`（2 列，隐藏表头，缩进 14）。检查器会即时创建和删除所选
项目的属性行——表单中只有骨架。

## 6.5 Layout —— `Layout.ui`

QDockWidget `LayoutPanel`：表单已接入 `LayoutPanel.cpp`。滚动区域内是 `TransformWidget`：
两个镜像按钮、间隔、两个 90° 旋转、使用原始图标的 3×3 锚点、X/Y/Width/Height 以及尺寸
联动。接着是 `AlignmentWidget`：Alignment 标题、Selection/Timeline、六个对齐和六个分布，
每三个之间留有间隔。原始 PNG 的各状态及 @2x 版本位于 `resources/icons/layout`。所有选中的
未锁定图层在一次撤销操作中编辑；带动画的属性在当前帧修改并保留关键帧。恢复的确切范围和
Ghidra 地址见 `MARKDOWN/RE_wnd_Layout.md`。

## 6.6 Track —— `Track.ui`

QDockWidget `trackPanel`：`widgetLayerHeader`（包含按钮 `toolButtonNewLayer`、
`toolButtonMoveLayerUp`、`toolButtonMoveLayerDown`、`toolButtonRemoveLayer` 的 QFrame）
以及 `composition-layers-header` —— 列为 Name / Visible / Muted / Blend / Opacity
（PosX/PosY 在选项中）的 QTreeWidget。表单中是静态的表头网格；图层行由单独的类构建。

## 6.7 Layer —— `Layer.ui`

QDockWidget `layerPanel` → `stackedWidget`：`pageLayer` 包含 `labelBreadCrumbTrail`、单独
的预览 `layerPreview` 和表单 `layerPropertiesWidget`（行：只读的 `layerType`、
`layerName`、`layerVisible`、`layerMuted`、`layerLocked`、`layerBlendMode`、
`layerOpacity`、`layerDimension`、`layerParent`、`layerLabelColor`、`layerPlaneColor`、
`layerCameraFov`、`layerID`）；`pageNoLayer`（`dummyWidget` —— “no layer selected”）。
可扩展表单的属性和随选择变化的更新由代码负责。`layerPreview` 与参考程序的
`RenderLayerViewJob` 一样，显示图层的源和蒙版，不含其特效、变换和不透明度
（`MARKDOWN/RE_wnd_Layer.md`）；文本图层没有预览。

## 6.8 History —— `History.ui`

QDockWidget `History` → `HistoryDockWidget`（根）：工具栏
`toolButtonUndo`/`toolButtonRedo`/`toolButtonClear` + `listViewHistory`（最近的操作，
单击即回退）。历史记录保存在 `MainWindow` 中；表单只是骨架。

## 6.9 Library —— `Library.ui`

QDockWidget `libraryPanel` → 根 `LibraryPanel`：`libraryStatus`（QLabel“Library”）、
`toolButtonReloadLibrary`、`libraryList`（QListWidget）。从文件夹加载内容由代码负责。

## 6.10 Preview360 —— `Preview360.ui`

QDockWidget `Preview360VideoPanel` → `Preview360VideoPanelWidget`（`360-viewer` 包装）：
带有 `comboBoxCurrentView`（7 个方向：Front/Back/Left/Right/Top/Bottom/Custom）的工具栏，
`toolButtonProperties` 打开 Yaw/Pitch/Roll/FOV、摄像机和环绕设置的编辑器；`glWidget` 是
`Viewer360View`，它通过参考程序的 EnvironmentMapViewer 模块绘制帧，没有该模块时则使用自己
在 CPU 上的透视投影（`MARKDOWN/RE_wnd_Viewer360.md`）。渲染和交互由代码负责。

## 6.11 AudioMeters —— `AudioMeters.ui`

QDockWidget `AudioMetersPanel` → `AudioMetersWidget`：`AudioMetersHoldPeaks`（QCheckBox
“Hold Peaks”，来自 QSettings）、`toolButtonResetPeaks`、`InputLevels` / `OutputLevels`
列（LED 列，`AudioMeterDisplay`，最小 32×90；颜色 `Theme/meterBackgroundColor` #111111，
峰值保持 #eeeeee）。各列在 paintEvent 中绘制，表单中是占位槽。电平来自 `AudioPlayer` 中
共享的 PCM 观察器：`MainWindow` 通过一个 `levelsChanged` 信号同时更新两列和 Viewer 内的
指示器。

## 6.12 Start —— `Start.ui`

QDockWidget `StartPanel` → `StartPanelWidget`：操作按钮“New Composite Shot”
（`toolButtonNewComp`）、“New Composite Shots From Footage”
（`toolButtonNewCompsFromMedia`）、“Import File”（`AddMedia`）、“Edit Screen”
（`toolButtonEditScreen`）——均为 minHeight 46、仅文本；右侧是带“Jump back in”
（`toolButtonNew`、`toolButtonOpen`、`toolButtonClearRecents`）和
`listViewRecentProjects` 的 `ProjectSideBarWidget`；下方是学习条。最近项目列表由代码负责。

## 6.13 Trimmer —— `Trimmer.ui`

位于共享 Viewer 组的停靠面板 `TrimmerDock` 中的控件 `trimmer-panel`（QWidget，允许拖放）。
工具栏“trimmer-toolbar-top”：`trimmerSetIn`/`trimmerSetOut`、`trimmerPosition`
（时间码）、`trimmerInsert`、`trimmerOverlay`；帧 `trimmer-frame`（类 `TrimmerWidget`，
绘制条）；“trimmer-toolbar-bottom”：`TrimmerSlider`（0..1000）、`trimmerRange`；
`trimmerSourceList`（QListWidget，maxHeight 90）。画布在 paintEvent 中绘制，表单中是占位控件。

## 6.14 Timeline —— `Timeline.ui`

QDockWidget `timelinePanel` → `TimelinePanelWidget`（深色 QSS #272727）。
`timelineToolStrip`（23 px，工具 Select/Hand/Slice/Rate Stretch + Slip/Slide/Ripple/Roll/
TrackSelect 的伸缩菜单 + `timelineSnap` + `timelineSettings`）。标题栏：`timelineClock`、
`timelineTimecode`、`timelineNewLayer`、`toolButtonMakeCompositeShot`、关键帧栏
（`timelinePreviousKeyframe`、`timelineToggleKeyframe`、`timelineNextKeyframe`、覆盖所有
插值类型的 `timelineKeyFrameType*`）、`timelineValueGraph`、`timelineGraphAutoZoom`、
`timelineRenderCache`、`timelineExport`。主体：`QSplitter` → `leftColumn`
（`timelineSearch` 行 + `timelineLayerTree` + `timelineZoomOut/Slider/In`）和
`stackedWidgetTimelines`（树状画布／数值曲线图）；右侧是 `timelineVScroll`（镜像）。外层
的 `timelinePanelPages` 在 Editor/Start 之间切换，一条 25 px 的底栏并排放置
`timelineStartTab` 和 `timelineCompositionTabs`，最右侧是面板菜单。原生 QDockWidget 的边框
和 QStatusBar 被隐藏，以免在底栏下方出现第二行。图层树、画布和数值曲线图是提升后的
`TimelineTree`、`TimelineCanvas` 和 `TimelineValueGraphView`；行结构以及
`timelinePreset_*` / `timelineTrackEnabled_*` / `timelineMask*` 由代码生成。

## 6.15 Export —— `Export.ui`（规格副本）

位于共享 Viewer 组的停靠面板 `ExportDock` 中的控件 `export-panel`。**重要：** 实际使用的
表单已经存在并**已接入**构建——`ui/ExportPanel.ui`；类 `ExportPanel` 通过
`Ui::ExportPanel`（`setupUi`）构建它，构造函数再用 `objectName "export-panel"`、边距为 0
的外层控件包裹它。其余动态逻辑在 `ExportPanel.cpp` 中：选择目录、从 QSettings 恢复
`Options/ExportDirectory`、把帧路径输出到 `plainTextProgress`、信号
`exportFrameRequested` / `exportContentsRequested`。

`ui/panels/Export.ui` 是**规格，未接入也不会加载**，创建它是为了与其他表单保持一致。它的
根是 `QDockWidget`（`objectName "export-panel"`），与其他面板窗口相同，因此边框上带有标准
的停靠按钮：**分离为独立窗口**（`QDockWidget::DockWidgetFloatable`）和**关闭**
（`DockWidgetClosable`；与 `DockWidgetMovable` 一起写在 `features` 属性中）。内部是主体
控件 `ExportPanel`，其控件树按参考对象名与已接入的表单一一对应：
`lineEditExportDirectory`、`toolButtonExportDirectory`（“…”）、`comboBoxPreset`
（H.264 (.mp4) / PNG / JPEG / OpenEXR / ProRes）、`pushButtonExportFrame`（“Export
Frame”）、`pushButtonExportContents`（“Export Contents”）、`plainTextProgress`（只读）。
今后的规则：让两个表单按 objectName 保持同步（测试按已接入表单的名称查找），或者在规格
偏离时将其删除，以免参考名称重复。请注意，在实际代码中 ExportPanel 是工作中的
`ExportDock` 里的一个 QWidget：移动、分离和关闭由 MainWindow 中的 QDockWidget 外壳提供，
主体表单保持不变。

## 7. 面板的通用观察

- 所有表单中的**参考对象名**都与构造函数一一对应，包括带后缀的名称（例如
  `timelineKeyFrameType` + 关键帧图标名）。
- **自定义绘制**（`TimelineCanvas`、`TimelineValueGraphView`、`TrimmerWidget`、
  `AudioMeterDisplay`、360 画布、棋盘格）都在 `paintEvent` 中；在表单中，这些控件要么被
  提升为实际的类（Timeline、Viewer），要么在 `setupUi()` 之后被它们替换（Trimmer、
  Preview360、AudioMeters）。
- **图标**在代码中设置，并可回退为文本（`makeButton`）；表单中不包含图标，也不引用资源
  （不把 `icons.qrc` 引入表单）。
- **动态部分**（填充列表、菜单、拖放、信号／槽、与选择同步）留在代码中——表单只描述静态
  骨架。
- **构建：** `uic` 编译 15 个已接入的表单（除 `Export.ui` 外的 `panels/*.ui`；CMake 中为
  AUTOUIC，qmake 中为 `FORMS`）；`Export.ui` 是规格，不参与构建。
- `timeline_regression.cpp` 和其他测试按布局、按钮和工具条按钮的 objectName 查找——编辑
  表单时请保留这些名称。

## 8. 默认布局（2026 年 9 月 27 日）

中央标签行：**Viewer / Trimmer / Export / Layer / 360 Viewer**。这五个面板都在同一个原生
停靠组中，标签位于底部。Layer 和 360 Viewer 可以直接使用；当前激活的是 Viewer 标签。左侧
是 Media/Text，右侧是 Effects/Controls/Layout/Track/Library/History，所有分组下方是
Timeline。QMainWindow 不使用单独的 centralWidget；这样可以保留原生的拖动、拆分／标签目标、
浮动／停靠以及面板菜单。布局状态的版本为 4：过时的保存内容不会恢复以前位于右侧的排列。

主菜单下方的 `widgetQuickActions` 栏默认可见：Logo/Open/Save/Options/Cut/Copy/Paste/
Undo/Redo。`Options/QuickActionsLayoutVersion=1` 会一次性修正以前的隐藏状态；之后对
`ShowMenuBarQuickActions` 的选择会直接生效而不重置。`timelineGraphAutoZoom` 按钮使用
IconOnly，并保留其工具提示、无障碍名称和现有的 toggled 处理函数。
