# OpenVegas Effects

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

![Main Interface](docs/main_ui.png)

An open (clean-room) reimplementation of a compositing / effects application in the
style of **VEGAS Effects**, written in **C++17 / Qt 6** and licensed under **GNU GPL v3**.

Written from scratch based on the behavioral architecture uncovered while reverse
engineering the reference binaries (see [`MARKDOWN/RE_VegasEffects.md`](MARKDOWN/RE_VegasEffects.md)).
This is **original code**: no proprietary sources are copied. The module layout
mirrors the observable high-level structure of the reference application so the
project grows into a compatible workflow, but every line is written anew.

Current state: **most of the reference's compositing workflow works on Windows** —
projects in the reference's `.vegfx` format, the timeline with nested composite
shots, 2D and 3D layers, text, models, masks and tracking, the native `.hfpl`
plugins of VEGAS Effects, audio and export. Open items are listed in
[`MARKDOWN/CHECKLIST.md`](MARKDOWN/CHECKLIST.md).

---

## What already works

| Area | Status |
|------|--------|
| Projects: the reference's `.vegfx` format (read and write), relative paths, auto save and recovery | Done |
| Timeline: composite shots, nested shots in tabs, 2D/3D layers, clips, slices and rate stretch, keyframes and the value graph, transitions | Done |
| Native `.hfpl` plugins of VEGAS Effects: all 321 modules run (241 2D effects, 43 Behaviors, 16 audio effects, 15 video transitions, 4 Geometry, 2 audio transitions); their controls, custom viewer UI and `.hfpr` presets | Done |
| OpenFX plug-ins: an OpenFX 1.4 host (`OFX_PLUGIN_PATH`, `Common Files\OFX\Plugins`), filters and transitions in the Effects panel, the plug-in cache and an out-of-process describe, saved in `.vegfx` under the reference's FXIDs | Done; not yet tried with commercial plug-ins |
| Text layers: styles, 3D extrusion, per-glyph Behaviors, the Text panel | Done |
| 3D: models (OBJ, FBX with skins and blend shapes, glTF, 3DS, LWO, Alembic optional), cameras, lights, shadows, depth per pixel | Done |
| Masks, motion tracking (MotionTrack), stabilisation | Done |
| Photoshop documents (`.psd`, `.psb`) by layers: blend modes, groups, masks, clipping, layer styles, adjustment layers, smart objects with Camera Raw and Gaussian Blur smart filters as editable effects; a smart object whose source is a Photoshop document becomes that document's own shot with its layers locked; Curves and Camera Raw looks as LUTs | Done |
| Viewer with overlays and reduced previews, 360 viewer, Layer, Trimmer and Layout panels | Done |
| Audio: libVLC playback, the master mix, Audio Meters, native audio effects in real time, voiceover recording | Done on Windows |
| Export: video, image sequences and audio through FFmpeg, the export queue and presets, OpenEXR (optional) | Done |
| Proxies, pre-renders, media cache, frame disk cache | Done |
| Interface in Russian, Japanese and Simplified Chinese, plugin strings included | Done |
| Windows installer (NSIS); `.deb` and `.rpm` packages | Windows verified; Linux packages not verified |
| Audio and VLC 4 on macOS/Linux | Not verified |

Details are in [`MARKDOWN/ARCHITECTURE.md`](MARKDOWN/ARCHITECTURE.md) and the
reverse-engineering notes in [`MARKDOWN/`](MARKDOWN/).

---

## Stack

| Layer | Choice |
|-------|--------|
| Language / UI | C++17, Qt 6 (Widgets, OpenGL; forms in `ui/`, the rest built in code) |
| Build | CMake 3.21+ (preferred) and `OpenVegasEffects.pro` |
| Compilers | MSVC 2022 (main), MinGW / LLVM-MinGW, GCC, Clang |
| Media | **libVLC** — video frames, PCM master mix, audio output and voiceover recording |
| Export, proxies, waveforms | **FFmpeg** executable found on `PATH` |
| Cache | QtSql (SQLite `cache.db`) and a frame disk cache |
| Plugins | Host for the native `.hfpl` modules of VEGAS Effects (OpenGL 4.1) |
| License | GNU GPL v3 ([LICENSE](LICENSE)) |

---

## Building and running

**Qt 6.8+** is required (Core/Gui/Widgets/OpenGLWidgets/Sql/Svg/Xml; LinguistTools for
the translations). Qt WebEngine is not needed. Before rebuilding on Windows, close
`OpenVegasEffects.exe` if linking fails because the file is in use.

### Native plugins (`.hfpl`)

The effects, transitions, audio effects, Behaviors and Geometry modules are the
reference's own `.hfpl` files, run by the port's host. They are not part of this
repository. The application finds them in an installed VEGAS Effects
(`C:\Program Files\FXhome\VEGAS Effects\Plugins`) or in the folders chosen in
**Options → Plugins** (another FXhome program's plugins work too). Without them the
built-in effects stay available.

### libVLC (media)

Qt Multimedia is not used. libVLC decodes video and audio, outputs the PCM master mix
and captures the microphone. The mixer accounts for clip intervals, mute, source
offset, speed, level and nested compositions; Audio Meters measures the same final
PCM. Speed changes use resampling with pitch shift. Native `.hfpl` audio effects and
audio transitions run in the realtime mix and in audio export alike.

The VLC headers and sources live in `thirdparty/vlc`. This is a source tree, not a
ready-to-use runtime. The `Vlc4Adapter.cpp` adapter uses its headers and checks the
VLC 4 ABI; VLC 3 uses separate compatible signatures. Running was verified with
Windows VLC 3.0.24; the VLC 4 adapter builds, but running with VLC 4 and testing on
macOS/Linux are still pending.

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
enumerated via `pactl` (also PipeWire Pulse compatibility). Hardware recording and
audio output require separate verification on the target OSes; the tests check PCM
and the VLC player state.

### Qt Creator (qmake)

Open [`OpenVegasEffects.pro`](OpenVegasEffects.pro), kit **Qt 6.8+ Widgets**
(MSVC / MinGW / Clang) → Build. Keep `SOURCES`/`HEADERS` in sync with
`CMakeLists.txt`.

On Windows, qmake generates up-to-date `Makefile.Debug` and `Makefile.Release` for
Qt Creator's build steps. Form headers, moc/rcc output, object files and translation
directories are separated by configuration; the executable lives in `debug/` or
`release/` inside the shadow build. After editing `.pro`, run **Build → Run qmake**.
Stale Makefiles from a different configuration may cause `C1083` for `ui_*.h`, even if
the new top-level Makefile is already in place.

### MSVC (presets)

The presets in [`CMakePresets.json`](CMakePresets.json) are the single source of truth for the IDEs:

Every build goes into `build/<OS>_<compiler>-<arch>-<configuration>/`:

| Preset | Toolchain | Directory |
|--------|-----------|-----------|
| `windows-msvc-debug` / `-release` | MSVC v143 (VS 2022) + Qt `msvc2022_64`; Release is RelWithDebInfo | `build/Windows_MSVC-x64-Debug/`, `build/Windows_MSVC-x64-Release/` |
| `windows-mingw-debug` / `-release` | MinGW GCC + Qt `mingw_64` | `build/Windows_MinGW-x64-Debug/`, `-Release/` |
| `windows-llvm-debug` / `-release` | LLVM-MinGW Clang + Qt `llvm-mingw_64` | `build/Windows_LLVM-x64-Debug/`, `-Release/` |
| `linux-debug` / `-release` | GCC + system Qt | `build/Linux_GCC-x64-Debug/`, `-Release/` |
| `linux-llvm-debug` / `-release` | Clang + system Qt | `build/Linux_LLVM-x64-Debug/`, `-Release/` |
| `macos-debug` / `-release`, `macos-x64-debug` / `-release` | Apple Clang, Apple silicon / Intel | `build/macOS_LLVM-arm64-Debug/`, `build/macOS_LLVM-x64-Debug/`, ... |

Beside them: the regression tests `build/Windows_MSVC-x64-Release-Tests/`
([`tests/`](tests/)), the installer staging `build/Windows_MSVC-x64-Release-Installer/`,
Qt Creator's qmake builds `build/Windows_MSVC-x64-Debug-qmake/` and the like.

From an **x64 Native Tools Command Prompt for VS 2022** window:

```bat
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug --parallel
```

From a regular terminal the same is done by `tools\msvc_build.cmd`, which loads the
MSVC environment itself:

```bat
tools\msvc_build.cmd build windows-msvc-debug
```

The script can be run from any directory. Without arguments it performs
`buildall windows-msvc-debug` (configure and build); `configure`, `build`, `probe` and
`clean` are also available. For example, `tools\msvc_build.cmd clean windows-msvc-release`
cleans only the Release results via CMake, preserving the build configuration.

### CMake with MinGW

```bash
cmake -S . -B build -G "MinGW Makefiles" ^
  -DCMAKE_PREFIX_PATH=C:/Qt/6.9.3/llvm-mingw_64 ^
  -DCMAKE_C_COMPILER=C:/Qt/Tools/llvm-mingw1706_64/bin/clang.exe ^
  -DCMAKE_CXX_COMPILER=C:/Qt/Tools/llvm-mingw1706_64/bin/clang++.exe ^
  -DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/llvm-mingw1706_64/bin/mingw32-make.exe
cmake --build build -j 8
```

### Linux and macOS

`tools/build_deb.sh` and `tools/build_rpm.sh` build the `openvegaseffects` package
(`.deb` through dpkg-buildpackage, `.rpm` through rpmbuild); the package ships the
application, its desktop entry and icon. On macOS `tools/setup_macos.sh` installs
Qt 6, CMake and Ninja through Homebrew, and `tools/macos_build.sh [debug|release]
[test] [deploy]` builds with the presets. These paths are not verified yet.

### Windows installer

The installer is built by one script from any directory:

```bat
tools\build_windows_installer.bat
```

It finds Visual Studio 2022 (vswhere) and the Qt `msvc2022_64` kit itself, builds
Release with Ninja in `build\Windows_MSVC-x64-Release-Installer`, installs it into a fresh prefix,
deploys Qt with windeployqt, copies the MSVC runtime DLLs next to the EXE and writes
`tools\OpenVegasEffects_Setup.exe`. `CMAKE_PREFIX_PATH` chooses another Qt kit and
`OPENVEGAS_VLC_RUNTIME_DIR` another libVLC runtime; a build tree configured with
another generator, compiler or Qt kit is started over.

By hand the same steps are (the `copy` line needs a Developer Command Prompt for
VS 2022, which sets `VCToolsRedistDir`). From the project root:

```bat
tools\msvc_build.cmd buildall windows-msvc-release
cmake --install build\Windows_MSVC-x64-Release --prefix build\installer-stage
"C:\Qt\6.9.3\msvc2022_64\bin\windeployqt.exe" --release --no-compiler-runtime build\installer-stage\bin\OpenVegasEffects.exe
copy "%VCToolsRedistDir%x64\Microsoft.VC143.CRT\*.dll" build\installer-stage\bin\
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

### Tests and CI

The regression suites are a CMake project of their own in [`tests/`](tests/) — see
[`tests/README.md`](tests/README.md). On pushes to `main`, pull requests and manual
runs, the [CI](.github/workflows/ci.yml) workflow builds the application with MSVC 2022
and Qt 6.9.3, runs the six suites via CTest and validates the translation catalogs.
The audio tests use libVLC 3.0.24 from the official VideoLAN server (archive pinned by
SHA-256) and the `dummy` output, so they check decoding and the master mix without an
audio device on the runner. Tests that run native plugins need an OpenGL 4.1 context
and skip themselves without one.

### Learn panel (no Qt WebEngine)

The reference displays VEGAS online tutorials in Home/Learn and a web-based Library
via Qt WebEngine. The port has no tutorials, and the Library is a native panel, so
WebEngine is not used: the **Learn** panel uses ordinary widgets with the same actions
(tutorials in the system browser, a new composite shot, media import). It is toggled
via **Window → Toggle Learn Sidebar**, and the state is remembered
(`learnSidebarIsOpen`). Help also opens in the system browser.

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
has no OpenEXR. Alembic import (`.abc`) is enabled the same way with
`-DOPENVEGAS_WITH_ALEMBIC=ON` from [`thirdparty/alembic`](thirdparty/alembic).

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
otherwise localized `/showIncludes` lines may not match CMake's prefix, Ninja will miss
header dependencies and mix incompatible object files after class changes. If the
directory was already built with the wrong code page, after configure run
`cmake --build build/Windows_MSVC-x64-Debug --target clean` and a full rebuild.

- **VS Code** — [`.vscode/`](.vscode/): `launch.json` (`cppvsdbg` debugger, reads MSVC
  PDBs; Debug / RelWithDebInfo / attach configurations), `tasks.json` (configure /
  build / clean / run), `qt6.natvis` (human-readable `QString`, `QList`, `QRect`… in
  the debugger). Extensions are listed in `extensions.json`. F5 builds and runs.
- **Zed** — [`.zed/debug.json`](.zed/debug.json) (CodeLLDB adapter, PDB next to the
  exe) and [`.zed/tasks.json`](.zed/tasks.json) with the same presets. `clangd` indexes
  `build/Windows_MSVC-x64-Debug/compile_commands.json`.

Running outside an IDE — add Qt's `bin` to PATH:

```powershell
$env:PATH = "C:/Qt/6.9.3/msvc2022_64/bin;" + $env:PATH
```

**Do not commit** `build/` — it is in [`.gitignore`](.gitignore).

---

## Localization

The interface is translated into Russian, Japanese and Simplified Chinese; the source
language is English.

| Catalog | Messages | Languages |
|---------|---------:|-----------|
| Interface (`openvegaseffects_<lang>.ts`) | 1905 | ru, ja, zh_CN — all translated |
| Plugin strings (`openvegaseffects_plugins_<lang>.ts`) | 6818 | ru, ja, zh_CN |

The ja/zh terminology comes from the reference's own catalogs
(`SAMPLES/VEGAS_Effects/Translations/*/VegasEffects_*.qm`): strings whose source text
matches get the reference's wording. The plugin catalogs hold the native plugins'
names, categories, control labels, units, list items and messages in the reference's
`Effect_<ShortName>` contexts; [`tools/plugin_translations.py`](tools/plugin_translations.py)
writes them. Details: [`translations/README.md`](translations/README.md).

### How it works

- The catalogs are [`translations/`](translations/); CMake compiles them via
  `qt_add_translations` and embeds them into the binary as `:/i18n/`. The Qt
  **LinguistTools** component is required; without it the build succeeds, but the
  interface stays English.
- Loading — `app::Translations::install()`, called in `main.cpp` **before** the
  widgets are created.
- The lookup order mirrors the reference: first
  `<user data>/Translations/<locale>/openvegaseffects_<locale>.qm` (you can drop in
  your own catalog), then the embedded one. Qt's own `qt_<locale>.qm` is loaded
  alongside.
- The language is selected in **Options → Interface → Language**, key
  `Options/Language`; empty = follow the system. Applied on the next launch.
- Two tests open Options and the panels in each language and fail on any text still
  showing an English word.

### Updating catalogs after editing strings

```bat
cmake --build --preset windows-msvc-debug --target update_translations
python tools/validate_translations.py
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
returns an empty path when they are missing (its installer lays out the tree), whereas
this port creates them itself, installed or portable.

The user `Plugins` folder also takes the port's own effect definitions (`.vfx`, JSON);
the native `.hfpl` modules are found as described under
[Native plugins](#native-plugins-hfpl).

---

## Repository structure

```
OpenVegas_Effects/
├── CMakeLists.txt / OpenVegasEffects.pro / CMakePresets.json
├── src/
│   ├── main.cpp          # WinMain-like entry → UTF-8 argv → QApplication
│   ├── core/             # Identifier, Result, Version, Log
│   ├── app/              # AppMain, user data paths, settings, translations, templates
│   ├── license/          # LicenseManager + free manager
│   ├── plugin/           # plugin registry and the host for native .hfpl modules
│   ├── media/            # media import, libVLC, audio, proxies, frame cache
│   ├── composition/      # composite shots, layers, clips, effects, keyframes
│   ├── project/          # .vegfx reading/writing, effect presets
│   ├── model3d/          # 3D model readers and the 3D renderer
│   ├── cache/            # SQLite cache (QtSql)
│   ├── render/           # frame rendering, text, export, pre-renders
│   └── ui/               # MainWindow and the panels, dialogs and overlays
├── ui/                   # Qt Designer forms (main window, panels, dialogs)
├── resources/            # icons, the Windows resource file, macOS files
├── translations/         # .ts catalogs (interface and plugin strings)
├── tests/                # regression suites (a CMake project of their own)
├── tools/                # build scripts, installers, packaging, RE helpers
├── MARKDOWN/             # architecture, checklist, reverse-engineering notes
├── docs/                 # screenshots
├── thirdparty/           # VLC headers, OpenEXR, Alembic (optional)
└── SAMPLES/              # reference binaries, RE material, sample projects (not part of the build)
```

`SAMPLES/` contains the reference binaries, reverse-engineering material and sample
projects (`SAMPLES/vegfx_projects`); it is not part of the open build.

---

## Documentation

1. **Architecture and plan** — [`MARKDOWN/ARCHITECTURE.md`](MARKDOWN/ARCHITECTURE.md)
2. **Open items** — [`MARKDOWN/CHECKLIST.md`](MARKDOWN/CHECKLIST.md)
3. **Development rules** — [`MARKDOWN/INIT.MD`](MARKDOWN/INIT.MD)
4. **Reverse engineering of the VEGAS Effects reference** — [`MARKDOWN/RE_VegasEffects.md`](MARKDOWN/RE_VegasEffects.md)
5. **Native `.hfpl` plugins and their host** — [`MARKDOWN/RE_HFPL_Plugins.md`](MARKDOWN/RE_HFPL_Plugins.md)
6. **Analysis of the reference distribution** — [`MARKDOWN/RE_VegasEffects_Distribution.md`](MARKDOWN/RE_VegasEffects_Distribution.md)
7. **Tests** — [`tests/README.md`](tests/README.md); **translations** — [`translations/README.md`](translations/README.md); **panel forms** — [`ui/panels/README.md`](ui/panels/README.md)

---

## License

The code in this repository, unless stated otherwise, is licensed under **GNU GPL v3**.
Full text: [`LICENSE`](LICENSE).

The reference binaries in `SAMPLES/` remain the property of their respective rights
holders and are kept for study purposes only.
