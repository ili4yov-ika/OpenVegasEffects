# CMake helpers

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

Scripts the main [`CMakeLists.txt`](../CMakeLists.txt) uses.

| File | What it does |
|------|--------------|
| [`DeployVlc.cmake`](DeployVlc.cmake) | Run after each build (`cmake -P`): copies the libVLC runtime - its libraries, `plugins/` with the decoder and capture modules, `plugins.dat` and the license files - from `OPENVEGAS_VLC_RUNTIME_DIR` into `vlc/` next to the executable |
| [`msvc_environment.cmd.in`](msvc_environment.cmd.in) | The template of `msvc_environment.cmd`, which Ninja's compile and link commands go through with MSVC: it switches the console to UTF-8 (so localized `/showIncludes` lines match CMake's prefix) and loads the environment of the Visual Studio that owns the chosen `cl.exe`, so headers of another installation are never mixed in |
