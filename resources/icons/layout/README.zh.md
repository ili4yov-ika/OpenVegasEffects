# VEGAS Effects 的 Layout 图标

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

这些 PNG 文件是 `SAMPLES/icons-from-VegesEffects/named_png/images/images/` 中未经修改的副本，提取自原始的 RCC 资源。全部 26 个图标名称都已通过 VegasEffects.exe 中 TransformWidget、AlignmentWidget 和 DirectionWidget 的 setupUi 函数确认（参见 `MARKDOWN/RE_wnd_Layout.md`）。

每个图标都包含普通、悬停、选中和禁用状态的图像，尺寸为 16×16 以及 @2x 的 32×32。`resources/icons.qrc` 将它们打包在 `:/icons/layout/` 下；`LayoutPanel.cpp` 显式地指定各个状态。编辑器在运行时不依赖 SAMPLES 目录。
