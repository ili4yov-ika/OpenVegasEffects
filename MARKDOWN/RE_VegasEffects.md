# RE / Анализ VegasEffects.exe (VEGAS Effects, ex-HitFilm)

Технический разбор исполняемого файла `VegasEffects.exe` из дистрибутива VEGAS Effects.
Инструменты: Ghidra 12.1.2 (GUI) + GhidraMCP-мост на `http://127.0.0.1:8080`.
Проект Ghidra: `%LOCALAPPDATA%\Temp\opencode\ghidramcp\proj\VegasEffects.gpr`.

Статус: **черновой разбор, анализ продолжается**. Обновлять по мере прогресса.

Состав поставки, версии сторонних библиотек, граф зависимостей модулей и
подсистема плагинов разобраны отдельно:
[`RE_VegasEffects_Distribution.md`](RE_VegasEffects_Distribution.md).

---

## 1. Идентификация

| Параметр | Значение |
|----------|----------|
| Файл | `VegasEffects.exe` (дистрибутив VEGAS Effects 2026) |
| Тип | PE64, база `0x140000000` |
| Продукт | VEGAS Effects (мага-GUI, бывший HitFilm; движок `biff`) |
| Приставка марок | также брендируется «VEGAS POST» (`1412bb388`) |
| Фреймворк | Qt5 (Core/Gui/Widgets/Xml/Network/WebChannel/Multimedia/WebEngineWidgets/WinExtras/Concurrent), OpenGL 3.2 Core (glew) |
| Крэш-репортер | BugSplat `MiniDmpSender`, база `fxhome_playground` |
| Форматы проекта | HitFilm `.hfp`/`.hfcs`; VEGAS Effects `.vegfx`/`.vegfxcs` |
| Символы | PDB не найден — все функции `FUN_*` |

Сегменты: `.text 140001000-140bd4fff`, `.rdata`, `.data`, `.pdata`, `.rsrc`, `.reloc`, `tdb`.

### Ключевые сторонние DLL
`FXMedia.dll`, `Flux.dll` (рендер), `Fusion.dll` (частицы 3D), `Marvin.dll` (аудио), `Project.dll`, `Tannen.dll` (менеджер плагинов, движок `biff`), `Widgets.dll`, `BugSplat64.dll`, `libfbxsdk.dll`, `archive.dll`, `libcrypto-3-x64.dll`, `glew32.dll`, `SETUPAPI.dll`, `POWRPROF.dll`.

### Наблюдаемые подсистемы (строки/импорты)
- OFX-хост: `"OFX Plugins"`, `"Delete OpenFX Plugins Cache"`, `tannen::biff::PluginManager`
- Внешняя интеграция: `"Open in VEGAS Image..."`, `?IsVegasImageFile@ImageAsset@project@biff@@`, реестр `SOFTWARE\MAGIX\VEGAS Pro`
- Рендер: `biff::ui::viewer::RenderManager`, `RenderCache`, `RenderExceptionReporter`, `prerender::ui::biff::PreRenderManager` (Proxy & Pre-Renders)
- Экспорт/сервер: `biff::ui::exporter::Server`, `VegasEffectsServer`, `VegasEffectsRenderClient`, `HitFilmRenderClient`; `QLocalServer`/`QLocalSocket` (named-pipe IPC)
- Лицензия: `"License is empty, no server request is required."`, `"License out of date. Requesting new license..."`
- WebEngine: импорты `Qt5WebEngineWidgets.dll` + `Qt5WebChannel.dll` (JS↔C++ мост), `QTWEBENGINE_REMOTE_DEBUGGING`,
  runtime helper `QtWebEngineProcess.exe` в пакете (HTML-поверхности: Home/learning sidebar и веб-Library).

---

## 2. Цепочка входа (подтверждено декомпиляцией)

```
entry (140a9d15c)
  → FUN_140a9cfe8            CRT startup
    → FUN_140aa5900          WinMain
        GetCommandLineW → CommandLineToArgvW → argv в UTF-8 (WideCharToMultiByte, CP 0)
        → FUN_1401c8fa0      appMain
            1) FUN_14024a4a0            BugSplat / crash-reporter init
            2) FUN_1401c2ce0(argc,argv) true main (см. §3)
            3) fxh::media::FXMediaShutdown()
        → cleanup argv → return code
```

### 2.1 FUN_14024a4a0 — BugSplat init
- `biff::flux::FluxVideoMemory::AvailableGPUNames()` — список видеокарт (используется для «системной инфы»).
- `MiniDmpSender` c прокси **`fxhome_playground`**, app-строка `VegasEffects_` (`1412cdd30`), build `6000` (через `FUN_140266620`), флаги `0x2014`.
- Все ручки-обработчики перенаправлены в общий `FUN_140249d00`:
  `set_terminate`, `_set_purecall_handler`, `_set_invalid_parameter_handler`, `_set_new_handler`, `_set_new_mode(1)`, `set_abort_behavior`.
- `MiniDmpSender::setGuardByteBufferSize(..., 0x1400000)` (20 МБ сторожевого буфера).
- Профиль: `QSettings BugSplat\UserID` → `setDefaultUserName`, email/description — пустые.
- `FUN_14024a230` — собирает `dxdiag.exe` (QProcess, startDetached) для диагностики GPU.

### 2.2 Вспомогательные функции запуска
| Функция | Назначение |
|---------|------------|
| `FUN_1401c1180` | `QSurfaceFormat(3.2, CoreProfile, swapInterval=0)` |
| `FUN_1401c1320` | реестр `SOFTWARE\%org%\%app%\GlobalOptions` → `EnableGLDebug` (bool vs "true") |
| `FUN_140233270` | QSettings `Options\EnableHighDpiScaling` (default true) |
| `FUN_140231010` | buildId==6000 → QSettings `Options\OpenOnboardingProject` |
| `FUN_140231190` | проверка «премиум/эдиции» (0x1388 vs 0x1770) |
| `FUN_140264040` | диспетчер std::function-лямбды (периодическое действие) |
| `FUN_1401c0fa0` | `QString::fromUtf8(len)` += (хелпер путей) |
| `FUN_1401c0f50` | хелпер путей (настройки) |

---

## 3. main() — FUN_1401c2ce0 (тело 1401c2ce0–1401c812a, ~21 КБ)

Разобрана стартовая часть (~150 инструкций); декомпиляция целиком упирается в таймаут моста 5 c.

### 3.1 Режим-диспетчеризация (server/console против GUI)
```
if (argc >= 2 && argv[1][0..8) == строка 0x1412bbb60   // префикс RenderClient/Server-токена ~ "VegasEffects-..."
              && (check1 [0x140bd5ee8](0xffffffff)==1 || check2 [0x140bd5ee0]()!=0))
    → «console/server» ветка (0x1401c2dd3):
        GetStdHandle(STD_OUTPUT_HANDLE); пишет строку статуса (0x1f4=500 / 0xe1=225 параметры)
        настройка путей (строки 1412cdd08/1412cdd10/1412cdd18/1412cdd1c)
        без Qt GUI
else
    → нормальный GUI-путь (0x1401c2ea9…)
```
Примечание: `QLocalServer`/`QLocalSocket` в импортах + строки `VegasEffectsServer`/`VegasEffectsRenderClient`/`HitFilmRenderClient`
→ рендер-подсистема запускает себя как дочерний процесс с аргументом-токеном (named pipe).

### 3.2 GUI-старт (порядок инициализации)
1. Одноэкземплярный ключ из пути exe; флаг HighDPI.
2. Qt-атрибуты через `[0x140bd9558](id, dl)` — восстановлены с точными значениями:
   - до `QApplication`: `0x12` AA_ShareOpenGLContexts = **true** (1401c3014), `0xf` AA_UseDesktopOpenGL = **true** (1401c3021);
   - `FUN_140233270` — это **не** сеттер, а чтение настройки `Options/EnableHighDpiScaling` (bool, дефолт `true`); её результат идёт в `0xd` AA_UseHighDpiPixmaps (1401c31d9) и `0x14` AA_EnableHighDpiScaling (1401c31ec);
   - `0x6` AA_DontUseNativeMenuBar = **true** (1401c31f4);
   - `0x2` AA_DontShowIconsInMenus = **false** (1401c3201: `XOR EDX,EDX` перед вызовом), т.е. иконки в меню включены.
   Флаги конструктора `QApplication` — `0x50f02`, что даёт Qt **5.15.2**.
3. Если `FUN_1402670b0()` true → **QtWebEngine remote debug**. Уточнено декомпиляцией:
   `FUN_1402670b0` = «рядом с exe лежит `config.ini`» (`1412cdd20`), а значение переменной —
   не рандом, а свободный TCP-порт: `QTcpSocket::bind(0, DontShareAddress)` →
   `QAbstractSocket::localPort()` → `QString::number(port,10).toLatin1()` →
   `qputenv("QTWEBENGINE_REMOTE_DEBUGGING", …)` (IAT `140bdc300`/`140bdc310`/`140bdc308`/
   `140bd93e0`/`140bd97d0`/`140bd9560`). Выполняется **до** конструктора `QApplication`.
4. `QApplication` ctor с флагами `0x50f02` (`[0x140bded88]`).
5. **Таймер обслуживания**: `(logicalCPUs % 10 + 5) * 60000` мс (~5–14 мин) → `FUN_140264040`.
6. **Лицензия/подписка**:
   - объект лицензии `FUN_140243fa0(0x1388)`;
   - регистрация 3 пакетов callbacks `[0x140bdb0c0](3, fn…)`;
   - `[0x140bd53d0](&cfg)` → enum статуса `EDI`:
     - `0x2B` → проверка премиума `FUN_140231190` → при ошибке модальный диалог (шаблоны `1412bbc60`/`1412bbcf0`, `%1`=статус);
     - `0x2E` / `0` → ок;
     - иное → диалог «license error, status=…».
   - Эдиция: `0x1388`(5000)/`0x1770`(6000) → строки `14153f290/298`, `14153f2a8/2b0`.
7. Пути настроек (реестр + GlobalOptions), хелперы `FUN_1401c0fa0`/`FUN_1401c0f50`.
8. Центрирование главного окна на primary-экране (QScreen geometry /2 + смещение `0x37`).
9. Регистрация переводов/шрифтов (`[0x140bdfab8](ECX=0..6)`), QSettings-объекты.
 10. Onboarding: `FUN_140231010` → `OpenOnboardingProject`.

### 3.3 Плагины (блок 0x1401c7400–0x1401c7c00, в ветке GUI-старта)

Логика плагинов **инлайн в main()**, сразу после путей настроек. Ключевые элементы:

| Адрес | Что делает |
|-------|-----------|
| `FUN_14025b000(dir, idx, mustExist)` | **резолвер папок пользовательских данных**. `idx`: 0=`Translations`, 1=`Templates\AV`, 2=`EnvironmentMaps`, 3=`ExportPresets`, 4=`PluginPresets`, 5=`Plugins`, 6=`Presets`, 7=`Textures`, 8=`Templates\Workspaces`, 9=`Objects`, 10=`Tutorials`. Индексы 1 и 8 идут через общую базу `Templates` с дописыванием `\AV` / `\Workspaces` — голого `Templates` не отдаёт ни один индекс. Третий аргумент **не** mkdir (прежняя формулировка неверна): при ненулевом значении строится `QDir`, и если каталога нет — возвращается **пустая** строка; каталог не создаётся никогда. Дерево раскладывает инсталлятор — отсюда и сообщение «run Setup to repair the installation» |
| `0x1401c7567` | `FUN_14025b000(..., 5, 1)` → путь `Plugins/` + QDir::exists |
| `0x1401c7585` | если папки `Plugins` нет → сообщение `"No plugins were loaded because they were not found at the expected location..."` (1412bc750) в буфер 0x400 |
| `0x1401c75ee` | `FUN_14025b000(..., 4, 1)` → путь `PluginPresets/` + проверка существует → при отсутствии строки `1412ba230` |
| `0x1401c7aaa` | `"Plugin log:"` (1412ba218) — пишется через глобальный лог-стрим `[0x140bd6048]` (ostream, `FUN_1401c9cc0` = `ostream::operator<<`) |
| `0x1401c7ac7` / `0x1401c7aed` | вызовы через IAT-слоты `0x140bd6180`/`0x140bd5fd0` (ostream-манипуляторы, в т.ч. endl), т.е. **это логгирование, а не вызовы PluginManager** |
| `FUN_1401c9cc0` | на самом деле `std::ostream::operator<<` (лог-файл плагинов), а не менеджер |
| `FUN_1401c84d0` | копирование `shared_ptr` (refcount `_ptr+0xc`) |
| `FUN_1401ca310` | инициализация `std::function` vtable для лямбды |
| `0x1401c78b3` | `[0x140bdfaa8]()` → глобал объекта; перечисление плагинов |
| `0x1401c793f..` | сборка структуры `Callbacks*` (pack `[0x1401c8e50]` из `[0x1401ca340]`) для PluginManager |

Прочие вызовы `FUN_14025b000` в main: idx 1 (`Templates\AV`), 8 (`Templates\Workspaces`), 7 (`Textures`) — пути для ресурсов эпизода.

### 3.4 Tannen.dll — PluginManager (сигнатуры из import-name таблицы exe)

- `?Create@PluginManager@tannen@biff@@SAABV?$shared_ptr@VAbstractPluginManager@project@biff@@@std@@…` (14148b73a):
  **возвращает `shared_ptr<AbstractPluginManager>`**, параметры: `shared_ptr<AbstractLicenseManager>`, `shared_ptr<Marvin>`, три wstring-пути (эдишн/плагины/ресурсы), `BiffHostEdition`, `project::biff::AbstractPluginManager::Callbacks*`, `int`, `LogMode`, `bool`, `int`, `void*`.
- Методы интерфейса: `Log()` (14148b89c), `HostEdition()` (1414d5082), `GetEffectProfiler()` (1414dbea6).
- Типы: `project::biff::AbstractPluginManager::Callbacks` (RTTI 141517da0), `project::biff::PluginID`, `PluginPresets`.
- IAT-слоты `0x140bd6180`/`0x140bd5fd0` и глобалы `0x140bd6040`/`0x140bd6048` относятся к **системе логгирования**, не к Tannen.

---

## 4. Дальнейшие направления анализа

- [ ] **OFX-хост**: IAT-слот и точка вызова `PluginManager::Create` (импорт Tannen.dll; искать конструктор `shared_ptr<AbstractPluginManager>` + передача license-manager/Marvin/путей в main). Анализ кода загрузки `.ofx` продолжить при импорте `Tannen.dll` в проект Ghidra.
- [ ] **OFX-кэш**: `CacheDBManager` (TimelineCacheDB/MediaCacheDB, `cache.db`), `Marvin::Create`, `CacheSettingsWidget` — `"Delete OpenFX Plugins Cache"`, удаление при выходе.
- [ ] **Рендер-пайплайн**: `biff::ui::exporter::Server` (QLocalServer), RenderClient/RenderServer-процессы, `fxh::media::FXMedia`.
- [ ] **Остаток main()** за `0x1401c4083` (~6800 строк дизассемблера, сохранены в tool-output).
- [ ] **Строка-токен** `0x1412bbb60` (8 байт, режим RenderClient/Server).
- [ ] Импорты `libfbxsdk` → модель ввода/экспорта 3D.

---

## 5. Интерфейс (портированная часть)

Данные восстановлены из строк/макетов `VegasEffects.exe` (см. `0x140c3874f` — конфиг горячих клавиш, тема `fxhome-interface-style`).

### Макеты экранов (типы панелей из `ScreenLayout`)
- **Effects screen** (горизонтальный `ScreenLayout`, `Orientation=2`):
  - слева контейнер: **Media** (Type 1) + **Text** (128);
  - центр: **Viewer** (32), **Trimmer** (2050), **Export** (2057);
  - справа: **Effects** (4), **Controls** (2), **Layout** (2055), **Track** (1024);
  - низ: **Start** (2056).
- **Edit screen**: **Editor** (16, таймлайн), Media/Effects/Controls/History/Text/Library, **Meters** (2051).

### Тема (QSS, `fxhome-interface-style` переменные)
- Тёмная: панели `rgb(34,34,34)` / `rgb(17,17,17)`, линии `rgb(27,27,27)`.
- Акцент/фокус `rgb(18,176,255)`, выделение `rgb(86,104,115)`.
- Шрифт `"Segoe UI, Trebuchet MS…"`, иконки 16px.
- Таймлайн: аудио `rgb(0,66,52)`, видео `rgb(43,63,97)`, выделение `rgb(86,104,115)`.
- Кнопки: default `rgb(44,44,44)`, hover `rgb(73,73,73)`, addon `rgb(48,140,48)`.

### Горячие клавиши
`Ctrl+Z/Y` (undo/redo), `Ctrl+N/O/S`, `Ctrl+Alt+S`, `Ctrl+D`, `Ctrl+A`, `Ctrl+R`, `Space` (play), `Home` (стоп), `End`, `I`/`O` (in/out), `Ctrl+Shift+O` (Import Media), `F2`, `Del`, стрелки ←/→ (покадрово), `J/K/L` (shuttle). Источник точных значений — восстановленный `/DefaultShortcutsWin.xml`; редактор назначений отклоняет дубликаты штатным сообщением о конфликте.

### Портировано в `src/ui`
- `Theme.{h,cpp}` — QSS тёмной темы из переменных выше; применяется в `AppMain::run`.
- `MediaPanel.{h,cpp}` — панель «Media» (список импортированных ассетов).
- `EffectInspector` — панель «Controls» (справа), связана с выбором клипа на таймлайне.
- `MainWindow` — доки лево=Media+Effects (tabified), право=Controls, низ=Editor(таймлайн), центр=Viewer; тулбар транспорта + горячие клавиши (Space/Home/End/I/O/стрелки/JKL).
- `Composition::clipAt(layer, clip)` — неконстантный доступ для редактирования параметров эффекта.
- `LayerPanel.{h,cpp}` — панель «Track/Layer» (Type 1024): дерево слоёв (Name, Vis, Mute, Blend combo, Opacity), тулбар +/↑/↓/Del; `bindModel(Composition)`, сигнал `layersModified`; закреплена справа tabified с Effect/Controls/History/Library. `Composition::{swapLayers, layerRef}` для переупорядочивания и редактуры полей (`Layer::visible/muted`).

### Learn Sidebar — портирован без Qt WebEngine

Референс держит Home/Learn как HTML-поверхности: в пакете лежит `QtWebEngineProcess.exe`,
в импортах — `Qt5WebEngineWidgets.dll` + `Qt5WebChannel.dll` (мост JS↔C++). Строки поверхности:
`learnWebViewSpace` (`1412c0e68`), `learnSidebarIsOpen` (`1412c10d0`), «Learn Sidebar» (`1412cb418`),
«Toggle Learn Sidebar» (`1412d0908`), `learn-onboarding` (`1412cdce0`), `learnSidebarAnimationWidth`
(`1414dde8a`), `learnCheckBoxWidget`, `learnSeparatorBar`, событие `LearnCheckBoxToggled` (`1414e627e`).
Содержимое — онлайн-уроки VEGAS (обучающие страницы с их сайта); без сети показывается
`OfflineWidget` с текстом «Go online to explore amazing tutorials…».

Кроме Learn, WebEngine в референсе обслуживает веб-версию библиотеки ассетов:
`biff::ui::common::LibraryWebView` (`FUN_14022ac90`/`FUN_140211790`: «Loading Library...»,
объект веб-канала `library`) поверх общего `biff::ui::common::BiffWebView`/`B::FXWebPage`
(`FUN_1402dbe50`; `FUN_1402db1e0` — контекстное меню «Show inspector»/«Reload» при
`QTWEBENGINE_REMOTE_DEBUGGING`; `FUN_1402dc290` грузит `http://localhost:<порт>` встроенного
сервера). Справка открывается ссылкой в системном браузере, следов активации через WebEngine
нет — у лицензии собственный диалог.

В порте WebEngine использовался только Learn-панелью (локальная заглушка с тремя ссылками), а
Library — нативный `LibraryPanel`, поэтому Qt WebEngine (вместе с WebChannel/Network и remote
debugging по `config.ini`) убран в октябре 2026: ~900 МБ Debug-развёртывания (Chromium,
`Qt6WebEngineCore`, Quick/Qml/Positioning), MSVC-only зависимость. Если когда-нибудь понадобится
веб-библиотека референса, её придётся подключать заново.

`LearnSidebar.{h,cpp}` теперь — обычный правый док: тот же objectName `LearnSidebar`, область
`learnWebViewSpace`, текст референса и три действия (уроки в системном браузере через Online
Help, новый композитный кадр, импорт медиа); состояние — `Settings::learnSidebarIsOpen()`,
команда Window › «Toggle Learn Sidebar».

**Инициализация OpenGL.** `AA_ShareOpenGLContexts` и `AA_UseDesktopOpenGL` по-прежнему ставятся в
`main.cpp` до `QApplication`, как в референсе (`1401c3019`/`1401c3026`): общие контексты нужны
самому вьюеру, который переходит между доками (360 Viewer показывает тот же `QOpenGLWidget`).

### Ключевые кадры и свойства (Project.dll, разобрано)

Разбор экспортируемого API `Project.dll` (3573 экспорта; имена MSVC-декорированы,
разворачивались через `undname.exe`). Декомпилятор для этого не нужен —
сигнатуры полностью восстанавливаются из таблицы экспорта.

#### Система типов свойств

Значение ключевого кадра — `boost::variant` ровно из 16 типов. Это система
типов свойств всего приложения:

```
int, unsigned int, float, double, bool,
Scale3D, Orientation3D, BiffColor,
FXPoint<2,float>, FXPoint<3,float>,
FXVector<2,float>, FXVector<3,float>,
FXID, FXMatrix<4,4,float>,
std::wstring, CustomProperty
```

#### `biff::project::KeyFrame`

```cpp
static shared_ptr<KeyFrame> Create(int time, const PropValue&, KeyFrameTemporalType);
static shared_ptr<KeyFrame> CreateFromXml(const XmlNode&);
int              Time() const;          // номер кадра, не секунды
const PropValue& Value() const;
```

У кадра **две независимые системы интерполяции**:

| | Временная (кривая значения) | Пространственная (траектория) |
|---|---|---|
| Тип | `KeyFrameTemporalType` | `KeyFrameSpatialType` |
| Управляющие точки | `FXVector<2,float>` вход/выход | `FXVector<3,float>` вход/выход |
| Влияние | `TemporalIncoming/OutgoingInfluence()` (float) | — |
| Блокировка | `SetTemporalControlPointsLocked(bool)` | `SetSpatialControlPointsLocked(bool)` |

**Конкретные значения обоих перечислений** (восстановлены из `VegasEffects.exe`
по подписям команд и именам иконок, команда «Set Keyframe Temporal Type»):

| Временной тип | Подпись меню | Иконка | Реализовано у нас |
|---|---|---|---|
| Constant | Convert To Constant | `key-frame-hold` | `TemporalType::Hold` |
| Linear | — | — | `TemporalType::Linear` |
| Smooth | Convert selected keyframe(s) to Smooth | `key-frame-easy-ease` | `TemporalType::EasyEase` |
| Smooth In | …to Smooth In | `key-frame-ease-in` | `TemporalType::EaseIn` |
| Smooth Out | …to Smooth Out | `key-frame-ease-out` | `TemporalType::EaseOut` |
| Manual Bezier | …to Manual Bezier | `key-frame-manual-bezier` | `TemporalType::ManualBezier` |

Пространственных типа три, у них своя тройка команд: «…to Linear spatial
interpolation», «…to Auto Bezier spatial interpolation», «…to Manual Bezier
spatial interpolation». Пространственная интерполяция у нас не реализована.

Имена иконок выдают происхождение: `easy-ease`, `ease-in`, `ease-out` — это
терминология After Effects, тогда как в меню те же типы названы Smooth. Ромбы
состояния — `key-frame-full` и `key-frame-void`.

Предикаты: `IsHold()`, `IsStraight()`, `IsSpatialLinear()`. Умолчания ручек —
`KeyFrame::DefaultTemporalIncoming/OutgoingInfluence(type)` и
`KeyFrameHelper::CalculateDefaultTemporalControlPoints(time, type, prev, next)`,
то есть ручки выводятся из соседних кадров.

Сериализация: `Serialize(XmlDocument&, XmlNode&)` / `XmlNode Serialize() const`.
Уведомления об изменениях — через `SetObserver(weak_ptr<ChangeObserver>)`.

#### `biff::project::KeyFrameList`

Контейнер — `std::map<int, shared_ptr<KeyFrame>>` (видно по типам итераторов),
то есть кадры упорядочены по времени. Ключевое:

```cpp
static shared_ptr<KeyFrameList> Create(const PropValue& defaultValue);
void Add(const KeyFramePtr&);
void Set(int time, PropValue);
void AdjacentKeyFrames(int time, KeyFramePtr& prev, KeyFramePtr& next) const;
KeyFramePtr const& NearestToTime(int) const;
KeyFramePtr const& FirstByTime() const;   // LastByTime()
bool CanTemporallyInterpolate() const;    // false для дискретных свойств
bool AreTimesRelative() const;            // времена могут быть относительными
```

Кадры адресуются **стабильным идентификатором**, а не только временем:
`GetByID(FXID)`, `Move(FXID, int newTime)`, `Remove(FXID)`, `Identifiers()`,
`IdentifiersAt(int)`.

**Механика интерполяции** (приватные методы, но сигнатуры экспортированы):

- `ParamTforTemporalCurveTime(prev, next, int time) -> double` — решает параметр
  безье по времени (классический шаг «решить кубический безье относительно x»);
- `ParamTforSpatialCurveLength(prev, next, double) -> double` — параметризация
  по длине дуги, чтобы движение по траектории шло с постоянной скоростью;
- `CreateTemporalCurveCache(prev, next)` / `CreateSpatialCurveCache(prev, next)`
  и `ClearCurveCaches()` — кэш на пару соседних кадров, инвалидируется
  флажками `Is*CurveDirty` у самого кадра;
- `ConfigureAutoBezierSpatialKeyFrame(kf)` — авто-безье для траектории.

**Вычисление значения живёт не в списке**, а у владельца свойства — сквозной
паттерн `...AtTime(int frame, ...)`:
`AbstractLayer::WorldTransformationAtTime`, `AbstractLayer::ParentTransformationAtTime`,
`Model3DTransformNode::RotationAtTime` / `ScaleAtTime`, `Effect3DLayer::ClipRotationAtTime`.

#### Сериализация в `.vegfx`

Проверено на `SAMPLES/vegfx_projects/project_1/Project.vegfx`:

```xml
<PropertyManager Version="7">
  <Prop Type="0" Spatial="1" CanInterpT="1">
    <Name>anchorPoint</Name>
    <Default V="4"><p3 X="0" Y="0" Z="0" /></Default>
```

- `Type` — `KeyFrameList::SpatialDataType`; `Spatial` — признак траектории;
  `CanInterpT` — `CanTemporallyInterpolate`. Встречающиеся сочетания:
  `Type=0/Spatial=0/CanInterpT=1` (69), `Type=1/…/CanInterpT=0` (62, дискретные
  свойства), `Type=1/…/CanInterpT=1` (40), `Type=0/Spatial=1/CanInterpT=1` (15).
- `V="4"` — **версия формата значения, а не тег типа**: она одинакова у всех.
  Тип кодирует **имя дочернего элемента**:

| Элемент | Тип |
|---------|-----|
| `<i>` | int |
| `<fl>` | float |
| `<db>` | double |
| `<b>` | bool |
| `<p3 X Y Z>` | `FXPoint<3,float>` |
| `<sc>` | `Scale3D` |
| `<or>` | `Orientation3D` |
| `<cl>` | `BiffColor` |
| `<id>` | `FXID` |

В образце 52 различных имени свойств: `anchorPoint`, `position`, `scale`,
`rotationX/Y/Z`, `orientation`, `opacity`, `castShadows`, `receiveShadows`,
`shadowColor`, `illuminated`, `scaleLinked`, `aoSampleRadius` и далее.

#### Панель Meters (2051) — где она на самом деле

Из recovered Screen XML в `VegasEffects.exe` видно два экрана:

- **Effects screen** (10 панелей): Media, Text, Viewer, Trimmer, Export,
  Effects, Controls, Layout, Track, Start. **Meters там нет вообще.**
- **Edit screen** (11 панелей): Trimmer, Viewer, Export, Library, Media,
  Effects, Controls, History, Text, Editor и **Meters**.

На Edit screen Meters лежит в собственном контейнере рядом с Editor:

```xml
<Container Stretch="2.10186"><Panel Type="16"   Name="Editor"/></Container>
<!-- Meters container [Meters] -->
<Container Stretch="0.0709111"><Panel Type="2051" Name="Meters" IsCurrentPanel="1"/></Container>
```

То есть это узкая полоса примерно в 3 % ширины строки, и она **видима**
(`IsCurrentPanel="1"`), а `IsActive="0"` означает лишь отсутствие фокуса.

Вывод для порта: наш `MainWindow` повторяет **Effects screen**, где Meters нет,
поэтому панель скрыта по умолчанию и остаётся доступной через меню Window.
Раньше она висела видимой и показывала пустую полоску справа от таймлайна.

#### Что это даёт порту



**Перенесено** в `src/composition/KeyFrame.{h,cpp}`:

- `KeyFrame` — стабильный `id`, время в **номерах кадров**, значение `QVariant`,
  `TemporalType` (Linear / Bezier / Hold), входная и выходная ручки безье с
  отдельными коэффициентами влияния и флагом связывания — как в референсе;
- `KeyFrameList` поверх `QMap<int, KeyFrame>` (тот же порядок по времени, что и
  у `std::map<int, shared_ptr<KeyFrame>>`): `adjacentKeyFrames()`,
  `nearestToTime()`, `firstByTime()`/`lastByTime()`, `locations()`;
- адресация по идентификатору: `byId()`, `moveById(id, newFrame)`,
  `removeById()` — перенос кадра во времени не рвёт ссылки;
- `canInterpolate()` — аналог `CanTemporallyInterpolate` / атрибута `CanInterpT`
  в `.vegfx`: у дискретных свойств значение держится до следующего кадра;
- `valueAt(frame)` — линейная интерполяция и кубический безье, решаемый
  относительно времени бисекцией (у референса это
  `ParamTforTemporalCurveTime` плюс кэш кривых на пару кадров; кэш пока не
  нужен).

`composition::Effect` получил `QMap<int, KeyFrameList> animation` (ключ — индекс
параметра) и `parameterAt(index, frame)`: статическое значение, пока кадров нет,
и анимированное, когда они появляются. Таймлайн рисует ромбы ключей на полосах
параметров — полосы уже были выровнены по строкам дерева.

Проверено отдельной пробой: линейная интерполяция, удержание до и после крайних
кадров, `Hold`, `CanInterpT=0`, симметричный безье через середину и с ранним
замедлением, сохранение идентичности при `moveById` — 11 проверок, все проходят.

**Не переносилось**: вторая, пространственная интерполяция (траектория в 3D с
параметризацией по длине дуги, `ParamTforSpatialCurveLength`) — она нужна вместе
с 3D-слоями, которых в порту нет.

### Родные плагины: ABI и загрузка (Tannen.dll, разобрано)

Разбор `Tannen.dll` (база `0x180000000`). Ghidra восстановила настоящие имена
типов и функций, поэтому картина точная.

#### Класс `biff::tannen::PluginFile`

Конструктор `PluginFile(const std::wstring& path, PluginManager*)` делает:

1. **`FUN_180351c00` — резолвер точек входа** (ленивый, вызывается один раз):

```c
m->hModule = LoadLibraryW(path);                 // иначе throw "LoadLibrary failed"
m->pfnPluginInfo = GetProcAddress(h, "PluginInfo");
m->pfnNotify     = GetProcAddress(h, "Notify");
if (!pfnPluginInfo || !pfnNotify) throw "Missing API";
```

2. `FUN_180351cf0(BiffHost&)` — заполняет структуру **`BiffHost`** (336 байт):
   сервисы хоста, отдаваемые плагину.
3. `CreateAPI(file, BiffHost&, tagBiffAPI&, 0)` — собирает **`tagBiffAPI`**
   (1296 байт): таблицу API.
4. `ReadPluginMetadata(file, BiffHost&, tagBiffAPI&)` — вызывает точку входа
   плагина: `PluginInfo(BiffHost*, tagBiffAPI*, PluginMetadata* out)`.
5. **Проверка пакета**: `magic == 0x089E31C7`, major `< 0x16` (22), и при
   major `== 0x15` (21) — minor `< 2`. То есть принимаются версии **до 21.1
   включительно**, что совпадает с версией движка HitFilm 21.1.2. Иначе
   `"The plugin package information doesn't match the current build."`
6. **После чтения метаданных модуль выгружается** (`FreeLibrary`) — DLL
   поднимается заново только когда плагин реально понадобится.

#### Сигнатуры точек входа

```c
int PluginInfo(BiffHost* host, tagBiffAPI* api, PluginMetadata* out);
int Notify(void* a, void* b, int message);   // message 0 = PluginLoaded
```

`Notify` при загрузке вызывается с `message = 0` и **обязан вернуть 1**, иначе
`"PluginLoaded returned error N"` (`FUN_180351eb0`).

#### Типы плагинов

Первое поле метаданных — тип, ремапится `0->0, 3->1, 1->2, 2->3, 4->4, 5->5`,
всё прочее — `"Invalid plugin type"`. Шесть типов соответствуют классам-обёрткам
и подпапкам `Plugins/`:

| Класс-обёртка | Папка |
|---------------|-------|
| `Plugin2DEffect` | `2D` |
| `PluginAudioEffect` | `Audio` |
| `PluginAudioTransition` | `AudioTransitions` |
| `PluginBehaviorEffect` | `Behavior` |
| `PluginGeometryEffect` | `Geometry` |
| `PluginVideoTransition` | `VideoTransitions` |

Отдельно: `PluginAE2DEffect` (плагины After Effects) и
`OFXPlugin2DEffectWrapper` / `OFXPluginTransitionWrapper` (обёртки OFX).

Строки отображения прогоняются через `QCoreApplication::translate` с контекстом,
своим у каждого плагина, — то есть плагины локализуются штатным механизмом Qt.
Есть также `WaitAEandHFPluginsToLoad` — AE- и HitFilm-плагины грузятся
асинхронно.

#### Перенесено в порт

- Новый `src/plugin/NativePlugin.{h,cpp}`: пробник родного формата. Валидирует
  кандидата **чтением таблицы экспорта PE**, а не `LoadLibrary`, — скан
  никогда не исполняет чужой код и не спотыкается о несовпадение разрядности.
- `PluginManager::scan()` раньше смотрел только верхний уровень папки и искал
  расширение `vfx`, которого в референсе нет вовсе. Теперь скан **рекурсивный**
  (плагины лежат в подпапках-категориях) и принимает `*.hfpl` / `*.hfplx`
  наряду с нашими JSON-пресетами `*.vfx`.
- `PluginKind` дополнен шестью реальными типами референса; категория берётся из
  имени папки.

Проверено на настоящих плагинах: 12 файлов `.hfpl` из шести категорий приняты
все; `Qt5Core.dll`, переименованная в `.hfpl`, и текстовый файл отклонены с
`"missing plugin entry point"`.

#### Блок идентичности в `.rdata` (разобран статически, перенесён в порт)

Каждый из 321 плагина, поставляемого с референсом, несёт в секции `.rdata`
блок идентичности. Он лежит подряд, без заголовка и таблицы длин:

```
UTF-16LE  "Copyright (c) 2011-2022 FXhome Ltd. All Rights Reserved."
ASCII     "com.FXHOME.HitFilm.<Id>"      <- уникальный идентификатор плагина
UTF-16LE  "FXhome"                       <- вендор
UTF-16LE  "<Category>"                   <- категория для панели Effects
...                                      <- дальше литералы модуля
```

Пример (`Plugins/AudioTransitions/Fade.hfpl`, смещение `0x1a00`):

```
00001a00  63 6f 6d 2e 46 58 48 4f 4d 45 2e 48 69 74 46 69  com.FXHOME.HitFi
00001a10  6c 6d 2e 41 75 64 69 6f 46 61 64 65 00 00 00 00  lm.AudioFade....
00001a20  46 00 58 00 68 00 6f 00 6d 00 65 00 00 00 00 00  F.X.h.o.m.e.....
00001a30  54 00 72 00 61 00 6e 00 73 00 69 00 74 00 69 00  T.r.a.n.s.i.t.i.
00001a40  6f 00 6e 00 73 00 20 00 2d 00 20 00 41 00 75 00  o.n.s. .-. .A.u.
00001a50  64 00 69 00 6f 00 00 00 46 00 61 00 64 00 65 00  d.i.o...F.a.d.e.
```

Что из этого достоверно:

- **Идентификатор** — уникален и является настоящим ключом плагина. Он **не
  всегда совпадает с именем файла**: `360LightsaberV2Auto.hfpl` объявляет
  `com.FXHOME.HitFilm.360LightswordAuto` — след переименования HitFilm →
  VEGAS. Ровно одно исключение из 321: `DepthMask.hfpl` и `DepthMatte.hfpl`
  — один и тот же модуль под двумя именами, оба объявляют
  `com.FXHOME.HitFilm.DepthToMatte`, поэтому уникальность идентификатора
  гарантировать нельзя.
- **Вендор** — `FXhome` у всех 321.
- **Категория** — та, по которой группирует панель Effects, и она **не равна
  имени папки**. Папка задаёт лишь класс-обёртку; плоская `Plugins/2D`
  разворачивается в 21 категорию:

  | Папка | Категории из метаданных |
  |-------|-------------------------|
  | `2D` (241) | 360° Video (18), Animation (5), Blurs (8), Channel (6), Color Correction (22), Color Grading (20), Depth (2), Distort (16), Generate (40), Gradients & Fills (4), Grunge (15), Keying (17), Lights & Flares (13), Particles & Simulation (6), Scene (3), Scopes (4), Sharpen (3), Stylize (14), Temporal (5), Video Clean-up (9), Warp (11) |
  | `Audio` (16) | Audio |
  | `AudioTransitions` (2) | Transitions - Audio |
  | `Behavior` (43) | Behavior |
  | `Geometry` (4) | Geometry |
  | `VideoTransitions` (15) | Transitions - Video |

За категорией идут остальные литералы модуля — подписи параметров, единицы
(`px`, `ms`, `px/s`, `f%`) и варианты перечислений. Пример `AudioEcho`:
`Echo | ms | Delay | f% | Falloff | Number of Echoes`; `360Blur`:
`px | Radius | Horizontal & Vertical | Horizontal | Vertical | Dimension`.
**Их роли статически неразличимы** — порядок задан пулом строк MSVC, а не
таблицей, и отображаемое имя присутствует не всегда (в 54 файлах из 321 сразу
за категорией идёт параметр). Чтобы разобрать их достоверно, нужна раскладка
структуры `PluginMetadata`, которую заполняет `PluginInfo` — это следующий шаг
по `Tannen.dll`.

#### Перенесено в порт

- `NativePluginInfo` дополнен полями `identifier`, `vendor`, `effectCategory`;
  разбор — тем же способом, что и таблица экспорта: чтением файла, без
  `LoadLibrary`.
- `PluginManager` теперь ключует плагин по объявленному идентификатору (с
  откатом на `native:<файл>` при коллизии, см. `DepthToMatte`), берёт категорию
  из метаданных, а отображаемое имя получает разбиением CamelCase хвоста
  идентификатора: `HighpassSharpen` → `Highpass Sharpen`, `HSL` остаётся `HSL`.
- Проверено пробником на всех 321 файле: идентификатор извлечён у 321/321,
  вендор и категория совпадают с независимым разбором на Python во всех случаях.

### Медиа-кэш: конфигурация (разобрано, перенесено в ядро)

`Marvin::Create` вызывается из main на `0x1401c6eb4`:

```
Marvin::Create(out_shared_ptr,
               wstring(MediaCacheDB),      // FUN_140231af0 -> toStdWString
               wstring(MediaCacheFiles),   // FUN_140231f00 -> toStdWString
               std::string(empty),
               bool)
```

Сразу за ним, на `0x1401c72e7`:
`marvin->SetDaysToKeepMediaCacheFiles(FUN_140232240())`.

**Оба path-геттера построены по одному алгоритму** (`FUN_140231af0` для
`Options/MediaCacheDB`, `FUN_140231f00` для `Options/MediaCacheFiles`):

1. если ключа в `QSettings` группы `Options` нет — вычислить значение по
   умолчанию и записать;
2. прочитать значение как `QString` (QVariant type 10);
3. **самовосстановление**: если каталог-родитель (`QFileInfo(path).dir()`)
   не существует — снова вычислить умолчание, перезаписать настройку и
   использовать её (страховка от исчезнувшего съёмного диска);
4. вернуть `QDir::toNativeSeparators(path)`.

**Умолчания** (`FUN_140231a00` / `FUN_140231e30`), обе от
`QStandardPaths::writableLocation`:

| Ключ | Значение по умолчанию |
|------|----------------------|
| `Options/MediaCacheDB` | `<writableLocation>/Media/cache.db` (`mkpath("Media")`, затем `absoluteFilePath("cache.db")`) |
| `Options/MediaCacheFiles` | `<writableLocation>/Media/Files` (`mkpath("Media/Files")`) |

**Срок хранения** (`FUN_140232240`), ключ
`Options/DaysToKeepMediaCacheFiles`: умолчание **30** (`0x1e`); если
сохранённое значение `>= 366` (`0x16e`), функция **перезаписывает настройку
значением 30** и возвращает 30. То есть допустимый диапазон — 0..365.

#### Перенесено в порт

- `Settings` получил `mediaCacheDbPath()`, `mediaCacheFilesPath()`,
  `daysToKeepMediaCacheFiles()` с теми же именами ключей, теми же умолчаниями,
  самовосстановлением пути и тем же ограничением 0..365 / сброс к 30.
- `AppMain::initializeCache()` раньше клал `cache.db` в папку **Presets**
  пользовательских данных — это было неверно. Теперь БД открывается по
  `Options/MediaCacheDB`, каталог кадров создаётся по `Options/MediaCacheFiles`,
  а после открытия выполняется отсечение по сроку хранения через
  `CacheDB::pruneOlderThan()`.
- В `OptionsDialog` страница **Cache** перестала быть заглушкой: два поля путей
  с кнопками обзора и счётчик дней с диапазоном 0..365 (0 = «Keep forever»).

Проверено запуском: при первом старте в ini появляются `Options/MediaCacheDB`
и `Options/MediaCacheFiles`, создаются `Media/cache.db` и `Media/Files/`.

### Правка раскладки по скриншотам и строкам (сверка порта с референсом)

Сверка кода UI с `SAMPLES/screenshots` и строками из Ghidra выявила расхождения;
исправлено:

**1. Центральный контейнер — группа вкладок.** Recovered Screen XML и все
скриншоты показывают `Viewer (32)`, `Trimmer (2050)` и `Export (2057)` как
вкладки одного центрального контейнера, а не как боковые доки. В порту Trimmer
висел слева рядом с Media, Export — справа рядом с Effects. Теперь центральный
виджет — `QTabWidget` с тремя страницами; `TrimmerPanel` и `ExportPanel`
переведены с `QDockWidget` на `QWidget`.

**2. Правая колонка.** Референс: `Effects | Controls | Layout | Track`. В порту
`Layout` и `Track` были скрыты (`hide()`), а между ними стояли Library и
History. Теперь четыре канонические вкладки идут первыми и видимы, Library (2059)
и History (256) — панели Edit screen — следуют за ними. По умолчанию поднят
`Controls`, как на `0.png`.

**3. Панель 1024 называется `Track`.** В порту заголовок дока был `Layers`; на
скриншотах вкладка подписана `Track`.

**4. Заголовок окна.** Референс: `<документ>[*] - <приложение>`
(`Untitled Project* - VEGAS Effects`, `ed_0001.vegfx* - VEGAS Effects`). В порту
было обратное `OpenVegas Effects - Untitled` без пометки изменений. Введён
`MainWindow::updateWindowTitle()`, звёздочка ставится при несохранённых правках.

**5. Подписи меню по строкам бинарника** (§6): `actionNew` → `New...`,
`actionOpen` → `Open...`, `actionAbout` → `About...` вместо
«New Project…»/«Open Project…»/«About OpenVegas Effects». Импорт медиа
переведён на `Ctrl+Shift+O` по восстановленному `/DefaultShortcutsWin.xml`.

**6. Панель Effects по `2.png`**: убраны заголовки колонок (в референсе их нет),
добавлены строка `Show All`, звезда-избранное на каждой строке эффекта, группа
`Favorites` вверху дерева и счётчик `N item(s)` в подвале — аналог
`424 item(s)` референса.

**7. Баг раскладки вьюера.** `ViewerWidget::buildToolbar()` добавлял панель
кнопок в `QVBoxLayout` без распорки, поэтому та растягивалась на всю высоту, а
кнопки уезжали в вертикальный центр; при этом `viewRect()` рассчитывает, что
панель занимает верхние 30 px. Панели задана фиксированная высота 30 px и
добавлена распорка; `toolStripRect()` сдвинут вниз на высоту панели, чтобы
палитра инструментов не перекрывала её.

**8. Панели Media и Controls не обновлялись при открытии проекта.**
`refreshAfterModelChange()` перестраивал таймлайн, панель слоёв и заголовок, но
не трогал медиатеку и инспектор. В результате после загрузки `.vegfx` таймлайн
показывал клипы проекта, а панель Media рядом сообщала `0 item(s)`. Добавлен
вызов `refreshMediaAndInspector()`.

**9. Таймкод в шапке таймлайна был захардкожен** строкой `00:00:23:18`
(значение, скопированное со скриншота референса) и никогда не менялся.
В референсе там позиция плейхеда (`00:00:02;21` в `crop_screenshot.png`).
Метка привязана к `setPlayheadPosition()` и форматируется по кадровой частоте
композиции.

**10. Канва вьюера рисовалась под палитрой инструментов.** `viewRect()`
начинал область с `x = 0`, а палитра занимает левые 30 px, поэтому наложенный
таймкод обрезался первым символом. Область канвы теперь начинается за палитрой —
как в референсе, где палитра стоит сбоку от кадра, а не поверх него.

Не исправлено (требует новых подсистем): панель `Start` в референсе — полоса
вкладок в статус-баре рядом с вкладками документов, в порту это нижний док;
transform-гизмо, ключевые кадры и `Value Graph` отсутствуют (см.
[`../SAMPLES/screenshots/crop_screenshot.md`](../SAMPLES/screenshots/crop_screenshot.md)).

### Layout (2055) и Start (2056) — портированы

**`LayoutPanel.{h,cpp}`** (reference RTTI `.?AVLayoutPanel@ui@biff@@`, objectName `LayoutPanel`,
строка `1412c5c08`). Панель — это два `common`-виджета один над другим:

- **`TransformWidget`** (`1412dd088`, setupUi `FUN_140315400`): ряд из `toolButtonMirrorVertical` /
  `toolButtonMirrorHorizontal` / `toolButtonCounterClockWise` / `toolButtonClockWise` над рамкой с
  `widgetDirection` (сетка 3×3), `spinBoxX` / `spinBoxY` / `spinBoxWidth` / `spinBoxHeight` и
  цепочкой `toolButtonScaleLinked`;
- **`AlignmentWidget`** (`1412dd618`, setupUi `FUN_140329cd0`): группа «Alignment» с
  `comboBoxAlignTo` (Selection / Timeline), шестью `toolButtonAlign*` и шестью
  `toolButtonDistribute*`.

> **Поправка от 5 сентября 2026.** Здесь раньше стояло, что панель содержит пять пресетов
> раскладки вьювера и селектор воркспейсов. Это было неверно: кнопки `toolButtonLayout1/2/2_1/2_2/4`
> с id `layout-*` строит `FUN_1402915c0` = **`ScopesPanelWidget`** (`1412d1678`, подсказки
> «One Scope» … «Four Scopes»), то есть это тайлинг осциллографов; а команды воркспейсов
> принадлежат меню `Window > Workspaces` (`FUN_14039e2e0`) и диалогу `WorkspaceDialog`
> (`FUN_1402bc970`). Разбор — в `RE_wnd_Layout.md` §10.
>
> `ViewerWidget::ViewerLayout` (`Single`, `Row`, `TwoOverOne`, `Four`, `RowFour`) и пункты меню
> Window оставлены как есть: раскладки вьювера в референсе действительно существуют
> (`PanelRenderState::ViewType`, `FourViewLayoutManager` / `RowViewLayoutManager`), но их пять
> значений следует считать восстановленными по поведению, а не по id этих кнопок.

**`StartPanel.{h,cpp}`** (reference `.?AVStartPanel@ui@biff@@` + внутренний
`.?AVStartPanelWidget@common@ui@biff@@`, objectNames `StartPanel` / `StartPanelWidget`,
строки `1412bccb0` / `1412d1af8`). По recovered Screen XML референс держит его снизу Effects screen —
здесь это нижний док, табом рядом с Editor. Содержимое восстановлено из строк:

- `toolButtonNewComp` → «New Composite Shot» (`1412d1c38`), tooltip «Create New Composite Shot»;
- `toolButtonNewCompsFromMedia` → «New Composite Shots\nFrom Footage» (`1412d1c10`);
- `toolButtonEditScreen` → «Edit Screen» (`1412cb2b8`), tooltip «Go to the Edit Screen» (`1412d3848`);
- `listViewRecentProjects` (`1412d8e38`) + «Clear Recents» (`14130b8f8`); список хранится в
  `Settings::recentProjects()` под ключом `RecentProjects` (имя как в референсе, `1412c7508`)
  и пополняется при открытии и сохранении проекта;
- строка обучения «Go online to explore amazing tutorials…» (`1412d7f70`) — в референсе это
  QtWebEngine-поверхность, здесь обычный link-label, чтобы панель осталась нативным виджетом.

Не портировано намеренно: Save/Delete Workspace пишут в статус-бар (нет модели воркспейсов),
tutorials-ссылка открывает нативную панель Learn (самих уроков в сборке нет).

### Вьюер (ViewerWidget): мультивидовые макеты, зум, шахматный фон
- Из референса: `biff::ui::viewer::ViewerLayoutManager` (абстрактный) с конкретными
  `FourViewLayoutManager` (сетка 2×2) и `RowViewLayoutManager` (рядом/строчно); состояние вьюера
  хранится в `project::PanelRenderState` (`SetLayout(LayoutType)`, `LayoutNumberOfViews()`,
  `SetActiveViewInLayout()`, `SetLayoutSplitterPosition()`); enum `ViewerMode`.
- Портировано (clean-room) в `ViewerWidget.{h,cpp}`:
  - `ViewerLayout` enum: `Single` (1), `Row` (2 рядом), `Four` (2×2) — переключается кнопками тулбара
    вьюера и меню View (View → 1-/2-/4-View Layout);
  - активный вью (`setActiveView`) подсвечивается рамкой `$focus` (18,176,255), клик по вью активирует его;
  - зум: `ViewScaleButton.{h,cpp}` (аналог `biff::ui::common::ViewScaleButton`): Fit + лестница
    процентов (12–200%), кнопки +/−, колесо мыши; команды View → Zoom to Fit/In/Out (id 40/41/42,
    клавиши `` ` ``/`=`/`-` из референса);
  - шахматный фон (аналог `PaintCheckerboard`/`ColorPainting`, надстройки `ShowCheckerboard2D`):
    чекбокс `Chk` в тулбаре и View → Checkerboard Background.
  - тул-палитра вьюера (reference `Viewer tools`: Select V, Hand H, Text T, Rectangle R,
    Ellipse E, Freehand F, Orbit B) — `ViewerTool` enum + вертикальная полоска слева и шорткаты
    V/H/T/R/E/F/B; «Hand» — реальное панорамирование активного вью (`setCursor` + сдвиг), «Select» —
    выбор/активация вью, «Rectangle»/«Ellipse» — rubber-band превью штриха; Text/Freehand/Orbit —
    выбираемые (заготовка под маски/фигуры).
- Источник истины фрейма — единый рендер; каждый вью масштабирует его по своей области (в 2D все вью
  показывают один кадр; в референсе это были бы разные орто/перспективные ракурсы 3D-вьюера).
- Сборки: CMake (MinGW) и qmake (MSVC) зелёные; новые файлы `src/ui/ViewScaleButton.{h,cpp}` добавлены
  в `CMakeLists.txt` и `OpenVegasEffects.pro`.

---

*Правила проекта: разбор ведётся в `MARKDOWN/` (см. `INIT.MD`). Синхронизировать с изменениями анализа.*
### ID типов панелей (recovered Screen XML)
| Type | Panel | Ported |
|------|-------|--------|
| 1 | Media | MediaPanel |
| 2 | Controls | EffectInspector |
| 4 | Effects | EffectsPanel |
| 16 | Editor (timeline) | TimelineWidget |
| 32 | Viewer | ViewerWidget |
| 128 | Text | TextPanel |
| 256 | History | - |
| 1024 | Track | LayerPanel |
| 2050 | Trimmer | TrimmerPanel |
| 2051 | Meters | - |
| 2055 | Layout | LayoutPanel |
| 2056 | Start | StartPanel |
| 2057 | Export | ExportPanel |
| 2059 | Library | - |

### Перенесено в порт
<!-- ПОВРЕЖДЕНО: текст утрачен при перекодировке (CP866 прочитан как UTF-8); восстановить из более ранней копии файла -->
- Layout ��������� � �ᯮ���� �� Effects screen XML: ���=Media+Text (tabified), �ࠢ�=Effects+Controls (tabified), 業��=Viewer, ���=Timeline.
<!-- ПОВРЕЖДЕНО: текст утрачен при перекодировке (CP866 прочитан как UTF-8); восстановить из более ранней копии файла -->
- TextPanel.{h,cpp}: ��� (Type 128) - �������� ������ ��� � ����� (��� addTextLayer -> Composition).
<!-- ПОВРЕЖДЕНО: текст утрачен при перекодировке (CP866 прочитан как UTF-8); восстановить из более ранней копии файла -->
- Import Media ��� = Ctrl+Shift+O (�ᯮ � �ਥ��).

### ID типов панелей (recovered, полный список из Screen XML)
| Type | Panel | ns/class (RTTI/objectName) | Ported |
|------|-------|---------------------------|--------|
| 1 | Media | MediaPanel | MediaPanel |
| 2 | Controls | ControlsPanel | EffectInspector |
| 4 | Effects | EffectsPanel | EffectsPanel |
| 16 | Editor (timeline) | SequenceTimelinePanel | TimelineWidget |
| 32 | Viewer | ViewerPanel | ViewerWidget |
| 128 | Text | TextPanel | TextPanel |
| 256 | History | HistoryPanel/HistoryWidget('listViewHistory') | HistoryPanel |
| 1024 | Track | LayerPanel | LayerPanel |
| 2050 | Trimmer | TrimmerPanel/TrimmerWidget(+Model) | TrimmerPanel |
| 2051 | Meters | AudioMetersPanel | AudioMetersPanel |
| 2055 | Layout | LayoutPanel | LayoutPanel |
| 2056 | Start | StartPanel/StartPanelWidget | StartPanel |
| 2057 | Export | ExportPanel/ExportPanelWidget | ExportPanel |
| 2059 | Library | LibraryPanel | LibraryPanel |

## 6. Реестр элементов UI (recovered, полный реестр)
### Действия (QAction objectName -> label)
actionNew -> 'New...' ; actionOpen -> 'Open...' ; actionSave -> 'Save' ; actionSaveAs -> 'Save As...'
actionProjectSettings -> 'Project Settings...' ; actionRecoveredSaves -> 'Recover Projects...' ; actionExit -> 'Exit'
actionAbout -> 'About...' ; actionOnlineHelp -> 'Online Help...' ; actionOptions -> 'Options...' ; actionHome -> 'Home'
actionUndo/Redo/Cut/Copy/Paste/Delete/Duplicate/SelectAll ; actionRippleDelete ; actionSlice
actionPasteAttributes -> 'Paste Attributes...' ; actionRemoveAttributes -> 'Remove Attributes...' ; actionRemoveEffects -> 'Remove Effects'
actionExportFontList -> 'Export Font List...'
### Меню (menu<Name>)
menuFile, menuEdit, menuEffects, menuImport, menuWindow, menuHelp, menuDebug, menuRecord, menuExport, menuOpenRecent

**Видимая меню-бар (референс, все 6 скриншотов):** `File · Edit · Effects · Window · Export · Help`.
`menuImport/menuRecord/menuDebug` в референсе НЕ на верхней строке (внутренние/скрытые); раскладки/зум живут
в Window (`WindowMenu` + `WorkspacesMenu`), рендер/экспорт — в Export. Наш `MainWindow::configureMenus` приведён
к этому порядку (6 меню): File(New/Open/Save/SaveAs/Import Media.../Exit), Edit, Effects(Show All/Browse Library),
Window(workspace-layouts/zoom/checkerboard + toggle панелей + Full Screen + Open in Trimmer),
Export(Render Current Frame/Snapshot/Project), Help(About).
### Кнопки панелей инструментов (toolButton<Name>)
toolButtonLogo, toolButtonUndo/Redo/Save/Open/Delete/Close/Cancel/OK/Properties/Maximize/Minimize/Upgrade
toolButtonNewComp, toolButtonNewCompsFromMedia, toolButtonEditScreen
toolButtonLayout1/2/2_1/2_2/4  (пресеты раскладок layout-1, layout-2, layout-2-1, layout-2-2, layout-4)
### Контейнеры/objectName
stackedWidgetScreens ('stacked-widget-screens'), windowHeaderWidget (кастомный заголовок окна), centralwidget, menuBar, tabWidget,
tabEditor, tabRendering, mainLayout, buttonsLayout, scopesLayout, gridLayout/_2.._7, horizontalLayout/_2/_3,
verticalLayout/_2.._5, scrollAreaWidgetContents; overaly biff::ui::TabBarOverlay / PanelOverlay
### Ключи настроек
AudioMetersHoldPeaks / label 'Hold Peaks' ; BeepSpeakerOnExport ; RenderBitDepth ; PrintRenderTimings ; ShowRenderTimings
TimelineCacheDB/MediaCacheDB/cache.db ; AutoCacheRenderEnabled ; DaysToKeepTimelineCacheFiles
LibraryMediaPath, PresetsPath ('/Templates','LibraryTemplatePath','blank'), ExportDirectory, PreRenderDirectoryPath
### Горячие клавиши (дополнение к набору 7)
Exit Ctrl+Q / Alt+F4 ; Redo также Ctrl+Shift+Z ; Close Active Panel Ctrl+W
Switch Home Ctrl+1 / Edit&Effects Ctrl+2 ; Create New Composite Shot Ctrl+Shift+N ; New Plane Ctrl+Shift+A
Slice Ctrl+Shift+D ; Ripple delete Alt+Del ; Open in Trimmer Ctrl+T ; Insert(триммер) B ; Overlay(триммер) N
Viewer tools (категория 6 Viewer, команды): 5000 Select V, 5001 Hand H, 5006 Text T, 5007 Rect R,
5008 Ellipse E, 5009 Freehand F, 5010 Orbit B, 5011 Rounded rect, 5012 Polygon, 5013 Star (без шортката),
5014 Freehand path, 5026 Toggle Full Screen preview Ctrl+Shift+F/Ctrl+Alt+F.
Editor sequence timeline (кат. 3): 2005 Select V, 2006 Hand H, 2007 Snap Shift+S, 2008 Slice C,
2009 Slip Y, 2010 Slide Shift+U, 2011 Ripple R, 2012 Roll E, 2013 Ripple delete Alt+Del, 2014 Rate S,
2015 Track-select-fwd A, 2016 Shift+A, 2102 Solid Ctrl+Alt+A, 2103 Text Ctrl+Alt+T, 2104 Grade Ctrl+Alt+G,
2110 Open in Trimmer Ctrl+T. Composite timeline (кат. 4): 3000-3020 (Select/Hand/Slice/Snap,
New Plane/Camera/Light/Grade/Text/Point Ctrl+Alt+[A,C,L,G,T,P], Move layer to playhead, Publish/Unpublish).
### Команды экспорта / Export
Export to File, Export Frame, Export Contents Area, Export In-to-Out Area, Add ... to Export Queue,
OpenEXR Export, ProRes Export, Export Camera ; watermarks :/watermarks/VegasEffects/viewer-watermark.png


### 6.1 Кнопки, группы и списки (по objectName из `.rdata`)

Экспортов у exe нет и функции обезличены, поэтому интерфейс восстанавливается
не по коду, а по строкам: Qt хранит `objectName` каждого виджета формы в
`.rdata`. Извлечено **618 уникальных имён**. Действия и меню уже перечислены
выше; ниже — то, чего в реестре не было.

### Ключевые кадры

```
toolButtonKeyFramePrevious  toolButtonKeyFrameToggle  toolButtonKeyFrameNext
toolButtonKeyFrameTypeConstant      toolButtonKeyFrameTypeLinear
toolButtonKeyFrameTypeManualBezier  toolButtonKeyFrameTypeAutoSmooth
toolButtonKeyFrameTypeAutoSmoothIn  toolButtonKeyFrameTypeAutoSmoothOut
toolButtonToggleGraph  toolButtonGraph
```

Тройка навигации подтверждает состав панели, который мы сделали. А вот типов
интерполяции у референса **шесть**, у нас в `TemporalType` три
(Linear / Bezier / Hold). Не хватает AutoSmooth и его односторонних вариантов.

### Инструменты редактора

```
toolButtonPointer  toolButtonHand  toolButtonTrackSelect
toolButtonSlice  toolButtonSlip  toolButtonSlide  toolButtonRoll
toolButtonRipple  toolButtonStretch  toolButtonMove
toolButtonMagnet
```

У нас `EditorTool` покрывает Select / Hand / Slice / Slip. Нет Slide, Roll,
Ripple, Stretch, Move, TrackSelect.

### Строка слоя и таймлайн

```
toolButtonSolo  toolButtonMute  toolButtonLock  toolButtonVisible
toolButtonRelink  toolButtonSyncScrolling  toolButtonMatchTimeline
toolButtonAutoZoom  toolButtonNewLayer  toolButtonNewTracks
toolButtonNewComp  toolButtonNewObject  toolButtonNewFolder
toolButtonNewCompsFromMedia  toolButtonMakeCompositeShot
stackedWidgetScreens  stackedWidgetTimelines  stackedWidgetContents
```

`stackedWidgetTimelines` подтверждает, что таймлайн — стек по одной странице на
композицию (отсюда закрываемые вкладки внизу). `toolButtonRelink` — штатная
перепривязка медиа.

### Вьювер

```
toolButtonPointer  toolButtonHand  toolButtonText  toolButtonMaskShape
toolButtonMaskPen  toolButtonVectorPath  toolButtonCameraOrbit  toolButtonPipette
toolButtonLayout1  toolButtonLayout2  toolButtonLayout2_1
toolButtonLayout2_2  toolButtonLayout4
toolButtonTransparent  toolButtonBlack  toolButtonWhite  toolButtonGrey
toolButtonPlaybackQuality  toolButtonZoom  toolButtonGrid  toolButtonOverlay
```

Имена инструментов и пять раскладок у нас совпадают. Фонов у референса четыре
(прозрачный / чёрный / белый / серый), у нас только шахматка.

### Панель текста

Полный набор форматирования: `Align*` (15 кнопок), `AllCaps`, `SmallCaps`,
`UpperCase`, `LowerCase`, `TitleCase`, `Strikethrough`, `Subscript`,
`Superscript`, `Underline`, `FontColorPipette`, `FontOutlineColorPipette`,
`AddOutline`, `RemoveOutline`. Плюс выравнивание/распределение объектов:
`Distribute{Bottom,Top,Left,Right,Horizontally,Vertically}`.

### Настройки

Разделы (`groupBox*`): General, Video, Audio, Image, Codec, Viewer, Controls,
Labels, MediaPanel, Defaults, EditorSequence, Animation, Background, Clipping,
Preset, Proxy, PreRender, CacheMedia, CacheTimeline, Relink, **OpenFX**,
FileInformation, TemplateMatch, SelectCompositeShots, а также 3D-группы
CoordinateSystem, ModelUnitScale, Normals, UVMapping, Plane, Fog, MotionBlur,
OpticalFlow.

Списки (`comboBox*`): Theme, OverrideLanguage, Workspaces, TimeFormat,
FrameRate, AspectRatio, PAR, BitDepth, ColorBitDepth, ColorSpace, ColorLevels,
SampleRate, Channels, ProxyQuality, PreviewMode, MediaTree{ArrangeBy,GroupBy,
DisplayMode}, VideoTrackHeight, AudioTrackHeight, WaveForm, FontFamily,
FontStyle, ScopeType и другие — всего 49.

### Форматы

```
3D-модели   *.3ds *.lwo *.obj *.fbx *.abc *.gltf *.glb
анимация    *.fbx *.abc *.gltf *.glb
изображения *.jpg *.jpeg *.png *.gif *.bmp *.tif *.tiff *.tga *.exr *.hdr
```

Alembic в самом exe фигурирует **только** в этих фильтрах импорта — то есть как
формат 3D-сцен и анимации. Работа с камерой (`OCameraSchema` / `ICameraSchema`)
живёт в `Alembic.dll`, см. раздел о стороннем стеке.

EXR обслуживает класс `fxh::media::FXImageFileOpenEXR` с методами
`CodecSupportsDataType(EXRCompression, FXChannelDataType)` и `CompressToFile(...)`
— то есть сжатие и тип данных выбираются, а не зашиты.

OpenFX представлен категорией «OFX Plugins», иконкой
`:/images/images/effect-ofx-plugin.png`, разделом настроек `groupBoxOpenFX` и
командой «Delete OpenFX Plugins Cache».

---

## 7. Native project format (.vegfx)

Clean-room schema recovered from `SAMPLES\vegfx_projects\project_1\Project.vegfx` (42,886 B, valid XML).
Read + written by `src/project/VegfxSerializer.{h,cpp}`; opened from MainWindow (Open/Save dialog filter
`*.vegfx`, dispatch by extension).

### 7.1 Document outline
```
VegasEffectsProject  @Version="0" @CurrentScreen="2" @AppVersion="5.0.2.0" @AppEdition="5000"
 └─ Project @Version="7"
     ├─ ID, Name
     ├─ ProjectSettings @Version="9"
     │    BPC=1000, AntialiasingMode, ReflectionMapSize=512, ModelTextureMaxSize=4096, ShadowMapSize=2048,
     │    LimitVideoDecodingTo8bit, UseLinearColor
     ├─ AssetList @Version="7" → Assets
     │    ├─ CompositionAsset @Version="19"   (primary comp; see 7.3)
     │    └─ media assets: <ref-name> @Version="3" (png) / @Version="10" (mp3)
     │         ID, Filename (ABSOLUTE path), MediaKind, Instances → Instance @Version="1" (CompID, Type, ID)
     ├─ Root           (media/folder tree)            → Editor @Version="5" (sequence tracks)
     ├─ Metadata, Effects
```

### 7.2 Media assets (AssetList/Assets)
- Each non-Composition child is a media asset. Element tag is the asset reference name
  (e.g. `<䮭.png>`, `<2_...png>`, `<ika ...mp3>`).
- `<Filename>` is an **absolute Windows path** (author machine); clean import calls
  `MediaManager::importFile(filename)` and, if missing, logs a warning but keeps the
  deterministic mediaId `media:`+cleanedPath so clips still reference their asset.
- `<Instances>` = list of use sites (CompID → composition GUID, Type, ID).

### 7.3 CompositionAsset (primary composition)
```
CompositionAsset @Version="19"
 ├─ ID, Name (e.g. "Облога"), CTI=1370, In=0, Out=1800
 ├─ TimelineZoom, SplitterPosition=440, TimelineTimeFormat=1000, TimelineSnapMode=1000, IsPrimary=1
 ├─ AudioVideoSettings: FrameCount=2100, AudioSampleRate=48000, Width=1080, Height=1920,
 │     PixelAspectRatio=0, PixelAspectRatioCustom=1, FrameRate=60
 ├─ RenderSettings @Version="2": Fog, MotionBlur, ShutterAngle=180, ShutterPhase=-90,
 │     MaxNumOfSamples=20, UseAdaptiveSamples
 └─ Layers → TextLayer @Version="13" | AssetLayer @Version="17"
```

Fog (October 2026). `RenderSettings` carries `FogEnabled`, `FogNearDistance` (900),
`FogFarDistance` (2000), `FogDensity` (1), `FogColor` (A R G B, black) and `FogFalloff`
(`CompositionRenderSettings::FalloffType` 0 Linear, 1 Exponential, 2 Exponential²). The
Composite Shot Properties dialog builds them in its Advanced tab (`FUN_14072b930`:
`groupBoxFog`, `checkBoxFogEnable`, `doubleSpinBoxNearClipDistance`/`FarClipDistance`/
`Density` with maximum 999999999, `comboBoxFallOff`, colour block with pipette; the Enable
box enables the rest) and names them in `FUN_140730a50` (context `CompositionSettingsDialog`:
"Fog", "Enable:", "Near Clip Distance:", "Far Clip Distance:", "Density:", "Fall Off:",
"Color:", items "Linear", "Exponential", "Exponential²"); `EditCompositionAssetCmd`
(`FUN_1404cda60`, "Edit Composite Shot") pushes one property command per changed field.
The renderer is Flux.dll's GLSL `ComputeFoggedFragment(fragmentColor, ecPos)` with
`uniform vec4 fogParams` (x density, y near, z far, w mode 1/2/3) and `fogColor`:
`d = length(ecPos)`; linear `(far - d) / (far - near)`, exponential
`exp(-density * d * 0.001)`, exponential² `exp(-density * density * d * d * 0.001)`;
clamped, then `mix(fogColor * a, colour, factor)` with the alpha kept. The port applies the
same per pixel (`model3d::Fog`) to 3D layers, models and Geometry text; 2D layers are not in
the scene and stay clear. For that, 3D media/plane/text/nested-shot layers are now projected
through the scene camera (`RenderWorker::renderClipIn3D`: content rendered flat, carried by
the layer transform, `QTransform::quadToQuad` per sheet or per cell when part of it is behind
the eye; fog from the ray–plane distance of each pixel). Neighbouring 3D layers form one
scene: each is drawn on its own and the scene is composited per pixel from far to near
(`RenderWorker::sceneDistances` - the ray–plane distance for a sheet, the layer position for a
model or Geometry text), while a 2D or Grade layer between them closes the scene, as HitFilm
and VEGAS Effects group 3D layers.

Composite Shot Properties (October 2026). `FUN_14072b930` builds CompositionSettingsDialog:
`comboBoxTemplate` with `toolButtonSave`/`toolButtonDelete`, `lineEditName` ("<MyComp>"),
`spinBoxDuration`; tab Standard with `groupBoxVideo` (`spinBoxWidth`, `spinBoxHeight`,
`toolButtonMatchTimeline`, editable `comboBoxFrameRate` 23.976/24/25 (PAL)/29.97 (NTSC)/30/50/
59.94/60, `comboBoxAspectRatio`) and `groupBoxAudio` (read-only `labelValueAudioSampleRate`);
tab Advanced with Fog (see above) and Motion Blur (`doubleSpinBoxShutterAngle` 0..720,
`doubleSpinBoxShutterPhase` -360..360, `spinBoxMaxSamples` 1..100, `checkBoxUseAdaptive`).
The aspect list is the PAR enum of `FUN_1403963f0` - Square Pixels (1.0), DV NTSC (0.91),
DV NTSC Wide (1.21), DV PAL (1.09), DV PAL Wide (1.46), HD Anamorphic 1080 (1.33), DVCPro HD
(1.5), Anamorphic 2:1 (2.0), Custom - whose exact values Project.dll keeps as a table
(10/11, 40/33, 12/11, 16/11, 4/3; 1.5 and 2.0). `AudioVideoSettings` stores `<PAR>` (enum) and
`<PARCustom>`.

What the PAR does (October 2026). Layers live in square units: the layer factory makes a New
Grade `round(Width * PixelAspectRatioValue)` wide (`FUN_140564460`), Flux builds the default
camera from the width and the PAR (`FUN_18039a680`: `CalculateLensZoom(HorizontalFieldOfView,
width, PAR)`) and gives every visual object its own PAR (`FUN_1803431f0` with
`ImageAsset/SolidAsset::PixelAspectRatio`, `MediaVideoStream::AspectRatio`, a shot's
`AudioVideoSettings::PixelAspectRatioValue`). So a shot is a `Width x PAR` by `Height` scene
squeezed into `Width x Height` pixels. The port renders that scene (`Composition::displaySize`)
and resamples it to the frame asked for; the viewer, full-screen preview and Layer panel show
frames at the square shape, viewer coordinates (positions, masks, text, layout) are square
units, a nested shot is placed at its square size, and an MP4/MOV export carries
`setsar=<PAR>`. Media keep their own pixel shape (October 2026): the stream's SAR (libVLC's
video track, `MediaAssetRef::PixelAspectRatio`) unless overridden (`MediaOverrideOptions::
PixelAspectRatio`, saved as `<OverridePAR>`/`<PAR>`; a still's `ImageAsset::PixelAspectRatio`
is its `<PAR>`), and are drawn `width x PAR` wide. The overrides are edited in Media Properties
(`biff::ui::media::MediaSettingsDialog`, retranslated by `FUN_14073de70`: Name, Path/Relink,
Container, Format, Codec, Duration, Resolution, Frame Rate, Aspect Ratio, Alpha, Color Levels,
Color Space with "From File", hardware decoding, Audio Sample Rate; History names from
`FUN_14034cf70`, property ids `0xaf1` frame rate, `0xaf2` PAR, `0xaf7` alpha, `0xaf9` color
levels, `0xafd` color space, `0xafa` hardware decoding, `0xaf5/0xaf6` trimmer points).
`MediaOverrideOptions` holds pairs (override flag, value): frame rate `+0x20/+0x28`, PAR
`+0x30/+0x34`, alpha `+0x38/+0x3c`, colour levels `+0x40/+0x44`, colour space `+0x48/+0x4c`; the
`MediaVideoStream` getters return the override or the file's value - `ColorLevels`/`ColorSpace`
0 (Automatic) when not overridden, so the combo index is the value (levels 0 automatic, 1
studio, 2 studio with super whites & blacks, 3 computer full; space 0 automatic, 1 Rec. 601, 2
Rec. 709); `Alpha` is 1 (premultiplied) for pixel formats 0x11/0x12, 0 (straight) otherwise;
`FrameRate` snaps 23.976/29.97/59.94 to their NTSC fractions; `AspectRatio` maps the stream's
value through `PARFromValue` (a value it does not know is square) and makes 1440x1080 of codec 4
HD Anamorphic. The port's dialog has them all (alpha only for files with alpha, levels/space/
hardware decoding only for decoded video, a sequence's rate without "From File"); levels and
the matrix are applied relative to libVLC's defaults, which it does not report. The same resampling fixed previews at another size than the shot (Half, Quarter,
Antialiased, the Layer panel's 960x540), which used to get full-scale content cropped into the
smaller frame; they now cost a full-size render.

Templates (`biff::ui::common::TemplateManager`): one `<GUID>.hft` per template in the folder
`DataLocation_Resolve_Folder(1)` resolves - registry `HKLM\SOFTWARE\<org>\<app>\Paths\Templates`
plus `\AV` - holding `<Templates><Template Version="0">` with `SystemTemplate`, `ID`, `Name`,
`Width`, `Height`, `FrameRate`, `PAR`, `PARValue` (used only for Custom), `AudioSampleRate` and
an optional `Duration` (writer `FUN_14044ff70`/`FUN_1404507a0`, reader `FUN_14044f770` - which
throws when PAR and PARValue or the sample rate disagree - and `FUN_1404503d0`). The system
templates come with the installer; their names are in the reference's catalogues under
`AVTemplate` (1080p Full HD @ 23.976...60, 720p HD, 4K/8K UHD up to 120 fps, 4K DCI 2160p, 2K DCI,
2K 16:9 QWXGA, 1440p QHD, 1440p/2.7K GoPro, 2.5K BMCC, 5K RED EPIC, 6K RED DRAGON, Instagram
Square, Vertical 1080p). The port builds that list in `app/AVTemplates` with the formats'
standard sizes, reads and writes user templates in the same `.hft` form under
`AppDataLocation/Templates/AV`, and Options' Default Template picks from the same list.

Export queue (October 2026). The reference's Export panel (`ExportPanelWidget`) has a Queue of
export tasks (`ExportTaskTreeView` over `ExportTaskItemModel`: Name, Preset, Output, Progress,
Duration, Elapsed), Start/Suspend Exporting and `ExportTaskActions` (Start Exporting, Force Start,
Reveal Output, Save Output As..., Duplicate Task(s), Remove Task(s), Remove Finished Task(s),
Select All); a queued task exports a snapshot of the project taken when it was added - Options >
Export "Default Snapshot Directory": "The directory where files required by tasks added to the
queue will be saved. These files are deleted automatically when the tasks finish." - and
`ExportTaskManager` keeps the queue in a tasks file ("The queue could not be restored because the
tasks file cannot be read or is invalid."). Options > Export "Time Format" (`ExportSettingsWidget`,
`FUN_1403c6560`/`FUN_1403c70e0`: Timecode, Natural, Seconds) only formats Duration/Elapsed. The
port follows that with `ui/ExportQueue` + `ui/ExportQueueView` (snapshot = a `.vegfx` written at
queue time, `ExportTasks.xml` in AppData) and runs every export through `render/ExportJob`, which
renders the shot with a RenderManager of its own.

### 7.4 Layers
Each layer wrapper (`AssetLayer`/`TextLayer`) contains an inner element **named after the layer itself**
(e.g. `<New Text>`, `<1_...png>`, `<䮭.png>`, `<...mp3>`) carrying the real fields. Finder helper:
the direct child that exposes a `<StartFrame>` child.
```
AssetLayer @Version="17"
 ├─ AssetID        → media asset GUID (resolves to a Filename in AssetList)
 ├─ AssetInstanceStart, AssetHasAudio, AssetHasVideo, AssetType  (0=Audio,1=Video/Image)
 └─ <named> @Version="2">: ID, Name, CompID, ParentLayerID, StartFrame, EndFrame, BlendMode(0=Normal),
      Visible, Muted, Locked, MotionBlurOn, Label, BehaviorEffects, PropertyManager
TextLayer @Version="13"
 ├─ Dimensions, IncludeInDepthMap, TextBox @Version="1" (MinX/MaxX/MinY/MaxY), Effects, GeometryEffects, Masks
 └─ <named>: same field set + PropertyManager (Text, FontSize, ...)
```

Text box placement (October 2026, Flux.dll). `<TextBox>` extents are layer space, Y up, and
`Mode` picks how the text sits in them. Point text (`Mode` 0): the text hangs off the layer
origin - `FUN_18051ca80` (first-baseline offset) returns 0 for it, so the first line's baseline
is the origin whatever `VerticalAlignment` says, and the line layout (`FUN_180519ca0`) gives it
no width to align in: Left/Left Justify/Justify lines start at x = 0, Center/Center Justify
centre on it, Right/Right Justify end on it; the first-line indent is added, the left/right/top/
bottom indents are not. The width aligned (`FUN_180517f20`) leaves out trailing spaces and the
last glyph's tracking. `MinX..MaxY` of point text is only this extent, recomputed from the
layout (`FUN_180523fc0`: `MaxY` = first line ascent, `MinY` = that minus the text height plus
the last line's descent) - project_1's "Very D.I.S.C.O" (Ubuntu Light 104) is
`-315.063..315.063 x -19.656..96.928`, ascent 0.932 and descent 0.189 em. Paragraph text
(`Mode` 1): the box is `[MinX, MaxX] x [MinY, MaxY]` wherever it is (the geometry builder
`FUN_1804d94e0` renders a `Width x Height` texture at `MinX, MinY`), lines start at the left
indent and align in `Width - Li - Ri`, and `VerticalAlignment` puts the first baseline at
`Height - Top` (Top), `Bottom + textHeight/2 + (Height - Top - Bottom)/2` (Middle) or
`Bottom + textHeight` (Bottom) from the box bottom, minus the first line's ascent. The port
had point text aligned in a frame-sized box, so a Top-aligned point line (project_1's "Very
D.I.S.C.O") was drawn at the top of the frame; `render/TextRender` now follows the rules above
and `TextStyle::paragraphOffset` keeps the paragraph box's place.

Saving (October 2026): a project opened from a file is written over that file's document
(`project/VegfxMerge`) — the port replaces only what it models and keeps everything else
(materials, shadows, AO, text formats, ViewerState, BinFolder, Metadata, Screens,
CompositionTemplate, editor objects). Keyed lists follow the model: assets (composite shots
included) and layers by `<ID>`, editor tracks by `<ID>`, properties by `<Name>`; the port's
`OpenVegas*` extensions are always rewritten.

Several composite shots (October 2026). A project is a list of `CompositionAsset`s in
`AssetList/Assets`; a nested shot is an ordinary `AssetLayer` whose `<AssetID>` is the other
shot's `<ID>` (`AssetLayer` is an `AbstractAssetInstance` in Project.dll, so a media file and a
composite shot are referenced the same way), with `AssetInstanceStart` as the in-point.
`<IsPrimary>` marks the shot VEGAS Pro gets back: "Set Primary Composite Shot"
(`FUN_1407582d0`) clears it on the previous primary and sets it on the chosen one, or clears
it — a project may have none. The open Editor tabs are `<OpenCompositeShots Version="0"
TimelineType TimelineId>` beside `<Project>` with one `<CompositeShot CompositionId Name>`
per tab: the writer is `FUN_140209440` (active timeline from ProjectMetadata 11000/11001),
the reader `FUN_140202ab0` (TimelineType only 1000 = editor sequence or 1404 = composite
shot, else "Invalid attribute value"; "Failed to load open composite shot list").
Deleting a shot in Media is `RemoveAssetCmd` (`FUN_1403ce320`, "Remove Asset"): every layer
that instances the asset goes with it in the same undo step.

The port reads every `CompositionAsset`: the primary one (else the first) becomes the root
`Composition` that also carries the project, the others `Composition::compositeShots()`;
`AssetLayer`s naming a shot become nested clips, linked once all shots are read, with links
that would close a loop (or point at the root) left unresolved. Each shot is written back as
its own `CompositionAsset` (root first) and `<OpenCompositeShots>` follows the tabs. Older
port saves kept nested shots in an `OpenVegasCompositions` block of the root shot; it is
still read, never written. The Media panel lists the shots (Open, Composite Shot
Properties..., Set Primary Composite Shot, Delete), New > Composite Shot creates an empty
one ("Composite Shot %1"), and each tab keeps its own playhead (`<CTI>`). Deleting the root shot
hands the project (ID, settings, source document, editor sequence, shot list) to the next shot;
only a project's last shot stays.

Composite shot files. Media's "Save Composite Shot" (`FUN_1407137d0`) refuses a shot whose
layers nest another one ("This composite shot cannot be saved because it contains one or more
embedded composite shots."), proposes `<shot name>.vegfxcs` (" [N]" appended while taken) and
writes it with `FUN_1402c8b10`: root `BiffCompositeShot` (`DAT_141542ab0`) with `Version="1"`,
`AppEdition` (5000) and `AppVersion`; `<Assets>` with every asset the shot's AssetLayers and
model layers use, each serialized as in a project and its paths rewritten by `FUN_1402d5b30`
(relative to the file when relative paths are on, else absolute); then the shot's own
`CompositionAsset`. File > Import > Composite Shot (`FUN_1407175e0`) takes "Composite Shots
(*.vegfxcs *.vegfx)", remembers the folder under `RecentFolders/AddCompShot`, lists the shots
the file holds (`FUN_1402c1840`), warns "No composite shots can be imported from this file."
when there are none, imports a single one directly and asks with `ImportCompositionDialog`
(`FUN_1407880a0`/`FUN_140787100`: "Project Name:", "Select Composite Shots", Cancel/Import)
otherwise. The port reads a `.vegfxcs` by laying it out as a one-shot project, brings the
nested shots and the media along, and gives a shot whose ID the project already has a new one. The composite shot's work
area is `<InPoint>`/`<OutPoint>` (older port saves wrote `In`/`Out`, still read), its length is
`AudioVideoSettings/FrameCount` even when a layer runs past it, and `Project/Name` is the file
name. Auto-saves follow FUN_1402cd290: `<base>.vegfx.autosave<N>` in `Options/AutoSavePath`.

Field semantics confirmed against `project_1` (the port reads and writes them natively):

- `ParentLayerID` — null GUID for a root layer; `Muted`, `Locked`, `MotionBlurOn` — 0/1.
  Older port files also carry `OpenVegasParentLayerID`/`OpenVegasLocked`/`OpenVegasMuted`,
  which still win when present.
- `AssetInstanceStart` — the media frame (at the composition's rate) shown at the layer's
  `StartFrame`, i.e. the source in-point. The music layer has 1853 (30.9 s at 60 fps), speed
  `1.0146` and 15376 frames, so it ends 290.9 s into the song — by the asset's
  `OutPoint` 8740 at 30 fps (291.3 s).
- PropertyManager `speed` (`<db>`) — playback rate of the asset.
- PropertyManager `audioLevel` (`<fl>`, dB) — the layer's Audio › Level. Its `<Animation>` key
  times (`Ti`, ms) are composition time like every other layer property, not media time: the
  music layer fades 0 → −60 dB over 29.70–30.05 s of its 30 s shot; its fade-in keys sit at
  −30.89/−30.40 s, where they landed when the layer was slid left by its in-point.
  `FUN_14033f3b0`/`FUN_140340e10` write `audioLevel` keys at `BiffTime::Milliseconds` times
  under a "Level" label — probably the editor's fade handles; the −60 dB floor itself was
  not found there as a constant.
PropertyManager: animatable props `<position>`, `<scale>`, `<opacity>`, `<rotationY>`, `aoSampleRadius`, `<anchorPoint>`
each `@Type=0/1 @Spatial=0 @CanInterpT=1`, `Name`, `Default`, `Static` (or `Animation`) with typed values
(`<p3 X Y Z>`, `<fl>`, `<i>`, `<sc X Y Z>`); animation is keyframes `<Key @STp= @Ti= @Tp= @V=>`
with `<TLk>/<SLk>/<InInf>/<OuInf>/<TInPt>/<TOuPt>/<SInPt>/<SOuPt>`.

### 7.5 Import mapping (VegfxSerializer)
| .vegfx | OpenVegas model |
|---|---|
| CompositionAsset/Name | Composition::setName |
| AudioVideoSettings Width/Height | setSize |
| AudioVideoSettings FrameRate | setFrameRate |
| AudioVideoSettings FrameCount | setDurationSeconds (frame/fps) |
| AssetLayer/AssetID → Assets/Filename | MediaManager::importFile |
| each layer StartFrame/EndFrame | Clip startSeconds = Start/fps, duration = (End-Start)/fps |
| BlendMode 0 | Layer.blendMode = "Normal" |
| TextLayer | Layer + Clip with a "Text" Effect (mediaId `media:text`) |
| layer Name collision | deduplicated ("Name 2", "Name 3", ...) since model keys layers by name |
| LayerBase PropertyManager opacity (0-100) | Layer.opacity (0.0-1.0), reads `<Static>` then `<Default>` |
| MediaAsset InPoint/OutPoint (frames) | MediaAsset::setTrimInPoint / setTrimOutPoint (int frames) |

### 7.6 Trimmer & Export panels (types 2050 / 2057)
- **TrimmerPanel** (`src/ui/TrimmerPanel.{h,cpp}`, object `trimmer-panel`, title "Trimmer"): inner
  `TrimmerWidget` (object `trimmer-frame`) paints a clip strip with in/out markers + scrub playhead,
  drop-down asset list, `In`/`Out`/`Insert`/`Overlay` toolbar (`setTrimInPoint`/`setTrimOutPoint`
  slots write back into the widget at 30 fps), and a bottom scrub `QSlider`. Signals
  `insertRequested`/`overlayRequested` emit the asset path. `Ctrl+T` = "Open in Trimmer"
  (reference shortcut, recovered via RE) opens the selected media asset; MediaPanel double-click also
  routes into the Trimmer.
- **ExportPanel** (`src/ui/ExportPanel.{h,cpp}`, object `export-panel`, title "Export"): output
  directory (`lineEditExportDirectory`/`toolButtonExportDirectory`) persisted to `QSettings`
  `Export/ExportDirectory` (reference setting), format combo (PNG/JPEG/OpenEXR/ProRes) and
  `Export Frame`/`Export Contents` buttons. `exportFrameRequested(path)` renders the current viewer
  frame to `frame-yyyyMMdd-hhmmss.<ext>`.
- Trimmer in/out points are **persisted** as `MediaAsset` `<InPoint>`/`<OutPoint>` (frames) on save and
  restored on load via `MediaManager::assetByFilePathForEdit`; frames assumed 30 fps per the
  reference `FrameRate` default.

Verified: Trimmer + Export compile and link under both CMake (MinGW) and qmake (MSVC) builds.

Verified: loading the sample yields 5 layers (2 text + 3 assets), 1080x1920 @ 60fps. Layer opacity
round-trips load→save→load. The `ProjectSerializer`/`.ovproj` (JSON) format was **removed**; File →
New / Open / Save / Save As all use native `.vegfx` via `VegfxSerializer`.

### 7.7 EditorSequence (timeline: video/audio tracks)
The legacy top-level `<Root>` → `<Editor @Version="5">` block maps to `EditorSequence`
(`src/composition/EditorSequence.{h,cpp}`), holding `SequenceTrack`s (video/audio) plus the audio
master bus. Both `loadFromFile` and `saveToFile` in `VegfxSerializer.cpp` handle it.

```
<Editor @Version="5">
 ├─ ID, Name, CTI, InPoint, OutPoint, <VideoPreviewSize>/<AudioPreviewSize>, PreviewMode
 ├─ VideoTracksCount, AudioTracksCount
 ├─ AudioVideoSettings: FrameCount, AudioSampleRate, Width, Height, FrameRate
 ├─ RenderSettings
 ├─ <Video><VideoTrack @Version="3">: ID, Name, Muted, Solo, Locked</Video>
 ├─ <Audio><AudioTrack @Version="3">: ID, Name, Muted, Solo, Locked
 │     └─ PropertyManager @Version="7" → Prop audioLevel/stereoBalance (<fl>)
 └─ <AudioMaster>: Name, <PropertyManager> → Prop audioLevel/stereoBalance
```

- `EditorSequence::fps` is the timeline frame rate (sample: `59.940`, i.e. 59.94fps).
- `frameCount`/`outPoint` derive from `AudioVideoSettings/FrameCount` (sample: `17982`).
- Audio properties are stored in per-track `PropertyManager`: `<fl>` for `audioLevel` and `stereoBalance`.
- `writeTrack` (lines ~663+) emits `AudioTrack` with `Version`, `ID`, `Name`, `Muted`, `Solo`,
  `Locked`, then `PropertyManager` with `Prop` entries for `audioLevel` / `stereoBalance`.

**Round-trip** (load → mutate → save → reload) verified with a throwaway harness
(`%TEMP%\opencode\vgtest\rt_main.cpp` + `rt.exe`, clang++/MinGW Qt):
- Load sample: `EditorSequence name=Editor fps=59.940 frameCount=17982 outPoint=17982`,
  `videoTracks=1 audioTracks=1 master=Master`, `V1 name=Video 1`, `A1 name=Audio 1 muted=0 solo=0`.
- Mutate `audioTracks[0]`: `muted=1`, `audioLevel=-6` → saved file contains
  `<AudioTrack ...><Muted>1</Muted>...<Prop><Name>audioLevel</Name><Default><fl>-6.000</fl>`
  and `stereoBalance <fl>0.000</fl>` — matches reference encoding.
- Both builds (CMake MinGW + qmake MSVC) compile/link green with the current `VegfxSerializer.cpp`.

### 7.8 Объектная модель `biff::project` (RTTI-интерфейсы слоёв/композиции)
Чистый срез публичных интерфейсов из RTTI-искажений функций (см. `?X@AbstractLayer@project@biff@@`
и родственные). Это интерфейсная поверхность для clean-room; исполнение каждого метода НЕ копируется
(извлекаются только имена/сигнатуры и структура). Полностью согласуется с моделью `.vegfx` (раздел 7)
и нашей `src/composition/*`.

**`AbstractLayer`** (`?X@AbstractLayer@project@biff@@`, базовый слой):
- выборка: `Name()→u16`, `ID()/ParentLayerID()→FXID`, `StartFrame()/EndFrame()/Length()→int`,
  `IsVisible()/IsMuted()/IsMotionBlurEnabled()/HasParentLayer()→bool`,
  `BlendMode()→project::Blend`, `Label()→LabelNumber`,
  `Properties()→shared_ptr<PropertyManager>`, `BehaviorEffects()→ListChangeObserver<EffectInstance>`,
  `ParentLayer(ParentComposition()/ParentComposition())`, `WorldTransformationAtTime(→FXMatrix)`,
  `PlayheadRelativeTime()/RelativeTimeToRelativeFrame()`, `LayerPickerPropertyNames()`.
- изменение: `SetName(u16)`, `SetVisible(bool)`, `SetMuted(bool)`, `SetStartFrame(int)`, `SetEndFrame(int)`,
  `SetMotionBlur(bool)`, `SetBlendMode(Blend)`, `SetLabel(LabelNumber)`, `SetParentComposition(...)`.
- `CompositionAsset::{AddLayer(LayerPtr), Layers()→ListChangeObserver<LayerPtr>,
  RenderSettings()→CompositionRenderSettings, ActiveCameraLayer(frame)→CameraLayerPtr}`.

**`CameraLayer`** (`Create(name,MMMM)`; add `CameraLayerGroupFactory`): `HasDepthOfField/NearClip/FarClip
(float)`, `FocusDistanceLayerID`, `SetDepthOfField/SetNearClip/SetFarClip/SetFocusDistanceLayer`,
`HorizontalFilmSize/VerticalFilmSize/Set*`, `LensZoomInPixels`, FOV/FL-calcs (`CalculateVerticalFOV/
HorizontalFOV/FocalLength/FStop/LensZoom/Aperture/PixelsPerMM`), `FocusDistance(frame, Project)`;
UI `viewer::CameraLayerControl`, `common::CameraLayerInfo`, `ui::EditCameraLayerCmd`,
`viewer::{FlowLayout, ...}`.

**`LightLayer`** (`Create(name, LightType)`; `LightLayerGroupFactory`):
`LightSourceType()→LightLayer::LightType`, `SetLightSourceType`, enums `LightType`/`FalloffType`;
UI `viewer::LightLayerControl`.

**`GradeLayer`** (`Create(name,H,W)`): `Width()/Height()→int`.

**`TextLayer`** (`Create(name,H,W, TextMode)`, `TextRefLayer::CreateTextLayer`):
`Text()→TextBox`, `Width()/Height()→float`, `IncludeInDepthMap/SetIncludeInDepthMap(bool)`; UI
`viewer::TextLayerControl`, `ui::EditTextLayerCmd`.

**`PointLayer`** (`Create(name)`): tracker/point-control; UI `viewer::PointLayerControl`.

**`EffectInstance`** (`Create(AbstractPluginManager, AbstractPlugin)`, `CreateFromXml`, `Copy`):
`Name/SetName(u16)`, `Enabled/SetEnabled(bool)`, `EffectInstanceData()`,
`ParentID()/ParentTimelineID()→FXID`, `SetParent(FXID, FXID, PluginLocation)/ClearParent`,
`UI()→AbstractPluginUITreeNode`, `LayerPickerPropertyNames/MaskPickerPropertyNames` +
`ChangeLayerPickerValues/ChangeMaskPickerValues/ClearInvalid...`, `EffectsHelper::RefreshParentOfEffectInstances`;
`AbstractSequenceObject::Effects()` и `Project::ScopeEffects()` — списки `EffectInstance`.
Наш аналог — `Effect + EffectSpec` (`src/plugin/*`), выборка эффектов.

**Сопутствующие типы**: `LayerPtr, AssetPtr, AssetInstanceList, TrackerPtr, TransitionPtr,
SequenceObjectPtr, SequenceTrackPtr, VideoTrackPtr, AudioTrackPtr, Model3DAssetPtr`,
`CompositionRenderSettings::{SetFogEnabled/NearDistance/FarDistance/Density/Falloff/Color,
SetMotionBlurEnabled, SetShutter...}` (ср. `<RenderSettings>` в 7.3), `PanelRenderState::ViewType`,
`Flux::{SetComposition, RenderLayer, RenderFrame, StartPlayback}`, `PlaybackEngine::Initialize`.
UI-фабрики новых слоёв: `CameraLayerGroupFactory`, `LightLayerGroupFactory`.

## 8. Вспомогательные процессы & multiprocess рендеринг

Пакет `SAMPLES/VEGAS_Effects/` содержит не только `VegasEffects.exe` (GUI, 23 МБ), но и набор
вспомогательных exe, образующих распределённую архитектуру (PE x64, кроме старого 32-битного
QuickTime-сервера):

| exe | биты | QModules | media/render DLL | роль |
|---|---|---|---|---|
| `VegasEffects.exe` | x64 | все GUI | Flux/Fusion/Marvin/Project/Tannen/FXMedia + `Widgets.dll` | главный (GUI + рендер в фоне) |
| `VegasEffectsServer.exe` | x64 | Core/Gui/Widgets | Flux/Fusion/Marvin/Project/Tannen/FXMedia, glew32, OpenGL, `libcrypto` | «VEGAS Effects Server for Vegas Pro integration» — внутренний локальный сервис |
| `VegasEffectsRenderClient.exe` | x64 | Core/Gui/Widgets/**Network**/Xml/WinExtras | Flux/Fusion/Marvin/Project/Tannen/FXMedia + `Widgets.dll` | вынесенный рендер-клиент (22 МБ), телеметрия `frame_added/used/rate` |
| `FXMediaTranscoder.exe` | x64 | Core | **FXMedia.dll**, Media Foundation (MFPlat/MFReadWrite/d3d11) | транскодинг медиа через Media Foundation |
| `FXMediaQTServer.exe` | **i386** | — | FXMedia (32-бит) | устаревший QuickTime-сервер |
| `VegasEffectsOFXSandbox.exe` | x64 | Widgets | Project/Tannen | изолированная песочница OFX-плагинов |
| `BsSndRpt64.exe` | x64 | — | dbghelp, WININET (BugSplat) | crash-report |
| `QtWebEngineProcess.exe` | x64 | WebEngine (Chromium) | — | стандартный Qt5 helper-процесс WebEngine (запускается `VegasEffects.exe` для HTML-поверхностей) |

### 8.1 IPC (named-pipe)
- Хост IPC-модуля exporter: класс `biff::ui::exporter::LocalServer` (анонимный ns `0x09a75817`),
  на `QLocalServer`/`QLocalSocket` + `PeekNamedPipe` (проверка пустоты pipe).
- Главный exe запускает `VegasEffectsServer`/`VegasEffectsRenderClient` через `QProcess`
  (`setProgram`/`setArguments`/`startDetached`/`terminate`/`kill`).
- Точки подключения: `/VegasEffectsRenderClient`, `/HitFilmRenderClient`.
- Лог: именованный лог `VegasProServerLog`.

### 8.2 Объектная модель (из RTTI-манглинга Server/RenderClient)
Имена подтверждают и точно совпадают с `.vegfx`-анализом (раздел 7) и с нашей чистой моделью:
- `biff::project::Project::CreateFromXml(XmlNode, AbstractPluginManager, Abstract3DEffectEngine, MediaManager::CreateOption)`,
  `Project::CreateProject(TimelineType, CreateOption)`, `PrimaryCompositionAsset()`, `Sequence()`.
- `biff::project::CompositionAsset::Create(name, AudioVideoSettings)`, `Layers()` (ListChangeObserver), `Template()`,
  `SetParentComposition`; `EditorSequence::VideoTracks()`, `IsEmpty()`.
- `biff::project::AbstractSequenceObject::{SetParentSequenceID,SetEndFrame}`; `SequenceObjectList::Insert`.
- `biff::project::MediaAsset::{AudioStream(),VFRCacheFileStatus(),SetVFRCacheFile}`;
  `biff::marvin::{MediaCacheFolderPath, OpenMediaDatabaseConnection, SetIsExport, GetSamples(AudioMixer)}`.
- Рендер `biff::flux::Flux::{Create(gl::FXRenderContext, FontCache,…), RenderFrame(EditorSequence|CompositionAsset,
  frameIndex, FrameRequest), SetOfflineMediaImage, Renderer(), FrameBufferSize(H,W,FXChannelOrder)}`.
- Медиа `fxh::media::{FXMediaStartup(FXMediaStartupOptions), FXMediaShutdown, FXChannelOrder}`.
- Экспорт: `OpenEXR Export`, `ProRes Export`, `Export Camera`, `Pre-Rendering`, `OpenProjectFile`.
- Водяные знаки: `:/watermarks/VegasEffects/{viewer-watermark.png, export-watermark.png, watermark-audio.wav}`.
- Ребрендинг: исходный проект **FXhome HitFilm** (путь PDB `...\GitLab-Runner\...\FXhome\hitfilm\HitFilm\x64\Release\`),
  MAGIX/MA-FX — VEGAS Effects.

### 8.3 Вывод для clean-room
- Рендер в главном процессе — кадровый (`Flux::RenderFrame(...frameIndex, FrameRequest)`), точно как наш
  `render::RenderManager::requestFrame(frameIndex, timeSeconds, size)` + `QThread`-`RenderWorker`.
- Отдельные процессы (Server/RenderClient/Transcoder/OFXSandbox) нужны главным образом для изоляции:
  рендера (не блокировать GUI), OFX-плагинов (песочница) и Media Foundation транскодинга.
- Для clean-room порта необязательно дублировать мультипроцессность: достаточно встроенного рендера на
  `QThread` (уже реализовано) и межпоточной публикации кадров в Viewer.


### 8.4 Palitra (alignment with pixel reference)
- Palitra privedena v sootvetstvie s SAMPLES/screenshots/0..5.md (all 6 reference one scheme):
  - app #1E1E1E, panels #252526, modal #2D2D30, selection #4A6A8A, accent/focus #007ACC.
  - text #CCCCCC, headers #FFFFFF, playhead #FFFFFF, timeline clip #6A8AAA/#3C3C3C, green "+" #4CAF50.
- Applied in Theme.h (ThemeColors) and TimelineWidget/ViewerWidget (playhead/accent).
