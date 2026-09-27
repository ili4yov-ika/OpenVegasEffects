# RE: Project.dll — объектная модель VEGAS Effects

> Анализ бинарника **`Project.dll`** (7.7 МБ) из `SAMPLES/VEGAS_Effects/`, выполненный
> через GhidraMCP. Цель — clean-room реимплементация: это чужой бинарник, изучается
> только для понимания архитектуры, код пишется заново (GPL v3).

## Что это

`Project.dll` — модуль **объектной модели** (ассеты / слои / композиции / эффекты /
медиа / кэш). Это **не `Tannen.dll`** (менеджер плагинов / `tannen::biff`).

Вся иерархия живёт в пространстве **`biff::project`**:
- Демиглинг MSVC даёт `biff::project::X` (в mangled-форме `X@project@biff@@`, т.е.
  **внешний `biff`**, **внутренний `project`**), а *не* `project::biff`.
- Поднеймспейсы: `biff::project::sql` (SQLite-кэш), `biff::project::stub`.
- Всего имён неймспейсов: ~1725 (из них ~171 — лямбды). Собственных классов модели
  `biff::project` — **~130**.

## Важная поправка для clean-room

В этом бинарнике **нет** отдельных классов `Composition`, `Layer`, `Asset`. Им
соответствуют (все с 0 собственных методов у «воображаемых» имён):

| Привычное имя | Реальная сущность | Методов |
|---|---|---|
| `Composition` | `CompositionAsset` (+ таймлайн `EditorSequence`) | 79 |
| `Layer` | база `AbstractLayer` + конкретные слои | 104 (у базы) |
| `Asset` | база `AbstractAsset` + конкретные ассеты | 16 (у базы) |

Конкретные слои: `AssetLayer`, `CameraLayer`, `Effect3DLayer`, `GradeLayer`,
`LightLayer`, `PointLayer`, `TextLayer`, `Model3DLayer`, `AssetRefLayer`,
`TextRefLayer`.
Конкретные ассеты: `MediaAsset`, `ImageAsset`, `SolidAsset`, `AudioAsset`,
`CompositionAsset`, `Model3DAsset`.

## Карта классов `biff::project` по категориям

### Абстрактные базовые
`Abstract3DEffect`, `Abstract3DEffectEngine`, `AbstractAsset`, `AbstractAssetInstance`,
`AbstractDB`, `AbstractLayer`, `AbstractLayerStub`, `AbstractModel3DImporter`,
`AbstractPlugin`, `AbstractPlugin2D`, `AbstractPluginBehavior`, `AbstractPluginInstance`,
`AbstractPluginManager`, `AbstractPluginUITreeNode`, `AbstractSequenceObject`,
`AbstractSequenceTrack`, `AbstractTimeline`.

### Ассеты / медиа
`Asset`, `AssetAudioObject`, `AssetInstanceList`, `AssetLayer`, `AssetList`, `AssetProxy`,
`AssetRefLayer`, `AssetVisualObject`, `AudioAsset`, `AudioObject`, `CompositionAsset`,
`CompositionAssetStub`, `CompositionRenderSettings`, `CompositionTemplate`,
`CompositionTemplateProperty`, `ConformedAudioFile`, `ImageAsset`, `ImageSequenceAssetRef`,
`MediaAsset`, `MediaAssetReader`, `MediaAssetRef`, `MediaAudioStream`, `MediaOverrideOptions`,
`MediaPath`, `MediaVideoStream`, `GenericAssetStub`, `Model3DAsset`,
`Model3DAssetInstance`, `Model3DAnimationAsset*`, `Model3DAnimationCache`,
`SolidAsset`, `AudioVideoSettings`, `AssetFrame`, `TrackingPoint`.

### Слои
`AbstractLayer`, `AssetLayer`, `AssetRefLayer`, `CameraLayer`, `Effect3DLayer`,
`GradeLayer`, `LightLayer`, `Model3DLayer`, `Model3DLayerStub`, `PointLayer`,
`TextLayer`, `TextRefLayer`, `GradeVisualObject`, `TextVisualObject`,
`AssetVisualObject`, `Curves2DInstanceData`, `TextToken`, `TextTokenFormat`, `TextBox`.

### Клипы / треки / таймлайн
`AbstractSequenceTrack`, `AudioTrack`, `VideoTrack`, `SequenceObjectList`,
`AbstractSequenceObject`, `EditorSequence`, `LinearSequencer`, `SequenceLayer`,
`ProjectRef`, `FrameBlockList`.

### Композиции
`CompositionAsset` + таймлайн `EditorSequence`; `CompositionRenderSettings`,
`CompositionTemplate`, `CompositionTemplateProperty`.

### Плагины / эффекты
`AbstractPlugin*`, `AbstractPluginManager`, `BuiltIn2DPluginFXID`, `Curves2DPlugin`,
`EffectInstance`, `EffectInstanceData`, `EffectsHelper`, `PluginInstanceData`,
`PluginUINode` + семейство UI-контролов `PluginUI*` (Angle, Button, CheckBox,
ColorPicker, ColorWheels, ComboBox, CustomControl, DirectoryPicker, FilePicker,
Histogram, Label, LayerPicker, MaskPicker, MotionPath, MultiLineString, Orientation,
Point, Point3D, Preset, Scale, SliderFloat, SliderInt, String, TonalCurves), `FXID`,
`Transition`, `KeyFrameHelper`.

### Анимация
`Animation`, `AnimationChannel`, `AnimationSampler`, `AnimationTarget`, `KeyFrame`,
`KeyFrameList`, `KeyFrameHelper`, `MotionPath`, `PathPoint`, `Orientation3D`.

### Рендер
`CompositionRenderSettings`, `PanelRenderState`, `RenderSettings`, `AudioVideoSettings`,
`Camera`, `Projection`, `Perspective`, `Orthographic`, `GeometryBatch`, `ClearCoat`,
`PBRMetallicRoughness`.

### Кэш / БД
`AbstractDB`, `CacheDBManager`, `MediaDB`, `TimelineDB`, `MediaFileModificationCache`,
`MediaFrameCache`, `Model3DAnimationCache`, `GeometryBatchListCacheItem`,
`Model3DRegularGrid`, `Model3DRegularGridCell`, `FrameBlockList`,
`biff::project::sql::{SqlCommand, SqlConnection, SqlDataReader}`, `SQLiteStorage`.

### 3D
`Camera`, `CameraLayer`, `LightLayer`, `Effect3DLayer`, `Model3DLayer`, `Model3DAsset*`,
`Model3DGroup`, `Model3DImportOptions`, `Model3DMaterial`, `Model3DMaterialOverride`,
`Model3DPostProcessor`, `Model3DRegularGrid`, `Model3DTransformNode`, `PointCloud`,
`Projection`, `Perspective`, `Orthographic`, `Orientation3D`.

### Документы / проект
`Project`, `ProjectHelper`, `ProjectMetadata`, `ProjectRef`, `ProjectSettings`,
`Document`, `DocumentException`, `BinFolder`, `AssetList`.

### Утилиты
`Accessor`, `BiffColor`, `BiffTime`, `ChangeObserver`, `CustomProperty`, `ErrorHandler`,
`Exception`, `FilePathHelper`, `FontCache`, `FontInfo`, `Importer`, `KeyFrameHelper`,
`MediaPath`, `NotificationData`, `NotificationState`, `PropertyManager`, `Tracker`.

## Ключевые адреса

### AbstractPluginManager (vtable/ctor)
- vtable: `0x1805b75c0`; ctor `0x180275550`; copy-ctor `0x180275560`; dtor `0x180275540`.
- **Чисто виртуальный интерфейс** — конкретная реализация `PluginManager` живёт в
  `Tannen.dll` (`biff::tannen::PluginManager::Create`).

### CompositionAsset (79 методов)
- `Create @ 0x180370f80`, `CreateFromXml @ 0x180371020`
- `Clone @ 0x180371860`, `TakeCopyOf @ 0x180371bf0`
- `ID @ 0x180371910`, `Type @ 0x180371930`, `Name/SetName @ 0x180371940/...950`
- `Width @ 0x180371ba0`, `Height @ 0x180371bb0`, `HasAudio/HasVideo @ 0x180371a00/...a10`
- `Layers @ 0x180372c80-...ca0`, `AddLayer @ 0x180372d60`, `RemoveLayer @ 0x180372f40`,
  `FindLayerByID @ 0x180372fa0`, `InsertLayer @ 0x180372e00`, `ActiveCameraLayer @ 0x180373080`
- `RenderSettings/Settings/Is3D @ 0x180372b80-...bc0`
- `Serialize @ 0x1803724f0/0x180372600`, `Proxy/SetProxy @ 0x180372b40/0x1803726e0`
- Таймлайн: `CurrentTimeIndicator @ 0x180373420`, `InPoint/OutPoint @ 0x180373440/...470`,
  `PreviousEdit/NextEdit @ 0x180373490/...5a0`, `TimelineZoom @ 0x1803736c0`,
  `SplitterPosition @ 0x1803736e0`, `TimelineTimeFormat @ 0x180373700`,
  `TimelineSnapMode @ 0x180373720`, `ViewerPanelState/LayerPanelState/Template @ 0x180373740-...780`
- `RespondToNotification @ 0x180376720`, `ProcessPixelDataChange @ 0x180376840`,
  `IsTemplate @ 0x180376510`

### AbstractLayer (база, 104 метода)
- `CreateFromXml @ 0x1802db9e0`, `Deserialize @ 0x1802e1000`, `Serialize`
- Трансформации: `WorldTransformationAtTime`, `ParentingWorldTransformationAtTime`
- Маск `UpdateMaskPickerValues`, трекеры `Trackers`, эффекты `Effects/GeometryEffects/BehaviorEffects`, `Supports*`
- Конкретные через `CreateFromXml`: `AssetLayer @ 0x1802e8bb0`, `CameraLayer @ 0x1802f0190`,
  `Effect3DLayer @ 0x1802f5e10`, `GradeLayer @ 0x1802faeb0`, `LightLayer @ 0x1802fe550`,
  `PointLayer @ 0x180300a80`, `TextLayer @ 0x180315710`, `Model3DLayer @ 0x1802ab180`

### MediaManager (33 метода)
- `Create @ 0x18039cf80` (объект 0x268 байт, shared_ptr-модель)
- `CreateMediaAsset @ 0x18039da70`, `CreateMergedMediaAsset @ 0x18039dfb0`,
  `CreateProxyMediaAsset @ 0x18039e150`, `CreateImageSequence @ 0x18039e4e0/...e7b0`,
  `CreateMediaAssetFromXml @ 0x18039ea70`
- `GetReader @ 0x18039ee40`, `GetUnmanagedReader @ 0x18039f420`
- `SleepAllMedia/WakeAllMedia @ 0x18039f5c0/...f650`, `RemoveCachedMediaFiles @ 0x18039f6e0`,
  `FrameCache @ 0x18039f8e0/...f8f0`
- `Reload @ 0x18039fd70`, `Relink ×3 @ 0x1803a00c0/0x1803a02d0/0x1803a03d0`,
  `RelinkMergedAudio ×2 @ 0x1803a04f0/0x1803a06a0`
- `Retain/Release @ 0x1803a0d60/...db0`; `~MediaManager @ 0x18039d450`

### EffectInstance (47 методов)
- `Create @ 0x1803f6150`, `CreateFromXml @ 0x1803f6200`, `Copy @ 0x1803f62d0`
- `Type @ 0x1803f6cd0`, `ID @ 0x1803f6dc0`, `PluginID @ 0x1803f6dd0`,
  `Plugin @ 0x1803f6de0`, `PluginManager @ 0x1803f6e30`, `Properties @ 0x1803f6e80`
- `InstanceData/SetInstanceData @ 0x1803f7a20/...7ac0`, `V2 @ 0x1803f7b50/...7b90`
- `Serialize ×2 @ 0x1803f7ce0/0x1803f7e00`, `Name/SetName @ 0x1803f80e0/...8200`,
  `Enabled/SetEnabled @ 0x1803f8350/...8360`, `SetParent/ClearParent @ 0x1803f8430/...86e0`,
  `TakeCopyOf @ 0x1803f8770`
- Маск-пикеры: `LayerPickerPropertyNames @ 0x1803f8a10`, `ChangeLayerPickerValues @ 0x1803f8c10`
- `~EffectInstance @ 0x1803f6a60`

### AssetList
`AddAsset @ 0x18035d630`, `Deserialize @ 0x18035dc10`.
`AssetProxy::Create @ 0x1806b0d0` (пример shared_ptr-фабрики).
`MediaAsset::MediaManager() @ 0x180394d50` — shared_ptr менеджера лежит на `0x18→0x138/0x140`.

## Механика / дизайн
- Всё строится на **`shared_ptr`** + `std::_Ref_count` (видно в `AssetProxy::Create`,
  `MediaManager::Create`).
- `MediaAsset::MediaManager()` — объект хранит `shared_ptr<MediaManager>` (смещения
  `0x18` → `0x138/0x140`).
- `Curves2DPlugin::PluginManager()` возвращает `AbstractPluginManager*` — связь
  «эффект → менеджер плагинов» через указатель/интерфейс.
- `AbstractPluginManager` — чисто виртуальный интерфейс; конкретная реализация
  (`biff::tannen::PluginManager`) в `Tannen.dll`. `Project.dll` — только модель.

## Вендоренные сторонние библиотеки (НЕ реимплементировать)
- **FBX SDK** — `fbxsdk` (FbxManager, FbxScene, FbxMesh, FbxNode, FbxImporter, FbxTime...)
- **Draco** — `draco` (CornerTable, MeshEdgebreakerDecoder, MeshPredictionScheme*,
  AttributeOctahedronTransform; используется через glTF KHR `DracoMeshCompression`)
- **assimp** — `Assimp` (AllocateFromAssimpHeap, AssimpModel3DImporter, `ASSIMP.DLL`)
- **Alembic** — `Alembic::Abc*`, `AbcCoreAbstract::v12`, `AbcGeom::v12`, + Imath (`Imath_3_1`)
- **OpenEXR** — `FXImageFileOpenEXR`, `FXOpenEXRCodecInfo` (обёртки VEGAS над OpenEXR)
- **glTF** — `Microsoft::glTF` + `rapidjson` (GLTF/GLB загрузчик)
- **FreeImage** — `FXImageFileFreeImage`, `FXFreeImageCodecInfo`
- **FFmpeg / медиа** — `fxh::media` (FXAbstractCodecInfo, FXCineFormCodecInfo,
  FXH264CodecInfo, FXImageFile*)
- **Qt5** — `Qt5Core.dll`, Qt-классы
- **fxtl** (собственный SDK VEGAS, но вендорен) — `FXID`, `FXList`, `FXVector`,
  `FXMatrix`, `FXPoint`, `fxtl::gl` (OpenGL-обёртки), `fxtl::thread::FXCriticalSection`
- **Boost** — `boost::shared_ptr`, `sp_counted_impl_pd`
- **SQLite** — `biff::project::sql::*`, `SQLiteStorage`
- **MSVC CRT/STL** — `std`, `Concurrency` (PPL), `_Ref_count*`, `_Func_impl*`
- **Windows API / runtime** — KERNEL32, ADVAPI32, OLE32, GDI32, OPENGL32, GLEW32,
  MSVCP140, `ARCHIVE.DLL`

## Что это значит для OpenVegasEffects
`Project.dll` — **ядро объектной модели**. Для clean-room-реимплементации брать за
основу иерархию `biff::project` из этого отчёта. Конкретный `PluginManager`
(регистр/запуск плагинов) — отдельный модуль `Tannen.dll`.

Именование: внешний namespace `biff`, вложенный `project` (то есть фактически
`biff::project`, несмотря на симметрию имени `Project.dll`).

## Перенос подтверждённых контрактов в код

- Разделение `AbstractLayer::Effects`, `BehaviorEffects`, `Trackers` и mask picker перенесено в
  структуру таймлайна: Behaviors больше не смешиваются с Effects, а маски принадлежат слою.
- `EffectInstance::{PluginID, Name, Enabled, Properties}` отражены в `composition::Effect`;
  значения и `KeyFrameList` сериализуются lossless в `OpenVegasEffects`, когда нативная схема
  конкретного плагина неизвестна.
- `AbstractLayer` identity/parenting и весь Transform сохраняются: стабильные ID,
  `parentLayerID`, 2D/3D-поля и кривые не теряются после открытия проекта.
- `MediaManager::Reload/Relink` отражены в панели Media: relink меняет asset id всех клипов,
  сохраняя монтаж. `EditorSequence` хранит video/audio track headers и параметры master track.
