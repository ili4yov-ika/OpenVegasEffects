# VEGAS Effects Layout icons

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

These PNG files are unchanged copies from `SAMPLES/icons-from-VegesEffects/named_png/images/images/`, extracted from the original RCC resources. All 26 icon names are confirmed by the TransformWidget, AlignmentWidget and DirectionWidget setupUi functions in VegasEffects.exe (see `MARKDOWN/RE_wnd_Layout.md`).

Each icon includes normal, hover, checked and disabled artwork, at 16×16 and @2x 32×32. `resources/icons.qrc` packages them under `:/icons/layout/`; `LayoutPanel.cpp` assigns the states explicitly. The editor does not depend on the SAMPLES directory at runtime.
