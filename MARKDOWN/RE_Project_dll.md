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

### Model3DGroup / Model3DTransformNode (5 октября 2026)
- `Model3DGroup` (impl 200 байт, `FUN_18029eb10`): ID (FXID) +0x00, имя (wstring) +0x10, батчи
  геометрии +0x38..+0x48, габариты +0x50..+0x64 (центр xyz, размер xyz), дочерние группы +0x68..+0x70.
  `AddGeometryBatch` @ 0x1802a04c0 расширяет габариты батчем; XML-тег `Model3DGroup` (@ 0x1802a0ad0).
- `Model3DTransformNode` (impl 0x128 байт): `FUN_1802cf210` строит узел из группы — копирует ID/имя,
  создаёт PropertyManager (+0x58) со свойствами `anchorPoint` (spatial, по умолчанию центр габаритов
  группы), `anchorPointOrientation`, `position` (spatial, тот же центр), `scale`, `scaleLinked`,
  `orientation` (spatial), `rotationX/Y/Z` (с диапазоном), `parentLayerID`; дочерние узлы — по
  дочерним группам (+0x70).
- `TransformationAtTime` @ 0x1802d1b00 = T(position) · `RotationAtTime` (anchorPointOrientation,
  orientation, rotationX/Y/Z) · `ScaleAtTime` (scale/100, нули заменяются малым значением);
  `OriginAtTime` @ 0x1802d2150 = T(−anchorPoint); `ParentingTransformationAtTime` @ 0x1802d1a30 =
  родитель · Transformation · Origin.
- XML (`FUN_1802cfde0`): `<Model3DTransformNode Version="3">`, `ID`, `GroupName`, `GroupID`,
  `PropertyManager`, `<Model3DTransformNodeChildren>`. Узел живёт в `Model3DAssetInstance::TransformNode`
  слоя `Model3DLayer::Model3DAssetInstances`.

### Model3DAnimationAsset / Model3DAnimationAssetInstance (9 октября 2026)
- Анимация модели — отдельный ассет `Model3DAnimationAsset` (файл + `AnimationIndex`), который
  играет экземпляр `Model3DAnimationAssetInstance` в списке `Model3DAssetInstance::AnimationInstances`
  слоя модели; `Model3DAnimationAssetPair` сводит ассет модели с ассетом анимации
  (`CanCombineAnimations`, `FindStaticGroupNode`).
- Свойства экземпляра (`FUN_180138f10`): `strength` (float, 100, с диапазоном, анимируемое),
  `loop` (bool, false), `customPlayback` (bool, false), `playback` (float, 0, анимируемое).
- `AssetFrame` @ 0x18013a900: без `customPlayback` кадр = `RelativeTimeToRelativeSubFrame(слой)` ×
  `FrameRate(анимации)` / частота композиции, с `loop` — `fmod(кадр, FrameCount − 1)`; с
  `customPlayback` — значение `playback` в этот момент.
- `FrameRate` @ 0x1801366d0: переопределённая частота (+0x78 флаг, +0x80 значение) или частота
  импортёра (не меньше 1,0); округляется до трёх знаков, 23,976 / 29,97 / 59,94 заменяются точными
  24000/1001, 30000/1001, 60000/1001. `FrameCount(rate)` = rate / FrameRate × кадры импортёра.
- Импортёры (RTTI): `AbstractModel3DImporter`, `AssimpModel3DImporter`, `FilmBoxModel3DImporter`,
  `AlembicModel3DImporter`, `GLTFModel3DImporter` (vtable 0x1805a4348). У glTF-импортёра
  `SetCurrentAnimationIndex` @ 0x1800ab6f0: кадры = (int)(длительность × 24.0) (константа
  0x1805c0348), `AnimationFrameRate` читает сохранённую частоту, `AnimationCount` — число анимаций.
- `FilmBoxModel3DImporter` (RTTI → vtable 0x1805a3288; FBX SDK из libfbxsdk.dll): загрузка
  (`FbxImporter::Import`, первый `FbxPose`, первый стек, `SetCurrentAnimationStack`), частота =
  `FbxTime::GetFrameRate(GlobalSettings.GetTimeMode())` (+0x40), число кадров `FUN_18007ba00` =
  конец `LocalTimeSpan` стека и TakeInfo его слоёв в кадрах файла минус min(0, начало). Слот 2 —
  кадры, 3 — частота, 4 — число анимаций (= слоёв стека 0), 5 — `SetCurrentAnimationIndex` (слой
  стека 0), 6 — имя (стека по индексу), 9 — `GetFileSystemUnits` (`FUN_18007fe40`: `FbxSystemUnit`
  cm → 3002, m → 3004, dm → 3003, Foot → 3007, Inch → 3006, km → 3005, mm → 3001, Yard → 3008,
  Mile → 3002 (так в коде), иное → 3010; база `AbstractModel3DImporter` даёт 3010). Узлы —
  `EvaluateGlobalTransform`/`GetNodeGlobalTransform` (с пивотами), геометрическое смещение,
  кластеры (в том числе dual quaternion), блендшейпы (`GetShapeChannel`, `FullWeights`), vertex
  cache. `FbxAxisSystem`/`ConvertScene` не импортируются — оси не конвертируются.
- `Model3DImportOptions` (0x50 байт): +0x11 FlipZY, +0x12 CenterAnchor, +0x13 AutoNormalize,
  +0x14 ModelScale, +0x18 ModelUnits (`LinearType`, по умолчанию 3010 Pixel), +0x1c NormalGen (0),
  +0x20 AutoSmoothingAngle (80), +0x24 FlipNormals, +0x25 UnifyNormals, +0x26 FlipUVs,
  +0x27 PreserveGroups (1), +0x28 центр габаритов, +0x34 ширина/высота/глубина (ImportBBox*),
  +0x40 масштаб s = `ConvertLinearToMM(ModelScale, units)` / 2.8352, +0x44 нормализация
  N = 500 / (|размер · 0.5| · s) (или 1, если размер ≈ 0), +0x48 UseAnimationScale (версия ≥ 4).
  `ConvertLinearToMM` (0x1802a4b50): мм 1, см 10, дм 100, м 1000, км 1e6, дюйм 25.4, фут 304.8,
  ярд 914.4, миля 1609344, пиксель 2.8352.
- `Model3DPostProcessor::Process` (0x1802c5490): габариты (`ComputeBoundingBox`: центр и размер
  исходной геометрии; при FlipZY меняет местами y/z, x центра не разворачивает; без AutoNormalize
  умножены на s), затем для точки: FlipZY (x, y, z) → (−x, z, y) (нормали так же, `SwapAxes`),
  p·s; AutoNormalize + центр: (p·s − c·s)·N; только AutoNormalize: (p·s − c)·N + c; только центр:
  p·s − c; FlipUVs: v → 1 − v; нормали (`ProcessNormals`) при FlipNormals/Unify/способах 1–3.
  Без каких-либо настроек геометрия копируется как есть.
- `AssimpModel3DImporter` (RTTI → vtable 0x1805a2a58): слоты 2–6 (анимации) — заглушки
  `AbstractModel3DImporter` (кадры 0, частота 0, число 0), так что OBJ/3DS/LWO в референсе без
  анимации; слот 9 — единицы файла 3004 (метр). Материалы (`FUN_1800670e0`): текстуры
  `aiGetMaterialTexture` типов 1 (diffuse → карта 0), 2 (specular → 1), 5 (height) и 6 (normals);
  цвета `$clr.diffuse/specular/ambient/emissive`, каждый канал обрезан до [0, 1]; блеск —
  `$mat.shininess` × `$mat.shinpercent` (если есть), не меньше 1; без `$mat.shininess` — блеск 1
  и specular (0, 0, 0, 0); `$mat.opacity` × константа 0x1805c0388; имя `?mat.name`.
- `AssimpModel3DImporter` (`FUN_180066af0`): `PP_GSN_MAX_SMOOTHING_ANGLE` = угол из настроек,
  `ReadFile(путь, 0x3f9ecb)` — CalcTangentSpace, JoinIdenticalVertices, Triangulate,
  GenSmoothNormals, SplitLargeMeshes, LimitBoneWeights, ValidateDataStructure,
  ImproveCacheLocality, RemoveRedundantMaterials, SortByPType, FindDegenerates, FindInvalidData,
  GenUVCoords, TransformUVCoords, FindInstances, OptimizeMeshes.
- `PanelRenderState` (экспорты 0x1803c32d0…0x1803c46f0): раскладка (`Layout`, `LayoutNumberOfViews`:
  1, 2, 3 или 4 вида), позиции видов и сплиттеры, состояния видов (`AddViewState(ViewType,
  LayoutType, RenderOptionsDefaults)` → `FUN_1803c0170`). Камера вида по типу: 0 — 2D-вид (его
  шахматка — `ShowCheckerboard2D`), 1 — без особых значений (активная камера), 2 — свободная
  перспектива (позиция (0, 100, 1000)), 3 — Front (0, 0, 1e6), 4 — Back (0, 0, −1e6), поворот
  (0, π, 0), 5 — Left (−1e6, 0, 0), (0, −π/2, 0), 6 — Right (1e6, 0, 0), (0, π/2, 0), 7 — Top
  (0, 1e6, 0), (−π/2, 0, 0), 8 — Bottom (0, −1e6, 0), (π/2, 0, −π); масштаб камеры 1. Опции вида из
  `RenderOptionsDefaults` VegasEffects.exe (`FUN_140233dc0`): ShowCheckerboard2D, ShowCheckerboard3D,
  ShowFloorPlane, ShowMotionPath, ShowEffectControls и цвет фона. XML (`FUN_1803c0b00`):
  `<ViewerState Version="5">`, `CurrentLayout`, `Layout0..4` (`PosN`, `SpN`), `Views/State`.
- Поворот слоя — `TransformationHelper::Rotation` @ 0x180321170 (у камеры, света и 3D-слоёв;
  2D — только Z): кватернионы по осям X, Y, Z (`FUN_1802b0900`, кватернион (w, x, y, z) с
  углом −`orientation` × 0.017453292), перемножены X·Y·Z (`FUN_1802b47d0`), в матрицу
  (`FUN_1800c9e20`, столбцовое хранение, v' = M·v), затем повороты X, Y, Z (`FUN_1802b4580`…:
  стандартные матрицы, домножаются справа `FUN_180068e80`) на −`rotationX/Y/Z`. Итог:
  Rx(−ox)·Ry(−oy)·Rz(−oz)·Rx(−rx)·Ry(−ry)·Rz(−rz). Так же `Model3DTransformNode::RotationAtTime`.
  `AutoOrientAtTime` @ 0x180323060 (режим 4) поворачивает к свойству `target`.
- `CameraLayer`: свойства (`FUN_1802eec70`) — `position` (0, 0, zoom), `target`, `orientation`,
  `rotationX/Y/Z`, `aperture`, `zoom`, `focusDistance`, `blurDoF`; impl (ctor 0x1802f0fd0) —
  NearClip +0x38 = 1, FarClip +0x3c = 100000, плёнка +0x40 = 36, +0x44 = 20.25 мм. XML — Version,
  Dimensions, HasDoF, AutoOrient (+AutoOrientPathAxis, AutoOrientLayer), FocusDistanceLayer, NearClip,
  FarClip, HorizontalFilmSize, VerticalFilmSize, LayerBase. `PixelsPerMM` = 2.8352,
  `MinimumAllowedFOV` = 0.001. `CalculateVerticalFOV(zoom, h)` = 2·atan(h/2 / zoom) (зум и высота в
  пикселях; 0.001 ≤ угол, ≥ π → 3.1315928); `CalculateLensZoom(fov, w, par)` = w·par / (2·tan(fov/2));
  `HorizontalFieldOfView(Lens)` 0…4 = 73.7398, 65.4705, 54.4322, 39.5978, 25.3608° (24/28/35/50/80 мм
  на 36 мм); `CalculateFStop` = фокусное / (aperture / PixelsPerMM). «New Camera» в VegasEffects.exe
  (`FUN_140a45760`, вызов из `FUN_1405a8740` с 17.72 и 100): объектив 2 (35 мм), zoom = фокус.
  `AutoOrientAtTime` режим 4 — к `target`.
- `Model3DMaterial` (impl 0x290): XML `<Material Version="7">` (`FUN_1802bf450` / чтение
  `FUN_1802c0700`) — ID, Name, IlluminationModel (+0x48), 13 имён текстур (+0x50…, `TextureMap` 0
  diffuse, 1 specular, 2 bump, 3 normal, 4 emissive, 5 occlusion, 6 metallic, 7 roughness, 8–10
  лак, 11–12 SG), DiffuseTextureHasAlphaChannel, MaxBumpDepth (1), цвета `<… R G B A/>` (+0x1f8
  diffuse, +0x208 specular, +0x218 ambient, +0x228 emissive), Shininess (+0x238, 1),
  Diffuse/SpecularReflectivity, Opacity (проценты), RefractionIndex (1), RefractionAmount,
  CK_SpecularReflectance (5), CK_Fresnel `<X Y Z/>` (0.9132, 0.9215, 0.9245), CK_Roughness (5),
  ReflectionType (1), OverrideMaterial, OcclusionStrength, MR_*Factor, SG_GlossinessFactor,
  AlphaCutOff, AlphaMode (Guess 0, Opaque 1, Blend 2, Mask 3); числа — `%g`. Список — `<Materials>`
  в `<Model3DAsset>` (ID, Name, ModelFileName, ParentFolderID, BoundingBox*, Model3DGroup,
  GeometryBatches, Materials, IsHidden).
- Импортёры материалов: Assimp (`FUN_1800670e0`) и FBX (`FUN_180080ef0`) — Phong; glTF
  (`FUN_1800af2a0`) — Metallic-Roughness: specular белый, base color и emissive переводятся из
  линейного в sRGB (12.92·c до 0.0031308, иначе 1.055·c^(1/2.4) − 0.055), metallic/roughness/
  alphaCutoff/occlusion strength/лак × 100, Opacity и обе Reflectivity 100, alphaMode MASK → 3,
  BLEND → 2, иначе 1; с `KHR_materials_pbrSpecularGlossiness` — Specular-Glossiness (diffuse и
  specular в sRGB, glossiness × 100).
- VegasEffects.exe: `Model3DSettingsDialog` (ui `FUN_1404da7c0`): вкладки Groups, Materials
  (`Model3DMaterialsTree`), Advanced (`Model3DAdvancedPane`), текущая — Materials; Name:, Path:,
  Upgrade, Import Maps, OK, Cancel. Строка материала — `Model3DMaterialWidget` (стрелка + имя в
  `InLineEdit`), строки свойств — `Model3DMaterialItemModel` (`FUN_1404fad30`), подписи по типам
  0x251d–0x2542 (`FUN_1404f5cc0`): Diffuse Map, Specular Map, Diffuse/Specular/Ambient/Emissive
  Color, Shininess, Diffuse/Specular Reflectivity, Opacity, Bump Map, Normal Map, Max Bump Depth,
  Index of Refraction, Refraction Amount, Illumination Model, Fresnel, Roughness, Specular
  Reflectance, Reflection Mode, Override Material, Emissive/Occlusion Map, Occlusion Strength,
  Metallic/Roughness Map, Roughness/Metallic Strength, Alpha Cut Off, Alpha Mode, Clear Coat
  Strength/Intensity Map/Roughness Strength/Roughness Map/Normal Map, SG Specular/Glossiness Map,
  SG Glossiness Strength.
- `LightLayer`: свойства (`FUN_1802fd7e0`) — `position` (0, 400, 0), `target`, `orientation`
  (у прожектора X = 90: ось −Z смотрит вниз), `rotationX/Y/Z`, `color` белый, `intensity` 100,
  `castShadows` false, `falloff` 1, `reach` 1000, `spotlightAngle` 15, `spotlightFeather` 50,
  `shadowOpacity` 50, `shadowDiffusion` 0, `shadowColor` чёрный; XML Version 4 с `<LightType>`.
  Материал слоёв (`FUN_1802e52c0`, у модели `FUN_1802f33b0`): `illuminated` true,
  `receiveShadows` true, `shadowColor` чёрный, `castShadows`/`castShadowsIfLayerDisabled`/
  `billboardedShadows`/`billboardedLights` false, `matAmbient` 100, `matDiffuse` 50,
  `matSpecular` 50, `matShininess` 100, `matEmissiveColor` чёрный, `castReflections`,
  `receiveReflections`, `specularReflectivity` 100.
- Флажок static `CreateProperty` (третий аргумент; в XML — `Type="1"`, анимируемые — `Type="0"
  CanInterpT="1"`): у света (`FUN_1802fce00`) статические только `castShadows` и `falloff`; `color`,
  `intensity`, `reach`, `spotlightAngle`, `spotlightFeather`, `shadowOpacity`, `shadowDiffusion`,
  `shadowColor` анимируются. У материала (`FUN_1802e52c0`) статические все флажки (`illuminated`,
  `receiveShadows`, `castShadows`, `castShadowsIfLayerDisabled`, `billboardedShadows`,
  `billboardedLights`, `castReflections…`, `receiveReflections`); `shadowColor`, `matAmbient`,
  `matDiffuse`, `matSpecular`, `matShininess`, `matEmissiveColor`, `specularReflectivity`
  анимируются. У камеры (`FUN_1802eec70`) анимируется всё: `aperture`, `zoom`, `focusDistance`,
  `blurDoF`. `selfShadowBias` создаётся у модели (`FUN_1802a5fc0`, при чтении старого проекта
  `FUN_1802a7500`): float, статический, по умолчанию 1, диапазон `DAT_18070af70` 1…1000
  (статический инициализатор `FUN_180001940`).
- `LayerType` (`AbstractLayer::Type`, слот +0x10): AssetLayer 0, Effect3DLayer 1, CameraLayer 2,
  LightLayer 3, GradeLayer 4, PointLayer 5, TextLayer 6, Model3DLayer 7.
- VegasEffects.exe, контроллеры камеры видов (`biff::ui::viewer::CameraController` и наследники
  `ActiveCameraController`, `NoCameraController`, `OrthoCameraController` vtable 0x141325e70,
  `PerspectiveCameraController` 0x141325dc0; слоты 11+ после QObject). Ortho: слот 11 — панорама
  (dx, dy пикселей) по типу вида: Front x −= dx/zoom, y += dy/zoom; Back x += dx, y += dy; Left
  z −= dx, y += dy; Right z += dx, y += dy; Top x −= dx, z −= dy; Bottom x += dx, z −= dy;
  слот 13 — зум протяжкой zoom × (1 − 0.001 × px); слот 16 — колесо zoom × (0.1 × щелчки + 1);
  `FUN_14095da90` ставит zoom не меньше 0.0002; слот 17 — точка зума под курсором (без Alt) через
  ClipAnchorPoint/ClipPosition. Perspective: слот 11 — панорама position += Right × (−dx) + Up × dy;
  слот 12 — наезд вдоль Heading; слот 13 — зум протяжкой 0.001/px; слот 14 — вращение на месте
  (углы += px × 0.1, в радианах × 0.017453292); слот 16 — колесо: наезд на 120 единиц за щелчок
  (к опорной точке, если она задана); слот 18 — облёт 0.0052359878 рад/px вокруг опорной точки;
  слот 19 — `FluxCamera::Create` с near 0.1, far 100000, FOV 1.0471976 (60°).
- VegasEffects.exe `Model3DAdvancedPane` (0x1404eaae0): список единиц Millimeter…Mile, Pixel
  (3001–3010), список способов нормалей From File (0), Generate Faceted + Auto Smoothing (1),
  Generate Faceted (2), From File + Auto Smoothing (3); модель панели по умолчанию — Meter, 80°.
- XML экземпляра (`FUN_180139820`): `<Model3DAnimationAssetInstance Version="1">`, `ID`, `Name`,
  `AssetID`, `ParentCompID`, `ParentLayerID`, `ParentInstanceID`, `PropertyManager`.
  XML ассета (`FUN_180134bb0`): `<Model3DAnimationAsset Version="3">`, `ID`, `ParentFolderID`,
  `IsHidden`, `Name`, `Filename`, `OverrideFrameRate`, `FrameRate` (`%g`), `AnimationIndex`.
- Теги значений вариантов PropertyManager по порядку типов: `i`, `u`, `fl`, `db`, `b`, `sc`, `or`,
  `cl`, `p2`, `p3`, `v2`, `v3`, `id`, `ma`, `st`, `cp` (bool — `b`).

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
