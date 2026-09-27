# RE: Flux.dll — рендер-движок / рендер бросает в OpenGL-контекст FXMedia

> Анализ бинарника **`Flux.dll`** (9.4 МБ) из `SAMPLES/VEGAS_Effects/`. Выполнен
> **headless-декомпиляцией через Ghidra (PyGhidra 3.1.0 + AnalyzeHeadless)**, как и
> `Tannen.dll` (см. `RE_Tannen_dll.md`). Цель — clean-room реимплементация: бинарник
> изучается только для понимания архитектуры, код пишется заново (GPL v3).

## Как получен результат

1. `Flux.dll` ранее был импортирован в Ghidra-проект, но не проанализирован.
2. Первый прогон (без `-noanalysis`) выполнил полный анализ: **26739 функций**
   (RTTI Analyzer 5.0s, x86 Constant Reference Analyzer 52.0s, всего 190s CPU).
3. Декомпиляция в папку `%TEMP%\opencode\flux_decomp_full` в фоне оборвалась после
   152 файлов — **причина: `Remove-Item -Recurse` по «живой» папке удалил часть
   незаблокированных файлов дампа** (моя ошибка, не скрипта).
4. Повторный чистый прогон с `-noanalysis` (анализ уже сохранён) в **foreground**:
   `DONE: wrote 26739 files, 0 failures` → на диске **26352 уникальных файла**
   (остальные 387 — коллизии санитизированных имён, например группы `Create`,
   `Update`, `Select` из разных классов перезаписывают друг друга).

Итоговый дамп: `SAMPLES/VEGAS_Effects/decompile-src/flux_decomp_full/` (26352 `.c`).
Вспомогательные списки: `flux_exports.txt` (282 экспорта), `flux_types.txt`
(319 токенов `biff::flux`), `flux_vftables.txt` (115 vftable-владельцев).

Рабочие скрипты (в `%TEMP%`, live): `decompile_pyghidra.py`, `decompile_addrs.py`
(точечная декомпиляция по адресам/именам), `flux_addr_inventory.py`, `flux_exports.py`.
Запуск (CWD без `+`, GUI Ghidra закрыт, лок снят):
```
<venv>\python.exe -m pyghidra.ghidra_launch --install-dir "C:\Program Files\Ghidra" \
  ghidra.app.util.headless.AnalyzeHeadless "D:\ghidra_projects\OpenVegasEffects" "VegasEffects" \
  -process Flux.dll [-noanalysis] \
  -scriptPath "<путь без +>" -postScript <script>.py [args...]
```

## Что это

`Flux.dll` — **рендер-движок HitFilm/VEGAS Effects**. Публичный фасад — классы
**`biff::flux`** (`Flux`, `FluxView`, `FluxCanvas`, `FluxCamera`, `FluxSelect`,
`FluxTextData`, `FluxOpenGLInfo`, `FluxVideoMemory`, `FluxUtilities`, `FluxCanvasCache`),
внутренняя библиотека — **`biff::flux::lib`** (собственно рендер-пайплайн).

`Flux.dll` при этом **не владеет** OpenGL-контекстом и медиа-слоями: контекст
приходит извне как `fxtl::gl::FXRenderContext` (модуль `FXMedia`), а объектная
модель — из `Project.dll` (`biff::project`). Граница модуля:

- **принимает:** `weak_ptr<fxtl::gl::FXRenderContext>` (контекст GL),
  `shared_ptr<biff::project::FontCache>` (шрифты), `shared_ptr<CompositionAsset>` /
  `shared_ptr<EditorSequence>` (композиции/секвенции), `Project&`;
- **отдаёт:** `shared_ptr<fxtl::gl::FXTexture>` (результат рендера).

Подтверждающие сигнатуры (из деманглинга `Flux::Create`, `Flux::Render*`, `FluxTextData`):

```
Flux::Create(weak_ptr<FXRenderContext> const&, shared_ptr<FontCache> const&,
             string const&, string const&, bool, bool, bool)          // @ 0x1802665c0
Flux::Render(shared_ptr<FluxView> const&) — FluxErr                   // @ 0x180267440
Flux::RenderFrame(shared_ptr<CompositionAsset> const&, int, FrameRequest const&, FrameRequest const*)
Flux::RenderFrame(shared_ptr<EditorSequence> const&, int, FrameRequest const&, FrameRequest const*)
Flux::RenderPreview(shared_ptr<CompositionAsset/EditorSequence> const&, int, FrameRequest const&)
Flux::RenderScopes(shared_ptr<FXTexture> const&)
Flux::AddModel3DAssetToCache(...), RenderTexture... (см. список экспортов)
```

## Публичный API (282 экспорта, полный список в flux_exports.txt)

**Реэкспорты из `Project.dll`** (заглушки/ре-экспорт символов, чтобы связать библиотеки):
`Abstract3DEffectEngine`, `AbstractAsset`, `AbstractAssetInstance`, `AbstractPlugin`
(c методами `About/Icon/Options/OptionsButtonName/GetControlLayerAttachement/ViewerOverlayMode`),
`AbstractSequenceTrack`, `AbstractTimeline`, `MediaManager` (enable_shared_from_this +
`shared_from_this`/`weak_from_this`), плюс Qt-moc-заглушки `default_constructor_closure` /
`copy_constructor_closure` (39) и `tr/trUtf8`.

**Собственные классы `biff::flux`** (адреса экспортируемых точек):

| Класс | Точки (примеры) | Назначение |
|---|---|---|
| `Flux` | рендер-входы: `Render`@`180267440`, `RenderFrame`@`180269f60/180269fb0`, `RenderPreview`@`18026a000/18026a050`, `RenderScopes`@`180266720`, `RenderLayer`@`180268720`, `RenderAsset`@`180268f80`, `RenderVisualObject`@`180269390`, `RenderTextUsingCurrentContext`@`18026a600`, `CopyTexture`@`18026aa10`, `RenderDebugSquare`@`18026aad0`, `Renderer`@`18026a990` (getter); ctor@`180266990` (полный ctor `FUN_180262fb0`), `~`@`180266a20`, `Create`@`1802665c0`; управление: `SetProject`@`180266880`, `SetComposition`@`180266cc0`, `SetSequence`@`180266dc0`, `SetLicense`@`1802666b0`, `SetWatermarkImage`@`18026a9d0`, `SetOfflineMediaImage`@`18026a9f0`, `SetDefaultEnvironmentMap`@`18026a8d0`, `SetCancelRender`@`18026a980`, `SetRenderInModel3DDialog`@`18026a910`, `SetHardwareVideoDecodingEnabled`@`18026abb0`(+`Is...`@`18026ab90`), `SetUseProxies`@`18026a8a0`, `SetUseEncodeDecodeDebugSquares`@`18026abe0`, `ShowEncodeDecodeDebugSquares`@`18026abf0`, `ResetFluxCache`@`180267320`; 3D-модели: `AddModel3DAssetToCache`@`18026a960`, `RemoveModel3DAsset`@`18026a450`, `RemoveModel3DAnimationAsset`@`18026a530`, `ClearModel3DAnimationAssets`@`18026a35b0`, `GetModel3DAnimationCache`@`18026ac00` | фасад движка |
| `FluxView` | ctor@`1802b56d0`/`1802b5750`, `Create`@`1802b55b0`/`1802b54f0`, `~`@`1802b5890`; `SetViewCamera/ViewCamera/SetDownsample/DownsampleValue/ResizeView/SetActive`@`1802b5a70..`; `RenderedChannelsShaderType`@`1802b5c90` | «окно» рендера (канвас + камера + downsample) |
| `FluxCanvas` | ctor@`18028a8a0/18028a930/18028a9e0`, `~`@`18028aa30`, `Create`@`18028a660/18028a710/18028a7c0`; `Bind`@`18028ab60`/`UnBind`@`18028ab90`/`Update`@`18028acd0`, `Copy`@`1802849c0`, `GetBuffer(Size)`@`18028ac80..`, `AssetWidth/Height`@`18028ac10/...` | GL-текстурный канвас |
| `FluxCanvasCache` | ctor@`18028b050`, `Create`@`18028b060` | кэш канвасов |
| `FluxCamera` | 5 ctor@`180284a70..180284f40`, `~`@`180285de0`, `CreateDefaultCamera`@`1802848f0`; `Copy`@`1802849c0`, `OrbitCamera`@`180285030`, `OrientateToCamera`@`180285a30`, `EyeMatrixView`@`180285fa0`, `GetCameraType`@`180285f10`, `SetEulerAngles/SetClipAnchorPoint/SetClipPosition/SetZoom` | камера (lib::Camera) |
| `FluxSelect` | ctor@`18029f6a0`, `~`@`18029f6f0`, `Select`@`1802a0b10`, `RenderSelectedWireFrameTriangles`@`1802a2c20` | пикинг объектов |
| `FluxTextData` | ctor@`1802a96e0`, `~`@`1802a97b0`; `GetTextBoundingBox`@`1802aa970`, `GetTextLines`@`1802aa980`, `GetTextOutlines`@`1802a9640`, `GetCursor*`, `SetCurrentTextTokenFormat` | текстовый рей-лей (раскладка/границы) |
| `FluxOpenGLInfo` | ctor@`180292440`, `~`@`180292480`, `InitializeOpenGLInfo`@`180292260`, `IsGPUBelowMinimumRequirements`@`180292420`, `GPUSupportedAntialiasingModes`@`1802939a0`, `VideoMemory`@`1802923f0` | паспорт GPU через GL |
| `FluxVideoMemory` | экспорт: `Create`@`180293c10`, `AvailableGPUNames`@`1802b22f0`, `RequiredVideoMemory`@`1802b2ce0`; внутренние: `MinimumRequiredSize`@`1802b22e0`, `DedicatedSize`@`1802b2760`, `SystemRAM`@`1802b2d10` | требования/отчёт по видеопамяти |
| `FluxUtilities` | `CreateBezierCurve`@`1802ae0c0/1802ae400`, `CurvedHandles`@`1802ae640` | статические утилиты кривых |
| vftable-экспорты | `1807897d0..18078ba68` (6 шт.) | публичные таблицы виртуальных функций |

## Инвентарь внутреннего движка `biff::flux::lib`

Из ссылок на vftable в дампе (115 владельцев, полный список в `flux_vftables.txt`);

- **Композиторы (рендер-пассы):** `AbstractCompositor`, `PlanarCompositor`,
  `PlanarParticleCompositor`, `AlphaShadowMapCompositor` (+`ColorPass`/`DepthPass`),
  `UnlitPrimitiveCompositorDepthPass`, `Unrolled3DModelParticleCompositor`
  (+`DepthMapCompositor`, `ShadowMapCompositor`), `GradeCompositor`.
- **Бленды:** `Blend`, `ShaderBlend`, `AddBlend`, `AddRGBABlend`, `MultiplyBlend`,
  `NormalBlend`, `GradeBlend`.
- **Материалы слоёв:** `AbstractMaterial`, `CompositionLayerMaterial`, `ImageLayerMaterial`,
  `VideoLayerMaterial`, `SolidLayerMaterial`, `TextBoxMaterial`, `Model3DLayerMaterial`,
  `GradeLayerMaterial`, `EditorSequenceMaterial`, `Effect3DLayerMaterial`,
  `Default/PreviewParticleMaterial`, `WorldGridMaterial`, `InvalidLayerMaterial`.
- **Шейдеры/кэши/пассы:** `AbstractAlphaShadowMapShader` + `AlphaShadowMap*Shader`,
  `PositionNormalPass*Shader`, `Unlit*Shader` (Asset/Model3D/Particle/Text),
  `DepthMap(+Cache)`, `DepthMapCacheItem`, `AlphaShadowMapCache`, `AmbientOcclusionMapCache`,
  `DynamicReflectionsMapCache` (+Cube/PlanarMap), `LayerTextureMapCache`,
  `Model3DMaterialTextureCache`, `FrameBufferCache`, `FluxCache`, `FluxCacheManager`,
  `ResourceCache`, `ResourceManager::ResourceManager_Impl`; фрагмент-шейдерные данные:
  `*FragmentShaderData/Func` (AmbientOcclusion, DepthTest, EnvironmentMap, GlancingAngle,
  PlanarClipping, PlanarReflection, SoftParticle; `BlendShaderData`).
- **Рендереры высокого уровня:** `Renderer`, `Camera`, `Canvas`, `CanvasCache(+Item)`,
  `DepthMapRenderer`, `DepthOfFieldRenderer`, `DynamicReflectionsRenderer`,
  `AmbientOcclusionRenderer` (+`AmbientOcclusionCacheGenerator`), `AlphaShadowMapRenderer`,
  `Model3DRenderer(+Group/+GroupNode)`, `MotionBlurRenderer` (+`MotionBlurCameraSample`),
  `CompositeShotDepthMapRenderer`, `FontRenderer` (+`FreeTypeFont`, `HarfBuzzWrapper`,
  `UnicodeHelper`, `CharacterCacheItem`, `CharacterOutline`, `TextBoxOutlineVertex`).
- **Геометрия/кривые:** `PrimitiveTree(+Node2D/Node3D)`, `TriangleList`, `BezierPath`,
  `BezierPoint`, `BezierCurve`, `LinearCurve`, `QuadraticCurve`, `CubicCurve` (все — с
  `RTTI_Type_Descriptor`), `OrientedBoundingBox`, `SelectionVolume`, `Mask3D`, `MaskData`,
  `MaskDataList`, `MaskRenderer`, `Fog`, `HalfPlane`, `WorldGrid`, `PingPong`.
- **Cоординация/математика:** `ViewFrustum` (+`OrthographicViewFrustum`,
  `PerspectiveViewFrustum`), `ClipWindow`, `ViewInfo`, `AffineCoordGenerator`,
  `TextureCoordGenerator`, `SphericalHarmonics`, `SplitSumApproximation`, `Mirror`,
  `RGBAToDepthConverter`, `LatLongToPixel`, `PostFilteredDimension`.
- **Поток данных (фетчер кадров):** `TimelineDecoder`, `TimelineFetcher`,
  `FetcherOutput` (`_anon_565E88A7`), `VisualObjectTransition`, `EditingLayerCacheManager`.
- **Системное:** нанолог-логирование `nanolog::QueueBuffer/RingBuffer`, `PPL/Concurrency`,
  `boost`, `std`, `Gdiplus::Image`.

## Ядро по существу

### `Flux::Create(...)` @ 1802665c0 (фабрика)
Принимает `weak_ptr<FXRenderContext>` (контекст GL из FXMedia) + `shared_ptr<FontCache>`
(из Project) + два `std::string` (похоже префиксы/настройки окружения) + 3 `bool`
(один из них — флаг «оффлайн-медиа». выделение 8 байт + вызов полного ctor `FUN_180262fb0`
на блоке 0xb8 байт, упаковка в `shared_ptr<Flux>`).

### Полный конструктор `FUN_180262fb0`
- Инициализация полей фасада: `shared_ptr<Project>` (+0x10/+0x18), renderer (+0x20),
  текущий кадр `int` (+0x3c), глобальный синглтон `g_Flux = DAT_1808aa160` (`@1808c8a68`)
  с `LOCK`/`UNLOCK`.
- Чтение `QSettings` (реестр/ini): `GlobalOptions/FluxLogLevel`, `FluxLogBuild`,
  `FluxLogPerf`, `FluxLogFilter`, `FluxLogDir` → инициализация **nanolog**-логгера
  с именем файла `HitFilm_Flux_<PID>.log` (префикс-строка `s_HitFilm_Flux_180789078`),
  `GetSystemTimePreciseAsFileTime` для таймингов.
- Дальше: настройки лога/дебага, создание `Model3DAnimationCache` (из `project`) →
  присвоение в renderer+0x458; флаги рендера на renderer+0x2e8 и +0x70 (`param_7`
  — вероятно «render in Model3D dialog», `param_6` — «offline media image»).
- Низкоуровневые ключи GlobalOptions из дампа: `FluxFetcherCompFetches`,
  `FluxFetcherEnabled`, `FluxFetcherLookAheadFrames`, `FluxFetcherSoftwareOnly`
  (подсистема prefetch кадров — `TimelineFetcher`/`TimelineDecoder`).

### Цикл кадра: `Flux::RenderFrame/RenderPreview` @ 180269f60/180269fb0, 18026a000/18026a050
Тонкие обёртки: сохраняют `frame` во `Flux+0x3c` и в `renderer+0x424`, затем диспатч:
- CompositionAsset → `FUN_1802659c0(this, comp, frame, mode, request, 0/req2)`;
- EditorSequence → `FUN_180265fd0(...)`.
`mode` = 1 (render) или 2 (preview).

### `Flux::Render(shared_ptr<FluxView>)` @ 180267440
Главный метод отрисовки во view (по сути `Renderer`-обёртка):
1. Логирует по `__FILE__` (`...\HitFilm\Modules\Flux\Flux.cpp`, строка 0x4c3).
2. Берёт `renderer = this+0x20`; проверяет `renderer+0x60` (активные данные проекта).
3. Билд списка `RenderSettings`-замыканий (рефконты/`LOCK`), получение
   `renderer+0x60/0x68` (settings) и `renderer+0x70/0x78`.
4. `FUN_1802703b0(...)` — рендер визуального объекта (visual object / viewport), затем
   `FUN_180264160(...)` — общий вход renderer'а.
5. `FUN_1803597c0(renderer+0x60, ...)` — канвас/композиция; `FUN_1803974d0(...)` —
   рендер сцены; `FUN_180398da0(0, w, h, ..., view, camera?, ..., channelMask, ...)` —
   **непосредственная отрисовка сцены** с камерой.
6. `switch(view+0x3c)` — выбор режима шейдера/каналов вывода: 0 → 0x4/0x18, 1(?) → 0x3/0x1c,
   2 → 0x10, 3 → 0xd/0x1e, 4 → 0xe/0x20, 5 → 0xf/0x22; если `renderer+0x1c0+0x7c` —
   используется альтернативный код.
7. Финал: если `DAT_1808aa15c==0` → `glFlush(); checkGlErr(); glFinish(); checkGlErr()`.
8. Если view активен (+0x24=1): `biff::project::Project::ScopeEffects(project)` →
   `FUN_18039af50(...)` — рендер scopes (осциллографы/векторскоп).
9. Обход `renderer+0x3d8/0x3e0` — список пост-рендер-колбэков (оверлеи), вызов vtable+0x18.

### View / Canvas / Camera
- **FluxView** = 0x68-байтный объект: вложенный `FluxCanvas` + `shared_ptr<FluxCamera>`
  (+0x10/+0x18), `Downsample` (+0x20), флаг активности (+0x24), +0x28 (композитор вывода).
  `FluxView::Create(camera, w, h, Downsample, BPC)` — canon из Tannen/Project;
  второй `Create(w,h,Downsample,BPC)` сам создаёт камеру.
- **FluxCanvas** — обёртка над GL-текстурой: `Bind()` = `glBindTexture`, `UnBind()`,
  `Update(w,h,Downsample)` = пересоздание текстуры канваса по масштабу
  (`FUN_1803cb320`), размеры `AssetWidth/Height`, `ContentWidth/Height`, `TopRight/BottomLeft`,
  `Texture()` (getter), `GetBuffer*`. Есть вариант `Create(shared_ptr<FXTexture const>)` —
  канвас поверх готовой текстуры FXMedia.
- **FluxCamera** — владеет `shared_ptr<lib::Camera>` (полный ctor `FUN_1803c2080`):
  позиция/цель/up (`FXPoint<3,float>`), near/far, FOV, тип (`FluxCameraType`),
  вспомогательные `EyeMatrixView/Model`, `ClipMatrix`, `OrbitCamera`, `OrientateToCamera`,
  `ClipLineSegmentNearPlane`, `BetweenNearAndFarClipPlanes` и т.д.
  `CreateDefaultCamera(w,h,?)` → `lib::Camera` через `FUN_18039a680`.

### GPU-паспорт: `FluxOpenGLInfo::InitializeOpenGLInfo(videoMem)` @ 180292260
Запросы `glGetIntegerv(0xD33 /*MAX_TEXTURE_SIZE*/)`; по объёму видеопамяти и
вендору (`FUN_18030f7d0` — вероятно проверка "Intel") выбирает лимиты
`MaxTextureWidth/Height` (`DAT_1808c8a60`) и `MaxTextureDepth (0x438/0x800/0x1000/0x2000)`:
- vmem<500 → 0x780/0x438; <800 → 0x800; 800..4000 → 0x1000; >4000 & MAX_TEXTURE>0x1FFF & !Intel → 0x2000.
Взаимодействует с `FluxVideoMemory::{RequiredVideoMemory, MinimumRequiredSize, SystemRAM, DedicatedSize}`.

### Пикинг: `FluxSelect::Select(view, FXPoint<2,float>*, FXList<FXID>&)` @ 1802a0b10
Полномасштабный пикинг: берёт размер канваса из view (offsets 0x38/0x3c), id объектов
`FXID`, строит `fxtl::gl::TriangleList` и через лямбду
`_Func_impl_no_alloc<<lambda_7dbf7e64...>, void, shared_ptr<TriangleList>>` отдаёт
результат в `FUN_1802a3ef0` (selection traversal) + `RenderSelectedWireFrameTriangles`.

### Текст: `FluxTextData` ctor @ 1802a96e0
Берёт **текущий** GL-контекст: `wglGetCurrentDC()` + `wglGetCurrentContext()`, хранит
`shared_ptr<biff::project::FontCache>`, инициализирует движок шрифтов
(`biff::flux::lib::FontRenderer`/`FreeTypeFont`/`HarfBuzzWrapper` — токены в дампе).
API: `GetTextBoundingBox`, `GetTextOutlines`, `GetTextLines`, `GetCursor*`,
`SelectText/SelectLine`, `GetFormatForString`, `SetCurrentTextTokenFormat`.

## «Отпечаток» исходников (__FILE__-строки в дампе)

```
C:\Gitlab-Runner\builds\BjqbSWcs\0\FXhome\hitfilm\hitfilm\HitFilm\Modules\Flux\
  Flux.cpp
  FluxLib\ResourceManager.cpp
  FluxLib\ResourcePool.cpp
  FluxLib\TimelineDecoder.cpp
  FluxLib\TimelineFetcher.cpp
```
То есть модуль зовётся **`Flux`**, внутренняя библиотека — **`FluxLib`**
(соответствует `biff::flux::lib`, «FluxLib» = папка `Modules\Flux\FluxLib`).
Сборка — GitLab CI FXhome/hitfilm/hitfilm.

## Связь с другими модулями

- **`FXMedia` (fxtl::gl)** — поставщик GL-контекста и текстур:
  `weak_ptr<FXRenderContext>` в `Flux::Create`, `shared_ptr<FXTexture>` в
  `FluxCanvas::Create(FXTexture)` / `Flux::RenderScopes(FXTexture)`,
  типы `fxtl::gl::FXGLexception/FXGLSLexception`, `fxtl::opencl::FXOpenCLexception`
  (поддержка OpenCL вычислений — видовые/масочные рендереры могут использовать OpenCL).
- **`Project.dll` (biff::project)** — модель: `CompositionAsset`, `EditorSequence`,
  `EffectInstance`, `Project::ScopeEffects`, `FontCache`, `Model3DAnimationCache`,
  `MediaManager`, enums `Downsample`/`BPC`/`FXAntialiasingMode`; реэкспорты абстрактных
  классов `AbstractPlugin/Asset/AssetInstance/SequenceTrack/Timeline/3DEffectEngine`.
- **`VegasEffects.exe`** — хост: создаёт `Flux::Create(...)`, каждый кадр зовёт
  `Render/Preview`, рисует scopes, использует `FluxSelect` для выделения.

## Артефакты и следующий шаг

- Дамп: `decompile-src/flux_decomp_full/` (26352 `.c`), списки: `flux_exports.txt`,
  `flux_types.txt`, `flux_vftables.txt`.
- Перекрёстный инвентарь `biff::flux::lib` в Ghidra-проекте (программа `Flux.dll`,
  анализ сохранён) уже пригоден для навигации по адресам.

**Дальше:** 1) сопоставить композиторы/пассы с полями `renderer+0x«0x60/0x100/0x1c0/0x3e0»`
(методом до-анализа доступа в `FUN_180398da0` и vtable-диспетчеров); 2) реконструировать
`ViewInfo/ClipWindow/Frustum` для функционального ядра; 3) распутать коллизии имён
(`FluxCanvas::Create`, `FluxView::Create` и пр.) точечной декомпиляцией по адресам
из `flux_exports.txt`; 4) связать пайплайн с `RE_VegasEffects.md` (точки вызова
`RenderPreview/RenderFrame` в хосте).

## Перенос подтверждённых контрактов в код

`RenderManager` следует recovered-схеме Flux View/Canvas: запрос несёт размер/downsample,
результат — RGBA-буфер вместе с фактическим размером, а кадры кэшируются по времени, размеру и
профилю качества. Слои композитятся снизу вверх с transform/opacity/blend mode; 3D использует
отдельные camera/light/model matrices. Маски выполняются отдельным alpha-pass после clip effects,
включая expansion, opacity, invert и feather. Text использует отдельный layout/render pass с
Point/Paragraph box. Такая декомпозиция повторяет границу `Flux::Render` и пост-рендер-пассов,
оставаясь на Qt/QImage вместо закрытого OpenGL ABI.
