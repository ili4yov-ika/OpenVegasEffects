# OpenVegas Effects

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

![Главное окно](docs/main_ui.png)

Открытая (clean-room) реализация приложения для композитинга и эффектов в стиле
**VEGAS Effects** на **C++17 / Qt 6** под лицензией **GNU GPL v3**.

Код написан с нуля по поведенческой архитектуре, восстановленной при реверс-инжиниринге
эталонных бинарников (см. [`MARKDOWN/RE_VegasEffects.md`](MARKDOWN/RE_VegasEffects.md)).
Это **оригинальный код**: проприетарные исходники не копируются. Раскладка модулей
повторяет наблюдаемую высокоуровневую структуру эталона, чтобы проект вырастал в
совместимый рабочий процесс, но каждая строка написана заново.

Текущее состояние: **на Windows работает бо́льшая часть рабочего процесса эталона** —
проекты в формате эталона `.vegfx`, таймлайн с вложенными композитными кадрами, 2D- и
3D-слои, текст, модели, маски и трекинг, нативные плагины VEGAS Effects `.hfpl`, звук и
экспорт. Открытые пункты — в [`MARKDOWN/CHECKLIST.md`](MARKDOWN/CHECKLIST.md).

---

## Что уже работает

| Область | Состояние |
|---------|-----------|
| Проекты: формат эталона `.vegfx` (чтение и запись), относительные пути, автосохранение и восстановление | Готово |
| Таймлайн: композитные кадры, вложенные кадры во вкладках, 2D/3D-слои, клипы, нарезка и растяжение скорости, ключевые кадры и график значений, переходы | Готово |
| Нативные плагины VEGAS Effects `.hfpl`: работают все 321 модуль (241 2D-эффект, 43 Behavior, 16 аудиоэффектов, 15 видеопереходов, 4 Geometry, 2 аудиоперехода); их элементы управления, собственный интерфейс во Вьювере и пресеты `.hfpr` | Готово |
| Текстовые слои: стили, 3D-выдавливание, побуквенные Behavior, панель Text | Готово |
| 3D: модели (OBJ, FBX со скинами и blend shapes, glTF, 3DS, LWO, Alembic по желанию), камеры, свет, тени, глубина по пикселям | Готово |
| Маски, трекинг движения (MotionTrack), стабилизация | Готово |
| Документы Photoshop (`.psd`, `.psb`) по слоям: режимы наложения, группы, маски, обтравка, стили слоя, корректирующие слои, смарт-объекты со смарт-фильтрами Camera Raw и «Размытие по Гауссу» как редактируемые эффекты | Готово |
| Вьювер с оверлеями и уменьшенным превью, 360-вьювер, панели Layer, Trimmer и Layout | Готово |
| Звук: воспроизведение через libVLC, общий микс, Audio Meters, нативные аудиоэффекты в реальном времени, запись закадрового голоса | Готово на Windows |
| Экспорт: видео, последовательности кадров и звук через FFmpeg, очередь экспорта и пресеты, OpenEXR (по желанию) | Готово |
| Прокси, предварительные рендеры, кэш медиа, дисковый кэш кадров | Готово |
| Интерфейс на русском, японском и упрощённом китайском, включая строки плагинов | Готово |
| Установщик для Windows (NSIS); пакеты `.deb` и `.rpm` | Windows проверен; пакеты для Linux не проверены |
| Звук и VLC 4 на macOS/Linux | Не проверено |

Подробности — в [`MARKDOWN/ARCHITECTURE.md`](MARKDOWN/ARCHITECTURE.md) и заметках по
реверс-инжинирингу в [`MARKDOWN/`](MARKDOWN/).

---

## Стек

| Слой | Выбор |
|------|-------|
| Язык / UI | C++17, Qt 6 (Widgets, OpenGL; формы в `ui/`, остальное собирается в коде) |
| Сборка | CMake 3.21+ (предпочтительно) и `OpenVegasEffects.pro` |
| Компиляторы | MSVC 2022 (основной), MinGW / LLVM-MinGW, GCC, Clang |
| Медиа | **libVLC** — кадры видео, общий PCM-микс, вывод звука и запись закадрового голоса |
| Экспорт, прокси, волновые формы | исполняемый файл **FFmpeg** из `PATH` |
| Кэш | QtSql (SQLite `cache.db`) и дисковый кэш кадров |
| Плагины | Хост для нативных модулей VEGAS Effects `.hfpl` (OpenGL 4.1) |
| Лицензия | GNU GPL v3 ([LICENSE](LICENSE)) |

---

## Сборка и запуск

Нужен **Qt 6.8+** (Core/Gui/Widgets/OpenGLWidgets/Sql/Svg/Xml; LinguistTools — для
переводов). Qt WebEngine не нужен. Если на Windows линковка падает из-за занятого
файла, перед пересборкой закройте `OpenVegasEffects.exe`.

### Нативные плагины (`.hfpl`)

Эффекты, переходы, аудиоэффекты, Behavior и модули Geometry — это собственные файлы
эталона `.hfpl`, которые исполняет хост порта. В репозитории их нет. Приложение находит
их в установленном VEGAS Effects (`C:\Program Files\FXhome\VEGAS Effects\Plugins`) или в
папках, выбранных в **Настройки → Плагины** (подходят и плагины другой программы FXhome).
Без них остаются доступны встроенные эффекты.

### libVLC (медиа)

Qt Multimedia не используется. libVLC декодирует видео и звук, выводит общий PCM-микс и
записывает микрофон. Микшер учитывает интервалы клипов, отключение звука, смещение
источника, скорость, уровень и вложенные композиции; Audio Meters измеряет тот же
итоговый PCM. Изменение скорости — ресэмплинг со сдвигом высоты тона. Нативные
аудиоэффекты и аудиопереходы `.hfpl` работают и в микшере реального времени, и при
экспорте звука.

Заголовки и исходники VLC лежат в `thirdparty/vlc`. Это дерево исходников, а не готовый
рантайм. Адаптер `Vlc4Adapter.cpp` использует его заголовки и проверяет ABI VLC 4;
для VLC 3 — отдельные совместимые сигнатуры. Запуск проверен с VLC 3.0.24 для Windows;
адаптер VLC 4 собирается, но запуск с VLC 4 и проверка на macOS/Linux ещё впереди.

Для упаковки подготовьте рантайм под целевую архитектуру в
`thirdparty/vlc/runtime/Windows`, `Darwin` или `Linux`: библиотеки вместе с их
зависимостями и плагинами. Можно указать и другой префикс опцией CMake
`-DOPENVEGAS_VLC_RUNTIME_DIR=<prefix>`; qmake читает одноимённую переменную окружения.
На Windows без подготовленного рантайма берётся установленный VLC из Program Files.
Обе сборки копируют рантайм в `vlc` рядом с EXE; установка CMake сохраняет эту
структуру, а NSIS проверяет наличие DLL и плагинов. Вместе с ними поставляются
`COPYING` и `COPYING.LIB`.

Загрузчик ищет библиотеки в `OPENVEGAS_VLC_DIR` (переопределение во время работы), рядом
с приложением, в настроенном префиксе и в системных каталогах. Поддерживаются DLL
Windows, dylib macOS и SONAME Linux; библиотека импорта не нужна. Без доступного
libVLC приложение всё равно запускается, но медиа и запись недоступны. Запись идёт через
DirectShow, qtsound или PulseAudio; на Linux устройства перечисляются через `pactl`
(в том числе совместимость PipeWire Pulse). Запись с устройства и вывод звука нужно
отдельно проверить на целевых ОС; тесты проверяют PCM и состояние плеера VLC.

### Qt Creator (qmake)

Откройте [`OpenVegasEffects.pro`](OpenVegasEffects.pro), кит **Qt 6.8+ Widgets**
(MSVC / MinGW / Clang) → Сборка. Держите `SOURCES`/`HEADERS` в соответствии с
`CMakeLists.txt`.

На Windows qmake генерирует актуальные `Makefile.Debug` и `Makefile.Release` для шагов
сборки Qt Creator. Заголовки форм, вывод moc/rcc, объектные файлы и каталоги переводов
разделены по конфигурациям; исполняемый файл лежит в `debug/` или `release/` внутри
теневой сборки. После правки `.pro` выполните **Сборка → Запустить qmake**. Устаревшие
Makefile другой конфигурации могут давать `C1083` для `ui_*.h`, даже если новый
верхний Makefile уже на месте.

### MSVC (пресеты)

Пресеты в [`CMakePresets.json`](CMakePresets.json) — единый источник настроек для IDE:

| Пресет | Инструменты | Каталог |
|--------|-------------|---------|
| `windows-msvc-debug` | MSVC v143 (VS 2022) + Qt `msvc2022_64` | `build/Windows_MSVC-x64/` |
| `windows-msvc-release` | то же, RelWithDebInfo | `build/Windows_MSVC-x64-Release/` |
| `windows-mingw-debug` / `-release` | MinGW GCC + Qt `mingw_64` | `build/Windows_MinGW-x64/` |
| `linux-debug` / `-release`, `macos-debug` / `-release` | системный компилятор + Ninja | `build/linux/`, `build/macos/` |

Из окна **x64 Native Tools Command Prompt for VS 2022**:

```bat
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug --parallel
```

Из обычного терминала то же делает `tools\msvc_build.cmd`, который сам загружает
окружение MSVC:

```bat
tools\msvc_build.cmd build windows-msvc-debug
```

Скрипт можно запускать из любого каталога. Без аргументов он выполняет
`buildall windows-msvc-debug` (настройку и сборку); доступны также `configure`, `build`,
`probe` и `clean`. Например, `tools\msvc_build.cmd clean windows-msvc-release` очищает
через CMake только результаты Release, сохраняя настройку сборки.

### CMake с MinGW

```bash
cmake -S . -B build -G "MinGW Makefiles" ^
  -DCMAKE_PREFIX_PATH=C:/Qt/6.9.3/llvm-mingw_64 ^
  -DCMAKE_C_COMPILER=C:/Qt/Tools/llvm-mingw1706_64/bin/clang.exe ^
  -DCMAKE_CXX_COMPILER=C:/Qt/Tools/llvm-mingw1706_64/bin/clang++.exe ^
  -DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/llvm-mingw1706_64/bin/mingw32-make.exe
cmake --build build -j 8
```

### Linux и macOS

`tools/build_deb.sh` и `tools/build_rpm.sh` собирают пакет `openvegaseffects` (`.deb`
через dpkg-buildpackage, `.rpm` через rpmbuild); пакет содержит приложение, его
ярлык и значок. На macOS `tools/setup_macos.sh` ставит Qt 6, CMake и Ninja через
Homebrew, а `tools/macos_build.sh [debug|release] [test] [deploy]` собирает по
пресетам. Эти пути пока не проверены.

### Установщик для Windows

Установщик собирает один скрипт, его можно запускать из любой папки:

```bat
tools\build_windows_installer.bat
```

Он сам находит Visual Studio 2022 (vswhere) и кит Qt `msvc2022_64`, собирает Release
через Ninja в `build\windows-installer`, устанавливает его в чистый префикс,
раскладывает Qt через windeployqt, кладёт рядом с EXE DLL рантайма MSVC и пишет
`tools\OpenVegasEffects_Setup.exe`. `CMAKE_PREFIX_PATH` выбирает другой кит Qt,
`OPENVEGAS_VLC_RUNTIME_DIR` — другой рантайм libVLC; дерево сборки, настроенное
другим генератором, компилятором или китом Qt, создаётся заново.

Вручную это те же шаги (строке `copy` нужна Developer Command Prompt for VS 2022,
которая задаёт `VCToolsRedistDir`). Из корня проекта:

```bat
tools\msvc_build.cmd buildall windows-msvc-release
cmake --install build\Windows_MSVC-x64-Release --prefix build\installer-stage
"C:\Qt\6.9.3\msvc2022_64\bin\windeployqt.exe" --release --no-compiler-runtime build\installer-stage\bin\OpenVegasEffects.exe
copy "%VCToolsRedistDir%x64\Microsoft.VC143.CRT\*.dll" build\installer-stage\bin\
"C:\Program Files (x86)\NSIS\makensis.exe" /DBUILD_DIR=build\installer-stage /DOUTPUT_FILE=build\OpenVegasEffects_Setup.exe tools\nsis_installer.nsi
```

NSIS требует подготовленный `bin` с приложением и `platforms\qwindows.dll`.
Во время компиляции `tools/nsis_payload.ps1` через Windows PowerShell составляет
список файлов, включая вложенные ресурсы Qt и плагины, добавленные в префикс.
Удаление использует этот список и убирает только опустевшие каталоги.

Файлы проектов `.vegfx` показываются со значком `resources/icons/iconfile.svg`: он
встроен в EXE вторым значком (индекс 1, `resources/icons/iconfile.ico`, пересоздаётся
командой `python tools/make_file_icon.py`). Установщик ссылается на него в
`DefaultIcon` ProgID `OpenVegasEffects.Project`, а программа при запуске регистрирует
тот же ProgID, `OpenWithProgids` и `Applications\OpenVegasEffects.exe` для текущего
пользователя (`app/FileAssociation`) — поэтому значок появляется и у портативной
сборки, когда её выбирают программой по умолчанию («Открыть с помощью → Всегда»).
Программой по умолчанию для `.vegfx` она себя сама не назначает.

### Тесты и CI

Регрессионные наборы — отдельный проект CMake в [`tests/`](tests/), см.
[`tests/README.ru.md`](tests/README.ru.md). При пушах в `main`, пул-реквестах и ручном
запуске workflow [CI](.github/workflows/ci.yml) собирает приложение с MSVC 2022 и
Qt 6.9.3, прогоняет шесть наборов через CTest и проверяет каталоги переводов. Аудиотесты
используют libVLC 3.0.24 с официального сервера VideoLAN (архив закреплён по SHA-256) и
вывод `dummy`, поэтому проверяют декодирование и общий микс без звукового устройства на
раннере. Тестам, которые запускают нативные плагины, нужен контекст OpenGL 4.1; без него
они пропускаются.

### Панель Learn (без Qt WebEngine)

Эталон показывает онлайн-уроки VEGAS в Home/Learn и веб-библиотеку через Qt WebEngine.
В порте уроков нет, а Library — нативная панель, поэтому WebEngine не используется:
панель **Learn** сделана из обычных виджетов с теми же действиями (уроки в системном
браузере, новый композитный кадр, импорт медиа). Она включается через **Окно →
Панель обучения**, состояние запоминается (`learnSidebarIsOpen`). Справка тоже
открывается в системном браузере.

### OpenEXR (экспорт кадров в `.exr`)

В Qt нет плагина записи EXR, поэтому пресет **OpenEXR (.exr)** в панели Export работает
только в сборке с OpenEXR. По умолчанию он выключен:

```bat
cmake --preset windows-msvc-debug -DOPENVEGAS_WITH_OPENEXR=ON
```

Порядок поиска: сначала установленный `find_package(OpenEXR 3 CONFIG)`, иначе исходники
из [`thirdparty/openexr`](thirdparty/openexr).

**Оговорка о зависимости.** Рядом с `thirdparty/openexr` нет Imath, поэтому OpenEXR в
CMake скачивает его с GitHub (ветка `main`) прямо во время настройки — сборка перестаёт
быть офлайновой и воспроизводимой. Сам склонированный OpenEXR — версия `4.0.0-dev` из
`main`, тогда как эталон поставляет стабильную 3.1. Для воспроизводимости стоит
закрепить релизные теги и положить Imath рядом.

Без этой опции экспорт в `.exr` не падает молча, а сообщает, что в сборке нет OpenEXR.
Импорт Alembic (`.abc`) включается так же — `-DOPENVEGAS_WITH_ALEMBIC=ON` из
[`thirdparty/alembic`](thirdparty/alembic).

### VS Code и Zed (отладка Qt MSVC)

Обе IDE настроены на Qt `msvc2022_64` и сами подхватывают окружение MSVC — отдельная
командная строка разработчика не нужна; задачи сборки вызывают `vcvars64.bat` (через
`vswhere -version "[17.0,18.0)"`, чтобы не подхватить параллельно установленную VS 18,
чьи инструменты не подходят к киту Qt).

Для кнопки сборки CMake Tools включено `cmake.useVsDeveloperEnvironment=always`, а
`cmake.buildTask=false` оставляет запуск расширению вместе с окружением пресета. Задачи
`Ctrl+Shift+B` и сборка перед F5 вызывают `tools/msvc_build.cmd`, который сам загружает
окружение MSVC. Для Ninja CMake также генерирует `msvc_environment.cmd`: команды
компиляции и линковки загружают окружение той установки, которой принадлежит выбранный
`cl.exe`. Это предотвращает `STL1001` при смешении компилятора VS 2022 с заголовками
VS 2026, даже если CMake Tools взял окружение более новой Visual Studio. Для
существующего каталога сборки один раз выполните настройку, чтобы обновить правила
Ninja. Запускающий скрипт также включает UTF-8 (`chcp 65001`): иначе локализованные
строки `/showIncludes` могут не совпасть с префиксом CMake, Ninja потеряет зависимости
от заголовков и после изменения классов смешает несовместимые объектные файлы. Если
каталог уже собирался с неверной кодовой страницей, после настройки выполните
`cmake --build build/Windows_MSVC-x64 --target clean` и полную пересборку.

- **VS Code** — [`.vscode/`](.vscode/): `launch.json` (отладчик `cppvsdbg`, читает PDB
  MSVC; конфигурации Debug / RelWithDebInfo / подключения), `tasks.json` (настройка /
  сборка / очистка / запуск), `qt6.natvis` (читаемые `QString`, `QList`, `QRect`… в
  отладчике). Расширения перечислены в `extensions.json`. F5 собирает и запускает.
- **Zed** — [`.zed/debug.json`](.zed/debug.json) (адаптер CodeLLDB, PDB рядом с exe) и
  [`.zed/tasks.json`](.zed/tasks.json) с теми же пресетами. `clangd` индексирует
  `build/Windows_MSVC-x64/compile_commands.json`.

Запуск вне IDE — добавьте `bin` Qt в PATH:

```powershell
$env:PATH = "C:/Qt/6.9.3/msvc2022_64/bin;" + $env:PATH
```

**Не коммитьте** `build/` — он в [`.gitignore`](.gitignore).

---

## Локализация

Интерфейс переведён на русский, японский и упрощённый китайский; исходный язык —
английский.

| Каталог | Строк | Языки |
|---------|------:|-------|
| Интерфейс (`openvegaseffects_<lang>.ts`) | 1905 | ru, ja, zh_CN — переведены все |
| Строки плагинов (`openvegaseffects_plugins_<lang>.ts`) | 6818 | ru, ja, zh_CN |

Японская и китайская терминология взята из собственных каталогов эталона
(`SAMPLES/VEGAS_Effects/Translations/*/VegasEffects_*.qm`): строки с совпадающим
исходным текстом получают формулировки эталона. Каталоги плагинов содержат названия,
категории, подписи элементов управления, единицы, пункты списков и сообщения нативных
плагинов в контекстах эталона `Effect_<ShortName>`; их пишет
[`tools/plugin_translations.py`](tools/plugin_translations.py). Подробности —
[`translations/README.ru.md`](translations/README.ru.md).

### Как это работает

- Каталоги лежат в [`translations/`](translations/); CMake компилирует их через
  `qt_add_translations` и встраивает в бинарник как `:/i18n/`. Нужен компонент Qt
  **LinguistTools**; без него сборка проходит, но интерфейс остаётся английским.
- Загрузка — `app::Translations::install()`, вызывается в `main.cpp` **до** создания
  виджетов.
- Порядок поиска как у эталона: сначала
  `<данные пользователя>/Translations/<locale>/openvegaseffects_<locale>.qm` (можно
  подложить свой каталог), затем встроенный. Рядом загружается собственный каталог Qt
  `qt_<locale>.qm`.
- Язык выбирается в **Настройки → Интерфейс → Язык**, ключ `Options/Language`; пусто —
  как в системе. Применяется при следующем запуске.
- Два теста открывают Настройки и панели на каждом языке и падают на любом тексте, где
  осталось английское слово.

### Обновление каталогов после правки строк

```bat
cmake --build --preset windows-msvc-debug --target update_translations
python tools/validate_translations.py
```

---

## Папки данных

При первом запуске приложение создаёт папки пользовательских данных, повторяя раскладку
эталона (восстановлена по его резолверу `FUN_14025b000`): `Translations`,
`Templates\AV`, `EnvironmentMaps`, `ExportPresets`, `PluginPresets`, `Plugins`,
`Presets`, `Textures`, `Templates\Workspaces`, `Objects`, `Tutorials`. Две из них
вложены в `Templates` — голой папки `Templates` у эталона нет. На Windows корень —
`%LOCALAPPDATA%\OpenVegas\OpenVegasEffects`.

Отличие от эталона намеренное: он только проверяет папки и возвращает пустой путь, если
их нет (дерево раскладывает его установщик), а порт создаёт их сам — и в установленной,
и в портативной сборке.

В пользовательскую папку `Plugins` можно класть и собственные описания эффектов порта
(`.vfx`, JSON); нативные модули `.hfpl` находятся так, как описано выше в разделе
«Нативные плагины».

---

## Структура репозитория

```
OpenVegas_Effects/
├── CMakeLists.txt / OpenVegasEffects.pro / CMakePresets.json
├── src/
│   ├── main.cpp          # вход в стиле WinMain → argv в UTF-8 → QApplication
│   ├── core/             # Identifier, Result, Version, Log
│   ├── app/              # AppMain, пути данных, настройки, переводы, шаблоны
│   ├── license/          # LicenseManager + бесплатный менеджер
│   ├── plugin/           # реестр плагинов и хост нативных модулей .hfpl
│   ├── media/            # импорт медиа, libVLC, звук, прокси, кэш кадров
│   ├── composition/      # композитные кадры, слои, клипы, эффекты, ключи
│   ├── project/          # чтение и запись .vegfx, пресеты эффектов
│   ├── model3d/          # чтение 3D-моделей и 3D-рендерер
│   ├── cache/            # кэш SQLite (QtSql)
│   ├── render/           # рендер кадров, текст, экспорт, предрендеры
│   └── ui/               # MainWindow, панели, диалоги и оверлеи
├── ui/                   # формы Qt Designer (главное окно, панели, диалоги)
├── resources/            # значки, ресурсный файл Windows, файлы macOS
├── translations/         # каталоги .ts (интерфейс и строки плагинов)
├── tests/                # регрессионные наборы (отдельный проект CMake)
├── tools/                # скрипты сборки, установщики, пакеты, помощники для RE
├── MARKDOWN/             # архитектура, чеклист, заметки по реверс-инжинирингу
├── docs/                 # скриншоты
├── thirdparty/           # заголовки VLC, OpenEXR, Alembic (по желанию)
└── SAMPLES/              # эталонные бинарники, материалы RE, примеры проектов (не входят в сборку)
```

`SAMPLES/` содержит эталонные бинарники, материалы реверс-инжиниринга и примеры
проектов (`SAMPLES/vegfx_projects`); в открытую сборку не входит.

---

## Документация

1. **Архитектура и план** — [`MARKDOWN/ARCHITECTURE.md`](MARKDOWN/ARCHITECTURE.md)
2. **Открытые пункты** — [`MARKDOWN/CHECKLIST.md`](MARKDOWN/CHECKLIST.md)
3. **Правила разработки** — [`MARKDOWN/INIT.MD`](MARKDOWN/INIT.MD)
4. **Реверс-инжиниринг эталона VEGAS Effects** — [`MARKDOWN/RE_VegasEffects.md`](MARKDOWN/RE_VegasEffects.md)
5. **Нативные плагины `.hfpl` и их хост** — [`MARKDOWN/RE_HFPL_Plugins.md`](MARKDOWN/RE_HFPL_Plugins.md)
6. **Анализ дистрибутива эталона** — [`MARKDOWN/RE_VegasEffects_Distribution.md`](MARKDOWN/RE_VegasEffects_Distribution.md)
7. **Тесты** — [`tests/README.ru.md`](tests/README.ru.md); **переводы** — [`translations/README.ru.md`](translations/README.ru.md); **формы панелей** — [`ui/panels/README.ru.md`](ui/panels/README.ru.md)

---

## Лицензия

Код в этом репозитории, если не указано иное, распространяется под **GNU GPL v3**.
Полный текст: [`LICENSE`](LICENSE).

Эталонные бинарники в `SAMPLES/` остаются собственностью их правообладателей и хранятся
только в учебных целях.
