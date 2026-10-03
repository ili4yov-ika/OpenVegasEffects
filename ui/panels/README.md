# Рапорт по переносу панель-окон в *.ui

Документ описывает формы панелей Qt Designer и их связь с рабочими классами.
Все перечисленные формы подключены к CMake и qmake; конструкторы панелей вызывают
`setupUi()`, а динамические списки, меню, рисование и сигналы остаются в C++.
`ui/panels/Export.ui` сохранён как справочная копия: рабочий Export использует
ранее существовавшую подключённую форму `ui/ExportPanel.ui`.

## 1. Что изучалось

| Исходник | Что дал |
|----------|---------|
| `src/ui/MainWindow.cpp` (`buildUi`) | Сборка общей dock-группы Viewer / Trimmer / Export / Layer / 360 Viewer; `viewerPage` → канвас + inline transport bar |
| `src/ui/ViewerWidget.{h,cpp}` | Сам канвас (`graphicsViewViewer`), лейаут-менеджер (Single/Row/TwoOverOne/Four/RowFour), тул-стрип (`viewer-tool-strip`, кнопки), `createViewOptionsBar()` → `toolButtonView`, `toolButtonOption`, `toolButtonPlaybackQuality` |
| `src/ui/ViewerTransportBar.{h,cpp}` | Именование `ViewerPlaybackWidget`, прогресс `progressBarRender`, строка скраббинга (`spinBoxCurrentTime`, `sliderCurrentFrame`, редактируемый `spinBoxFrameCount`/Timeline Duration с Undo/Redo), ряд кнопок (cluster order), `addTrailingWidget(...)` для view-options |
| `src/ui/ViewScaleButton.{h,cpp}` | Зум-лесенка (Fit, 12%…200%): «−», ярлык `viewScaleLabel`, «+»; перекрытие objectName на `toolButtonZoom` |
| `resources/icons.qrc` | Набор доступных иконок `/icons/*` |
| `ui/ExportPanel.ui`, `ui/MainWindow.ui` | Стиль .ui проекта: `<class>` = класс владельца, имя корневого виджета, явные margins/spacing, `<string notr="true">` для нетранслируемого |

## 2. Что перенесено

`Viewer.ui` владеет компоновкой центральной вкладки и использует два
промотированных рабочих виджета:

```
ViewerPanel (QWidget, форма класса openvegas::ui::ViewerPanel)
└── rootLayout (QVBoxLayout, margins 0, spacing 0)
    ├── graphicsViewViewer (ViewerWidget; min 320×180)
    └── ViewerPlaybackWidget (ViewerTransportBar)
```

Класс формы — `openvegas::ui::ViewerPanel`. Вложенные `ViewerWidget` и
`ViewerTransportBar` сохраняют собственные reference object names, tooltips,
спец-стили и рабочие соединения. `ViewerPanel` получает оба указателя из формы
и добавляет панель опций Viewer в хвост `ViewerTransportBar`.

## 3. Что осталось в коде (динамика)

В .ui **намеренно не** перенесено — эти части не являются статическим деревом:

- **Канвас** — промотированный `ViewerWidget`; кадр, motion path, таймкод и
  координаты мыши рисуются в его `paintEvent`.
- **Тул-стрип** — ширина 24 px и позиция слева задаются кодом (`toolStripRect`);
  кнопки 22×22 px выровнены по общей оси с однопиксельными полями, а локальный
  стиль изолирует их от общих отступов `QToolButton`,
  курсорные шорткаты (V/H/T/R/E/F/B), flyout фигур на `toolButtonMaskShape`
  (Rec/RRect/Ellipse/Polygon/Star) и выпадающие меню.
- **Меню буттонов** transport/options: «View» (лейауты), «Options» (Show Motion
  Path, Background Color, Checkerboard, Color Channels, Full Screen Preview,
  Scale/Fit/In/Out), Quality (4 профиля × playback/paused), zoom-лесенка.
- **Иконки** — ставятся в коде (`makeButton`) с фолбэком на текст, когда SVG не
  растеризуется; в .ui ресурсы не подключены, чтобы не тащить `icons.qrc`.
- **Сигналы/слоты** — весь воркинг (`scrubRequested`, `playPauseRequested`, …).

## 4. Реализованное подключение

1. `ViewerWidget` и `ViewerTransportBar` объявлены в `<customwidgets>` формы.
2. `ViewerPanel` вызывает `setupUi()` и отдаёт рабочие указатели `MainWindow`.
3. `CMAKE_AUTOUIC_SEARCH_PATHS` включает `ui/panels`; формы перечислены также в
   `OpenVegasEffects.pro`.
4. Timeline использует промотированные `TimelineTree`, `TimelineCanvas` и
   `TimelineValueGraphView`; Trimmer, Preview360 и Meters заменяют только
   рисуемые заглушки на рабочие custom widgets после `setupUi()`.

## 5. Наблюдения

- `audioMeterIcon` — компактный рисуемый двухканальный LED-индикатор без подписи.
  Он получает master RMS из `AudioPlayer`, расположен перед Options и открывает
  полную панель Audio Meters по щелчку.
- `toolButtonZoom` — составной виджет: в коде `ViewScaleButton` сперва получает
  собственное имя `ViewScaleButton`, затем перекрывается на `toolButtonZoom`.
  В форме оставлено итоговое (reference) имя, чтобы темы/тесты не сломались.
- `sliderCurrentFrame` в коде неактивен, пока `duration == 0` (enable включается
  в `setDuration`). В форме флаг `enabled=false` соответствует начальному
  состоянию нового проекта.
- Порядок кнопок в левом и центре-кластере — порядок конструктора референса
  (Export Frame → Loop → In → Out → Meters | First → Prev → Next → Play), не
  выдуман заново.

---

# 6. Прочие панели MainWindow

Аналогичные формы используются всеми остальными док-панелями/вкладками главного окна.
Класс формы — класс владельца дерева (`openvegas::ui::…`),
имена виджетов — **reference object names** (темы и тесты ходят по ним),
динамика (paintEvent, попапы, списки, иконки) перенесена в комментарии и в
статический каркас. Для **Export** в `ui/panels/` создана spec-копия
(`Export.ui`), но рабочая реализация остаётся на существующей подключённой
форме `ui/ExportPanel.ui` (см. 6.15).

Дерево доков по `src/ui/MainWindow.cpp` (≈486–660), screen «Effects»:

- **Left:** Media (`mediaPanel`) + Text (`textPanel`, оба в левом доке)
- **Center:** стек `stackedWidgetScreens` — вкладки Viewer / Trimmer / Export
- **Right:** Controls / Effects / Layout / Track / Layer / Library / History
  и 360 Viewer (`Preview360VideoPanel`, скрыт) + Learn (нативный док, скрыт)
- **Bottom:** Timeline (`timelinePanel`) с общей полосой Start/композиций +
  AudioMeters (`AudioMetersPanel`, скрыт по умолчанию). Форма Start встроена
  второй страницей Timeline, поэтому отдельной системной dock-полосы нет.

| # | Исходник | Форма | Корневой виджет / objectName |
|---|----------|-------|------------------------------|
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

`media-panel` повторяет вертикальный порядок эталонных скриншотов: верхняя
строка `toolButtonImport` / `MediaPanelNewFolderButton` и две кнопки вида,
`media-panel-search` («Search in Project Media»), отдельная строка
`mediaArrangeButton` / `mediaGroupButton`, `QListWidget listView` (drag-drop) и
нижняя строка `MediaPanelFolderButton` / `MediaPanelCompositeShotButton` /
`MediaPanelTrashButton` со счётчиком `media-panel-count`. Все меню сортировки,
группировки и видов подключены; обе команды создания папки работают, а
Composite Shot использует общий проверенный путь создания вложенной композиции
из выделенных слоёв. Контекстное меню и drag/drop остаются в коде.

## 6.2 Text — `Text.ui`

`textPanel` — три группы: `groupBoxCharacter` (шрифт, `spinBoxFontSize`, цвет
`QColorDialog`-кнопка, жирный/курсив/подчёркивание/зачёркивание, дистанция/
блур/направление/растение обводок `strokeColor`, `strokeWidth`, метрики
`tracking`/`leading`), `groupBoxParagraph` (выравнивание
`toolButtonAlignLeft/Center/Right`, vertical-align, отступы `left/right/top/
bottom`-спинбоксы), `groupBoxBackground` (`checkBoxBackground`, цвет, радиус).
Форма подключена через `setupUi()`. Код назначает извлечённые SVG всем toggle-кнопкам,
подключает к designer-спинбоксам горизонтальный scrub жест, создаёт дополнительные
обводки и color-picker строки; корневой layout допускает сжатие до ширины dock без
скрытого горизонтального диапазона прокрутки.

## 6.3 Effects — `Effects.ui`

`effectsPanel` — `effects-panel-search` «Search field», чек «show all types»
`effects-panel-show-all`, `QTreeWidget` (2 колонки, заголовки скрыты:
эффекты/переходы), `effects-panel-count`. Дерево наполняется из
`EffectsPanel::EffectsListModel`; показ `toolButtonOptions` в строке поиска —
код.

## 6.4 Controls (EffectInspector) — `Controls.ui`

QDockWidget `controlsPanel` с root-виджетом `ControlsPanel` (spacer из инспектора,
клик по пину скрывает панель). Внутри: `controlsSelectionTitle` + `controlsPin`;
`controlsCurrentTime` (кликабельный таймкод) + иконка часов; `controlsSearch` +
`controlsCollapseAll` + `controlsExpandAll`; `controlsTree` — `QTreeWidget`
(2 колонки, hidden header, indentation 14). Инспектор собирает/удаляет листы
свойств для выбранных операций на лету — в форме только каркас.

## 6.5 Layout — `Layout.ui`

QDockWidget `LayoutPanel`: форма подключена к `LayoutPanel.cpp`. Внутри прокрутки —
`TransformWidget`: две кнопки отражения, промежуток, два поворота на 90°,
опорная точка 3×3 с оригинальными значками, X/Y/Width/Height и связка размеров.
Далее `AlignmentWidget`: заголовок Alignment, Selection/Timeline, шесть
выравниваний и шесть распределений с промежутком между тройками.
Оригинальные PNG-состояния и @2x находятся в `resources/icons/layout`.
Все выбранные незаблокированные слои редактируются одной операцией Undo;
анимированные свойства меняются в текущем кадре с сохранением ключей.
Точные границы восстановления и адреса Ghidra: `MARKDOWN/RE_wnd_Layout.md`.

## 6.6 Track — `Track.ui`

QDockWidget `trackPanel`: `widgetLayerHeader` (QFrame с кнопками
`toolButtonNewLayer`, `toolButtonMoveLayerUp`, `toolButtonMoveLayerDown`,
`toolButtonRemoveLayer`) и `composition-layers-header` — QTreeWidget, колонки
Name / Visible / Muted / Blend / Opacity (PosX/PosY в опциях). В форме —
статичный шапка-грид; строки слоёв строятся отдельным классом.

## 6.7 Layer — `Layer.ui`

QDockWidget `layerPanel` → `stackedWidget`: `pageLayer` с `labelBreadCrumbTrail`
с отдельным просмотром `layerPreview` и формой `layerPropertiesWidget` (rows: `layerType` readonly, `layerName`,
`layerVisible`, `layerMuted`, `layerLocked`, `layerBlendMode`, `layerOpacity`,
`layerDimension`, `layerParent`, `layerLabelColor`, `layerPlaneColor`,
`layerCameraFov`, `layerID`) и `pageNoLayer` (`dummyWidget` — «no layer selected»). Свойства
растущей формы и изменение по selection — код.

## 6.8 History — `History.ui`

QDockWidget `History` → `HistoryDockWidget` (root): тул-бар
`toolButtonUndo`/`toolButtonRedo`/`toolButtonClear` + `listViewHistory`
(recent actions, клик = revert). История лежит в `MainWindow`; форма — каркас.

## 6.9 Library — `Library.ui`

QDockWidget `libraryPanel` → root `LibraryPanel`: `libraryStatus` (QLabel
«Library»), `toolButtonReloadLibrary`, `libraryList` (QListWidget). Заливка
из диров/СДК — код.

## 6.10 Preview360 — `Preview360.ui`

QDockWidget `Preview360VideoPanel` → `Preview360VideoPanelWidget` (обвязка
`360-viewer`): тул-бар с `comboBoxCurrentView` (7 направлений: Front/Back/Left/
Right/Top/Bottom/Custom), `toolButtonProperties` открывает редактор Yaw/Pitch/Roll/FOV,
камеры и wrap; `glWidget` заменяется CPU-канвасом перспективной проекции. Рендер/интеракция —
в коде.

## 6.11 AudioMeters — `AudioMeters.ui`

QDockWidget `AudioMetersPanel` → `AudioMetersWidget`: `AudioMetersHoldPeaks`
(QCheckBox «Hold Peaks», из QSettings), `toolButtonResetPeaks`, колонки
`InputLevels` / `OutputLevels` (LED-колонки, `AudioMeterDisplay`, min 32×90;
цвета `Theme/meterBackgroundColor` #111111, hold-peak #eeeeee). Печать колонок —
paintEvent, в форме слот-заглушки.
Уровни поступают из общего PCM observer в `AudioPlayer`: `MainWindow` обновляет
обе колонки и встроенный Viewer-индикатор одним сигналом `levelsChanged`.

## 6.12 Start — `Start.ui`

QDockWidget `StartPanel` → `StartPanelWidget`: `action`-кнопки «New Composite
Shot» (`toolButtonNewComp`), «New Composite Shots From Footage»
(`toolButtonNewCompsFromMedia`), «Import File» (`AddMedia`), «Edit Screen»
(`toolButtonEditScreen`) — все minHeight 46, text-only; справа
`ProjectSideBarWidget` с «Jump back in» (`toolButtonNew`, `toolButtonOpen`,
`toolButtonClearRecents`) и `listViewRecentProjects`; снизу learning strip.
Список недавних — код.

## 6.13 Trimmer — `Trimmer.ui`

Виджет `trimmer-panel` внутри дока `TrimmerDock` общей группы Viewer (QWidget, drop-
enabled). Тул-бар «trimmer-toolbar-top»: `trimmerSetIn`/`trimmerSetOut`,
`trimmerPosition` (таймкод), `trimmerInsert`, `trimmerOverlay`; кадр
`trimmer-frame` (класс `TrimmerWidget`, draw strip); «trimmer-toolbar-bottom»:
`TrimmerSlider` (0..1000), `trimmerRange`; `trimmerSourceList` (QListWidget,
maxHeight 90). Канвас — paintEvent, в форме заглушка.

## 6.14 Timeline — `Timeline.ui`

QDockWidget `timelinePanel` → `TimelinePanelWidget` (тёмный QSS #272727).
`timelineToolStrip` (23px, тулы Select/Hand/Slice/Rate Stretch + stretch-menu
для Slip/Slide/Ripple/Roll/TrackSelect + `timelineSnap` + `timelineSettings`).
Хедер: `timelineClock`, `timelineTimecode`, `timelineNewLayer`,
`toolButtonMakeCompositeShot`, keyframe-бар (`timelinePreviousKeyframe`,
`timelineToggleKeyframe`, `timelineNextKeyframe`, `timelineKeyFrameType*` на все
интерполяции), `timelineValueGraph`, `timelineGraphAutoZoom`,
`timelineRenderCache`, `timelineExport`. Тело: `QSplitter` → `leftColumn`
(строка `timelineSearch` + `timelineLayerTree` + `timelineZoomOut/Slider/In`)
и `stackedWidgetTimelines` (трид-канвас / value-graph); справа `timelineVScroll`
(зеркальный). Внешний `timelinePanelPages` переключает Editor/Start, а единый
25-px футер содержит соседние `timelineStartTab` и
`timelineCompositionTabs`, затем меню панели справа. Нативная рамка QDockWidget и QStatusBar
скрыты, чтобы не создавать вторую строку под футером. Дерево-слои/канвас — заглушки
(в живые страницы промоты при wiring), структура rows и `timelinePreset_*` /
`timelineTrackEnabled_*` / `timelineMask*` генерируются в коде.

## 6.15 Export — `Export.ui` (spec-копия)

Виджет `export-panel` внутри дока `ExportDock` общей группы Viewer. **Важно:** рабочая
форма уже существует и **подключена** в сборку — `ui/ExportPanel.ui`; класс
`ExportPanel` строит её через `Ui::ExportPanel` (`setupUi`), а конструктор
оборачивает её во внешний виджет `objectName "export-panel"` с нулевыми
margins. Остальная динамика в `ExportPanel.cpp`: выбор директории, восстановление
`Options/ExportDirectory` из QSettings, вывод пути кадра в `plainTextProgress`,
сигналы `exportFrameRequested` / `exportContentsRequested`.

`ui/panels/Export.ui` — **спецификация, не подключена и не загружается**, создана
для единообразия остальных форматов. Корень переделан в `QDockWidget`
(`objectName "export-panel"`), как у остальных панель-окон, чтобы на рамке были
стандартные кнопки дока: **отстыковка в полноценное окно**
(`QDockWidget::DockWidgetFloatable`) и **закрытие** (`DockWidgetClosable`;
вместе с `DockWidgetMovable` в свойстве `features`). Внутри — виджет-тело
`ExportPanel` с деревом, повторяющим подключённую форму 1-в-1 по reference
object-именам: `lineEditExportDirectory`, `toolButtonExportDirectory` («…»),
`comboBoxPreset` (H.264 (.mp4) / PNG / JPEG / OpenEXR / ProRes),
`pushButtonExportFrame` («Export Frame»), `pushButtonExportContents` («Export
Contents»), `plainTextProgress` (readonly).
Правило на будущее: держать обе формы в синхроне по objectName (тесты ходят по
именам подключённой формы), либо удалить spec, если она разойдётся, — чтобы не
плодить дубликаты reference-имён. Обратите внимание: в живом коде ExportPanel —
это QWidget внутри рабочего `ExportDock`: перемещение, отсоединение и закрытие
обеспечивает оболочка QDockWidget в MainWindow, форма тела остаётся прежней.

## 7. Общие наблюдения для панелей

- **Reference object names** во всех формах 1-в-1 из конструкторов, включая
  суффиксные (например `timelineKeyFrameType` + имя иконки ключевого фрейма).
- **Кастомная отрисовка** (`TimelineCanvas`, `TimelineValueGraphView`,
  `TrimmerWidget`, `AudioMeterDisplay`, 360-канвас, шахматка) — это
  `paintEvent`; в формах соответствующие виджеты — пустые placeholder-ы с
  минимальными размерами, реальные классы помечены в комментариях формы для
  будущего `promote`.
- **Иконки** ставятся в коде с фолбэком на текст (`makeButton`); в .ui иконки
  не записаны, ресурсы не подключены (`icons.qrc` не тащим в формы).
- **Динамика** (заполнение списков, меню, драг-н-дроп, сигналы/слоты,
  синхронизация с selection) остаётся в коде — формы описывают только статичный
  каркас.
- **Валидация:** все 16 форм (`panels/*.ui` + `Viewer.ui`) проходят сканирование
  `[xml]` PowerShell без ошибок; `uic` на panels-формы при сборке не вызывается.
- Проверить при будущем подключении: `timeline_regression.cpp` завязан на
  objectName лейаутов/кнопок/кнопок тул-стрипа — имена сохранены в `Timeline.ui`.

## 8. Расположение по умолчанию (27.09.2026)

Центральный ряд вкладок: **Viewer / Trimmer / Export / Layer / 360 Viewer**.
Все пять панелей находятся в одной нативной dock-группе, вкладки расположены снизу.
Layer и 360 Viewer доступны сразу; активна вкладка Viewer. Слева остаются Media/Text,
справа — Effects/Controls/Layout/Track/Library/History, ниже всех групп — Timeline.
QMainWindow работает без отдельного centralWidget; это сохраняет нативные drag,
split/tab targets, float/dock и меню панелей. Состояние раскладки имеет версию 4:
устаревшие сохранения не восстанавливают прежнее расположение справа.

Панель `widgetQuickActions` под главным меню по умолчанию видима: Logo/Open/Save/Options/Cut/Copy/Paste/Undo/Redo. `Options/QuickActionsLayoutVersion=1` однократно исправляет прежнее скрытие; дальнейший выбор `ShowMenuBarQuickActions` применяется без сброса. Кнопка `timelineGraphAutoZoom` использует IconOnly и сохраняет tooltip, accessible name и существующий toggled-handler.
