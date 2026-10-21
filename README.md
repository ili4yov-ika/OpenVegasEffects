# OpenVegas Effects

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a> |
</p>

![Main Interface](docs/main_ui.png)

An open (clean-room) reimplementation of a compositing / effects application in the
style of **VEGAS Effects**, written in **C++17 / Qt 6** and licensed under **GNU GPL v3**.

Written from scratch based on the behavioral architecture uncovered while reverse
engineering the reference binaries (see `SAMPLES/VEGAS_Effects/MARKDOWN/RE_VegasEffects.md`).
This is **original code**: no proprietary sources are copied. The module layout
mirrors the observable high-level structure of the reference application so the
project can gradually grow into a compatible workflow, but every line is written
anew.

Current state: **an early skeleton** — a docked window, a free license manager, plugin
scan and registry, a composition object model, an SQLite cache, and a placeholder renderer.

---

## What already works

| Area | Status |
|------|--------|
| Entry point (WinMain-style → UTF-8 argv → QApplication) | Done |
| User data folders (11, created on first launch) | Done |
| Settings via QSettings (ini) | Done |
| File logging | Done |
| Free license manager (no activation / servers) | Done |
| PluginManager + registry, native discovery of `.vfx` in `Plugins/` | Done (scan/registry) |
| Composition object model / layers / clips / effects | Skeleton |
| MediaManager / media import | Skeleton |
| SQLite cache (`CacheDB`) | Done |
| Background render worker (placeholder) | Skeleton |
| UI: MainWindow + docks (Effects panel, Viewer, Timeline) | Done (skeleton) |
| Learn sidebar (native widgets, no Qt WebEngine) | Done |

Details and the roadmap are in [`MARKDOWN/ARCHITECTURE.md`](MARKDOWN/ARCHITECTURE.md).

---

## Stack

| Layer | Choice |
|-------|--------|
| Language / UI | C++17, Qt 6 (Widgets, UI built in code) |
| Build | CMake 3.21+ (preferred) and `OpenVegasEffects.pro` |
| Compilers | MSVC 2022, LLVM-MinGW (Clang), MinGW, GCC |
| Media | **libVLC** — video frames, PCM master mix, audio output and voiceover recording |
| Cache | QtSql (SQLite `cache.db`) |
| Plugins | Foundation for a future OFX host |
| License | GNU GPL v3 ([LICENSE](LICENSE)) |

---

## Building and running

**Qt 6.8+** is required (Core/Gui/Widgets/OpenGLWidgets/Sql). Qt WebEngine is not
needed. Before rebuilding on Windows, close `OpenVegas Effects.exe` if linking fails
because the file is in use.

### libVLC (media)

Qt Multimedia is no longer required. libVLC decodes video and audio, outputs the PCM
master mix and captures the microphone. The mixer accounts for clip intervals, mute,
source offset, speed, level and nested compositions; Audio Meters measures the same
final PCM. Speed changes use resampling with pitch shift. Realtime native `.hfpl`
DSP/AudioTransition are not yet connected; audio export keeps a separate
FFmpeg/native DSP path.

The VLC headers and sources live in `thirdparty/vlc`. This is a source tree, not a
ready-to-use runtime. The `Vlc4Adapter.cpp` adapter uses its headers and checks the
VLC 4 ABI; VLC 3 uses separate compatible signatures. In this session, launch was
verified with Windows VLC 3.0.24; the VLC 4 adapter builds, but running with VLC 4
and testing on macOS/Linux are still pending.

For packaging, prepare a runtime for the target architecture in
`thirdparty/vlc/runtime/Windows`, `Darwin` or `Linux`: the libraries together with
their dependencies and plugins. You can also point to a different prefix via the
CMake option `-DOPENVEGAS_VLC_RUNTIME_DIR=<prefix>`; qmake reads the environment
variable of the same name. On Windows, when no prepared runtime is present, the
installed VLC from Program Files is used. Both builds copy the runtime into `vlc`
next to the EXE; the CMake install preserves this structure, and NSIS verifies that
the DLLs and plugins are present. `COPYING` and `COPYING.LIB` are shipped as well.

The loader looks for the libraries in `OPENVEGAS_VLC_DIR` (a runtime override), next
to the application, in the configured prefix and in the system directories. Windows
DLLs, macOS dylibs and Linux SONAMEs are supported; no import library is needed.
Without an available libVLC the application still starts, but media and recording are
unavailable. Recording uses DirectShow, qtsound or PulseAudio; on Linux devices are
enumerated via `pactl` (also PipeWire Pulse compatibility). After switching away from
Qt, the microphone must be selected again in Options: the old base64 device IDs do
not match the new IDs. Hardware recording and audio output require separate
verification on the target OSes; the tests check PCM and the VLC player state.

### Qt Creator (qmake)

Open [`OpenVegasEffects.pro`](OpenVegasEffects.pro), kit **Qt 6.8+ Widgets**
(MSVC / MinGW / Clang) → Build. Keep `SOURCES`/`HEADERS` in sync with
`CMakeLists.txt`.

On Windows, qmake generates up-to-date `Makefile.Debug` and `Makefile.Release` for
Qt Creator's build steps. Form headers, moc/rcc output, object files and translation
directories are separated by configuration; the executable lives in `debug/` or
`release/` inside the shadow build. After editing `.pro`, run **Build → Run qmake**.
Stale Makefiles from a different configuration may cause `C1083` for `QAudioBuffer`
and `ui_*.h`, even if the new top-level Makefile is already in place.

### CMake

```bash
# LLVM-MinGW (recommended, no MSVC needed)
cmake -S . -B build -G "MinGW Makefiles" ^
  -DCMAKE_PREFIX_PATH=C:/Qt/6.9.3/llvm-mingw_64 ^
  -DCMAKE_C_COMPILER=C:/Qt/Tools/llvm-mingw1706_64/bin/clang.exe ^
  -DCMAKE_CXX_COMPILER=C:/Qt/Tools/llvm-mingw1706_64/bin/clang++.exe ^
  -DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/llvm-mingw1706_64/bin/mingw32-make.exe
cmake --build build -j 8
```

### MSVC (presets)

The presets in [`CMakePresets.json`](CMakePresets.json) are the single source of truth for both IDEs:

| Preset | Toolchain | Directory |
|--------|-----------|-----------|
| `windows-msvc-debug` | MSVC v143 (VS 2022) + Qt `msvc2022_64` | `build/Windows_MSVC-x64/` |
| `windows-msvc-release` | same, RelWithDebInfo | `build/Windows_MSVC-x64-Release/` |
| `windows-mingw-debug` / `-release` | MinGW GCC + Qt `mingw_64` | `build/Windows_MinGW-x64/` |

From an **x64 Native Tools Command Prompt for VS 2022** window:

```bat
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug --parallel
```

### GitHub Actions CI

On pushes to `main`, pull requests and manual runs, the [CI](.github/workflows/ci.yml)
workflow builds the application with MSVC 2022 and Qt 6.9.3, runs six regression
suites via CTest and validates the translation catalogs. The audio tests use libVLC
3.0.24 from the official VideoLAN server (archive pinned by SHA-256) and the `dummy`
output, so they check decoding and the master mix without an audio device on the runner.

### Learn panel (no Qt WebEngine)

The reference displays VEGAS online tutorials in Home/Learn and a web-based Library
via Qt WebEngine (a page served by a built-in localhost server; its package includes
`QtWebEngineProcess.exe` and Chromium resources). The port has no tutorials, and the
Library is a native panel, so WebEngine was removed: the **Learn** panel uses ordinary
widgets with the same actions (tutorials in the system browser, a new composite frame,
media import). It is toggled via **Window → Toggle Learn Sidebar**, and the state is
remembered (`learnSidebarIsOpen`). Help also opens in the system browser, and no
web-component license is used. The panel is now available in MinGW builds too
(previously it required an MSVC kit), and the Debug deployment directory is about
900 MB smaller.

### OpenEXR (frame export to `.exr`)

Qt has no EXR writer plugin, so the **OpenEXR (.exr)** preset in the Export panel only
works in a build with OpenEXR. It is disabled by default:

```bat
cmake --preset windows-msvc-debug -DOPENVEGAS_WITH_OPENEXR=ON
```

Lookup order: first an installed `find_package(OpenEXR 3 CONFIG)`, otherwise the
sources from [`thirdparty/openexr`](thirdparty/openexr).

**A caveat about the dependency.** There is no Imath next to `thirdparty/openexr`, so
CMake's OpenEXR then pulls it from GitHub (the `main` branch) right at configure
time — meaning the build is no longer offline or reproducible. The cloned OpenEXR
itself is version `4.0.0-dev` from `main`, whereas the reference ships stable 3.1.
For reproducibility, release tags should be pinned and Imath vendored alongside.

Without this option, `.exr` export does not fail silently; it reports that the build
has no OpenEXR.

### VS Code and Zed (Qt MSVC debugging)

Both IDEs are configured for Qt `msvc2022_64` and pick up the MSVC environment
themselves — no separate developer prompt is needed; the build tasks call
`vcvars64.bat` (via `vswhere -version "[17.0,18.0)"`, so as not to catch a
concurrently installed VS 18 whose toolset does not match the Qt kit).

For the CMake Tools build button, `cmake.useVsDeveloperEnvironment=always` is enabled,
while `cmake.buildTask=false` leaves the launch to the extension together with the
preset environment. The `Ctrl+Shift+B` tasks and the pre-F5 build call
`tools/msvc_build.cmd`, which loads the MSVC environment itself. For Ninja, CMake also
generates `msvc_environment.cmd`: the compile and link commands load the environment
of the installation that owns the selected `cl.exe`. This prevents `STL1001` when
mixing the VS 2022 compiler with VS 2026 headers, even if CMake Tools picked the
environment of a newer Visual Studio. For an existing build directory, run configure
once to refresh the Ninja rules. The launcher also enables UTF-8 (`chcp 65001`):
otherwise Russian `/showIncludes` lines may not match CMake's prefix, Ninja will miss
header dependencies and mix incompatible object files after class changes. If the
directory was already built with the wrong code page, after configure run
`cmake --build build/Windows_MSVC-x64 --target clean` and a full rebuild. From a
regular terminal use:

```bat
tools\msvc_build.cmd build windows-msvc-debug
```

The script can be run from any directory. Without arguments it performs
`buildall windows-msvc-debug` (configure and build); `configure`, `build`, `probe` and
`clean` are also available. For example, `tools\msvc_build.cmd clean windows-msvc-release`
cleans only the Release results via CMake, preserving the build configuration.

For the installer, first build Release, install the application into a temporary
prefix and deploy the Qt dependencies. From the project root:

```bat
tools\msvc_build.cmd buildall windows-msvc-release
cmake --install build\Windows_MSVC-x64-Release --prefix build\installer-stage
"C:\Qt\6.9.3\msvc2022_64\bin\windeployqt.exe" --release build\installer-stage\bin\OpenVegasEffects.exe
"C:\Program Files (x86)\NSIS\makensis.exe" /DBUILD_DIR=build\installer-stage /DOUTPUT_FILE=build\OpenVegasEffects_Setup.exe tools\nsis_installer.nsi
```

NSIS requires a prepared `bin` containing the application and `platforms\qwindows.dll`.
During compilation, `tools/nsis_payload.ps1` compiles a file list via Windows
PowerShell, including nested Qt resources and plugins added to the prefix.
Uninstallation uses this list and removes only directories that have become empty.

`.vegfx` project files are shown with the `resources/icons/iconfile.svg` icon: it is
embedded into the EXE as the second icon (index 1, `resources/icons/iconfile.ico`,
regenerated by `python tools/make_file_icon.py`). The installer references it in the
`DefaultIcon` of the `OpenVegasEffects.Project` ProgID, and on startup the program
registers the same ProgID, `OpenWithProgids` and `Applications\OpenVegasEffects.exe`
for the current user (`app/FileAssociation`) — so the icon also appears for a portable
build when it is chosen as the default program ("Open with → Always"). It does not
make itself the default program for `.vegfx`.

- **VS Code** — [`.vscode/`](.vscode/): `launch.json` (`cppvsdbg` debugger, reads MSVC
  PDBs; Debug / RelWithDebInfo / attach configurations), `tasks.json` (configure /
  build / clean / run), `qt6.natvis` (human-readable `QString`, `QList`, `QRect`… in
  the debugger). Extensions are listed in `extensions.json`. F5 builds and runs.
- **Zed** — [`.zed/debug.json`](.zed/debug.json) (CodeLLDB adapter, PDB next to the
  exe) and [`.zed/tasks.json`](.zed/tasks.json) with the same presets. `clangd` indexes
  `build/Windows_MSVC-x64/compile_commands.json`.

Running outside Qt Creator — add Qt bin/plugins to PATH:

```powershell
$env:PATH = "C:/Qt/6.9.3/llvm-mingw_64/bin;" + $env:PATH
$env:QT_QPA_PLATFORM_PLUGIN_PATH = "C:/Qt/6.9.3/llvm-mingw_64/plugins"
```

**Do not commit** `build/` — it is in [`.gitignore`](.gitignore).

---

## Localization

The interface is translated into Russian, Japanese and Simplified Chinese; the source
language is English.

| Language | Coverage | Source |
|----------|----------|--------|
| Russian | 216/233 | written for this port |
| 日本語 | 206/233 | 79 strings from the reference `.qm`, the rest written for this port |
| 简体中文 | 206/233 | 79 strings from the reference `.qm`, the rest written for this port |

What remains untranslated is format strings (`%1:%2:%3:%4`, `00:00:00:00`, `|<`, `+1`),
which do not need translation.

The ja/zh terminology comes from
`SAMPLES/VEGAS_Effects/Translations/*/VegasEffects_*.qm` (12,041 messages, converted
with `lconvert`) — strings whose source text matches get the reference's own wording.
Strings translated for the port are marked with `<translatorcomment>` and appear in
Qt Linguist as needing native-speaker proofreading.

### How it works

- The catalogs are [`translations/openvegaseffects_*.ts`](translations/); CMake
  compiles them via `qt_add_translations` and embeds them into the binary as
  `:/i18n/`. The Qt **LinguistTools** component is required; without it the build
  succeeds, but the interface stays English.
- Loading — `app::Translations::install()`, called in `main.cpp` **before** the
  widgets are created.
- The lookup order mirrors the reference: first
  `<user data>/Translations/<locale>/openvegaseffects_<locale>.qm` (you can drop in
  your own catalog), then the embedded one. The matching `qt_<locale>.qm` is picked up
  alongside.
- The language is selected in **Options → Interface → Language**, key
  `Options/Language`; empty = follow the system. Applied on the next launch.

### Updating catalogs after editing strings

```bat
cmake --build --preset windows-msvc-debug --target update_translations
```

---

## Runtime data folders

On first launch the application creates the user data folders, reproducing the
reference layout (recovered from its resolver `FUN_14025b000`):
`Translations`, `Templates\AV`, `EnvironmentMaps`, `ExportPresets`,
`PluginPresets`, `Plugins`, `Presets`, `Textures`, `Templates\Workspaces`,
`Objects`, `Tutorials`. Two of them are nested under `Templates` — the reference does
not expose a bare `Templates` folder. On Windows the root is
`%LOCALAPPDATA%\OpenVegas\OpenVegasEffects`.

The difference from the reference is intentional: it only checks the folders and
returns an empty path when they are missing (the installer lays out the tree), whereas
this port has no installer and creates them itself.

Put effect definition files (`.vfx`, JSON) into the `Plugins` folder so they appear in
the **Effects** panel.

---

## Repository structure

```
OpenVegas_Effects/
├── CMakeLists.txt / OpenVegasEffects.pro
├── src/
│   ├── main.cpp          # WinMain-like entry → UTF-8 argv → QApplication
│   ├── core/             # Identifier, Result, Version, Log
│   ├── app/              # AppMain init, UserDataPaths, Settings
│   ├── license/          # LicenseManager + free manager
│   ├── plugin/           # Plugins/ , PluginPresets/ — discovery + registry
│   ├── media/            # media import
│   ├── composition/      # Composition / layers / clips / effects
│   ├── cache/            # SQLite cache (QtSql)
│   ├── render/           # background render worker (placeholder)
│   └── ui/               # MainWindow, Effects panel, Viewer, Timeline
├── resources/            # icons
├── MARKDOWN/             # plans / reverse-engineering notes for developers
├── tools/                # utilities (analyzers, NSIS, svg_to_ico, …)
└── SAMPLES/              # reference binaries and RE notes (not part of the build)
```

`SAMPLES/` contains the reference binaries and reverse-engineering notes; it is not
part of the open build.

---

## Documentation

1. **Architecture and plan** — [`MARKDOWN/ARCHITECTURE.md`](MARKDOWN/ARCHITECTURE.md)
2. **Development rules** — [`MARKDOWN/INIT.MD`](MARKDOWN/INIT.MD)
3. **Reverse engineering of the VEGAS Effects reference** — [`MARKDOWN/RE_VegasEffects.md`](MARKDOWN/RE_VegasEffects.md)
4. **Analysis of the reference distribution** — [`MARKDOWN/RE_VegasEffects_Distribution.md`](MARKDOWN/RE_VegasEffects_Distribution.md)

---

## License

The code in this repository, unless stated otherwise, is licensed under **GNU GPL v3**.
Full text: [`LICENSE`](LICENSE).

The reference binaries in `SAMPLES/` remain the property of their respective rights
holders and are kept for study purposes only.
