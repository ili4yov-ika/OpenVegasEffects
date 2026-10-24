# リソース

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

アプリケーションがコード以外に埋め込む、またはインストールするものです。

| パス | 内容 |
|------|------|
| [`icons.qrc`](icons.qrc) | すべてのアイコンを `:/icons/...` として埋め込む Qt リソースファイル |
| [`icons/`](icons/) | アイコン: プログラム独自の SVG (`toolbar/`、`text/`、`timeline/`)、リファレンスの PNG をそのままコピーしたもの (`layout/` - その README を参照 - と `viewer/`)、アプリケーションのロゴ (`logo.svg`、`logo.png`、`logo.ico`)、`.vegfx` ファイルのアイコン (`iconfile.svg`、`iconfile.ico`)、スプラッシュ画面 (`splash.svg`) |
| [`OpenVegasEffects.rc`](OpenVegasEffects.rc) | Windows のリソーススクリプト: 実行ファイルのアイコン (インデックス 0) と `.vegfx` ファイルのアイコン (インデックス 1、ファイルの関連付けで使用) |
| [`macos/Info.plist`](macos/Info.plist) | macOS ビルド用のバンドル定義 |

`logo.ico` は `tools/svg_to_ico.py` で `logo.svg` から、`iconfile.ico` は
`tools/make_file_icon.py` で `iconfile.svg` から作られます。
