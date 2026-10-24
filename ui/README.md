# Qt Designer forms

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

The `.ui` forms of the main window, dialogs and panels. CMake (AUTOUIC) and qmake
(`FORMS`) compile them; the classes in [`src/ui/`](../src/ui/) call `setupUi()` and add
the dynamic parts in code.

| Form | Class |
|------|-------|
| [`MainWindow.ui`](MainWindow.ui) | The main window: menus, the quick actions bar |
| [`OptionsDialog.ui`](OptionsDialog.ui) | Options (the category list and page stack; the pages are built in code) |
| [`AboutDialog.ui`](AboutDialog.ui) | About |
| [`ExportPanel.ui`](ExportPanel.ui) | The Export panel |
| [`panels/`](panels/) | The dock panels: Viewer, Timeline, Controls, Layer, Layout, Track, Media, Text, Effects, Library, History, Trimmer, Start, Audio Meters, 360 Viewer - described in [`panels/README.md`](panels/README.md) |

Widget names are the reference's object names: themes and tests look them up, so keep
them when editing a form. Text that must not be translated is marked `notr="true"`.
