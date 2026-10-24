# 工具

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

用于构建、打包、翻译和逆向工程的辅助工具。

## 构建与打包

| 文件 | 作用 |
|------|------|
| [`msvc_build.cmd`](msvc_build.cmd) | 加载 Visual Studio 2022 环境后配置并构建 CMake 预设：`msvc_build.cmd <命令> [预设]`，命令为 `configure`、`build`、`buildall`、`probe` 或 `clean` |
| [`build_windows_installer.bat`](build_windows_installer.bat) | 构建 Windows 安装程序：用 MSVC 和 Ninja 进行 Release 构建、`cmake --install`、windeployqt、MSVC 运行时、NSIS |
| [`nsis_installer.nsi`](nsis_installer.nsi)、[`nsis_payload.ps1`](nsis_payload.ps1) | NSIS 脚本及其打包的文件清单 |
| [`build_deb.sh`](build_deb.sh)、[`debian/`](debian/) | `.deb` 软件包 |
| [`build_rpm.sh`](build_rpm.sh)、[`rpm/`](rpm/) | `.rpm` 软件包 |
| [`setup_macos.sh`](setup_macos.sh)、[`macos_build.sh`](macos_build.sh) | macOS：通过 Homebrew 安装 Qt、CMake 和 Ninja；按预设构建 |
| [`openvegaseffects.desktop`](openvegaseffects.desktop) | Linux 桌面条目 |
| [`svg_to_ico.py`](svg_to_ico.py)、[`make_file_icon.py`](make_file_icon.py) | 从 SVG 生成应用程序图标和 `.vegfx` 文件图标 |

## 翻译

| 文件 | 作用 |
|------|------|
| [`validate_translations.py`](validate_translations.py) | 检查 `.ts` 目录中的每条有效消息：完整性、占位符、复数形式、标记、文件过滤器 |
| [`plugin_translations.py`](plugin_translations.py) | 根据参考程序的目录写出插件目录 `openvegaseffects_plugins_<语言>.ts` |
| [`merge_ts_translations.py`](merge_ts_translations.py) | 按上下文或唯一的源字符串，用参考目录填充目录 |
| [`finish_nonempty_translations.py`](finish_nonempty_translations.py) | 将已填写的翻译标记为完成，且不重新格式化文件 |
| [`normalize_ui_double_properties.py`](normalize_ui_double_properties.py) | 规范化 `.ui` 表单中 `QDoubleSpinBox` 的浮点属性 |

## 逆向工程

| 文件 | 作用 |
|------|------|
| [`hfpl_runtime_probe.cpp`](hfpl_runtime_probe.cpp) | 原生 `.hfpl` 模块的控制台探测程序 (目标 `hfpl_runtime_probe`，`msvc_build.cmd probe`)：加载模块，在各种宿主上下文中运行，并报告它们读取和绘制的内容 |
| `*Reference.java`、[`LayerStrings.java`](LayerStrings.java) | 以只读方式提取参考程序的构建函数和表 (Controls、Options、Layer、Timeline、360 Viewer、Behaviors) 的 Ghidra 脚本，用于 [`MARKDOWN/`](../MARKDOWN/) 中的笔记 |
