# Qt Designer フォーム

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

メインウィンドウ、ダイアログ、パネルの `.ui` フォームです。CMake (AUTOUIC) と qmake
(`FORMS`) がコンパイルし、[`src/ui/`](../src/ui/) のクラスが `setupUi()` を呼んで、動的な
部分をコードで追加します。

| フォーム | クラス |
|----------|--------|
| [`MainWindow.ui`](MainWindow.ui) | メインウィンドウ: メニュー、クイックアクションバー |
| [`OptionsDialog.ui`](OptionsDialog.ui) | オプション (カテゴリーの一覧とページのスタック。ページはコードで作成) |
| [`AboutDialog.ui`](AboutDialog.ui) | バージョン情報 |
| [`ExportPanel.ui`](ExportPanel.ui) | Export パネル |
| [`panels/`](panels/) | ドックパネル: Viewer、Timeline、Controls、Layer、Layout、Track、Media、Text、Effects、Library、History、Trimmer、Start、Audio Meters、360 Viewer - 説明は [`panels/README.ja.md`](panels/README.ja.md) |

ウィジェット名はリファレンスのオブジェクト名です。テーマやテストがこの名前で探すので、
フォームを編集するときは変えないでください。翻訳しないテキストには `notr="true"` が付いています。
