# Layer — просмотр выбранного слоя

Проверено 2026-09-27 через Ghidra MCP, VegasEffects.exe.

## Подтверждённое поведение оригинала

`LayerPanelWidget` (`141322378`) — окно просмотра выбранного слоя, а не список
дорожек. `FUN_1408cf1b0` обрабатывает отсутствие/множественное выделение и
неподдерживаемый тип. Для единственного слоя с изображением вызывает
`FUN_1408cec90`, передаёт слой компонентам просмотра (`FUN_140913980`,
`FUN_14090bcf0`) и обновляет breadcrumb именем слоя.
`FUN_1408ceb40` использует строку `No Composite Shot Selected` (`141322318`).
`FUN_1408d09d0` направляет изменения свойств и выделения в панель.

## Render pipeline панели (19 октября 2026)

Ghidra MCP по VegasEffects.exe и дамп Flux.dll (`decompile-src/flux_decomp_full`):

- **Какие слои.** `FUN_1408cf1b0` пропускает только слой с типом 0
  (`AbstractLayer` vtable +0x10 — `AssetLayer`: footage или plane), чей ассет
  не композитный шот (тип 3) и проходит проверку ассета (+0x60). Текст (6),
  Grade (4), Model3D (7), камера, свет, точка и вложенный шот — «Invalid Layer
  Type».
- **Прокси.** `LayerProxy@viewer` (vtable 0x141323e58, наследник
  `CompositionProxy`): ширина/высота (+0x68/+0x70) — `FUN_140a22e00`, размер
  слоя: у asset-слоя — ширина и высота его ассета (у Grade/Text/Model3D свои);
  PAR (+0x78) — `FUN_140a23040`; состояние вида (+0x88/+0x90) —
  `CompositionAsset::LayerPanelState`, отдельное от вьюера. `FUN_140913980`
  создаёт холст этого размера, не больше 3840 (8192 для ассетов особого вида) и
  `FluxOpenGLInfo::MaxTexture*`.
- **Время.** `FUN_14090bcf0`: транспорт панели — длина слоя
  (`EndFrame − StartFrame`), позиция — кадр шота минус `StartFrame`: кадр слоя.
- **Рендер.** `RenderLayerViewJob` (`FUN_1408fc880`) вызывает
  `Flux::RenderLayer(слой, шот, кадр, холст, RenderOptions, камера вида, фон,
  0x13, downsample, …)`. В Flux (`FUN_1802c8be0`) флаги: 0x1 — метод 3
  материала слоя (`VideoLayerMaterial`/`ImageLayerMaterial`/`SolidLayerMaterial`:
  исходник), 0x2 — метод 5 `AbstractMaterial` → `FUN_180426b30` над
  `MaskDataList` материала (+0x58; `MaskData` строит `FUN_1804269c0` с
  `Mask::IsVisible/BlendMode/IsInverted`) — маски, 0x4 — `FUN_180345bf0`,
  проверяющий `EffectInstance::Enabled`, — эффекты, 0x10 — скопы
  (`Project::ScopeEffects`). Диалог 3D-модели передаёт 0x9 (исходник). Значит,
  панель Layer рисует **исходник слоя и его маски** — без эффектов, без
  Transform, opacity и blend — на холсте размера слоя, в его кадре.

## Реализация

`Layer.ui` содержит breadcrumb, `layerPreview`, редактор свойств и отдельную
страницу сообщений. Панель входит в общую dock-группу Viewer/Trimmer/Export/Layer/360 Viewer.
С 19 октября 2026 preview следует эталону (раздел выше): только Media с
картинкой (не вложенный шот) и Plane; копия шота размера слоя — у footage
картинка ассета в квадратной форме (ширина × PAR), у plane кадр шота — с этим
слоем одним, без эффектов (и Behavior), с нулевым Transform, opacity 1, Normal,
2D, без родителя и motion blur; маски сдвинуты на половину разницы размеров (в
порту они заданы в пространстве шота над картинкой клипа по центру). Кадр —
тот же момент шота, то есть кадр слоя. Отдельный RenderManager, исходная модель
не меняется; скрытый слой виден только в preview. Холст уменьшается до 960×540
для панели (у эталона — до 3840). Перемотка и изменение свойств запрашивают
новый кадр; результат прежнего выделения отбрасывается по номеру запроса.

Text, Model3D, Point, Camera, Light, Grade, вложенный шот и чисто звуковое Media
— Invalid Layer Type, как у эталона. Редактор их свойств оставлен
как расширение приложения. Нет композиции/слоя/единственного выделения — отдельное
сообщение, старый кадр очищается.

Редактор меняет имя, visibility, mute, lock, blend, opacity, размерность, parent,
цвет метки и Plane. Заблокированный слой допускает только разблокировку;
неизменённое значение не создаёт Undo. Циклический parent исключён, включая
циклы в уже загруженной модели. Undo адресует слой по ID и безопасен после
закрытия панели. Для Camera доступен вертикальный FOV 20–140°: поле использует
3D renderer и 360 Viewer; оно сохраняется в расширениях проекта
`OpenVegasCameraFieldOfView` и `CameraFieldOfView` вложенных композиций.
Формат native camera/lens этим расширением не восстановлен.

## Проверки

`timeline_regression`: layerPanelLockCyclesAndStableUndo,
layerPreviewIsolatedAndStaleSelection, cameraFovProjectRoundTrip,
layerPanelShowsTheSourceAndItsMasks (исходник без эффекта Invert, поворота,
масштаба и opacity, маска в координатах холста слоя, текст — не asset-слой).

## Live-проверка 2026-09-27

Повторный live-декомпилят FUN_1408cf1b0 подтверждает No/Multiple Selection, проверку renderable asset и вызов FUN_1408cec90. Exact native render pipeline этим не восстановлен.
