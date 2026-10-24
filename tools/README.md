# Tools

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

Build, packaging, translation and reverse-engineering helpers.

## Build and packaging

| File | What it does |
|------|--------------|
| [`msvc_build.cmd`](msvc_build.cmd) | Configures and builds a CMake preset with the Visual Studio 2022 environment loaded: `msvc_build.cmd <command> [windows-msvc-debug|windows-msvc-release]`, the command being `configure`, `build` (configures a tree not configured yet), `buildall`, `probe`, `clean` or `tests` (builds and runs the regression tests in `build/Windows_MSVC-x64-Release-Tests`) |
| [`build_windows_installer.bat`](build_windows_installer.bat) | Builds the Windows installer: Release with MSVC and Ninja, `cmake --install`, windeployqt, the MSVC runtime, NSIS |
| [`nsis_installer.nsi`](nsis_installer.nsi), [`nsis_payload.ps1`](nsis_payload.ps1) | The NSIS script and the payload list it compiles |
| [`build_deb.sh`](build_deb.sh), [`debian/`](debian/) | The `.deb` package |
| [`build_rpm.sh`](build_rpm.sh), [`rpm/`](rpm/) | The `.rpm` package |
| [`setup_macos.sh`](setup_macos.sh), [`macos_build.sh`](macos_build.sh) | macOS: installing Qt, CMake and Ninja through Homebrew; building with the presets |
| [`openvegaseffects.desktop`](openvegaseffects.desktop) | The Linux desktop entry |
| [`svg_to_ico.py`](svg_to_ico.py), [`make_file_icon.py`](make_file_icon.py) | The application icon and the `.vegfx` file icon from their SVGs |

## Translations

| File | What it does |
|------|--------------|
| [`validate_translations.py`](validate_translations.py) | Checks every active message of the `.ts` catalogs: completeness, placeholders, plural forms, markup, file filters |
| [`plugin_translations.py`](plugin_translations.py) | Writes the plugin catalogs `openvegaseffects_plugins_<lang>.ts` from the reference's catalogs |
| [`merge_ts_translations.py`](merge_ts_translations.py) | Fills a catalog from a reference catalog by context or by an unambiguous source string |
| [`finish_nonempty_translations.py`](finish_nonempty_translations.py) | Marks filled translations finished without reformatting the file |
| [`normalize_ui_double_properties.py`](normalize_ui_double_properties.py) | Normalizes floating-point properties of `QDoubleSpinBox` in `.ui` forms |

## Reverse engineering

| File | What it does |
|------|--------------|
| [`hfpl_runtime_probe.cpp`](hfpl_runtime_probe.cpp) | A console probe of the native `.hfpl` modules (target `hfpl_runtime_probe`, `msvc_build.cmd probe`): loads modules, runs them in the various host contexts and reports what they read and draw |
| `*Reference.java`, [`LayerStrings.java`](LayerStrings.java) | Ghidra scripts that extract the reference's builders and tables (Controls, Options, Layer, Timeline, 360 Viewer, Behaviors) read-only, for the notes in [`MARKDOWN/`](../MARKDOWN/) |
