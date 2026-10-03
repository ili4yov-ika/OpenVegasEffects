# Правило «3D-слоям нужна камера»

Проверено 1 октября 2026: массовая декомпиляция VegasEffects.exe
(`SAMPLES/VEGAS_Effects/decompile-src/VegasEffects.exe/10_bulk_addr`) и headless-Ghidra
по Project.dll (импорт без анализа, функции созданы по экспортам).

## Что такое «3D-композит»

`biff::project::CompositionAsset::Is3D` (Project.dll `0x180372bc0`) — не флаг, а
`std::any_of` по слоям композиции (`+0x10 → +0xe8`) с лямбдой из таблицы `0x1805be008`;
её оператор вызова (`0x1803817e0`) возвращает `layer->Type() == 2`. Тип 2 — `CameraLayer`:
`ActiveCameraLayer` (`0x180373080`) перебирает те же слои по `Type() == 2` и выбирает видимую
(`+0x1a4`). Итого: **композит трёхмерен тогда и только тогда, когда в нём есть хотя бы
один слой-камера.** Отдельного свойства «3D» у композита нет.

Размерность слоя — виртуальный `Dimension()` (`vtable +0x38`, 0 = 2D); слои-источники света —
`Type() == 3`.

## Где правило соблюдается (VegasEffects.exe, контекст переводов `CompositionTools`/`PasteUtilities`)

| Функция | Действие | Сообщение | Ключ подсказки |
|---|---|---|---|
| `FUN_1405a38a0` «Set Layer Dimension(s)» | слой делают 3D в композите без камеры | «Composite shots cannot contain 3D layers without a camera. Do you want to add a camera?» | `ShowAddCameraDialog` |
| `FUN_1405a8740` (создание слоя) | создаётся слой с `Dimension() != 0` в композите без камеры | «To create this layer you must first add a camera. Do you want to add a camera?» | `ShowAddCameraDialog` |
| `FUN_140a59960` «Paste Layer(s)» | вставляются слои, которым нужен 3D, в композит без камеры | «The composite shot requires a camera to accommodate the layers to be pasted. Do you want to add a camera?» | `ShowAddCameraDialog` |
| `FUN_140593b70` «Remove Layer(s)» | удаляются все камеры 3D-композита (и остаётся что-то кроме них) | «3D composite shots must have at least one camera. All 3D layers will be converted to 2D or removed if all cameras are removed. Do you want to continue?» | `ShowRemoveCameraDialog` |

Подсказка — `QMessageBox` (`FUN_1405b2bc0`: значок «вопрос», кнопки Yes|Cancel `0x404000`,
по умолчанию Yes, свойство `add-messagebox-auto-accept` = ключ, по которому BiffEventFilter
запоминает ответ). Флажки в Options → Warnings (`FUN_1403b27e0`/`FUN_1403b3220`):
`ShowAddCameraDialog` ↔ «Prompt me before converting 2D composite shots to 3D»,
`ShowRemoveCameraDialog` ↔ «Prompt me before converting 3D composite shots to 2D»
(группа `Options`, флажок включён, пока ключа нет в настройках).

Поведение при ответе:

* добавление: Yes → `InsertLayerCmd` с камерой, построенной по AudioVideoSettings
  композиции (`FUN_140a45760`), затем исходное действие; Cancel → ничего не меняется;
* удаление последней камеры: Yes → удаление слоёв, затем лямбда `FUN_1405b2af0` делит
  оставшиеся слои: источники света (`Type 3`) удаляются (`FUN_1403771a0`), остальные слои
  с `Dimension() != 0` переводятся в 2D (`FUN_140379570(..., 0)`); всё одной записью Undo
  «Remove Layer(s)». Подсказки нет, если хоть одна камера остаётся или удаляются все слои.

Дополнительно: когда композит перестаёт быть 3D, вьюер (`FUN_1408e6df0`, событие `0x5de`)
сбрасывает многовидовую раскладку в одиночный вид (`PanelRenderState::SetLayout(0)`).

## Внешнее подтверждение

Руководство HitFilm (движок, на котором построен VEGAS Effects, разделы «Working in 3D» /
«Virtual Cameras», manula.com и fxhome.com) описывает то же: 3D-слоям для видимости нужна
камера, при переводе слоя в 3D камера добавляется, а удаление или выключение всех камер
возвращает композит в 2D. Старые версии HitFilm добавляли камеру молча; VEGAS Effects
спрашивает (`ShowAddCameraDialog`).

## Реализация в порте (2 октября 2026)

Правило включено, модуль `src/ui/CameraRule.{h,cpp}`:

* `compositionIs3D` — есть ли слой `Camera`; `layerNeedsCamera` — 3D-слой, источник света,
  3D-модель (не сама камера).
* `newCameraLayer` (аналог `FUN_140a45760`, «New Camera») — камера стоит там же, где камера
  рендера по умолчанию (`defaultCameraForCanvas`: Z = половина высоты / tan(FOV/2), тот же FOV),
  поэтому картинка при добавлении не сдвигается. Так же теперь создаётся и камера из меню
  New Layer — раньше она стояла в начале координат внутри слоёв.
* `confirmAddCamera` — вопрос без заголовка, Yes|Cancel, Yes по умолчанию, ключ Options
  `Adding3DCameras`; «Больше не показывать» запоминает только Yes (auto-accept референса).
  Тексты и контексты (`CompositionTools`, `PasteUtilities`, `LayerFactory`) — референса,
  ja/zh_CN взяты из его каталогов `VegasEffects_*.qm`.
* Где спрашивается: 2D/3D в строке слоя таймлайна (`TimelineWidget::setLayerDimension`),
  Dimension в Controls и панели Layer, Paste Attributes; создание Light/3D-модели
  (`MainWindow::pushLayersNeedingCamera`); Paste/Duplicate слоя. Камера и действие — одна
  запись History («Set Layer Dimension(s)» или макрос создания/вставки); Cancel ничего не меняет.
* Удаление (`deleteSelectedLayer`, Track): если уходят все камеры и остаются другие слои —
  вопрос `Removing3DCameras`, затем `convertTo2D` (источники света удаляются, 3D-слои → 2D) в
  том же макросе «Remove Layer(s)». 3D-модель в порте не бывает 2D и остаётся (её показывает
  камера по умолчанию) — единственное отличие от лямбды `FUN_1405b2af0`.
* Когда композит перестаёт быть 3D, вьюер возвращается к одиночному виду.
* Проекты, где 3D-слои уже есть без камеры, при открытии не меняются; рендер с камерой по
  умолчанию остаётся запасным путём.
* Не охвачено: перенос слоёв в новый композит (Make Composite Shot) и вложенные композиты.
