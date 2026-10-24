# CMake 辅助脚本

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

主 [`CMakeLists.txt`](../CMakeLists.txt) 使用的脚本。

| 文件 | 作用 |
|------|------|
| [`DeployVlc.cmake`](DeployVlc.cmake) | 每次构建后运行 (`cmake -P`)：把 libVLC 运行时 - 它的库、包含解码与采集模块的 `plugins/`、`plugins.dat` 和许可证文件 - 从 `OPENVEGAS_VLC_RUNTIME_DIR` 复制到可执行文件旁的 `vlc/` |
| [`msvc_environment.cmd.in`](msvc_environment.cmd.in) | `msvc_environment.cmd` 的模板，Ninja 通过它执行 MSVC 的编译与链接命令：把控制台切换为 UTF-8 (使本地化的 `/showIncludes` 行与 CMake 的前缀相符)，并加载所选 `cl.exe` 所属 Visual Studio 的环境，避免混入其他安装的头文件 |
