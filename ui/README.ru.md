# Формы Qt Designer

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

Формы `.ui` главного окна, диалогов и панелей. Их компилируют CMake (AUTOUIC) и qmake
(`FORMS`); классы в [`src/ui/`](../src/ui/) вызывают `setupUi()` и достраивают
динамические части в коде.

| Форма | Класс |
|-------|-------|
| [`MainWindow.ui`](MainWindow.ui) | Главное окно: меню, панель быстрых действий |
| [`OptionsDialog.ui`](OptionsDialog.ui) | Настройки (список категорий и стек страниц; страницы строятся в коде) |
| [`AboutDialog.ui`](AboutDialog.ui) | О программе |
| [`ExportPanel.ui`](ExportPanel.ui) | Панель Export |
| [`panels/`](panels/) | Док-панели: Viewer, Timeline, Controls, Layer, Layout, Track, Media, Text, Effects, Library, History, Trimmer, Start, Audio Meters, 360 Viewer - описаны в [`panels/README.ru.md`](panels/README.ru.md) |

Имена виджетов - это имена объектов эталона: по ним их находят темы и тесты, поэтому
сохраняйте их при правке формы. Текст, который не переводится, помечен `notr="true"`.
