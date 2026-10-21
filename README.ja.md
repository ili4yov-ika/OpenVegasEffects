# OpenVegas Effects

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a> |
</p>

![Main Interface](docs/main_ui.png)

**VEGAS Effects** スタイルのコンポジティング／エフェクトアプリケーションを
**C++17 / Qt 6** で実装した、オープン（クリーンルーム）な再実装です。ライセンスは
**GNU GPL v3** です。

リファレンスバイナリのリバースエンジニアリングで明らかになった動作アーキテクチャに
基づき、ゼロから記述しています（`SAMPLES/VEGAS_Effects/MARKDOWN/RE_VegasEffects.md`
参照）。これは**オリジナルコード**です。プロプライエタリなソースは一切コピーして
いません。モジュール構成はリファレンスアプリの観測可能な高レベル構造を踏襲しており、
プロジェクトが互換性のあるワークフローへ段階的に成長できるようにしていますが、
すべての行は新たに書かれています。

現在の状態：**初期スケルトン** — ドックウィンドウ、自由なライセンスマネージャー、
プラグインのスキャンとレジストリ、コンポジションのオブジェクトモデル、SQLite
キャッシュ、プレースホルダーレンダラー。

---

## 実装済みの機能

| 領域 | 状態 |
|------|------|
| エントリポイント（WinMain 風 → UTF-8 argv → QApplication） | 完了 |
| ユーザーデータフォルダー（11 個、初回起動時に作成） | 完了 |
| QSettings（ini）による設定 | 完了 |
| ファイルログ | 完了 |
| 自由なライセンスマネージャー（アクティベーション／サーバーなし） | 完了 |
| PluginManager + レジストリ、`Plugins/` 内の `.vfx` のネイティブ検出 | 完了（スキャン／レジストリ） |
| コンポジションのオブジェクトモデル／レイヤー／クリップ／エフェクト | スケルトン |
| MediaManager／メディアのインポート | スケルトン |
| SQLite キャッシュ（`CacheDB`） | 完了 |
| バックグラウンドレンダーワーカー（プレースホルダー） | スケルトン |
| UI: MainWindow + ドック（Effects パネル、Viewer、Timeline） | 完了（スケルトン） |
| Learn サイドバー（ネイティブウィジェット、Qt WebEngine 不使用） | 完了 |

詳細とロードマップは [`MARKDOWN/ARCHITECTURE.md`](MARKDOWN/ARCHITECTURE.md) にあります。

---

## 技術スタック

| レイヤー | 選択 |
|-------|--------|
| 言語／UI | C++17、Qt 6（Widgets、UI はコードで構築） |
| ビルド | CMake 3.21+（推奨）と `OpenVegasEffects.pro` |
| コンパイラー | MSVC 2022、LLVM-MinGW（Clang）、MinGW、GCC |
| メディア | **libVLC** — ビデオフレーム、PCM マスターミックス、音声出力、ナレーション録音 |
| キャッシュ | QtSql（SQLite `cache.db`） |
| プラグイン | 将来の OFX ホストの基盤 |
| ライセンス | GNU GPL v3（[LICENSE](LICENSE)） |

---

## ビルドと実行

**Qt 6.8+** が必要です（Core/Gui/Widgets/OpenGLWidgets/Sql）。Qt WebEngine は不要
です。Windows で再ビルドする前に、ファイルが使用中でリンクに失敗する場合は
`OpenVegas Effects.exe` を閉じてください。

### libVLC（メディア）

Qt Multimedia は不要になりました。libVLC がビデオと音声をデコードし、PCM
マスターミックスを出力し、マイクをキャプチャします。ミキサーはクリップ区間、
ミュート、ソースオフセット、速度、レベル、ネストされたコンポジションを考慮します。
Audio Meters は同じ最終 PCM を測定します。速度変更にはピッチシフト付きリサンプ
リングを使用します。リアルタイムのネイティブ `.hfpl` DSP/AudioTransition はまだ
接続されていません。音声エクスポートは別個の FFmpeg／ネイティブ DSP パスを維持
します。

VLC のヘッダーとソースは `thirdparty/vlc` にあります。これはソースツリーであり、
すぐに使えるランタイムではありません。`Vlc4Adapter.cpp` アダプターはそのヘッダー
を使用し、VLC 4 ABI を確認します。VLC 3 は別個の互換シグネチャを使用します。今回
のセッションでは Windows VLC 3.0.24 での起動を確認しました。VLC 4 アダプターは
ビルドできますが、VLC 4 での実行と macOS/Linux でのテストはまだです。

パッケージングでは、`thirdparty/vlc/runtime/Windows`、`Darwin`、`Linux` のいずれか
にターゲットアーキテクチャのランタイムを用意してください。ライブラリとその依存
関係、プラグインを含めます。CMake オプション
`-DOPENVEGAS_VLC_RUNTIME_DIR=<prefix>` で別のプレフィックスを指定することもでき
ます。qmake は同名の環境変数を読み取ります。Windows では、用意されたランタイムが
ない場合、Program Files にインストールされた VLC が使用されます。どちらのビルドも
ランタイムを EXE の隣の `vlc` にコピーします。CMake install はこの構造を保持し、
NSIS は DLL とプラグインの存在を検証します。`COPYING` と `COPYING.LIB` も同梱
されます。

ローダーは `OPENVEGAS_VLC_DIR`（実行時の上書き）、アプリケーションの隣、設定された
プレフィックス、システムディレクトリでライブラリを探します。Windows DLL、macOS
dylib、Linux SONAME をサポートし、インポートライブラリは不要です。libVLC が利用
できない場合でもアプリは起動しますが、メディアと録音は利用できません。録音には
DirectShow、qtsound、PulseAudio を使用します。Linux では `pactl` でデバイスを列挙
します（PipeWire Pulse 互換も含む）。Qt から切り替えた後は、Options でマイクを
選び直す必要があります。古い base64 デバイス ID は新しい ID と一致しません。ハード
ウェア録音と音声出力はターゲット OS で別途検証が必要です。テストでは PCM と VLC
player の状態を確認します。

### Qt Creator（qmake）

[`OpenVegasEffects.pro`](OpenVegasEffects.pro) を開き、キット **Qt 6.8+ Widgets**
（MSVC / MinGW / Clang）→ Build。`SOURCES`/`HEADERS` を `CMakeLists.txt` と同期
させてください。

Windows では、qmake が Qt Creator のビルドステップ用に最新の `Makefile.Debug` と
`Makefile.Release` を生成します。フォームヘッダー、moc/rcc、オブジェクトファイル、
翻訳ディレクトリは構成ごとに分かれます。実行ファイルはシャドウビルド内の `debug/`
または `release/` にあります。`.pro` を変更したら **Build → Run qmake** を実行して
ください。別構成の古い Makefile が残っていると、新しいトップレベル Makefile が
あっても `QAudioBuffer` と `ui_*.h` で `C1083` が発生することがあります。

### CMake

```bash
# LLVM-MinGW（推奨、MSVC 不要）
cmake -S . -B build -G "MinGW Makefiles" ^
  -DCMAKE_PREFIX_PATH=C:/Qt/6.9.3/llvm-mingw_64 ^
  -DCMAKE_C_COMPILER=C:/Qt/Tools/llvm-mingw1706_64/bin/clang.exe ^
  -DCMAKE_CXX_COMPILER=C:/Qt/Tools/llvm-mingw1706_64/bin/clang++.exe ^
  -DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/llvm-mingw1706_64/bin/mingw32-make.exe
cmake --build build -j 8
```

### MSVC（プリセット）

[`CMakePresets.json`](CMakePresets.json) のプリセットは、両方の IDE にとっての
唯一の情報源です：

| プリセット | ツールチェーン | ディレクトリ |
|--------|-----------|-----------|
| `windows-msvc-debug` | MSVC v143（VS 2022）+ Qt `msvc2022_64` | `build/Windows_MSVC-x64/` |
| `windows-msvc-release` | 同上、RelWithDebInfo | `build/Windows_MSVC-x64-Release/` |
| `windows-mingw-debug` / `-release` | MinGW GCC + Qt `mingw_64` | `build/Windows_MinGW-x64/` |

**x64 Native Tools Command Prompt for VS 2022** ウィンドウから：

```bat
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug --parallel
```

### GitHub Actions CI

`main` へのプッシュ、プルリクエスト、手動実行時に、[CI](.github/workflows/ci.yml)
ワークフローが MSVC 2022 と Qt 6.9.3 でアプリをビルドし、CTest で 6 つの回帰スイート
を実行し、翻訳カタログを検証します。オーディオテストは VideoLAN 公式サーバーの
libVLC 3.0.24（アーカイブは SHA-256 で固定）と `dummy` 出力を使用するため、ランナー
にオーディオデバイスがなくてもデコードとマスターミックスを確認できます。

### Learn パネル（Qt WebEngine 不使用）

リファレンスは Qt WebEngine を介して Home/Learn に VEGAS のオンラインチュートリアル
を表示し、Web ベースの Library（組み込み localhost サーバーが配信するページ。パッ
ケージには `QtWebEngineProcess.exe` と Chromium リソースが含まれます）を表示します。
この移植版にはチュートリアルがなく、Library はネイティブパネルであるため、
WebEngine を削除しました。**Learn** パネルは同じ操作（システムブラウザーでチュー
トリアルを開く、新しいコンポジットフレーム、メディアのインポート）を通常のウィジェ
ットで提供します。**Window → Toggle Learn Sidebar** で切り替えられ、状態は記憶され
ます（`learnSidebarIsOpen`）。ヘルプもシステムブラウザーで開き、Web コンポーネント
のライセンスは使用しません。このパネルは MinGW ビルドでも利用可能になり（以前は
MSVC キットが必要でした）、Debug 配置ディレクトリは約 900 MB 小さくなりました。

### OpenEXR（`.exr` へのフレーム書き出し）

Qt には EXR 書き出しプラグインがないため、Export パネルの **OpenEXR (.exr)**
プリセットは OpenEXR を有効にしたビルドでのみ動作します。デフォルトでは無効です：

```bat
cmake --preset windows-msvc-debug -DOPENVEGAS_WITH_OPENEXR=ON
```

検索順序：まずインストール済みの `find_package(OpenEXR 3 CONFIG)`、見つからなければ
[`thirdparty/openexr`](thirdparty/openexr) のソースを使用します。

**依存関係に関する注意。** `thirdparty/openexr` の隣に Imath がないため、CMake の
OpenEXR は configure 時に GitHub（`main` ブランチ）から取得します。つまりビルドは
オフラインでも再現可能でもなくなります。クローンされた OpenEXR 自体は `main` の
`4.0.0-dev` 版ですが、リファレンスは安定版の 3.1 を同梱しています。再現性のために、
リリースタグを固定し、Imath を隣にベンダリングすることを推奨します。

このオプションがない場合、`.exr` 書き出しは黙って失敗せず、ビルドに OpenEXR が
ないことを報告します。

### VS Code と Zed（Qt MSVC デバッグ）

どちらの IDE も Qt `msvc2022_64` 用に設定され、MSVC 環境を自身で取得します。別途
developer prompt は不要です。ビルドタスクは `vcvars64.bat` を呼び出します
（`vswhere -version "[17.0,18.0)"` を使用し、並行してインストールされた VS 18 の
ツールセットが Qt キットと一致しないものを拾わないようにしています）。

CMake Tools のビルドボタンには `cmake.useVsDeveloperEnvironment=always` が有効で、
`cmake.buildTask=false` により起動はプリセット環境とともに拡張機能に任されます。
`Ctrl+Shift+B` タスクと F5 前のビルドは `tools/msvc_build.cmd` を呼び出し、これが
MSVC 環境を自身で読み込みます。Ninja の場合、CMake は `msvc_environment.cmd` も
生成します。コンパイルとリンクのコマンドは、選択された `cl.exe` が属するインストー
ルの環境を読み込みます。これにより、CMake Tools がより新しい Visual Studio の環境
を選んだ場合でも、VS 2022 コンパイラーと VS 2026 ヘッダーを混在させたときの
`STL1001` を防ぎます。既存のビルドディレクトリでは、Ninja ルールを更新するために
一度 configure を実行してください。ランチャーは UTF-8（`chcp 65001`）も有効に
します。そうしないと、ロシア語の `/showIncludes` 行が CMake のプレフィックスと一致
せず、Ninja がヘッダー依存関係を見落とし、クラス変更後に互換性のないオブジェクト
ファイルを混在させる可能性があります。ディレクトリが誤ったコードページでビルド済み
の場合は、configure 後に `cmake --build build/Windows_MSVC-x64 --target clean` を
実行し、完全に再ビルドしてください。通常のターミナルからは次を使用します：

```bat
tools\msvc_build.cmd build windows-msvc-debug
```

スクリプトは任意のディレクトリから実行できます。引数なしでは
`buildall windows-msvc-debug`（configure と build）を実行します。`configure`、
`build`、`probe`、`clean` も使用できます。例えば
`tools\msvc_build.cmd clean windows-msvc-release` は CMake 経由で Release の結果
だけをクリーンし、ビルド構成を保持します。

インストーラーの場合は、まず Release をビルドし、アプリを一時プレフィックスに
インストールして Qt の依存関係を配置します。プロジェクトルートから：

```bat
tools\msvc_build.cmd buildall windows-msvc-release
cmake --install build\Windows_MSVC-x64-Release --prefix build\installer-stage
"C:\Qt\6.9.3\msvc2022_64\bin\windeployqt.exe" --release build\installer-stage\bin\OpenVegasEffects.exe
"C:\Program Files (x86)\NSIS\makensis.exe" /DBUILD_DIR=build\installer-stage /DOUTPUT_FILE=build\OpenVegasEffects_Setup.exe tools\nsis_installer.nsi
```

NSIS には、アプリと `platforms\qwindows.dll` を含む準備済みの `bin` が必要です。
コンパイル中に `tools/nsis_payload.ps1` が Windows PowerShell 経由でファイルリスト
を作成し、ネストされた Qt リソースやプレフィックスに追加されたプラグインも含めます。
アンインストールはこのリストを使用し、空になったディレクトリだけを削除します。

`.vegfx` プロジェクトファイルは `resources/icons/iconfile.svg` アイコンで表示され
ます。これは EXE に 2 番目のアイコン（インデックス 1、
`resources/icons/iconfile.ico`、`python tools/make_file_icon.py` で再生成）として
埋め込まれます。インストーラーは `OpenVegasEffects.Project` ProgID の `DefaultIcon`
でこれを参照し、起動時にプログラムは同じ ProgID、`OpenWithProgids`、
`Applications\OpenVegasEffects.exe` を現在のユーザー向けに登録します
（`app/FileAssociation`）。これにより、ポータブルビルドを既定のプログラムとして
選択した場合（「プログラムから開く → 常に」）にもアイコンが表示されます。`.vegfx`
の既定のプログラムに自身を設定することはありません。

- **VS Code** — [`.vscode/`](.vscode/)：`launch.json`（`cppvsdbg` デバッガー、
  MSVC PDB を読み取り。Debug / RelWithDebInfo / attach 構成）、`tasks.json`
  （configure / build / clean / run）、`qt6.natvis`（デバッガーで `QString`、
  `QList`、`QRect`… を読みやすく表示）。拡張機能は `extensions.json` にあります。
  F5 でビルドして実行します。
- **Zed** — [`.zed/debug.json`](.zed/debug.json)（CodeLLDB アダプター、PDB は exe
  の隣）と同じプリセットの [`.zed/tasks.json`](.zed/tasks.json)。`clangd` は
  `build/Windows_MSVC-x64/compile_commands.json` をインデックスします。

Qt Creator の外で実行する場合 — Qt の bin/plugins を PATH に追加します：

```powershell
$env:PATH = "C:/Qt/6.9.3/llvm-mingw_64/bin;" + $env:PATH
$env:QT_QPA_PLATFORM_PLUGIN_PATH = "C:/Qt/6.9.3/llvm-mingw_64/plugins"
```

**コミットしないでください** `build/` — [`.gitignore`](.gitignore) に含まれています。

---

## ローカリゼーション

インターフェースはロシア語、日本語、簡体字中国語に翻訳されています。ソース言語は
英語です。

| 言語 | カバレッジ | ソース |
|------|----------|--------|
| ロシア語 | 216/233 | この移植版のために作成 |
| 日本語 | 206/233 | 79 文字列はリファレンスの `.qm` から、残りはこの移植版のために作成 |
| 簡体字中国語 | 206/233 | 79 文字列はリファレンスの `.qm` から、残りはこの移植版のために作成 |

未翻訳のものはフォーマット文字列（`%1:%2:%3:%4`、`00:00:00:00`、`|<`、`+1`）で、
翻訳する必要はありません。

ja/zh の用語は `SAMPLES/VEGAS_Effects/Translations/*/VegasEffects_*.qm`
（12,041 メッセージ、`lconvert` で変換）から取得しています。ソーステキストが一致
する文字列はリファレンス自身の表現になります。移植版のために翻訳された文字列は
`<translatorcomment>` でマークされ、Qt Linguist でネイティブスピーカーによる校正が
必要なものとして表示されます。

### 仕組み

- カタログは [`translations/openvegaseffects_*.ts`](translations/) です。CMake は
  `qt_add_translations` でコンパイルし、`:/i18n/` としてバイナリに埋め込みます。
  Qt **LinguistTools** コンポーネントが必要です。これがない場合でもビルドは成功
  しますが、インターフェースは英語のままです。
- 読み込み — `app::Translations::install()`。ウィジェットが作成される**前に**
  `main.cpp` で呼び出されます。
- 検索順序はリファレンスを踏襲します。まず
  `<user data>/Translations/<locale>/openvegaseffects_<locale>.qm`（独自のカタログ
  を置くこともできます）、次に埋め込みカタログです。一致する `qt_<locale>.qm` も
  同時に取得されます。
- 言語は **Options → Interface → Language** で選択し、キーは `Options/Language`
  です。空 = システムに従う。次回起動時に適用されます。

### 文字列編集後にカタログを更新する

```bat
cmake --build --preset windows-msvc-debug --target update_translations
```

---

## 実行時データフォルダー

初回起動時に、アプリケーションはユーザーデータフォルダーを作成し、リファレンスの
レイアウトを再現します（リゾルバー `FUN_14025b000` から復元）：`Translations`、
`Templates\AV`、`EnvironmentMaps`、`ExportPresets`、`PluginPresets`、`Plugins`、
`Presets`、`Textures`、`Templates\Workspaces`、`Objects`、`Tutorials`。このうち
2 つは `Templates` の下にネストされています。リファレンスは裸の `Templates`
フォルダーを公開しません。Windows ではルートは
`%LOCALAPPDATA%\OpenVegas\OpenVegasEffects` です。

リファレンスとの違いは意図的です。リファレンスはフォルダーを確認するだけで、存在
しない場合は空のパスを返します（ツリーはインストーラーが配置します）。一方、この
移植版にはインストーラーがなく、自身で作成します。

エフェクト定義ファイル（`.vfx`、JSON）を `Plugins` フォルダーに置くと、
**Effects** パネルに表示されます。

---

## リポジトリ構成

```
OpenVegas_Effects/
├── CMakeLists.txt / OpenVegasEffects.pro
├── src/
│   ├── main.cpp          # WinMain 風エントリ → UTF-8 argv → QApplication
│   ├── core/             # Identifier, Result, Version, Log
│   ├── app/              # AppMain 初期化、UserDataPaths、Settings
│   ├── license/          # LicenseManager + 自由なマネージャー
│   ├── plugin/           # Plugins/、PluginPresets/ — 検出 + レジストリ
│   ├── media/            # メディアのインポート
│   ├── composition/      # Composition / レイヤー / クリップ / エフェクト
│   ├── cache/            # SQLite キャッシュ（QtSql）
│   ├── render/           # バックグラウンドレンダーワーカー（プレースホルダー）
│   └── ui/               # MainWindow、Effects パネル、Viewer、Timeline
├── resources/            # アイコン
├── MARKDOWN/             # 開発者向けの計画 / リバースエンジニアリングノート
├── tools/                # ユーティリティ（アナライザー、NSIS、svg_to_ico など）
└── SAMPLES/              # リファレンスバイナリと RE ノート（ビルドには含まれない）
```

`SAMPLES/` にはリファレンスバイナリとリバースエンジニアリングノートが含まれています。
オープンビルドには含まれません。

---

## ドキュメント

1. **アーキテクチャと計画** — [`MARKDOWN/ARCHITECTURE.md`](MARKDOWN/ARCHITECTURE.md)
2. **開発ルール** — [`MARKDOWN/INIT.MD`](MARKDOWN/INIT.MD)
3. **VEGAS Effects リファレンスのリバースエンジニアリング** — [`MARKDOWN/RE_VegasEffects.md`](MARKDOWN/RE_VegasEffects.md)
4. **リファレンス配布物の分析** — [`MARKDOWN/RE_VegasEffects_Distribution.md`](MARKDOWN/RE_VegasEffects_Distribution.md)

---

## ライセンス

このリポジトリのコードは、特に明記がない限り **GNU GPL v3** でライセンスされて
います。全文：[`LICENSE`](LICENSE)。

`SAMPLES/` 内のリファレンスバイナリは各権利者の所有物であり、研究目的でのみ保管
されています。
