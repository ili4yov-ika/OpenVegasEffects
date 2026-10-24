# 开发者文档

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

项目的计划、规则和逆向工程笔记。关于参考应用程序的笔记用俄语写成，
[`ARCHITECTURE.md`](ARCHITECTURE.md) 为英语。

| 文档 | 内容 |
|------|------|
| [`ARCHITECTURE.md`](ARCHITECTURE.md) | 参考程序的行为如何对应到本仓库的各个模块 |
| [`CHECKLIST.md`](CHECKLIST.md) | 项目的未完成事项 |
| [`INIT.MD`](INIT.MD) | 开发规则 |
| [`RE_VegasEffects.md`](RE_VegasEffects.md) | 参考应用程序 (VegasEffects.exe)：启动、窗口、面板、设置 |
| [`RE_VegasEffects_Distribution.md`](RE_VegasEffects_Distribution.md) | 参考程序发行包的内容以及每个库的用途 |
| [`RE_Project_dll.md`](RE_Project_dll.md) | Project.dll：对象模型 - 项目、合成镜头、图层、关键帧、`.vegfx` 格式 |
| [`RE_Flux_dll.md`](RE_Flux_dll.md) | Flux.dll：渲染引擎 |
| [`RE_Tannen_dll.md`](RE_Tannen_dll.md) | Tannen.dll：插件与特效宿主 |
| [`RE_Plugin_Engine.md`](RE_Plugin_Engine.md) | 原生 `.hfpl` 插件的引擎 |
| [`RE_HFPL_Plugins.md`](RE_HFPL_Plugins.md) | `.hfpl` 模块：文件格式、宿主的 API 与上下文、移植版运行了什么以及如何验证；[`RE_HFPL_Plugins/hfpl_metadata.tsv`](RE_HFPL_Plugins/hfpl_metadata.tsv) 列出了所有模块 |
| [`RE_3D_Camera_Rule.md`](RE_3D_Camera_Rule.md) | 3D 合成镜头需要摄像机的规则 |
| [`RE_UI_Stubs.md`](RE_UI_Stubs.md) | 界面占位项的审查以及每一项的处理方式 |
| [`RE_wnd_Layer.md`](RE_wnd_Layer.md)、[`RE_wnd_Layout.md`](RE_wnd_Layout.md)、[`RE_wnd_Viewer360.md`](RE_wnd_Viewer360.md) | Layer、Layout 和 360 Viewer 面板 |

笔记中的地址 (`FUN_14…`、`0x18…`) 是 Ghidra 中显示的参考程序二进制文件的地址；这些二进制
文件本身不在仓库中 (见 [`SAMPLES/README.zh.md`](../SAMPLES/README.zh.md))。
