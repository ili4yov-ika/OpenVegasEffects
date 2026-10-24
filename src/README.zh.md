# 源代码

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

应用程序的 C++17 / Qt 6 代码，每个模块一个文件夹。模块划分沿用参考程序的上层结构
(见 [`MARKDOWN/ARCHITECTURE.md`](../MARKDOWN/ARCHITECTURE.md))。

| 文件夹 | 内容 |
|--------|------|
| [`main.cpp`](main.cpp) | 入口：UTF-8 命令行、`QApplication`、翻译、`app::AppMain` |
| [`core/`](core/) | 基础类型：`Identifier`、`Result`、`Version`、文件日志 |
| [`app/`](app/) | 启动 (`AppMain`)、用户数据文件夹、设置、翻译、AV 模板、项目默认值、插件文件夹、文件关联、GPU 驱动检查 |
| [`license/`](license/) | 许可证接口，以及始终报告有效许可证的免费管理器 |
| [`composition/`](composition/) | 模型：合成镜头、图层、片段、特效、关键帧、蒙版、文字样式、转场、Behaviors 及其模拟、运动跟踪 |
| [`project/`](project/) | 以参考程序的格式读写 `.vegfx` 项目和 `.vegfxcs` 合成镜头、合并项目、特效预设 (`.hfpr`)、按图层导入 Photoshop 文档 (`PsdImport`) |
| [`media/`](media/) | 媒体导入与探测、libVLC (`VlcBackend`、`Vlc4Adapter`)、视频解码、实时音频混音器 (`AudioPlayer`)、旁白录制、波形、规整音频、代理、已解码帧缓存、Photoshop 文档 (`PsdDocument`) |
| [`plugin/`](plugin/) | 插件注册表和原生 `.hfpl` 模块的宿主 (`NativeEffectRender`、`NativePlugin`)：2D 特效、转场、音频、Behaviors、Geometry、查看器中的自定义界面 |
| [`model3d/`](model3d/) | 3D 模型读取 (OBJ、FBX、glTF、3DS、LWO、Alembic)、网格和 3D 渲染器 |
| [`render/`](render/) | 帧渲染 (`RenderManager`)、文字、图层几何、导出及其预设、通过 FFmpeg 进行音视频编码、预渲染、帧磁盘缓存 |
| [`cache/`](cache/) | SQLite 媒体缓存数据库 |
| [`ui/`](ui/) | 主窗口、面板 (Timeline、Viewer、Controls、Layer、Layout、Media、Text、Effects、Export 等)、对话框和查看器叠加层 |

这些类加载的 Qt Designer 表单位于仓库根目录的 [`ui/`](../ui/)。
