# Developer documents

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

The plans, rules and reverse-engineering notes of the project. The notes on the
reference application are written in Russian; [`ARCHITECTURE.md`](ARCHITECTURE.md) is in
English.

| Document | What it covers |
|----------|----------------|
| [`ARCHITECTURE.md`](ARCHITECTURE.md) | How the reference's behaviour maps onto the modules of this repository |
| [`CHECKLIST.md`](CHECKLIST.md) | Open items of the project |
| [`INIT.MD`](INIT.MD) | Development rules |
| [`RE_VegasEffects.md`](RE_VegasEffects.md) | The reference application (VegasEffects.exe): start-up, windows, panels, settings |
| [`RE_VegasEffects_Distribution.md`](RE_VegasEffects_Distribution.md) | What the reference's distribution holds and what each library is for |
| [`RE_Project_dll.md`](RE_Project_dll.md) | Project.dll: the object model - projects, composite shots, layers, keyframes, the `.vegfx` format |
| [`RE_Flux_dll.md`](RE_Flux_dll.md) | Flux.dll: the render engine |
| [`RE_Tannen_dll.md`](RE_Tannen_dll.md) | Tannen.dll: the plugin and effect host |
| [`RE_Plugin_Engine.md`](RE_Plugin_Engine.md) | The engine of the native `.hfpl` plugins |
| [`RE_HFPL_Plugins.md`](RE_HFPL_Plugins.md) | The `.hfpl` modules: file format, the host's API and contexts, what the port runs and how it was verified; [`RE_HFPL_Plugins/hfpl_metadata.tsv`](RE_HFPL_Plugins/hfpl_metadata.tsv) lists every module |
| [`RE_3D_Camera_Rule.md`](RE_3D_Camera_Rule.md) | The rule that a 3D composite shot needs a camera |
| [`RE_UI_Stubs.md`](RE_UI_Stubs.md) | An audit of the interface's placeholders and how each was closed |
| [`RE_wnd_Layer.md`](RE_wnd_Layer.md), [`RE_wnd_Layout.md`](RE_wnd_Layout.md), [`RE_wnd_Viewer360.md`](RE_wnd_Viewer360.md) | The Layer, Layout and 360 Viewer panels |

Addresses in the notes (`FUN_14…`, `0x18…`) are those of the reference binaries as
Ghidra shows them; the binaries themselves are not in the repository (see
[`SAMPLES/README.md`](../SAMPLES/README.md)).
