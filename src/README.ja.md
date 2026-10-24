# ソースコード

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

アプリケーションの C++17 / Qt 6 のコードで、モジュールごとにフォルダーが分かれています。
モジュールの構成はリファレンスの上位構造に倣っています ([`MARKDOWN/ARCHITECTURE.md`](../MARKDOWN/ARCHITECTURE.md) を参照)。

| フォルダー | 内容 |
|------------|------|
| [`main.cpp`](main.cpp) | エントリーポイント: UTF-8 のコマンドライン、`QApplication`、翻訳、`app::AppMain` |
| [`core/`](core/) | 基本型: `Identifier`、`Result`、`Version`、ファイルログ |
| [`app/`](app/) | 起動 (`AppMain`)、ユーザーデータのフォルダー、設定、翻訳、AV テンプレート、プロジェクトの既定値、プラグインフォルダー、ファイルの関連付け、GPU ドライバーの確認 |
| [`license/`](license/) | ライセンスのインターフェースと、常に有効なライセンスを報告するフリーのマネージャー |
| [`composition/`](composition/) | モデル: コンポジットショット、レイヤー、クリップ、エフェクト、キーフレーム、マスク、テキストスタイル、トランジション、Behaviors とそのシミュレーション、モーショントラッキング |
| [`project/`](project/) | リファレンス形式の `.vegfx` プロジェクトと `.vegfxcs` コンポジットショットの読み書き、プロジェクトの統合、エフェクトプリセット (`.hfpr`)、Photoshop ドキュメントのレイヤー単位の読み込み (`PsdImport`) |
| [`media/`](media/) | メディアの読み込みとプローブ、libVLC (`VlcBackend`、`Vlc4Adapter`)、動画のデコード、リアルタイムオーディオミキサー (`AudioPlayer`)、ナレーション録音、波形、コンフォームしたオーディオ、プロキシ、デコード済みフレームのキャッシュ、Photoshop ドキュメント (`PsdDocument`) |
| [`plugin/`](plugin/) | プラグインレジストリとネイティブ `.hfpl` モジュールのホスト (`NativeEffectRender`、`NativePlugin`): 2D エフェクト、トランジション、オーディオ、Behaviors、Geometry、ビューアー上の独自 UI |
| [`model3d/`](model3d/) | 3D モデルの読み込み (OBJ、FBX、glTF、3DS、LWO、Alembic)、メッシュ、3D レンダラー |
| [`render/`](render/) | フレームのレンダリング (`RenderManager`)、テキスト、レイヤーのジオメトリ、書き出しとそのプリセット、FFmpeg による映像と音声のエンコード、プリレンダー、フレームのディスクキャッシュ |
| [`cache/`](cache/) | SQLite のメディアキャッシュデータベース |
| [`ui/`](ui/) | メインウィンドウ、パネル (Timeline、Viewer、Controls、Layer、Layout、Media、Text、Effects、Export など)、ダイアログ、ビューアーのオーバーレイ |

これらのクラスが読み込む Qt Designer フォームは、リポジトリ直下の [`ui/`](../ui/) にあります。
