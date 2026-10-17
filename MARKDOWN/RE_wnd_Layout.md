# Layout / Раскладка — аудит 3 октября 2026

Панель редактирует расположение выделенных слоёв: отражение, поворот на 90°, опорную точку числовых координат, X/Y/Width/Height, пропорции, выравнивание и распределение. Это не выбор рабочих пространств и не многовидовой Viewer.

## Источники

- [Introducing the Layout Panel — VEGAS Effects](https://www.manula.com/manuals/fxhome/vegas-effects/quickstart/en/topic/introducing-the-layout-panel). При этом аудите сервер вернул HTTP 403; содержимое и снимок страницы непосредственно не получены. Геометрия формы проверена по загруженному в Ghidra VegasEffects.exe, а значки — по извлечённым ресурсам. Пиксельное совпадение со снимком страницы не заявляется.
- `FUN_140315400`: setupUi TransformWidget; `FUN_1403164a0`: его переводы.
- `FUN_140329cd0`: setupUi AlignmentWidget; `FUN_14032b3a0`: его переводы.
- `FUN_14042edf0`: конструктор DirectionWidget; `FUN_14042dcd0`: его setupUi. `1412dd1b0` — имя Direction enum; `1412f09a8`…`1412f0ae8` — строки имён значков.
- `SAMPLES/icons-from-VegesEffects/named_png/images/images/`: оригинальные PNG из RCC. В `resources/icons/layout/` перенесены 26 значков × четыре состояния × два разрешения; файлы включены в `resources/icons.qrc`, поэтому SAMPLES не требуется для запуска.

## Подтверждённая структура

TransformWidget: два отражения, фиксированный промежуток 10 px, два поворота; оставшаяся ширина строки свободна. Под ними рамка: DirectionWidget слева, растягиваемый промежуток, четыре строки числовых полей справа, связка размеров между Width и Height. У оригинального числового гридa spacing 6 и внутренние отступы 6.

DirectionWidget: сетка 3×3, spacing/margins 0, девять checkable/autoExclusive кнопок с NoFocus и icon-only/no-background. Значки по строкам: `top-left/top/top-right`, `left/center/right`, `bottom-left/bottom/bottom-right`; центр отмечен по умолчанию. После третьей строки и третьего столбца оригинал добавляет расширяющие spacer-ы. В порте сетка кнопок 72×72 и вертикальный spacer под ней, отдельное растяжение справа.

AlignmentWidget: отдельный заголовок Alignment, Align to (Selection / Timeline), строка трёх горизонтальных и трёх вертикальных выравниваний, Distribute Objects, строка трёх вертикальных и трёх горизонтальных распределений. Между тройками растягиваемый spacer. Это отдельный heading QLabel, а не QGroupBox title. У исходного outer layout верхний отступ 6, нижний 0; внутренние строки кнопок имеют верхний/нижний отступ 3.

## Реализация

Форма `ui/panels/Layout.ui` подключена и к CMake AUTOUIC, и к qmake FORMS. `LayoutPanel.cpp` назначает оригинальные QIcon Normal/Active/Checked/Disabled, включая @2x. При отсутствии выбора все операции отключены. Выравнивание к Timeline работает с одним слоем, к Selection — с двумя и более; распределение — с тремя и более. Добавлена вертикальная прокрутка при уменьшении высоты дока.

Выбор опорной точки меняет только смысл X/Y, не перемещая слои; значения обновляются и при фокусе в числовом поле. Link сохраняет соотношение сторон. Изменение объединённой рамки масштабирует размеры и промежутки всей выборки; alignment/distribution передают отдельные рамки каждого слоя.

Mirror Vertical отражает относительно вертикальной оси, то есть меняет знак X scale; Mirror Horizontal меняет знак Y scale. Обработчики MainWindow применяют отражение и поворот ко всем выбранным незаблокированным слоям. Одна операция — одна запись History/Undo с сохранением всей выборки. Команда разрешает слои по ID; при перестановке слоёв Undo не затрагивает чужой слой. Изменение существующего ключа сохраняет ID, интерполяцию и bezier handles; статическое свойство остаётся статическим, а анимированное редактируется в текущем кадре.

## Границы восстановления

С 5 октября 2026 рамка слоя — то, что рисует рендер (`render/LayerGeometry`): медиа своим размером × PAR (вложенный шот — квадратным размером), текст — рамкой глифов, plane — кадром; поворот вокруг anchor у медиа и вокруг центра у plane/текста, как в `RenderWorker::renderClip`; 3D-лист — углы, спроецированные камерой сцены, как в `renderClipIn3D`, с его Behaviors в матрице листа (родитель × Behaviors × лист, как `ParentTransformationAtTime` эталона; с 17 октября 2026); Grade — весь кадр. Камера, свет и точка рамки не имеют и в выравнивании не участвуют. Правка рамки (`fitLayerToBox`) меняет масштаб по осям (при 90°/270° оси меняются, при другом угле — равномерно по площади), затем сдвигает слой на остаток центра, так что рамка встаёт точно; 3D-лист сдвигается на экранный сдвиг, переведённый к его глубине. Модель 3D (с 9 октября 2026) — охват на холсте точек её граней, позированных анимациями и Transform узлов, через камеру сцены; при подгонке сдвиг переводится к глубине центра модели. Parent с 17 октября 2026 входит в рамку (пункт о родителях); с того же дня — рамка слоя с Behavior (17 октября 2026). TransformWidget эталона (`TransformSettingsModel`, VegasEffects 0x140316c00..0x140329cd0) меряет слои через `AbstractLayer::WorldTransformationAtTime(время, проект, true, …, true)` — `TransformationHelper::WorldTransformationAtTimePreAlignToMotion` (Project.dll 0x180322d20): родитель, собственная трансформация с Behavior, симуляция, AlignToTarget, начало координат; эффекты рамку не меняют. Порт: `render::layerBehaviorTransformAt` — то же перемещение картинки 2D-слоя, что `RenderWorker::applyClipBehaviors` (матрицы экземпляра MotionTrack, проверенные Behavior по порядку, затем симулируемые), — входит в `layerGeometry` после родителя, так что рамка Layout, выравнивание и подгонка (`fitLayerToBox`) идут за Behavior. Тест `layerBoxFollowsItsBehaviors`. Behavior 3D-листов в рамку пока не входят. Форма и перечисленные обработчики проверены отдельно от оригинального Flux render pipeline. С 17 октября 2026 числовые поля X, Y, Width, Height — `DoubleScrubberSpinBox` эталона (Widgets.dll, `biff::ui::widgets`, с 17 октября 2026 — `ui::DoubleScrubberSpinBox`): в покое только чтение с курсором «↔» и одним знаком после точки (`DisplayDecimals` 1, нули обрезаются); перетаскивание левой кнопкой — один `singleStep` на пиксель от значения при нажатии, курсор скрыт и возвращается в точку нажатия (`CursorUtilities::SetPosition`), на краях диапазона значение держится; с Shift при нажатии — шаг `SnappingIncrement` (10) на каждые 5 px с привязкой к кратному в сторону движения (`0x180028820`); с `FlipEnabled` Ctrl меняет знак; щелчок без перетаскивания — правка текста (`SetEditMode`: без префикса/суффикса, 3 знака, текст выделен), Tab/Backtab/прочий фокус — тоже; Esc или выход без изменения — `Revert` и снова скраббер; Shift+стрелки — к следующему кратному 10. Поля в форме — StrongFocus, диапазон ±99999 (setupUi VegasEffects 0x140315400). Перетаскивание фиксируется одной правкой при отпускании (`mouseReleased`). Тест `layoutFieldsScrubLikeTheReference`.

Рамка в эталоне (`FUN_1403186c0`, 0x14031a4e0, 0x14031ca50, 0x14031f5c0; AlignmentWidget — 0x14032c260):
собственный размер слоя (`GradeLayer::Width/Height`, `TextLayer::Width/Height` + `TextBox::MinX/MinY`,
размер ассета) × `AbstractLayer::WorldTransformationAtTime` на `PlayheadRelativeTime` — Behaviors слоя
входят, Behaviors подобъектов (глифов, `SubObjectTransformationAtTime`) нет; текстовая рамка Flux
(`GetTextBoundingBox` → `FUN_18039ee40`) времени не принимает.

## Проверки

`timeline_regression`: `layoutBoxesFrameModels` (рамка модели против отрисованных пикселей, подгонка, анимация), `layoutBoxesFollowTheRenderedLayer` (рамка против отрисованных пикселей: 2D, поворот вокруг anchor, PAR, 3D-лист; подгонка медиа, текста и 3D-листа к заданной рамке; камера без рамки), `layoutNativeFormAndControls` (иконки, отсутствие выбора, опорная точка при фокусе, пропорции, выравнивание, направления поворота, компактная/широкая форма), `layoutMultipleSelection` (выравнивание и распределение), `layoutEditsPreserveAnimationAndUndo` (ключи и Undo/Redo после перестановки слоёв). Снимки сохраняются в temp как `openvegas-layout-panel*.png`.

CMake/MSVC Debug-приложение собрано. Все три проверки Layout прошли (QtTest: 5 passed с init/cleanup, 0 failed). Снимки компактной, широкой и пустой панели проверены визуально. Добавлена явная OBJECT_DEPENDS-связь LayoutPanel.cpp → Layout.ui: у установленной пары Qt/CMake AUTOUIC декларирует иной путь byproduct, поэтому без этой связи Ninja мог обновить форму, но оставить старый объект до следующей сборки. Изменение Layout.ui теперь сразу планирует пересборку LayoutPanel.cpp.
