# ツール

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

ビルド、パッケージ作成、翻訳、リバースエンジニアリングのための補助ツールです。

## ビルドとパッケージ作成

| ファイル | 役割 |
|----------|------|
| [`msvc_build.cmd`](msvc_build.cmd) | Visual Studio 2022 の環境を読み込んで CMake プリセットを構成・ビルドします: `msvc_build.cmd <コマンド> [windows-msvc-debug|windows-msvc-release]`、コマンドは `configure`、`build`（未構成のツリーは先に構成）、`buildall`、`probe`、`clean`、`tests`（`build/Windows_MSVC-x64-Release-Tests` で回帰テストをビルドして実行） |
| [`build_windows_installer.bat`](build_windows_installer.bat) | Windows インストーラーを作成します: MSVC と Ninja による Release ビルド、`cmake --install`、windeployqt、MSVC ランタイム、NSIS |
| [`nsis_installer.nsi`](nsis_installer.nsi)、[`nsis_payload.ps1`](nsis_payload.ps1) | NSIS スクリプトと、それがまとめるファイルの一覧 |
| [`build_deb.sh`](build_deb.sh)、[`debian/`](debian/) | `.deb` パッケージ |
| [`build_rpm.sh`](build_rpm.sh)、[`rpm/`](rpm/) | `.rpm` パッケージ |
| [`setup_macos.sh`](setup_macos.sh)、[`macos_build.sh`](macos_build.sh) | macOS: Homebrew による Qt、CMake、Ninja のインストール、プリセットでのビルド |
| [`openvegaseffects.desktop`](openvegaseffects.desktop) | Linux のデスクトップエントリー |
| [`svg_to_ico.py`](svg_to_ico.py)、[`make_file_icon.py`](make_file_icon.py) | SVG からアプリケーションのアイコンと `.vegfx` ファイルのアイコンを作成 |

## 翻訳

| ファイル | 役割 |
|----------|------|
| [`validate_translations.py`](validate_translations.py) | `.ts` カタログの有効なメッセージをすべて検査: 網羅性、プレースホルダー、複数形、マークアップ、ファイルフィルター |
| [`plugin_translations.py`](plugin_translations.py) | リファレンスのカタログからプラグインのカタログ `openvegaseffects_plugins_<言語>.ts` を書き出します |
| [`merge_ts_translations.py`](merge_ts_translations.py) | 参照カタログから、コンテキストまたは一意な原文によってカタログを埋めます |
| [`finish_nonempty_translations.py`](finish_nonempty_translations.py) | ファイルを整形し直さずに、入力済みの翻訳を完了済みにします |
| [`normalize_ui_double_properties.py`](normalize_ui_double_properties.py) | `.ui` フォーム内の `QDoubleSpinBox` の浮動小数点プロパティを正規化します |

## リバースエンジニアリング

| ファイル | 役割 |
|----------|------|
| [`hfpl_runtime_probe.cpp`](hfpl_runtime_probe.cpp) | ネイティブ `.hfpl` モジュールのコンソール用プローブ (ターゲット `hfpl_runtime_probe`、`msvc_build.cmd probe`): モジュールを読み込み、さまざまなホストのコンテキストで実行し、何を読み何を描くかを報告します |
| `*Reference.java`、[`LayerStrings.java`](LayerStrings.java) | リファレンスを読み取り専用で解析し、ビルダーやテーブル (Controls、Options、Layer、Timeline、360 Viewer、Behaviors) を取り出す Ghidra スクリプト。[`MARKDOWN/`](../MARKDOWN/) のメモに使います |
