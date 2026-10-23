# Значки Layout из VEGAS Effects

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

Эти PNG — неизменённые копии из `SAMPLES/icons-from-VegesEffects/named_png/images/images/`, извлечённые из исходных ресурсов RCC. Все 26 имён значков подтверждены функциями setupUi виджетов TransformWidget, AlignmentWidget и DirectionWidget в VegasEffects.exe (см. `MARKDOWN/RE_wnd_Layout.md`).

У каждого значка есть изображения для обычного состояния, наведения, нажатого и недоступного, в размере 16×16 и @2x 32×32. `resources/icons.qrc` упаковывает их под `:/icons/layout/`; `LayoutPanel.cpp` назначает состояния явно. Во время работы редактор не зависит от каталога SAMPLES.
