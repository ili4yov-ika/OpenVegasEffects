# OpenVegas Effects

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a> |
</p>

![Main Interface](docs/main_ui.png)

一个以 **VEGAS Effects** 风格实现合成/特效应用的开放（clean-room）重实现，使用
**C++17 / Qt 6** 编写，以 **GNU GPL v3** 授权。

完全根据逆向参考二进制时揭示的行为架构从零编写（见
`SAMPLES/VEGAS_Effects/MARKDOWN/RE_VegasEffects.md`）。这是**原创代码**：不复制任何
专有源码。模块划分参照参考应用中可观察的高层结构，使项目能够逐步发展为兼容的
工作流，但每一行都是重新编写的。

当前状态：**早期骨架** — 停靠窗口、自由的许可证管理器、插件扫描与注册表、
合成对象模型、SQLite 缓存以及占位渲染器。

---

## 已实现的功能

| 区域 | 状态 |
|------|------|
| 入口（WinMain 式 → UTF-8 argv → QApplication） | 完成 |
| 用户数据文件夹（11 个，首次启动时创建） | 完成 |
| 通过 QSettings（ini）保存设置 | 完成 |
| 文件日志 | 完成 |
| 自由的许可证管理器（无激活/服务器） | 完成 |
| PluginManager + 注册表，在 `Plugins/` 中本地发现 `.vfx` | 完成（扫描/注册表） |
| 合成对象模型 / 图层 / 片段 / 特效 | 骨架 |
| MediaManager / 媒体导入 | 骨架 |
| SQLite 缓存（`CacheDB`） | 完成 |
| 后台渲染工作线程（占位） | 骨架 |
| UI：MainWindow + 停靠面板（Effects 面板、Viewer、Timeline） | 完成（骨架） |
| Learn 侧边栏（原生控件，不使用 Qt WebEngine） | 完成 |

详情与路线图见 [`MARKDOWN/ARCHITECTURE.md`](MARKDOWN/ARCHITECTURE.md)。

---

## 技术栈

| 层 | 选择 |
|-------|--------|
| 语言 / UI | C++17、Qt 6（Widgets，UI 以代码构建） |
| 构建 | CMake 3.21+（首选）与 `OpenVegasEffects.pro` |
| 编译器 | MSVC 2022、LLVM-MinGW（Clang）、MinGW、GCC |
| 媒体 | **libVLC** — 视频帧、PCM 主混音、音频输出与旁白录制 |
| 缓存 | QtSql（SQLite `cache.db`） |
| 插件 | 未来 OFX 宿主的基础 |
| 许可证 | GNU GPL v3（[LICENSE](LICENSE)） |

---

## 构建与运行

需要 **Qt 6.8+**（Core/Gui/Widgets/OpenGLWidgets/Sql）。不需要 Qt WebEngine。在
Windows 上重新构建前，若因文件被占用而链接失败，请关闭 `OpenVegas Effects.exe`。

### libVLC（媒体）

不再需要 Qt Multimedia。libVLC 负责解码视频和音频、输出 PCM 主混音并采集麦克风。
混音器会考虑片段区间、静音、源偏移、速度、电平以及嵌套合成；Audio Meters 测量
同一个最终 PCM。变速使用带音高偏移的重采样。实时的原生 `.hfpl`
DSP/AudioTransition 尚未接入；音频导出保留独立的 FFmpeg/原生 DSP 路径。

VLC 的头文件和源码位于 `thirdparty/vlc`。这是源码树，而不是可直接使用的运行时。
`Vlc4Adapter.cpp` 适配器使用其头文件并检查 VLC 4 ABI；VLC 3 使用单独的兼容签名。
本次会话已在 Windows VLC 3.0.24 上验证启动；VLC 4 适配器可以构建，但在 VLC 4 下
运行以及在 macOS/Linux 上的测试仍待完成。

打包时，请在 `thirdparty/vlc/runtime/Windows`、`Darwin` 或 `Linux` 中准备目标架构
的运行时：库及其依赖和插件。也可以通过 CMake 选项
`-DOPENVEGAS_VLC_RUNTIME_DIR=<prefix>` 指定其他前缀；qmake 读取同名环境变量。
在 Windows 上，如果没有准备好的运行时，则使用 Program Files 中已安装的 VLC。
两种构建都会把运行时复制到 EXE 旁的 `vlc` 目录；CMake install 会保留此结构，
NSIS 会验证 DLL 和插件是否存在。还会随附 `COPYING` 和 `COPYING.LIB`。

加载器会在 `OPENVEGAS_VLC_DIR`（运行时覆盖）、应用程序旁边、配置的前缀以及系统
目录中查找库。支持 Windows DLL、macOS dylib 和 Linux SONAME；无需导入库。若没有
可用的 libVLC，应用仍会启动，但媒体和录制不可用。录制使用 DirectShow、qtsound
或 PulseAudio；Linux 通过 `pactl` 枚举设备（也兼容 PipeWire Pulse）。从 Qt 切换
后，需要在 Options 中重新选择麦克风：旧的 base64 设备 ID 与新的 ID 不匹配。硬件
录制和音频输出需要在目标操作系统上单独验证；测试会检查 PCM 和 VLC player 状态。

### Qt Creator（qmake）

打开 [`OpenVegasEffects.pro`](OpenVegasEffects.pro)，套件 **Qt 6.8+ Widgets**
（MSVC / MinGW / Clang）→ Build。保持 `SOURCES`/`HEADERS` 与
`CMakeLists.txt` 同步。

在 Windows 上，qmake 会为 Qt Creator 的构建步骤生成最新的 `Makefile.Debug` 和
`Makefile.Release`。表单头文件、moc/rcc、对象文件和翻译目录按配置分开；可执行
文件位于影子构建中的 `debug/` 或 `release/`。修改 `.pro` 后，请执行
**Build → Run qmake**。来自不同配置的旧 Makefile 可能导致 `QAudioBuffer` 和
`ui_*.h` 出现 `C1083`，即使新的顶层 Makefile 已经就位。

### CMake

```bash
# LLVM-MinGW（推荐，无需 MSVC）
cmake -S . -B build -G "MinGW Makefiles" ^
  -DCMAKE_PREFIX_PATH=C:/Qt/6.9.3/llvm-mingw_64 ^
  -DCMAKE_C_COMPILER=C:/Qt/Tools/llvm-mingw1706_64/bin/clang.exe ^
  -DCMAKE_CXX_COMPILER=C:/Qt/Tools/llvm-mingw1706_64/bin/clang++.exe ^
  -DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/llvm-mingw1706_64/bin/mingw32-make.exe
cmake --build build -j 8
```

### MSVC（预设）

[`CMakePresets.json`](CMakePresets.json) 中的预设是两个 IDE 的唯一事实来源：

| 预设 | 工具链 | 目录 |
|--------|-----------|-----------|
| `windows-msvc-debug` | MSVC v143（VS 2022）+ Qt `msvc2022_64` | `build/Windows_MSVC-x64/` |
| `windows-msvc-release` | 相同，RelWithDebInfo | `build/Windows_MSVC-x64-Release/` |
| `windows-mingw-debug` / `-release` | MinGW GCC + Qt `mingw_64` | `build/Windows_MinGW-x64/` |

在 **x64 Native Tools Command Prompt for VS 2022** 窗口中：

```bat
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug --parallel
```

### GitHub Actions CI

在推送到 `main`、拉取请求以及手动运行时，[CI](.github/workflows/ci.yml) 工作流会
使用 MSVC 2022 和 Qt 6.9.3 构建应用，通过 CTest 运行六组回归测试，并校验翻译
目录。音频测试使用来自 VideoLAN 官方服务器的 libVLC 3.0.24（归档以 SHA-256
固定）和 `dummy` 输出，因此无需 runner 上的音频设备即可检查解码和主混音。

### Learn 面板（不使用 Qt WebEngine）

参考应用通过 Qt WebEngine 在 Home/Learn 中显示 VEGAS 在线教程，并显示基于 Web 的
Library（由内置 localhost 服务器提供的页面；其包中包含 `QtWebEngineProcess.exe`
和 Chromium 资源）。此移植版没有教程，而 Library 是原生面板，因此移除了
WebEngine：**Learn** 面板使用普通控件提供相同操作（在系统浏览器中打开教程、新建
合成帧、导入媒体）。可通过 **Window → Toggle Learn Sidebar** 切换，状态会被记住
（`learnSidebarIsOpen`）。帮助也会在系统浏览器中打开，不使用 Web 组件许可证。
该面板现在也出现在 MinGW 构建中（之前需要 MSVC 套件），Debug 部署目录约缩小了
900 MB。

### OpenEXR（导出 `.exr` 帧）

Qt 没有 EXR 写入插件，因此 Export 面板中的 **OpenEXR (.exr)** 预设仅在启用了
OpenEXR 的构建中可用。默认关闭：

```bat
cmake --preset windows-msvc-debug -DOPENVEGAS_WITH_OPENEXR=ON
```

查找顺序：先使用已安装的 `find_package(OpenEXR 3 CONFIG)`，否则使用
[`thirdparty/openexr`](thirdparty/openexr) 中的源码。

**依赖注意事项。** `thirdparty/openexr` 旁边没有 Imath，因此 CMake 的 OpenEXR 会在
configure 阶段直接从 GitHub（`main` 分支）拉取它——这意味着构建不再离线，也无法
复现。克隆的 OpenEXR 本身是来自 `main` 的 `4.0.0-dev` 版本，而参考应用提供的是
稳定的 3.1。为保证可复现性，应固定发布标签并在旁边 vendoring Imath。

没有此选项时，`.exr` 导出不会静默失败，而是报告构建未包含 OpenEXR。

### VS Code 与 Zed（Qt MSVC 调试）

两个 IDE 都配置为使用 Qt `msvc2022_64`，并自行获取 MSVC 环境——无需单独的
developer prompt；构建任务会调用 `vcvars64.bat`（通过
`vswhere -version "[17.0,18.0)"`，以避免捕获并行安装的 VS 18，其工具集与 Qt
套件不匹配）。

对于 CMake Tools 的构建按钮，启用了 `cmake.useVsDeveloperEnvironment=always`，
而 `cmake.buildTask=false` 将启动交给扩展并携带预设环境。`Ctrl+Shift+B` 任务和
F5 前的构建会调用 `tools/msvc_build.cmd`，它自行加载 MSVC 环境。对于 Ninja，
CMake 还会生成 `msvc_environment.cmd`：编译和链接命令会加载所选 `cl.exe` 所属
安装的环境。即使 CMake Tools 选择了较新 Visual Studio 的环境，这也能防止 VS 2022
编译器与 VS 2026 头文件混用时出现 `STL1001`。对于现有构建目录，请运行一次
configure 以刷新 Ninja 规则。启动器还会启用 UTF-8（`chcp 65001`）：否则俄语
`/showIncludes` 行可能无法匹配 CMake 的前缀，Ninja 会漏掉头文件依赖，并在类更改
后混入不兼容的对象文件。如果目录已用错误的代码页构建过，请在 configure 后运行
`cmake --build build/Windows_MSVC-x64 --target clean` 并完整重建。在普通终端中
使用：

```bat
tools\msvc_build.cmd build windows-msvc-debug
```

该脚本可从任意目录运行。不带参数时执行 `buildall windows-msvc-debug`（configure
和 build）；也可使用 `configure`、`build`、`probe` 和 `clean`。例如，
`tools\msvc_build.cmd clean windows-msvc-release` 仅通过 CMake 清理 Release 结果，
同时保留构建配置。

对于安装程序，先构建 Release，将应用安装到临时前缀并部署 Qt 依赖。在项目根目录
下：

```bat
tools\msvc_build.cmd buildall windows-msvc-release
cmake --install build\Windows_MSVC-x64-Release --prefix build\installer-stage
"C:\Qt\6.9.3\msvc2022_64\bin\windeployqt.exe" --release build\installer-stage\bin\OpenVegasEffects.exe
"C:\Program Files (x86)\NSIS\makensis.exe" /DBUILD_DIR=build\installer-stage /DOUTPUT_FILE=build\OpenVegasEffects_Setup.exe tools\nsis_installer.nsi
```

NSIS 需要准备好的 `bin`，其中包含应用和 `platforms\qwindows.dll`。编译期间，
`tools/nsis_payload.ps1` 会通过 Windows PowerShell 生成文件列表，包括嵌套的 Qt
资源和添加到前缀中的插件。卸载时使用该列表，并且只删除已变为空目录的文件夹。

`.vegfx` 项目文件使用 `resources/icons/iconfile.svg` 图标显示：它作为第二个图标
嵌入 EXE（索引 1，`resources/icons/iconfile.ico`，由
`python tools/make_file_icon.py` 重新生成）。安装程序将其引用为
`OpenVegasEffects.Project` ProgID 的 `DefaultIcon`，程序在启动时会为当前用户注册
相同的 ProgID、`OpenWithProgids` 和 `Applications\OpenVegasEffects.exe`
（`app/FileAssociation`）——因此当便携版被选为默认程序时（“打开方式 → 始终”），
图标也会显示。它不会把自己设为 `.vegfx` 的默认程序。

- **VS Code** — [`.vscode/`](.vscode/)：`launch.json`（`cppvsdbg` 调试器，读取 MSVC
  PDB；Debug / RelWithDebInfo / attach 配置）、`tasks.json`（configure / build /
  clean / run）、`qt6.natvis`（在调试器中以可读形式显示 `QString`、`QList`、
  `QRect`…）。扩展列在 `extensions.json` 中。F5 构建并运行。
- **Zed** — [`.zed/debug.json`](.zed/debug.json)（CodeLLDB 适配器，PDB 位于 exe
  旁）和 [`.zed/tasks.json`](.zed/tasks.json)，使用相同的预设。`clangd` 索引
  `build/Windows_MSVC-x64/compile_commands.json`。

在 Qt Creator 之外运行——将 Qt bin/plugins 添加到 PATH：

```powershell
$env:PATH = "C:/Qt/6.9.3/llvm-mingw_64/bin;" + $env:PATH
$env:QT_QPA_PLATFORM_PLUGIN_PATH = "C:/Qt/6.9.3/llvm-mingw_64/plugins"
```

**不要提交** `build/`——它位于 [`.gitignore`](.gitignore) 中。

---

## 本地化

界面已翻译为俄语、日语和简体中文；源语言为英语。

| 语言 | 覆盖率 | 来源 |
|------|----------|--------|
| 俄语 | 216/233 | 为本移植版编写 |
| 日本語 | 206/233 | 79 条来自参考 `.qm`，其余为本移植版编写 |
| 简体中文 | 206/233 | 79 条来自参考 `.qm`，其余为本移植版编写 |

未翻译的是格式字符串（`%1:%2:%3:%4`、`00:00:00:00`、`|<`、`+1`），它们不需要
翻译。

ja/zh 术语来自 `SAMPLES/VEGAS_Effects/Translations/*/VegasEffects_*.qm`
（12,041 条消息，通过 `lconvert` 转换）——源文本匹配的字符串会采用参考应用自身的
措辞。为本移植版翻译的字符串会标记 `<translatorcomment>`，并在 Qt Linguist 中
显示为需要母语者校对。

### 工作原理

- 目录为 [`translations/openvegaseffects_*.ts`](translations/)；CMake 通过
  `qt_add_translations` 编译它们，并作为 `:/i18n/` 嵌入二进制文件。需要 Qt
  **LinguistTools** 组件；没有它构建仍会成功，但界面保持英语。
- 加载——`app::Translations::install()`，在创建控件**之前**于 `main.cpp` 中调用。
- 查找顺序与参考应用一致：首先
  `<user data>/Translations/<locale>/openvegaseffects_<locale>.qm`（可放入自己的
  目录），然后是内嵌目录。会同时拾取匹配的 `qt_<locale>.qm`。
- 语言在 **Options → Interface → Language** 中选择，键为 `Options/Language`；
  留空 = 跟随系统。下次启动时应用。

### 编辑字符串后更新目录

```bat
cmake --build --preset windows-msvc-debug --target update_translations
```

---

## 运行时数据文件夹

首次启动时，应用会创建用户数据文件夹，复现参考布局（从其解析器
`FUN_14025b000` 恢复）：`Translations`、`Templates\AV`、`EnvironmentMaps`、
`ExportPresets`、`PluginPresets`、`Plugins`、`Presets`、`Textures`、
`Templates\Workspaces`、`Objects`、`Tutorials`。其中两个嵌套在 `Templates` 下
——参考应用不会暴露单独的 `Templates` 文件夹。在 Windows 上根目录为
`%LOCALAPPDATA%\OpenVegas\OpenVegasEffects`。

与参考应用的不同是有意为之：它只检查文件夹，不存在时返回空路径（目录树由安装
程序布置），而此移植版没有安装程序，会自行创建它们。

将特效定义文件（`.vfx`、JSON）放入 `Plugins` 文件夹，它们就会出现在 **Effects**
面板中。

---

## 仓库结构

```
OpenVegas_Effects/
├── CMakeLists.txt / OpenVegasEffects.pro
├── src/
│   ├── main.cpp          # WinMain 式入口 → UTF-8 argv → QApplication
│   ├── core/             # Identifier, Result, Version, Log
│   ├── app/              # AppMain 初始化、UserDataPaths、Settings
│   ├── license/          # LicenseManager + 自由管理器
│   ├── plugin/           # Plugins/、PluginPresets/ — 发现 + 注册表
│   ├── media/            # 媒体导入
│   ├── composition/      # Composition / 图层 / 片段 / 特效
│   ├── cache/            # SQLite 缓存（QtSql）
│   ├── render/           # 后台渲染工作线程（占位）
│   └── ui/               # MainWindow、Effects 面板、Viewer、Timeline
├── resources/            # 图标
├── MARKDOWN/             # 供开发者使用的计划 / 逆向笔记
├── tools/                # 工具（分析器、NSIS、svg_to_ico 等）
└── SAMPLES/              # 参考二进制与逆向笔记（不参与构建）
```

`SAMPLES/` 包含参考二进制和逆向笔记；它不参与开放构建。

---

## 文档

1. **架构与计划** — [`MARKDOWN/ARCHITECTURE.md`](MARKDOWN/ARCHITECTURE.md)
2. **开发规则** — [`MARKDOWN/INIT.MD`](MARKDOWN/INIT.MD)
3. **VEGAS Effects 参考逆向工程** — [`MARKDOWN/RE_VegasEffects.md`](MARKDOWN/RE_VegasEffects.md)
4. **参考发行版分析** — [`MARKDOWN/RE_VegasEffects_Distribution.md`](MARKDOWN/RE_VegasEffects_Distribution.md)

---

## 许可证

本仓库中的代码，除非另有说明，均以 **GNU GPL v3** 授权。完整文本：
[`LICENSE`](LICENSE)。

`SAMPLES/` 中的参考二进制仍归其各自权利人所有，仅用于研究目的。
