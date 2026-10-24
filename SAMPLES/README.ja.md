# サンプルとリファレンス資料

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

開発用の資料です。ここにあるものはビルドには含まれません。

| フォルダー | 内容 |
|------------|------|
| `VEGAS_Effects/` | リファレンスアプリケーション (VEGAS Effects 5.0.2) のローカルコピー: バイナリ、ネイティブ `.hfpl` プラグイン (`Plugins/`)、翻訳、環境マップ、逆コンパイルしたソース (`decompile-src/`)。**リポジトリには含まれません** (`SAMPLES/.gitignore`)。リバースエンジニアリング用ツールや、リファレンスのプラグインを必要とするテストを動かすには、インストール済みのコピーをここに置いてください |
| [`vegfx_projects/`](vegfx_projects/) | サンプルプロジェクト: `project_1` (リファレンスで作った 2D プロジェクト)、`project_2` (3D モデル)、`project_3` (面にテキストを貼った立方体、キーフレームによる回転、すべてに掛けたエフェクト、オーディオエフェクト付きの音楽ファイル 2 つ。テスト `buildProject3` が書き出します) |
| [`screenshots/`](screenshots/) | ピクセル単位の分析を添えたリファレンスのスクリーンショット。インターフェースの正解の基準 |
| [`icons-from-VegesEffects/`](icons-from-VegesEffects/) | リファレンスのリソースから取り出したアイコンとそのカタログ |

サンプルプロジェクトのメディアファイル (`*.mp3`、`*.mp4`、`*.png`、`*.psd`) は、意図的に
追加しない限り `vegfx_projects/.gitignore` で無視されます。
