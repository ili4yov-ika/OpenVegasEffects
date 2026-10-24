# CMake ヘルパー

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

メインの [`CMakeLists.txt`](../CMakeLists.txt) が使うスクリプトです。

| ファイル | 役割 |
|----------|------|
| [`DeployVlc.cmake`](DeployVlc.cmake) | ビルドのたびに (`cmake -P`) 実行され、libVLC ランタイム - ライブラリ、デコーダーとキャプチャのモジュールを含む `plugins/`、`plugins.dat`、ライセンスファイル - を `OPENVEGAS_VLC_RUNTIME_DIR` から実行ファイル横の `vlc/` にコピーします |
| [`msvc_environment.cmd.in`](msvc_environment.cmd.in) | `msvc_environment.cmd` のテンプレート。Ninja は MSVC のコンパイルとリンクのコマンドをこれを通して実行します。コンソールを UTF-8 に切り替え (ローカライズされた `/showIncludes` の行が CMake のプレフィックスと一致するように)、選ばれた `cl.exe` が属する Visual Studio の環境を読み込むので、別のインストールのヘッダーが混ざりません |
