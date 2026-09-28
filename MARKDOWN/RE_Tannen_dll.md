# RE: Tannen.dll — менеджер плагинов / эффектов / хост

> Анализ бинарника **`Tannen.dll`** (8.2 МБ) из `SAMPLES/VEGAS_Effects/`. Выполнен
> **headless-декомпиляцией через Ghidra (PyGhidra + AnalyzeHeadless)** в обход
> GhidraMCP, который стабильно обслуживал активную программу `Project.dll`.
> Цель — clean-room реимплементация: это чужой бинарник, изучается только для
> понимания архитектуры, код пишется заново (GPL v3).

## Как получен результат

Канал GhidraMCP (bridge на порту 8080) держал процесс **Ghidra с открытым `Project.dll`**
как активную программу и не переключался на `Tannen.dll`. Пользователь условно указал
на API `GhidraScript`/PyGhidra — поэтому `Tannen.dll` декомпилирован **headless**:

1. Завершён фоновый процесс GhidraMCP (PID 33576, `javaw` + `Ghostscript`-classloader),
   удерживавший лок проекта → снят устаревший `VegasEffects.lock`.
2. Настроен **PyGhidra** на Python 3.13 (venv + `pyghidra`/`jpype1` wheels из
   `Ghidra\Features\PyGhidra\pypkg\dist`) — Ghidra 12 убрал Jython, `.py`-скрипты
   требуют CPython.
3. Исправлен скрипт декомпиляции (нельзя наследовать `GhidraScript` в PyGhidra —
   формат: обычный Python-модуль с глобалами `currentProgram`, `println`
   и `ghidra.app.decompiler.DecompInterface`).
4. Запуск (важно: **CWD без `+`**, и **позиционные `<project_location> <project_name>`**,
   иначе AnalyzeHeadless путает их с флагами; символ `+` в пути тоже валиден/невалиден
   по-разному):
   ```
   <venv>\python.exe pyghidra_launcher.py "C:\Program Files\Ghidra" --headless \
     "D:\ghidra_projects\OpenVegasEffects" "VegasEffects" \
     -process "Tannen.dll" -noanalysis \
     -scriptPath "C:\Users\Admin\AppData\Local\Temp\opencode" \
     -postScript decompile_pyghidra.py <выходная_папка>
   ```
   Результат: **25116 функций**, записано **24765** `.c` (часть имён коллизируется
   после санитизации — перезаписываются).

Итог: **`D:\Devs\C++\OpenVegas_Effects\SAMPLES\VEGAS_Effects\decompile-src\tannen_decomp\`**
(24765 файлов). Рабочая копия скрипта: `tools/ghidra-mcp/decompile_pyghidra.py`
(актуальная live-версия в `%TEMP%`, в проект копия для справки).

> Позже (sandbox) `pyghidra_launcher.py` стал падать (`python.exe ... line 1 MZ…`,
> venv `python.exe` интерпретировался как скрипт). Надёжный обход — запускать
> модуль напрямую:
> ```
> <venv>\python.exe -m pyghidra.ghidra_launch --install-dir "C:\Program Files\Ghidra" \
>   ghidra.app.util.headless.AnalyzeHeadless "D:\ghidra_projects\OpenVegasEffects" "VegasEffects" \
>   -process sandbox.exe [-noanalysis] \
>   -scriptPath "C:\Users\Admin\AppData\Local\Temp\opencode" -postScript <script>.py [args...]
> ```
> (без `+` в CWD/`-scriptPath`, GUI Ghidra закрыт, лок снят).

## Что это

`Tannen.dll` — модуль **менеджера плагинов и эффектов / хоста**, конкретные реализации
для абстрактных интерфейсов из `Project.dll` (`biff::project`). Пространство имён —
**`biff::tannen`** (MSVC-демиглинг `PluginManager::PluginManager` →
`biff::tannen::PluginManager`, mangled `...@tannen@biff@@`).

В отличие от `Project.dll` (объектная модель ~130 классов), здесь — **конкретика**:
- конкретные реализации плагинов/эффектов/переходов;
- OFX-хост-интеграция (OpenFX): загрузка/кэш плагинов, обёртки эффектов и переходов;
- работа с файлами плагинов (`AEPluginFile`, `PluginFile`), путями, лицензированием
  (`AuthorizePlugin`, `AbstractLicenseManager`), рендером (`RenderManager`,
  `FluxRenderer`), UI-контролами `PluginUI*`.

## Ключевые символы (из имён декомпилированных функций)

Сигнатуры самого `PluginManager` (конкретный, от `biff::project::AbstractPluginManager`).
После полного анализа (re-run без `-noanalysis`, 25116 функций) имена восстановлены,
и сигнатуры получены **дословно из деманглинга** (см. `decompile-src/tannen_decomp_full/`):

**Приватный конструктор** (`PluginManager.c`, mangled `?PluginManager@PluginManager@tannen@biff@@…`):
`biff::tannen::PluginManager::PluginManager(
  vector<std::basic_string<char>> const&,      // param_1: пути поиска плагинов
  shared_ptr<biff::marvin::Marvin> const&,     // param_2: Marvin-хост
  std::wstring const&, std::wstring const&, std::wstring const&,  // param_3/4/5
  BiffHostEdition,{, Callbacks*, int, PluginManager::LogMode,
  bool, bool, void*, vector<fxtl::FXID> const*,  // param_13 (out: уже загруженные FXID)
  shared_ptr<biff::project::AbstractLicenseManager> const&)  // param_14`

**Статическая фабрика** (`Create.c`, mangled `?Create@PluginManager@tannen@biff@@SA?AV?$shared_ptr@VAbstractPluginManager@project@biff@@@std@@…@Z`):
`static std::shared_ptr<biff::project::AbstractPluginManager> biff::tannen::PluginManager::Create(
  vector<std::basic_string<char>> const&,  // param_1: пути поиска плагинов
  shared_ptr<biff::marvin::Marvin> const&, // param_2
  std::wstring const&, std::wstring const&, std::wstring const&,  // param_3/4/5
  BiffHostEdition, Callbacks*, int, LogMode, bool, bool, void*)`  // 12 params
— внутри `Create` аллоцирует `PluginManager*` через `FUN_18058bb60(0x10)` (это
`operator new`/RAII-holder, затем приватный ctor) и вызывает конструктор, передавая
`vector<fxtl::FXID>* = NULL` (param_13) и out-`shared_ptr<AbstractLicenseManager>*`
(param_14) — т.е. **`vector<fxtl::FXID>` — это внутренний out-параметр конструктора,
а не вход `Create`**. Возвращает `shared_ptr<AbstractPluginManager>`.

Мангла подтверждает подключение **Marvin.dll** (`shared_ptr<biff::marvin::Marvin>`)
как хост-сервиса.

Определяющие символы чистого интерфейса (`OfxGetPlugin`, `PluginInfo`) лежали бы под
именами `FUN_*`, т.к. `-noanalysis` не именовал таблицу экспорта; их функция выполняет
OFX-машинерия: `GetOFXHost`, `LoadOFXPlugins`, `TryLoadOFXPlugin`,
`TryLoadOFXPluginByUpgradingMajorVersion`, `HasOFXPlugins`, `IsInOFXPluginEntryPoint`,
`SetInOFXPluginEntryPoint`, `ClearOFXCacheOnClose`.

## Конкретные плагины (наследники `AbstractPlugin*`)

| Файл | Назначение |
|---|---|
| `Plugin2DEffect` / `_Plugin2DEffect` | конкретный 2D-эффект |
| `PluginAE2DEffect` / `_PluginAE2DEffect` | AE-стиль 2D-эффект |
| `PluginAudioEffect` / `_PluginAudioEffect` | аудио-эффект |
| `PluginAudioTransition` / `_PluginAudioTransition` | аудио-переход |
| `PluginVideoTransition` / `_PluginVideoTransition` | видео-переход |
| `PluginGeometryEffect` / `_PluginGeometryEffect` | геометрический эффект |
| `PluginBehaviorEffect` / `_PluginBehaviorEffect` | поведение |
| `PluginUIButton` … `PluginUIString` | UI-контролы плагина (Button, CheckBox,
  ColorPicker, DirectoryPicker, Label, LayerPicker, MaskPicker, MultiLineString,
  Orientation, String) |
| `OFXPlugin2DEffectWrapper` / `OFXPluginTransitionWrapper` | обёртки OFX-эффектов/переходов |
| `PluginFile` / `_PluginFile`, `AEPluginFile` / `_AEPluginFile` | файл плагина (disk/load) |

Абстрактные базы для контракта: `AbstractPlugin`, `AbstractPlugin2D`,
`AbstractPluginAudio`, `AbstractPluginAudioTransition`, `AbstractPluginBehavior`,
`AbstractPluginGeometry`, `AbstractPluginInstance`, `AbstractPluginManager`,
`AbstractPluginVideoTransition`, `AbstractAsset`, `AbstractAssetInstance`.

## Служебные глобальные функции

`GetPluginManager` / `GetPluginManger`, `GetPluginFile`, `GetPluginPaths`,
`Plugins`, `PluginByID`, `PluginCategoryTree`, `FindPluginsByType`, `LoadPlugin`,
`InitializePlugin`, `CancelPluginLoading`, `HasFinishedLoadingPlugins`,
`GlobalPluginProperties`, `PluginPresetsPath`, `ReadPluginMetadata`,
`MissingPluginsToDeserialize` / `ArePluginsToDeserializeMissing` /
`AddMissingPluginToDeserialize` / `ClearMissingPluginsToDeserialize`,
`AuthorizePlugin`, `CreateAPI`, `GetOFXHost`, `GetFluxRenderer`.

Рендер: `BeginBackgroundRender`, `EndBackgroundRender`, `IsBackgroundRender`,
`BeginRender`, `EndRender`, `Render`, `RenderManager`, `RenderCheckpoint`,
`CustomUIRender`, `GetScratchRenderBuffer`, `ClearReservedRenderBuffer`,
`GetRenderGrowth`.

Применение к слоям: `ApplyTransformToLayer`, `CreatePointLayer`,
`AttacheCustomViewerControlToLayerPicker`, `CreateLayerPicker`, `GetAssetInfo`,
`GetAssetTexture`, `Get3DModelBatchesInstances`.

## Интерактивный разбор (GhidraMCP, восстановленный канал)

После перезапуска Ghidra канал MCP снова активен и теперь обслуживает `Tannen.dll`
(проверено: `OFXPlugin2DEffectWrapper`, `GetOFXHost` найдены; адреса `Project.dll`
отсутствуют). Разобрано:

### PluginManager: фабрика и синглтон

- `PluginManager::Create` — две перегрузки:
  - `@0x1803aea60` (внутренняя): возвращает `shared_ptr<PluginManager>`, принимает
    `vector<FXID>`; **хранит глобальный синглтон** в `DAT_1807c8658` (ptr)/
    `DAT_1807c8660` (refcount ctrl), потокобезопасно (LOCK/UNLOCK) заменяя старый.
  - `@0x1803aec60` (публичная, экспорт): возвращает `shared_ptr<AbstractPluginManager>`,
    принимает `shared_ptr<AbstractLicenseManager>`; **запрашивает у Marvin.dll**
    capability-флаги и OR`ит биты 4/8 в `LogMode` (edition). Mangled-имя:
    `?Create@PluginManager@tannen@biff@@SA?AV?$shared_ptr@VAbstractPluginManager@project@biff@@...`.
- Приватный ctor `@0x1803b0750` (`??0PluginManager@tannen@biff@@AEAA@...`): ставит
  vtable, создаёт суперобъект `AbstractPluginManager` (this+8) через `FUN_1803aefb0`,
  копирует license `shared_ptr` в слоты 0xd7/0xd8 суперобъекта, флаг `flags&0x40 ->
  byte+0x6a1`.
- `FUN_1803aefb0` (внутренняя инициализация, объект 0x6c8 байт): захват OpenGL
  (`wglGetCurrentDC/Context` → слоты 0x22/0x23), строки хоста через `QString`,
  `basic_iostream<wchar_t>` (лог, 0x59), `_Mtx_init_in_situ` (mutex 0xd,0x17,0x35,
  рекурсивный 0x45, 0x96), `std::_Random_device` → instance-ID (0xab),
  `EffectProfiler` (0xd5), default `1.0f` (0x4f).
- Деструктор `@0x1803b0d70`: флаг `0x6b0=1`, мьютекс 0x1a8, создаёт
  `_Ref_count_obj2<OFX::OpenGLContext>` из текущего WGL DC/ctx, чистку `glFlush/
  glFinish`, и **`QDir::removeRecursively()`** временного каталога (путь от
  `DAT_1807692c8`) при выключении.
- Vtable PluginManager = `0x1806bb6e0`; слот0 = scalar deleting destructor
  `@0x1803acc30`. Общий thunk `PluginManager()` (`@0x18025f8c0`) — аксессор
  возврата `AbstractPluginManager*` из `shared_ptr` (this+8 → ctrl+8) для всех
  плагинов (`Plugin2DEffect`, `PluginAE2DEffect`, `PluginAudioEffect`,
  `PluginVideoTransition`, `PluginGeometryEffect`, `PluginBehaviorEffect`, `...`).

### OFX-подсистема: песочница и регистрация

`PluginManager::LoadOFXPlugins @0x1803b6790` (`?LoadOFXPlugins@PluginManager@tannen@biff@@`):
- **Песочница (out-of-process)**: если не режим `0x6a0` и нет флага `0x40`, сначала
  **запускает внешний процесс-песочницу** для перечисления OFX-плагинов:
  - имя exe = `<dir текущего exe> + ".exe"`, имя сборки = `"HitFilmOFXSandbox"` или
    `"VegasEffectsOFXSandbox"` (выбор по host-edition `subobj+0x198 == 0x9c4`).
  - запуск через `QProcess::start` с аргументом `-HostEdition <edition>`, ожидание
    `waitForFinished(2000)`; при выключении пишет `"quit\n"` в stdin,
    эскалация `terminate` → `kill`.
- **Регистрация in-process**: по каждому OFX-плагину (векторы на `subobj+0x430..0x438`
  контейнеров, у каждого вектор плагинов на `+0xe0..+0xe8`):
  - де-дупликация по ID (wstring) и MajorVersion; спец-кейс
    `"com.FXHOME.HitFilm.MuzzleFlash"` (len 0x1e).
  - `set-host`: `FUN_180325520(pluginHost, 0, this)`; ошибка
    `"Unable to set host: Failed to load ofx plugin"`.
  - инстанциация обёрток через `OFX::OFXPlugin2DEffectWrapper::...` (эффекты) и
    `OFX::OFXPluginTransitionWrapper::...` (переходы), регистрация `FUN_1803af620`.
  - финализация/валидация по флагам `0x608`, `0x628/0x629/0x62a`, `0x5c0/0x5d0/0x5b0`.
  - проверка флага выключения `0x6b0` в циклах (отмена загрузки).

Вывод: OFX-плагины перечисляются **в песочнице-процессе**, а сами обёртки
`OFXPlugin2DEffectWrapper`/`OFXPluginTransitionWrapper` собираются в
PluginManager in-process. Пространство `biff::tannen::OFX`.

### Иерархия OFX-хоста (подтверждена в live-БД)

- `PluginManager::GetOFXHost @0x1803b22d0` — просто возвращает
  `OFXGlobalHost&`, **встроенный** объект по смещению `subobj+0x418`
  (в состоянии `AbstractPluginManager`, 0x6c8 байт).
- `FUN_180325520` — set-host на `OFXGlobalHost`: перестраивает вектор
  `biff::tannen::OFX::Plugin`-обёрток из action-списка OFX-библиотеки
  (вектор пар `[shared_ptr<OFX::Plugin>]` на `+0xe0..+0xf0`).
- `OFX::Plugin` (конструктор `FUN_1803028a0`): `OFX::Plugin::vftable` +
  mutex (`+5`) + **внутренний объект 0x6b0 байт** (`FUN_180301010`),
  поля `param[2]` = внутреннее состояние.
- Внутреннее состояние OFX-плагина (`FUN_180301010`, 0x6b0):
  - `[0..2]` = (wrapper-backref, host-интерфейс, ofx-plugin/action данные);
  - копия строк хоста (wstring из host `+0x90..+0xa8`);
  - пара версий **major/minor** в `vector<int>` (`+0x55/0x56`) из `data+0x18/0x1c`;
  - **флаг «загружен/активен» на `+0x189`** (тот же `lVar13+0x189`, что
    проверяется в `LoadOFXPlugins`/`ApplyTransformToLayer`).

Сводно: `PluginManager ⊃ AbstractPluginManager(0x6c8) ⊃ OFXGlobalHost(+0x418)
⊃ vector<shared_ptr<OFX::Plugin>>`, у каждого `OFX::Plugin` своё состояние 0x6b0
с флагом загрузки `+0x189`. Это и есть внутренний OpenFX-хост VEGAS Effects.

### МОСТ OFX-плагин → модель: `OFXPlugin2DEffectWrapper`

`OFXPlugin2DEffectWrapper : biff::project::AbstractPlugin` (наследует от
`project::AbstractPlugin::vftable`), держит `shared_ptr<OFX::Plugin>` на `this+8`
(конструктор `@0x18031bb70`, деструктор `@0x18031bc90`).

**Vtable = `0x1806b24b0`** (маппинг слотов — это поверхность `AbstractPlugin`,
реализуемая OFX-обёрткой чтением из состояния OFX::Plugin):
- слот0: scalar deleting destructor (`FUN_18031b8f0`)
- слот1 `ID()` → `fxtl::FXID const&` (рамка; из OFX::Plugin +0x38)
- слот2 `ReverseDomainName` (обратный домен плагина, напр. com.FXHOME.X)
- слот4 `Path`, слот5 `Name`, слот6 `Keywords`
- слот7 `Author`, слот8 `Copyright`, слот9 `CategoryPath`
- слотA `Version`, слотB `RequiredFeatures`, слотC `Properties`,
  слотD `GlobalProperties`, слотE `UI`
- слотF `SetupInstance`, слот10 `ShutdownInstance`, слот11 `SyncData`
- слот13 `NotifyPropertyChanged`, слот16 `NotifySetParent`
- слот18 `InitializePlugin`, слот1C `RescalePositionControls`
- далее UI-хелперы `AbstractPlugin`: About/OptionsButtonName/
  GetControlLayerAttachment/ViewerOverlayMode/CustomUISetup/...

**Поток создания OFX-instance (чистый OpenFX-мост):**
- `SetupInstance(AbstractPluginInstance*) @0x18031ee10` → делегирует
  `OFX::Plugin::SetupInstance` `FUN_180302c20`.
- `FUN_180302c20`:
  1. создаёт `std::_Ref_count_obj2<OFX::PluginContextInstance>` (0x80),
     payload `FUN_180305410(plugin_ctx, project_instance)` — **привязка
     project `AbstractPluginInstance` к контексту OFX**;
  2. регистрирует его в per-plugin `vector<PluginContextInstance>`
     на `host+0x5e0..0x5f0`;
  3. вызывает виртуал project-инстанса по `*instance_vtbl + 0x68`
     (дофейн вайринга в модель).
- `PluginContextInstance` (конструктор `FUN_180305410`): **двойная vtable**
  (`PluginContextInstance::vftable` на base и `param[2]` — вторичный OFX-
  host-интерфейс, множественное наследование), mutex `+4`, внутреннее
  состояние **0x2f0** (`FUN_180304ab0` связывает project-инстанс).

Полная цепочка адаптера (clean-room-цель):
```
AbstractPluginInstance (project)  <─привязка─  PluginContextInstance (dual-vtbl, 0x2f0)
      ▲                                        (vector на host+0x5e0)
      │ vtable+0x68
OFXPlugin2DEffectWrapper (vtbl 0x1806b24b0) : AbstractPlugin
      └ shared_ptr<OFX::Plugin> (state 0x6b0, флаг +0x189)
```
Т.е. каждый загруженный OpenFX-плагин представлен тройкой «обёртка-плагин →
OFX::Plugin → использование», а per-instance рендеринг живёт в
`PluginContextInstance`.

## VegasEffectsOFXSandbox.exe (side-analysis, live-БД)

Программа загружена в тот же Ghidra-проект (база `0x140000000`), статика-линкует
Tannen/Project и **экспортирует** их классы по ordinal'ам (`AbstractPlugin@project@biff`,
`EffectProfiler@tannen`, `BiffColor`, `CustomProperty`). Это и есть out-of-process
OFX-песочница, которую `LoadOFXPlugins` запускает `QProcess::start -HostEdition`.

**`main` = `FUN_140001470(argc, argv)`** (CRT-startup `entry @0x140003428`):
1. **Парсинг `-HostEdition <int>`** → edition; при `== 0x9c4` окно называется
   «Vegas Effects OFX Sandbox», иначе «HitFilm OFX Sandbox» — тот же edition-флаг,
   что в `LoadOFXPlugins`.
2. Qt-bootstrap: org=«FXhome», QLocale(en_US), `QApplication`, все effect'ы Qt
   отключены.
3. **GL-инициализация**: скрытый `QOpenGLWidget`, `makeCurrent`, **`glewInit()`**,
   проверка `QOpenGLContext::isValid()`; при неудаче — `cout << "Failed to initialize gl context"`.
   (OFX-рендер требует GL-контекст.)
4. При успехе: `QTimer(500ms)` + **`QThread` + `QWinEventNotifier` на stdin
   (`GetStdHandle(-10)`)** — канал команд от родителя; включён в отдельном потоке;
   `aboutToQuit`-лямбда `FUN_140002d80` освобождает shared-holder плагина.
5. `QApplication::exec()` → корректное завершение потока читателя (quit→wait→terminate).

### Полный headless-анализ (повторный запуск без анализа? — см. ниже) и лямбды

Полный анализ (без `-noanalysis`) восстановил имена (`entry`, `main`,
`FUN_140002d80`/`aboutToQuit`, `paintGL`, `initializeGL`, vtable-слоты
`QOpenGLWidget` и др.). **Анонимные лямбды `0x140002e10`/`0x140002e40` остались
label'ами** (байты не дизассемблированы в code-units, `createFunction` падает
`OverlappingFunctionException` «overlap with another namespace»). Они прочитаны
принудительным `DisassembleCommand` + headless-декомпиляцией таргетов.

**Дизассемблер lямбда-обёрток** (это НЕ тела обработчиков, а тонкие wrapper'ы):

`0x140002e10`:
```
MOV R8,RDX ; TEST ECX,ECX ; JZ .L25
CMP ECX,1  ; JNZ .L37
LEA RCX,[RDX+0x10] ; JMP 0x140002100   ; <-- реальный stdin-обработчик
.L25: TEST R8,R8 ; JZ .L37
      MOV EDX,0x30 ; MOV RCX,R8 ; JMP 0x140003194 (free/delete)
.L37: RET
```
`0x140002e40` — идентичная обёртка, но `JMP 0x140001e10` (реальный таймер-обработчик).

### stdin-обработчик `FUN_140002100` (цель `0x140002e10`)
`QWinEventNotifier::activated`-лямбда, тело = читает ОДНУ строку из `cin`
(`std::getline`, разделитель `'\n'`), конвертирует в `QString`:
- **если строка == `"quit"`** → строит массив 9 пустых `local_*`-аргументов,
  `QMetaObject::invokeMethod(capturedQObject, "quit", Qt::QueuedConnection, args)`,
  освобождает shared-holder (`param_1[1]`, refcount-блок), затем
  `QCoreApplication::quit()` + `QThread::quit(param_1[3])`.
- иначе — ничего (протокол поддерживает только `quit`).

**stdin-протокол хост-канала**: родитель пишет `"quit\n"` в stdin
песочницы, песочница завершает GL-хост и поток-читатель. Рядом с данными песочницы
в `.rdata` лежат narrow-литералы `"quit"` **и `"stop"`** — вероятно, второй канал
завершения (впрочем, в `FUN_140002100` разбирается только `"quit"`; прочие
плагин-команды идут через PluginManager/Qt-слоты).

### таймер-обработчик `FUN_140001e10` (цель `0x140002e40`) — отложенное создание PluginManager
`QTimer`-лямбда, вызывается по таймеру (500 мс):
1. Если в capture уже есть PluginManager (`*param_1[0] != 0`) и его vtable-слот
   `+0x88` вернул «идёт загрузка» ≠ 0, и
   `biff::tannen::PluginManager::CancelPluginLoading(param_1[0])` вернул false →
   **`QCoreApplication::quit()`** (загрузка не отменяется / уже не нужна).
2. Иначе, если флаг «started» (`param_1[4]`) == 0:
   - ставит флаг = 1;
   - заполняет 2 строки через `FUN_1400024a0(char*, &DAT_140005834)` — это **пустые**
     wstring (байты `00 00…` в `&DAT_140005834` декодируются как пустая UTF-16);
   - вызывает **`biff::tannen::PluginManager::Create(
       vector<fxtl::FXID>&, shared_ptr<marvin::Marvin>&, wstring&, wstring&, wstring&,
       BiffHostEdition( = param_1[2] = edition из main, см. ниже ),
       Callbacks*( = param_1[3] ), 0, 0x4e, true, true, ...)`**;
   - пишет полученный `shared_ptr<PluginManager>` в `*param_1[0]` (capture).

То есть Sandbox **отложенно** создаёт PluginManager первым же тиком таймера после
старта event-loop: GL готов → тесты/плагин-загрузка в фоне → окно не блокируется.
Совпадает с мотивом «показать окно быстро, грузить плагины асинхронно».

**Происхождение edition и callbacks (полный анализ `FUN_140001470`=main)**:
- edition: argv разбирается в цикле (`local_198=argv`, `local_b8[0]=argc`); если аргумент
  равен `"-HostEdition"`, следующее значение конвертируется `strtol(...,10)`
  и сохраняется в `local_f0[0]` (по умолчанию `0x898`=2200). Это значение напрямую
  попадает в capture таймер-лямбды (`pQStack_170=(QThread*)local_f0`) и затем в
  `FUN_140001e10` как `param_1[2]` → **аргумент `BiffHostEdition` фабрики Create**.
  → **`BiffHostEdition`: 0x898 (2200) = HitFilm, 0x9c4 (2500) = Vegas Effects** (по
  `local_f0[0]==0x9c4` выбирается заголовок `L"Vegas Effects OFX Sandbox"`, иначе
  `L"HitFilm OFX Sandbox"`).
- callbacks: `param_1[3]` capture — `local_88`/`local_78` (builder-логика песочницы);
  собственно `Callbacks*` заполняется/передаётся самой фабрикой на стороне Tannen.
- capture песочницы (только показано): до основного вызова main создаёт
  `QOpenGLWidget` (скрытый, `QWidget::show`→`makeCurrent`→`hide`), `glewInit()`,
  `QTimer(500 мс)`, `QThread`-читатель и `QWinEventNotifier`(GetStdHandle(-10)=STDIN)
  в отдельном потоке — т.е. **это out-of-process GL-хост** для `LoadOFXPlugins`.

### `FUN_140002d80` (aboutToQuit-лямбда)
`switch(param_1)`: при `0` — `free(param_2)`; при `1` — освобождает `shared_ptr`,
хранящийся в `*(param_2+0x10)` (refcount-блок, LOCK/UNLOCK, кооперативный dtor).
Освобождает shared-holder плагина на выходе.

**Вывод**: Sandbox — это **HL GL-хост с минимальным stdin-протоколом**: родитель
(PluginManager) запускает его с edition-флагом, песочница инициализирует GL,
первым тиком таймера создаёт собственный PluginManager (async), а по `"quit\n"`
из stdin корректно завершает GL-хост и поток-читатель. Лямбды `0x140002e10`/`0x140002e40`
— тонкие wrapper'ы на реальные `FUN_140002100`/`FUN_140001e10`.

### Перекрёстная ссылка: sandbox `PluginManager::Create(...)` ↔ Tannen `Create`

Вызов в `FUN_140001e10` (таймер-лямбда песочницы) **позиционно совпадает 1:1**
с восстановленной статикой `biff::tannen::PluginManager::Create` (12 входных аргументов):

| # | sandbox (значение) | Tannen `Create` параметр |
|---|---|---|
| 1 | `local_40` — пустой `vector<>` (дикомпилятор sandbox ошибочно типизировал как `vector<fxtl::FXID>`) | `vector<basic_string<char>> const&` — пути поиска плагинов |
| 2 | `&local_88` — `shared_ptr<Marvin>` | `shared_ptr<biff::marvin::Marvin> const&` |
| 3 | `&local_70` — пустой `wstring` | `std::wstring const&` |
| 4 | `local_60` — пустой `wstring` (из `DAT_140005834`) | `std::wstring const&` |
| 5 | `local_30` — пустой `wstring` (из `DAT_140005834`) | `std::wstring const&` |
| 6 | `uVar5 = param_1[2]` — edition из main | `BiffHostEdition` |
| 7 | `param_1[3]` — callbacks | `Callbacks*` (`AbstractPluginManager::Callbacks`) |
| 8 | `0` | `int` |
| 9 | `0x4e` (=78) | `PluginManager::LogMode` (78) |
| 10 | `true` | `bool` |
| 11 | `true` | `bool` |
| 12 | `1` | `void*` |

- Обе `wstring` (arg4/arg5) построены `FUN_1400024a0`(build-wstring) из
  `&DAT_140005834`, чьи байты `00 00 00 00 …` = **пустая UTF-16 строка** → в песочнице
  все три `wstring` **пустые** (не нужны лицензия/пути).
- Рядом с `DAT_140005834` в `.rdata` лежат **narrow-литералы** `"quit"`, `"stop"`,
  `"-HostEdition"`, `"Invalid argument"`, `"Vegas Effects OFX Sandbox"`,
  `"HitFilm OFX Sandbox"` — т.е. stdin-протокол песочницы, помимо `"quit"`,
  распознаёт и `"stop"`.
- `Create` возвращает `shared_ptr<AbstractPluginManager>`; итерация результата в
  `FUN_140001e10` (`*param_1[0]`) — таймер-лямбда сохраняет его в capture для
  последующих вызовов (`CancelPluginLoading` на vtable+0x88).

Итог: sandbox вызывает **ту самую** фабрику `PluginManager::Create`, что и
продуктовый хост, просто с пустыми строками и edition/callbacks из main — это
подтверждает единый API-контракт для out-of-process и in-process хостов.

### Виртуальные контракты (перечислены по vftable-слотам, полный анализ)

Полный анализ восстановил RTTI-сигнатуры и vftable. Ключевые чистые контракты
(`biff::project` из `Project.dll`), на которые опирается sandbox:

**`AbstractPluginManager`** (vftable `0x1400059a0`; dtor-слот 0 → `FUN_140001380`):
слот `0` = скорее всего `??1AbstractPluginManager`/deleting-dtor, **слоты 1..29
(~29 шт.) — `_purecall`** (чисто виртуальные методы). Конструкторы (default/copy)
по мангла `??0AbstractPluginManager@project@biff@@QEAA@XZ` / `…@AEBV012@@Z` при `0x1370`.
→ чистая абстрактная база с dtor + ~29 чисто виртуальными методами (остальные
методы-`mocked` в песочнице отсутствуют — их реализует Tannen).

**`AbstractPlugin`** (vftable `0x140005ac0`; dtor-слот 0 → `FUN_1400012b0`):
слоты `1..30` — `_purecall`; **конкретные виртуалы** (реализованы, дефолтные для
базы): `31=About`, `32=OptionsButtonName`, `33=Options`, `34=GetControlLayerAttachement`
(мангла `?GetControlLayerAttachement@AbstractPlugin@project@biff@@UEBA?BV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@XZ` → возвращает `string const`),
`35=Icon`, `45=Icon`. → база с dtor + ~30 чисто виртуальными + ~6 конкретными
метаданными-аксессорами (About/Options/Icon/…) для UI.

Это напрямую определяет минимальную поверхность для clean-room
`AbstractPluginManager::Callbacks` и интерфейса `AbstractPlugin` в
`Project.dll`, которые Tannen.dll наследует и реализует.

## Связь с Project.dll

- `biff::project::AbstractPluginManager` (чисто виртуальный, `Project.dll`) ←
  конкретный `biff::tannen::PluginManager` (Tannen.dll).
- Оба подключены к **Marvin.dll** (`biff::marvin::Marvin`) — общий хост-сервис.
- Классы модели (`biff::project`), а конкретика/реализация эффектов — `biff::tannen`.
  Для clean-room это подтверждает: модель в `Project.dll`, а **реализации и
  менеджер плагинов — в Tannen.dll**; держать их следует отдельными модулями.

## Замечания

- Первый проход был с `-noanalysis` → символы таблицы экспорта не именовались, часть
  функций — `FUN_*`. **Повторный полный проход (без `-noanalysis`) восстановил имена
  и мангл-сигнатуры** → `decompile-src\tannen_decomp_full\` (25116 функций, записано
  24765 после санитизации/коллизий). Для `Tannen.dll` теперь есть и точные сигнатуры
  (`Create`, приватный ctor), которых не было в sandbox-проходе.
- **Sandbox тоже полный-анализирован** → `opencode\sandbox_decomp_full\` (210 функций):
  имена функций не изменились (`FUN_*` остались, т.к. у exe нет PDB/таблицы экспорта
  для статического кода), но восстановлены **Qt-типы** (`QOpenGLWidget`, `QTimer`,
  `QThread`, `QWinEventNotifier`) и **RTTI-манглы + vftable** чистых контрактов
  `AbstractPlugin`/`AbstractPluginManager` (см. «Виртуальные контракты»).
- Дубли имён в `decompile-src\tannen_decomp\` (и `_X`-префиксы) — из-за коллизий
  после санитизации имён в файлах; сами функции различны.
- Headless-Ghidra на этом проекте должен запускаться **из CWD без `+`** и с учётом
  блокировки: перед запуском закрывать GUI/сервер Ghidra и снимать `VegasEffects.lock`.
  Надёжный запуск с PyGhidra — напрямую модулем (см. «Как получен результат»), т.к.
  `pyghidra_launcher.py` при повторных запусках падает, интерпретируя venv
  `python.exe` как скрипт.

---

## VegasEffects.exe: QT5-интерфейс и функции приложения

Цель: понять бутстрап главного приложения `VegasEffects.exe` (Qt5, 23 МБ, **41249
функций**) — как строится интерфейс и где реально вызывается `PluginManager::Create`.
Точечная декомпиляция по адресам на уже проанализированном образе (без
`-noanalysis`; санитизированные логи в `opencode\ve_entry.log`, `ve_startup.log`,
`ve_main.log`, `ve_bootstrap.log`).

### Цепочка entry → main
- `entry` `0x140a9d15c` → CRT `FUN_140a9cfe8` (`__scrt_common_main_seh`) →
  `FUN_140a9d5ec` (проверка PE) + `FUN_140aa5900` = `wmainCRTStartup`-оболочка
  (`GetCommandLineW` → `CommandLineToArgvW` → conversion → вызов **`FUN_1401c8fa0` = `main`**).
- `main` `0x1401c8fa0`: `FUN_14024a4a0()` (init) → **`FUN_1401c2ce0(param_1,param_2)`**
  (реальное Qt5-тело) → `fxh::media::FXMediaShutdown()`.

### `FUN_14024a4a0` — init / BugSplat crash-handler
Инициализация **BugSplat** (crash-reporting, `MiniDmpSender`):
- `biff::flux::FluxVideoMemory::AvailableGPUNames()`.
- Строки продукта: `fxhome_playground` (app-id BugSplat),
  `s_fxhome_vegaseffects_win` (имя БД), `FUN_140266620(local_f0, 6000)` → версия **6000**.
- `set_terminate` / `_set_purecall_handler` / `_set_invalid_parameter_handler` /
  `_set_new_handler` / `_set_new_mode(1)` / `signal(0x16)` / `_set_abort_behavior(0,3)`
  → все на `FUN_140249d00`.
- `MiniDmpSender::MiniDmpSender(..., 0x2014)`; `setGuardByteBufferSize(0x1400000)`;
  **UserID** из `QSettings["BugSplat"]` (регистр `UserID`); `setDefaultUserName/UserEmail/
  UserDescription`; `setFlags(flags|1)`; `sendAdditionalFile(...)`.

### `FUN_1401c2ce0` — полный Qt5-бутстрап (интерфейс и функции)

Однэкземплярность и окружение:
- **QSharedMemory** `"VegasEffects-7EEB73DC-5E20-4D5B-A1C8-57ED133E8940"` — single-instance;
  при конфликте: если `DAT_1414e3178 != 0` — skip, иначе диалог
  «Another instance of %1 is already running… Only one instance is allowed at a time.»
- **`console`** CLI-аргумент → `AttachConsole(ATTACH_PARENT_PROCESS)` / `AllocConsole`,
  буфер `dwSize.Y = 500`, `freopen_s` на `CONIN$`/`CONOUT$`/`CONOUT$`; флаг `local_fa4[0]`
  = «консоль подключена» (позже = 11-й bool-аргумент `Create`).
- `CoInitializeEx(NULL, COINIT_MULTITHREADED)` (throw «COM not initialized»).
- `QCoreApplication::setLibraryPaths` (абс. путь exe + `./`),
  `setOrganizationName`/`setApplicationName`, `QLocale(0x1f,0xe1)` (RU), `QPixmapCache`.
- **QTWEBENGINE_REMOTE_DEBUGGING**: `QTcpSocket::bind(0,2)` → `localPort()` →
  `qputenv("QTWEBENGINE_REMOTE_DEBUGGING", port)` (когда `FUN_1402670b0()`).
- `QSurfaceFormat` (OpenGL) + `FXMediaStartup`.

Класс приложения:
- **`BiffUserPref`** (`biff::ui::common`, vftable) — пользовательские предпочтения.
- **`BiffApplication`** (`biff::ui::common::BiffApplication::vftable`) — подкласс
  `QApplication` (`QApplication::QApplication(..., 0x50f02)`), org/app уже заданы.
- `QTranslator` × 2: файл `<base>_<locale>.qm` и **`VegasEffects_`**-префиксный
  (перевода имени продукта); `installTranslator`.
- `BiffStyle` (`biff::ui::common::BiffStyle::vftable`, QProxyStyle) + `setStyleSheet`.
- `BiffEventFilter` (`biff::ui::common::BiffEventFilter::vftable`, installEventFilter).
- **`SplashScreen`** (`biff::ui::widgets::SplashScreen`, `:/images/images/splash.png`,
  центрирование по `QCursor::pos()`/`primaryScreen`, devicePixelRatio).
  Проверка импорта и вызова в Ghidra уточнила контракт конструктора: он получает
  только `QScreen*`, `QPixmap const&` и `Qt::WindowFlags`; вызова
  `QSplashScreen::showMessage` в `VegasEffects.exe` нет. Надпись
  **«VEGAS Effects»** уже нарисована в PNG-ресурсе, а завершение выполняется
  отдельным `SplashScreen::Complete()`. В OpenVegas название так же находится
  в `splash.svg`, дополнительно поверх него показывается текущий реальный этап
  запуска.

Менеджеры (через shared_ptr `_Ref_count`/`_Ref_count_obj2`), все `biff::ui`:
`BiffDocument`, `SignalSlotController`, `ShortcutsController`, `ModelManager`,
`TaskbarProgressIndicator`, `PreRenderManager`, `ProxyMediaManager`,
`ColorLabelManager`, `ExportTaskManager`.

Главный поток (после UI-подсистем) — **тот же порядок, что и дизайн API Tannen**:
1. **`biff::marvin::Marvin::Create`** (`local_df8`) — аудио/mix-движок; при провале
   критический диалог «The audio mixing and playback engine could not be initialized».
   Успех → `Marvin::SetDaysToKeepMediaCacheFiles`, `...::SetDaysToKeepCacheFiles`.
   В реализации OpenVegas слышимый preview переведён на Qt Multimedia:
   `QMediaPlayer + QAudioOutput`; наблюдающий `QAudioBufferOutput` формирует
   уровни мастер-индикатора из того же потока и не требует внешнего libVLC.
2. **`biff::project::CacheDBManager::Create`** — кэш таймлайна; провал →
   «The timeline cache could not be initialized.»
3. Проверка `local_d60` (путь плагинов) → пусто ⇒ «No plugins were loaded because
   they were not found at the expected location. Please run Setup…»
4. Сообщение дня / URL (`local_c90`, ключ 4) и плагин-лист (`local_d60`, ключ 5)
   через `FUN_14025b000` (QSettings/Qt-утилита).
5. **Product edition** `local_fa0[1]`: `5000` → `local_fa8 = 0x9c4`,
   `6000` → `local_fa8 = 0x898` (по умолчанию `0x898`).
6. Логика отладки: `(**(code **)(*plVar27+8))(plVar27,&DAT_14153be88)` + `GetKeyState(VK_SHIFT)`
   → `LVar75 = 1` (обычный) или `3` (с Shift) — **это 9-й аргумент (LogMode)**.
7. Строится главное окно: `FUN_1401d84a0` (обычный запуск, ассоциирует `BiffApplication`)
   или `FUN_14021e8d0`; окно сохраняется в `local_f10` (`QWidget`), `winId()`, GL-контекст.
8. **`biff::tannen::PluginManager::Create`** — см. ниже, 12 аргументов.
9. `PluginManager::Log` — если `local_fa4[0]` (консоль): «Plugin log:» → `wcout`.
10. Проверка текстур `local_d68` (ключ 7) → «No textures were loaded…Setup…».
11. **`biff::fusion::Fusion::Create`** + `Fusion::SetLicense(shared_ptr<AbstractLicenseManager>)`.
12. `FUN_1401d3cb0` (event filter), `ColorBlockWidget::SetUseNativeColorPicker`,
    `SplashScreen::Complete`, **`QApplication::exec()`** → цикл событий.

### `biff::tannen::PluginManager::Create` в exe (реальный вызов) — кросс-ссылка с контрактом

Адрес вызова внутри `FUN_1401c2ce0` (лог `ve_bootstrap.log`, ~строки 4815–4823).
12 аргументов — **совпадает с сигнатурой, восстановленной из Tannen.dll**:

```
biff::tannen::PluginManager::Create(
    local_868,                                   // out: shared_ptr<AbstractPluginManager> (регистр rcx/rax)
    local_750,                                   // shared_ptr<Marvin>
    pbVar74,                                     // wstring = (*local_f88 + 0x110)  [член объекта по +0x110]
    pbVar62,                                     // wstring = QString::toStdWString(local_d60)  [plugin-list path]
    pbVar60,                                     // wstring = QString::toStdWString(local_c90)  [msg/url]
    BVar19,                                      // wstring (из QCoreApplication::applicationName)
    (Callbacks*)CONCAT44(in_stack..., local_fa8),// Callbacks* (старшие б) + BiffHostEdition (младшие б)
    (int)local_d8,                               // int (дескриптор/флаг платформы)
    LVar75,                                      // LogMode: 1 (обычный) | 3 (Shift-отладка)
    true,                                        // bool
    local_fa4[0],                                // bool = «консоль подключена» (аргумент -console)
    (void*)(...))                                // void*
```

Это **подтверждает единый контракт `Create`** между:
- декомпиляцией `Tannen.dll` (само определение, 12 параметров),
- песочницей `VegasEffectsOFXSandbox.exe` (позиционно 1:1),
- и реальным вызовом в `VegasEffects.exe`.

### Edition-маппинг (важное уточнение)
- В **sandbox** (`FUN_140001470`): `"-HostEdition <N>"` → `strtol`, default `0x898`
  (2200, HitFilm), `0x9c4` (2500, Vegas) — это **`BiffHostEdition`**.
- В **exe** два уровня:
  - **Product edition** (владелец `local_fa0[1]`, выставляется `FUN_140243fa0(&local_fa0,5000,…)`):
    `5000` или `6000`, влияет на строку версии и меню активации:
    `5000` → `DAT_14153f2b0/…f298`, `6000` → `DAT_14153f2a8/…f290`;
    иначе throw «invalid product edition».
  - Маппинг в **`BiffHostEdition`** для `PluginManager::Create`:
    product `5000` → host `0x9c4` (2500), product `6000` → host `0x898` (2200).
- Это значит: в exe продукт-редакция (5000/6000) транслируется в host-редакцию
  (0x898/0x9c4) перед передачей в `Create`. Sandbox берёт её прямо из argv.

### Вклады в clean-room реимплементацию
- **Интерфейс приложения** — Qt5: собственные `BiffApplication`(QApplication),
  `BiffStyle`(QProxyStyle), `BiffEventFilter`, `BiffUserPref`, `SplashScreen`;
  ресурсы `:/images/images/splash.png|logo.png`; переводы `<base>_<locale>.qm`
  и `VegasEffects_<product>_<locale>.qm`.
- **Подсистемы запуска** (порядок важен): Marvin(аудио) → CacheDBManager →
  PluginManager::Create → Fusion(+SetLicense) → главное окно → `exec()`.
- **`PluginManager::Create` подтверждён как статический фабричный** с сигнатурой из
  Tannen.dll; exe передаёт реальные путь-плагинов, путь-текстур, msg/url, edition,
  LogMode и флаги консоли.
- BugSplat (`MiniDmpSender`) — опциональный external crash-report, для clean-room
  можно исключить (не влияет на интерфейс/функции).

## Главное окно: MainAppWindow и UnlicensedMainWindow

Декомпиляция `VegasEffects.exe` (вызовы из бутстрапа `FUN_1401c2ce0`).

### Два класса главного окна
- `FUN_1401d84a0` — создаёт **`biff::ui::MainAppWindow`** (обычный запуск). Размер
  объекта 0x50:
  - базовая инициализация через `FUN_140288bf0` (общий для обоих классов);
  - `*(param_1+0x38) = biff::ui::common::CommonModel::Observer::vftable`; член
    `param_1+0x40` = объект 0x18 (обнулённый, «weak»-контейнер);
  - три vftable `biff::ui::MainAppWindow::vftable` на `+0/0x10/0x38`
    (мультинаследование: QWidget, API, Observer);
  - `param_1+0x48` = объект 0xf8 через `FUN_1401e55c0` → `FUN_1401d4450(...)`
    (оконная модель/экземпляр `MainAppWindowModel`), с байндом на `(*model+0xe8/0xf0)`
    — QSharedPointer на `QWidget`, созданный как
    `FUN_1402d0450(0x40, "%1 has discovered unsaved projects:", window)`
    (диалог «…обнаружены несохранённые проекты»);
  - в конце: `QWidget::style()->(...+0x68)(style, *(*model+0x60))` — применение
    стиля/ресурсов к модели.
- `FUN_14021e8d0` — создаёт **`biff::ui::UnlicensedMainWindow`** (Shift-режим отладки
  или нелицензированный): тот же каркас, но `UnlicensedMainWindow::vftable`,
  второй контейнер 0x18 на `+0x48` через `FUN_14021e6a0`.

Вывод для clean-room: у «настоящего» VEGAS Effects два варианта главного окна —
лицензированное после активации и урезанное без лицензии.

### biff::ui::common::FullScreenPreviewWidget (`FUN_140286870`)
- Класс на базе `QOpenGLWidget` (`QOpenGLWidget::QOpenGLWidget(0,0)`), размер 0x30+;
- vftable `::common::FullScreenPreviewWidget::vftable` на `+0/0x10`;
- член `+0x30` = объект 200 байт с вложенным **`biff::ui::common::DelayedFunc`**
  (QObject, отложенные вызовы через `QTimer`), контейнером `ExternalRefCountData`
  и bind на `self` (singleton `self_exref`);
- настройка: `setWindowFlags` + `setFocusPolicy(0)` (Qt::NoFocus),
  `installEventFilter` на self, connect `screenAdded`/`screenRemoved`
  (объект `QApplication`), connect на метод модели `(*model+0x168)` (позиция),
  подписка на событие логирования (`LAB_1401f9bf0`);
- `FUN_140250b20` пишет указатель этого окна в `(*obj + 0x218)` — превью-виджет
  на весь экран.

### biff::ui::EventFilter (`FUN_1401d3cb0`)
- `QObject`-подкласс (без QWidget), vftable `biff::ui::EventFilter::vftable`;
- члены `+0x10/+0x18` = `QSharedPointer` (ExternalRefCountData + указатель) —
  хранит модель; используется `QObject::installEventFilter(mainWindow, filter)`
  и также фильтр ставится на self.

### Восстановление геометрии главного окна (`FUN_1402896e0` = MainAppWindow::Show)
Прямой перенос для Qt6-`MainWindow` (ключи `QSettings` подпапки **`MainWindow`**):
- читает `Geometry` (QByteArray, тип 0xc) → `QWidget::restoreGeometry`;
- `NormalGeometry` (QRect, тип 0x13) → сохраняется как «нормальная» геометрия;
- `WindowState` (int, тип 2) — доказывает, что state хранится отдельно от geometry;
- `QSettings::contains("MainWindow/NormalGeometry")` → восстановлен ли layout;
- логика показа:
  - если handled_bVar4 и NormalGeometry есть: если `isMaximized`/`isFullScreen`
    или state==2 (Qt::WindowMaximized) → `showMaximized`, иначе `show`;
  - если NormalGeometry нет: `isMaximized`/`isFullScreen` → `showMaximized`;
  - если restoreGeometry не удался (первый запуск/др. экран): `showMaximized` +
    нормальная геометрия = центрированная 0x400×0x2d8 (**1024×728**) по
    `QScreen::availableGeometry(primaryScreen)`.
- Реализовано в Qt6: `MainWindow::restoreWindowGeometry()` + сохранение в
  `closeEvent` (см. `src/ui/MainWindow.cpp`).

## Перенос подтверждённых контрактов в код

`PluginManager::pluginIdsByKind(BehaviorEffect)` теперь является границей между ветками Effects и
Behaviors. Сканер `.hfpl` использует восстановленные категории Tannen, сохраняет PluginID,
параметры, группы, choices и причину недоступности. Создать экземпляр можно только для plugin spec
с исполняемым backend: неизвестный закрытый ABI не вызывается. Это позволяет безопасно открыть
проект, показать нативный effect/behavior и сохранить его данные без ложного обещания рендера.

Для Behavior дополнительно восстановлены wrappers `TransformationAtTime` (RVA `0x359160`,
`Notify(102)`, capability `6`), `SimulateBehavior` (RVA `0x3594f0`, `Notify(103)`, capability
`9`), `OpacityAtTime` (`Notify(104)`, capability `7`) и `SubObjectTransformationAtTime`
(RVA `0x3593e0`, `Notify(105)`, capability `8`). Код реализует два frame-результата — матрицу и
opacity — вместе с host callbacks `GetPreBehaviorEffectTransformation` и
`GetOriginTransformation/GetLayer*` в диапазоне API `+0x300..+0x330`. Массовый probe подтвердил
одинаковый результат в основном и worker-thread у 19 поставляемых Behavior. Для `Notify(103)`
дополнительно восстановлены `tagBiffBehaviorSC`, callbacks force/drag и интегратор
`velocity/position` из `Project.dll`. `Acceleration`, `Gravity` и `Drag` исполняются в общем
покадровом проходе с одной скоростью; отдельная регрессия сравнивает результат
всего стека в основном и worker-thread. Layer-dependent `AttractTo`, `Follow`
и `RepelFrom` вместе с `Throw` доводят production-набор до 26/43. Восстановленный
посимвольный `Notify(105)` (`PluginBehaviorEffect` VA `0x18034c810`, `PluginFile` RVA
`0x3593e0`) довёл его до 42/43 за счёт `Typewriter`, `DropInByChar`,
`StringFade`, `CentralSpiral`, `CinemaStyle`, `DoomoDesigns`, `Random`, `Random2`,
`RichTick`, `ShuffleIn`, `WavyStyle` и пяти направленных модулей с ClipValue.
Контекст MC размером `0x78` передаёт время в миллисекундах в
`+0x08/+0x0c`, count в `+0x58` и указатель на 0x5c-byte записи в `+0x60`;
opacity float находится в каждой записи по `+0x40`. ClipValue содержит флаг
`+0x44`, четыре float `+0x48..+0x54` и флаг `+0x58`. Fragment shader Flux
сопоставляет четыре float с локальными границами глифа X-left/right и Y-top/bottom;
они применяются к заливке и контуру в `TextRender`.

### Выбор целевого слоя в Behavior (повторная проверка в Ghidra)

`PluginHostAPI::CreateLayerPicker` (`0x18037f960`, слот `+0x98`) регистрирует
параметр слоя. `GetNumberOfLayers` (`0x1803884d0`, `+0x360`) и `GetLayerID`
(`0x180388620`, `+0x368`) перечисляют слои текущей композиции: ID записывается
в 0x28-байтный буфер, а имя служит только подписью. `GetLayerPosition`
(`0x180372630`, `+0x328`) ищет слой по ID и считывает анимированную позицию
на запрошенном кадре; `-4` означает отсутствующий слой, `-5` — отсутствие
проекта. Runtime теперь передаёт композицию и ID исходного слоя в Behavior,
а поле выбора сохраняет ID, так что одинаковые имена и переименование не
меняют ссылку. Изменение формата кэша метаданных заставляет повторно собрать
список параметров `.hfpl` и показывает `Target` у `AttractTo`.

Дизассемблирование `AttractTo.hfpl` (`Notify` RVA `0x16c60`, обработчик
`Notify(103)` RVA `0x15d90`) показало, почему одних host callbacks было
недостаточно: модуль ищет FXID через список записей по `SC+0x30/+0x38`.
Каждая запись имеет шаг `0x80`: указатель на строку ID в `+0x00`, интервал
времени в `+0x18/+0x20` и матрицу 4×4 в `+0x40`. Runtime формирует массив
всех слоёв на каждом шаге, включая источник, цель и их позиции. Зонд
`behavior-graph` проверяет два слоя с одинаковым именем, ненулевое смещение,
равенство отдельного вызова и общего стека, перестановку слоёв и отдельный
worker-thread. При позиции цели `(240, 80)` на 30-м кадре результаты:
`AttractTo` `(19.6868, 6.56226)`, `Follow` `(153.201, 51.0671)`,
`RepelFrom` `(-18.6661, -6.22205)`. Все три включены в production whitelist.
У `Throw` нет layer picker. Его обработчик `Notify(103)` (RVA `0x17180`)
подаёт импульс при нулевом времени; начало симуляции с `1/FPS` пропускало
импульс при исходном `Acceleration Time = 0`. После запуска первого шага с
`t=0` зонд получает `(500, 0)` за 30 кадров с параметрами по умолчанию.
Общий стек, перестановка слоёв и worker-thread дают тот же результат, поэтому
`Throw` также включён. Восстановление полного transform/orientation состояния
слоёв и других модулей остаётся открытым.

Восстановленный порядок запуска и геометрия окна также отражены в `AppMain`/`MainWindow`: сначала
менеджеры медиа и плагинов, затем модель и окно; `MainWindow/Geometry`, `NormalGeometry` и
`WindowState` читаются и записываются раздельно.
