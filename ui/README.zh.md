# Qt Designer 表单

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

主窗口、对话框和面板的 `.ui` 表单。由 CMake (AUTOUIC) 和 qmake (`FORMS`) 编译；
[`src/ui/`](../src/ui/) 中的类调用 `setupUi()`，并在代码中补充动态部分。

| 表单 | 类 |
|------|----|
| [`MainWindow.ui`](MainWindow.ui) | 主窗口：菜单、快捷操作栏 |
| [`OptionsDialog.ui`](OptionsDialog.ui) | 选项 (类别列表和页面堆栈；页面在代码中构建) |
| [`AboutDialog.ui`](AboutDialog.ui) | 关于 |
| [`ExportPanel.ui`](ExportPanel.ui) | Export 面板 |
| [`panels/`](panels/) | 停靠面板：Viewer、Timeline、Controls、Layer、Layout、Track、Media、Text、Effects、Library、History、Trimmer、Start、Audio Meters、360 Viewer - 说明见 [`panels/README.zh.md`](panels/README.zh.md) |

控件名称就是参考程序的对象名称：主题和测试按这些名称查找控件，编辑表单时请保留。
不需要翻译的文本标有 `notr="true"`。
