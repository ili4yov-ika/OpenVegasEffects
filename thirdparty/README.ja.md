# サードパーティのコード

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

コードと一緒に置いているライブラリとヘッダーです。

| フォルダー | 内容 | 用途 |
|------------|------|------|
| [`vlc/`](vlc/) | VLC のヘッダーとソース | メディア層が実行時に読み込む libVLC の API (`include/`)。`runtime/<OS>` にはパッケージ用に用意したランタイムを置けます。ソースはビルドしません |
| [`openexr/`](openexr/) | OpenEXR | `.exr` フレームの書き出し。`-DOPENVEGAS_WITH_OPENEXR=ON` のときのみ |
| [`alembic/`](alembic/) | Alembic | `.abc` モデルの読み込み。`-DOPENVEGAS_WITH_ALEMBIC=ON` のときのみ |
| `openfx/` | OpenFX 標準 (git サブモジュール) | 参照用のみ。ビルドしません |
| `kdenlive/` | Kdenlive のソース | ローカルの参照資料。リポジトリには含まれません (`.gitignore`) |

各ライブラリのフォルダーにはそれぞれのライセンスファイルがあります。OpenEXR の Imath
依存に関する注意点はメインの [`README.ja.md`](../README.ja.md) にあります。
