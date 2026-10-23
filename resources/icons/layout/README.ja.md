# VEGAS Effects の Layout アイコン

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

これらの PNG ファイルは、元の RCC リソースから抽出した `SAMPLES/icons-from-VegesEffects/named_png/images/images/` の無変更のコピーです。26 個のアイコン名はすべて、VegasEffects.exe の TransformWidget、AlignmentWidget、DirectionWidget の setupUi 関数で確認済みです（`MARKDOWN/RE_wnd_Layout.md` を参照）。

各アイコンには通常、ホバー、チェック、無効の各状態の画像があり、16×16 と @2x の 32×32 があります。`resources/icons.qrc` がこれらを `:/icons/layout/` にまとめ、`LayoutPanel.cpp` が各状態を明示的に割り当てます。エディターは実行時に SAMPLES ディレクトリに依存しません。
