# VEGAS Effects メディアのプレビューアイコン

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

これらの PNG は `SAMPLES/icons-from-VegesEffects/named_png/images/images/` から無変更でコピーしたもので、元の RCC リソースから抽出されています。リファレンスのメディアパネルが画像の代わりに表示する、枠付きの 106×60（@2x 212×120）のプレビューです。`resources/icons.qrc` が `:/icons/media/` にまとめ、`MediaPanel.cpp` はビデオのフレームを読み込んでいる間はビデオ用を、画像のないものにはオーディオ、3D モデル、3D モデルアニメーション、コンポジットショット、オフライン（ファイルなし）用を表示します。
