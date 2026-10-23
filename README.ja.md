# OpenVegas Effects

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

![メイン画面](docs/main_ui.png)

**VEGAS Effects** スタイルのコンポジット／エフェクトアプリケーションを、オープンな
（クリーンルーム方式の）実装として **C++17 / Qt 6** で書き直したものです。ライセンスは
**GNU GPL v3** です。

リファレンスのバイナリをリバースエンジニアリングして明らかになった動作上のアーキテクチャを
もとに、ゼロから書かれています（[`MARKDOWN/RE_VegasEffects.md`](MARKDOWN/RE_VegasEffects.md)
を参照）。これは**オリジナルのコード**であり、プロプライエタリなソースは一切コピーして
いません。モジュール構成はリファレンスで観察できる上位構造に合わせてあり、互換性のある
ワークフローへと育てていけるようにしていますが、すべての行は新たに書かれています。

現在の状況：**リファレンスのコンポジットワークフローの大部分が Windows で動作します** —
リファレンスの `.vegfx` 形式のプロジェクト、ネストしたコンポジットショットを持つ
タイムライン、2D／3D レイヤー、テキスト、モデル、マスクとトラッキング、VEGAS Effects の
ネイティブ `.hfpl` プラグイン、オーディオ、エクスポート。未完了の項目は
[`MARKDOWN/CHECKLIST.md`](MARKDOWN/CHECKLIST.md) にあります。

---

## すでに動作するもの

| 分野 | 状態 |
|------|------|
| プロジェクト：リファレンスの `.vegfx` 形式（読み込みと書き出し）、相対パス、自動保存と復元 | 完了 |
| タイムライン：コンポジットショット、タブで開くネストショット、2D／3D レイヤー、クリップ、スライスとレートストレッチ、キーフレームと値グラフ、トランジション | 完了 |
| VEGAS Effects のネイティブ `.hfpl` プラグイン：321 モジュールすべてが動作（2D エフェクト 241、Behavior 43、オーディオエフェクト 16、ビデオトランジション 15、Geometry 4、オーディオトランジション 2）。コントロール、ビューアー上の独自 UI、`.hfpr` プリセット | 完了 |
| テキストレイヤー：スタイル、3D 押し出し、文字単位の Behavior、Text パネル | 完了 |
| 3D：モデル（OBJ、スキンとブレンドシェイプ付き FBX、glTF、3DS、LWO、Alembic は任意）、カメラ、ライト、影、ピクセル単位の深度 | 完了 |
| マスク、モーショントラッキング（MotionTrack）、スタビライズ | 完了 |
| オーバーレイと縮小プレビューを備えたビューアー、360 ビューアー、Layer・Trimmer・Layout パネル | 完了 |
| オーディオ：libVLC による再生、マスターミックス、Audio Meters、ネイティブオーディオエフェクトのリアルタイム処理、ボイスオーバー録音 | Windows で完了 |
| エクスポート：FFmpeg によるビデオ、連番画像、オーディオ、エクスポートキューとプリセット、OpenEXR（任意） | 完了 |
| プロキシ、プリレンダー、メディアキャッシュ、フレームのディスクキャッシュ | 完了 |
| ロシア語・日本語・簡体字中国語のインターフェイス（プラグインの文字列を含む） | 完了 |
| Windows インストーラー（NSIS）、`.deb` と `.rpm` パッケージ | Windows は検証済み、Linux パッケージは未検証 |
| macOS／Linux でのオーディオと VLC 4 | 未検証 |

詳細は [`MARKDOWN/ARCHITECTURE.md`](MARKDOWN/ARCHITECTURE.md) と
[`MARKDOWN/`](MARKDOWN/) のリバースエンジニアリングのノートにあります。

---

## 技術スタック

| 層 | 採用技術 |
|----|----------|
| 言語／UI | C++17、Qt 6（Widgets、OpenGL。フォームは `ui/`、それ以外はコードで構築） |
| ビルド | CMake 3.21+（推奨）と `OpenVegasEffects.pro` |
| コンパイラー | MSVC 2022（メイン）、MinGW／LLVM-MinGW、GCC、Clang |
| メディア | **libVLC** — ビデオフレーム、PCM マスターミックス、音声出力、ボイスオーバー録音 |
| エクスポート、プロキシ、波形 | `PATH` 上の **FFmpeg** 実行ファイル |
| キャッシュ | QtSql（SQLite `cache.db`）とフレームのディスクキャッシュ |
| プラグイン | VEGAS Effects のネイティブ `.hfpl` モジュールのホスト（OpenGL 4.1） |
| ライセンス | GNU GPL v3（[LICENSE](LICENSE)） |

---

## ビルドと実行

**Qt 6.8+** が必要です（Core/Gui/Widgets/OpenGLWidgets/Sql/Svg/Xml、翻訳には
LinguistTools）。Qt WebEngine は不要です。Windows でファイルが使用中のためリンクに失敗
する場合は、再ビルドの前に `OpenVegasEffects.exe` を閉じてください。

### ネイティブプラグイン（`.hfpl`）

エフェクト、トランジション、オーディオエフェクト、Behavior、Geometry モジュールは
リファレンス自身の `.hfpl` ファイルで、ポートのホストが実行します。これらはこの
リポジトリには含まれていません。アプリケーションはインストール済みの VEGAS Effects
（`C:\Program Files\FXhome\VEGAS Effects\Plugins`）または **オプション → プラグイン** で
選んだフォルダーから見つけます（他の FXhome 製品のプラグインも使えます）。プラグインが
なくても組み込みのエフェクトは利用できます。

### libVLC（メディア）

Qt Multimedia は使用しません。libVLC がビデオとオーディオをデコードし、PCM マスター
ミックスを出力し、マイクを録音します。ミキサーはクリップの区間、ミュート、ソースの
オフセット、速度、レベル、ネストしたコンポジションを考慮し、Audio Meters は同じ最終
PCM を計測します。速度変更はピッチシフトを伴うリサンプリングです。ネイティブ `.hfpl` の
オーディオエフェクトとオーディオトランジションは、リアルタイムミックスでもオーディオの
エクスポートでも動作します。

VLC のヘッダーとソースは `thirdparty/vlc` にあります。これはソースツリーであり、すぐに
使えるランタイムではありません。`Vlc4Adapter.cpp` アダプターはそのヘッダーを使って
VLC 4 の ABI を確認し、VLC 3 には互換性のある別のシグネチャを使います。起動は Windows
版 VLC 3.0.24 で確認済みです。VLC 4 アダプターはビルドできますが、VLC 4 での実行と
macOS／Linux での検証はまだです。

パッケージングでは、対象アーキテクチャ向けのランタイム（ライブラリとその依存関係、
プラグイン）を `thirdparty/vlc/runtime/Windows`、`Darwin`、`Linux` に用意します。CMake
オプション `-DOPENVEGAS_VLC_RUNTIME_DIR=<prefix>` で別のプレフィックスを指定することも
でき、qmake は同名の環境変数を読みます。Windows で用意されたランタイムがない場合は、
Program Files にインストールされた VLC を使います。どちらのビルドもランタイムを EXE の
隣の `vlc` にコピーし、CMake のインストールはこの構成を保ち、NSIS は DLL とプラグインの
存在を検証します。`COPYING` と `COPYING.LIB` も同梱されます。

ローダーは `OPENVEGAS_VLC_DIR`（実行時の上書き）、アプリケーションの隣、設定された
プレフィックス、システムディレクトリの順にライブラリを探します。Windows の DLL、macOS の
dylib、Linux の SONAME に対応し、インポートライブラリは不要です。libVLC がなくても
アプリケーションは起動しますが、メディアと録音は使えません。録音には DirectShow、
qtsound、PulseAudio を使い、Linux ではデバイスを `pactl` で列挙します（PipeWire の
Pulse 互換も含む）。デバイスからの録音と音声出力は、対象 OS ごとに別途確認が必要です。
テストは PCM と VLC プレーヤーの状態を確認します。

### Qt Creator（qmake）

[`OpenVegasEffects.pro`](OpenVegasEffects.pro) を開き、キット **Qt 6.8+ Widgets**
（MSVC／MinGW／Clang）→ ビルド。`SOURCES`／`HEADERS` は `CMakeLists.txt` と一致させて
ください。

Windows では qmake が Qt Creator のビルドステップ用に最新の `Makefile.Debug` と
`Makefile.Release` を生成します。フォームのヘッダー、moc／rcc の出力、オブジェクト
ファイル、翻訳ディレクトリは構成ごとに分かれ、実行ファイルはシャドウビルド内の `debug/`
または `release/` にあります。`.pro` を編集したら **ビルド → qmake の実行** を行って
ください。別の構成の古い Makefile が残っていると、新しい最上位の Makefile があっても
`ui_*.h` について `C1083` が出ることがあります。

### MSVC（プリセット）

[`CMakePresets.json`](CMakePresets.json) のプリセットが IDE 共通の唯一の設定元です：

| プリセット | ツールチェーン | ディレクトリ |
|------------|----------------|--------------|
| `windows-msvc-debug` | MSVC v143（VS 2022）+ Qt `msvc2022_64` | `build/Windows_MSVC-x64/` |
| `windows-msvc-release` | 同上、RelWithDebInfo | `build/Windows_MSVC-x64-Release/` |
| `windows-mingw-debug` / `-release` | MinGW GCC + Qt `mingw_64` | `build/Windows_MinGW-x64/` |
| `linux-debug` / `-release`、`macos-debug` / `-release` | システムのコンパイラー + Ninja | `build/linux/`、`build/macos/` |

**x64 Native Tools Command Prompt for VS 2022** ウィンドウから：

```bat
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug --parallel
```

通常のターミナルからは、MSVC 環境を自分で読み込む `tools\msvc_build.cmd` で同じことが
できます：

```bat
tools\msvc_build.cmd build windows-msvc-debug
```

スクリプトはどのディレクトリからでも実行できます。引数なしでは
`buildall windows-msvc-debug`（構成とビルド）を実行し、`configure`、`build`、`probe`、
`clean` も使えます。たとえば `tools\msvc_build.cmd clean windows-msvc-release` は、
ビルド構成を残したまま CMake で Release の成果物だけを消去します。

### MinGW での CMake

```bash
cmake -S . -B build -G "MinGW Makefiles" ^
  -DCMAKE_PREFIX_PATH=C:/Qt/6.9.3/llvm-mingw_64 ^
  -DCMAKE_C_COMPILER=C:/Qt/Tools/llvm-mingw1706_64/bin/clang.exe ^
  -DCMAKE_CXX_COMPILER=C:/Qt/Tools/llvm-mingw1706_64/bin/clang++.exe ^
  -DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/llvm-mingw1706_64/bin/mingw32-make.exe
cmake --build build -j 8
```

### Linux と macOS

`tools/build_deb.sh` と `tools/build_rpm.sh` は `openvegaseffects` パッケージをビルド
します（`.deb` は dpkg-buildpackage、`.rpm` は rpmbuild）。パッケージにはアプリケーション、
デスクトップエントリ、アイコンが含まれます。macOS では `tools/setup_macos.sh` が Homebrew
で Qt 6、CMake、Ninja をインストールし、`tools/macos_build.sh [debug|release] [test]
[deploy]` がプリセットでビルドします。これらの手順はまだ検証されていません。

### Windows インストーラー

インストーラーは 1 つのスクリプトでビルドでき、任意のフォルダーから実行できます：

```bat
tools\build_windows_installer.bat
```

スクリプトは Visual Studio 2022（vswhere）と Qt の `msvc2022_64` キットを自動で見つけ、
`build\windows-installer` で Ninja により Release をビルドし、新しいプレフィックスに
インストールし、windeployqt で Qt を配置し、MSVC ランタイムの DLL を EXE の隣に
コピーして `tools\OpenVegasEffects_Setup.exe` を作成します。`CMAKE_PREFIX_PATH` で
別の Qt キットを、`OPENVEGAS_VLC_RUNTIME_DIR` で別の libVLC ランタイムを指定できます。
別のジェネレーター、コンパイラー、Qt キットで構成されたビルドツリーは作り直されます。

手動では同じ手順は次のとおりです（`copy` の行には `VCToolsRedistDir` を設定する
Developer Command Prompt for VS 2022 が必要です）。プロジェクトルートから：

```bat
tools\msvc_build.cmd buildall windows-msvc-release
cmake --install build\Windows_MSVC-x64-Release --prefix build\installer-stage
"C:\Qt\6.9.3\msvc2022_64\bin\windeployqt.exe" --release --no-compiler-runtime build\installer-stage\bin\OpenVegasEffects.exe
copy "%VCToolsRedistDir%x64\Microsoft.VC143.CRT\*.dll" build\installer-stage\bin\
"C:\Program Files (x86)\NSIS\makensis.exe" /DBUILD_DIR=build\installer-stage /DOUTPUT_FILE=build\OpenVegasEffects_Setup.exe tools\nsis_installer.nsi
```

NSIS には、アプリと `platforms\qwindows.dll` を含む準備済みの `bin` が必要です。
コンパイル中に `tools/nsis_payload.ps1` が Windows PowerShell 経由でファイルリスト
（ネストした Qt リソースやプレフィックスに追加されたプラグインを含む）を作成します。
アンインストールはこのリストを使い、空になったディレクトリだけを削除します。

`.vegfx` プロジェクトファイルは `resources/icons/iconfile.svg` のアイコンで表示されます。
このアイコンは EXE に 2 番目のアイコンとして埋め込まれています（インデックス 1、
`resources/icons/iconfile.ico`、`python tools/make_file_icon.py` で再生成）。
インストーラーは ProgID `OpenVegasEffects.Project` の `DefaultIcon` でこれを参照し、
プログラムは起動時に同じ ProgID、`OpenWithProgids`、`Applications\OpenVegasEffects.exe`
を現在のユーザーに登録します（`app/FileAssociation`）。そのためポータブル版でも、既定の
プログラムとして選んだとき（「プログラムから開く → 常に使う」）にアイコンが表示されます。
`.vegfx` の既定のプログラムに自分自身を設定することはありません。

### テストと CI

回帰テストは [`tests/`](tests/) にある独立した CMake プロジェクトです。
[`tests/README.ja.md`](tests/README.ja.md) を参照してください。`main` への push、
プルリクエスト、手動実行のたびに、[CI](.github/workflows/ci.yml) ワークフローが
MSVC 2022 と Qt 6.9.3 でアプリケーションをビルドし、CTest で 6 つのスイートを実行し、
翻訳カタログを検証します。オーディオテストは VideoLAN 公式サーバーの libVLC 3.0.24
（アーカイブは SHA-256 で固定）と `dummy` 出力を使うため、ランナーにオーディオデバイスが
なくてもデコードとマスターミックスを確認できます。ネイティブプラグインを実行するテストには
OpenGL 4.1 のコンテキストが必要で、ない場合は自動的にスキップされます。

### Learn パネル（Qt WebEngine なし）

リファレンスは Home／Learn で VEGAS のオンラインチュートリアルを、Qt WebEngine で
Web ベースのライブラリを表示します。ポートにはチュートリアルがなく、ライブラリは
ネイティブパネルなので WebEngine は使いません。**Learn** パネルは通常のウィジェットで
作られ、同じ操作（システムのブラウザーでチュートリアル、新しいコンポジットショット、
メディアの読み込み）を備えています。**ウィンドウ → 学習サイドバーをトグル** で切り替え、
状態は記憶されます（`learnSidebarIsOpen`）。ヘルプもシステムのブラウザーで開きます。

### OpenEXR（`.exr` へのフレームのエクスポート）

Qt には EXR の書き出しプラグインがないため、Export パネルの **OpenEXR (.exr)** プリセット
は OpenEXR 付きのビルドでのみ動作します。既定では無効です：

```bat
cmake --preset windows-msvc-debug -DOPENVEGAS_WITH_OPENEXR=ON
```

検索順序：まずインストール済みの `find_package(OpenEXR 3 CONFIG)`、なければ
[`thirdparty/openexr`](thirdparty/openexr) のソース。

**依存関係についての注意。** `thirdparty/openexr` の隣には Imath がないため、CMake の
OpenEXR は構成時に GitHub（`main` ブランチ）から Imath を取得します。つまりビルドは
オフラインでも再現可能でもなくなります。クローンした OpenEXR 自体は `main` の
`4.0.0-dev` で、リファレンスは安定版 3.1 を同梱しています。再現性のためには、リリース
タグを固定し、Imath も同梱すべきです。

このオプションなしでも `.exr` のエクスポートは黙って失敗せず、ビルドに OpenEXR がない
ことを知らせます。Alembic（`.abc`）の読み込みも同様に `-DOPENVEGAS_WITH_ALEMBIC=ON` で
[`thirdparty/alembic`](thirdparty/alembic) から有効にできます。

### VS Code と Zed（Qt MSVC のデバッグ）

どちらの IDE も Qt `msvc2022_64` 向けに設定され、MSVC 環境を自分で取り込みます。別途
開発者用プロンプトは不要で、ビルドタスクは `vcvars64.bat` を呼び出します
（`vswhere -version "[17.0,18.0)"` を使い、Qt キットと合わないツールセットを持つ並行
インストールの VS 18 を拾わないようにしています）。

CMake Tools のビルドボタンには `cmake.useVsDeveloperEnvironment=always` が有効で、
`cmake.buildTask=false` によりプリセットの環境とともに拡張機能に起動を任せます。
`Ctrl+Shift+B` のタスクと F5 前のビルドは、MSVC 環境を自分で読み込む
`tools/msvc_build.cmd` を呼び出します。Ninja 用に CMake は `msvc_environment.cmd` も
生成し、コンパイルとリンクのコマンドは選択された `cl.exe` を持つインストールの環境を
読み込みます。これにより、CMake Tools が新しい Visual Studio の環境を選んだ場合でも、
VS 2022 のコンパイラーと VS 2026 のヘッダーを混ぜたときの `STL1001` を防ぎます。既存の
ビルドディレクトリでは、Ninja のルールを更新するため一度構成を実行してください。起動
スクリプトは UTF-8（`chcp 65001`）も有効にします。そうしないと、ローカライズされた
`/showIncludes` の行が CMake のプレフィックスと一致せず、Ninja がヘッダーの依存関係を
見失い、クラスの変更後に互換性のないオブジェクトファイルを混在させます。誤ったコード
ページで既にビルドしたディレクトリでは、構成後に
`cmake --build build/Windows_MSVC-x64 --target clean` を実行して完全に再ビルドします。

- **VS Code** — [`.vscode/`](.vscode/)：`launch.json`（`cppvsdbg` デバッガー、MSVC の
  PDB を読む。Debug／RelWithDebInfo／アタッチの構成）、`tasks.json`（構成／ビルド／
  クリーン／実行）、`qt6.natvis`（デバッガーで `QString`、`QList`、`QRect` などを
  読みやすく表示）。拡張機能は `extensions.json` にあります。F5 でビルドして実行します。
- **Zed** — [`.zed/debug.json`](.zed/debug.json)（CodeLLDB アダプター、PDB は exe の隣）
  と同じプリセットを使う [`.zed/tasks.json`](.zed/tasks.json)。`clangd` は
  `build/Windows_MSVC-x64/compile_commands.json` をインデックスします。

IDE の外で実行する場合は、Qt の `bin` を PATH に追加します：

```powershell
$env:PATH = "C:/Qt/6.9.3/msvc2022_64/bin;" + $env:PATH
```

`build/` は**コミットしないでください**。[`.gitignore`](.gitignore) に含まれています。

---

## ローカライズ

インターフェイスはロシア語、日本語、簡体字中国語に翻訳されています。元の言語は英語です。

| カタログ | メッセージ数 | 言語 |
|----------|-------------:|------|
| インターフェイス（`openvegaseffects_<lang>.ts`） | 1905 | ru、ja、zh_CN — すべて翻訳済み |
| プラグインの文字列（`openvegaseffects_plugins_<lang>.ts`） | 6818 | ru、ja、zh_CN |

日本語と中国語の用語はリファレンス自身のカタログ
（`SAMPLES/VEGAS_Effects/Translations/*/VegasEffects_*.qm`）から取っており、原文が一致
する文字列にはリファレンスの表現が使われます。プラグインのカタログには、ネイティブ
プラグインの名前、カテゴリー、コントロールのラベル、単位、リストの項目、メッセージが
リファレンスの `Effect_<ShortName>` コンテキストで収められており、
[`tools/plugin_translations.py`](tools/plugin_translations.py) が書き出します。詳細は
[`translations/README.ja.md`](translations/README.ja.md) にあります。

### 仕組み

- カタログは [`translations/`](translations/) にあります。CMake が `qt_add_translations`
  でコンパイルし、`:/i18n/` としてバイナリに埋め込みます。Qt の **LinguistTools**
  コンポーネントが必要で、ない場合もビルドは成功しますが、インターフェイスは英語のまま
  です。
- 読み込みは `app::Translations::install()` で、ウィジェットを作成する**前に**
  `main.cpp` で呼び出されます。
- 検索順序はリファレンスと同じです。まず
  `<ユーザーデータ>/Translations/<locale>/openvegaseffects_<locale>.qm`（独自のカタログを
  置けます）、次に埋め込みのカタログ。Qt 自身の `qt_<locale>.qm` も一緒に読み込まれます。
- 言語は **オプション → インターフェイス → 言語** で選び、キーは `Options/Language`、
  空ならシステムに従います。次回の起動時に適用されます。
- 2 つのテストが各言語でオプションとパネルを開き、英語の単語が残っているテキストが
  あれば失敗します。

### 文字列を編集した後のカタログの更新

```bat
cmake --build --preset windows-msvc-debug --target update_translations
python tools/validate_translations.py
```

---

## 実行時のデータフォルダー

初回起動時、アプリケーションはリファレンスの構成（そのリゾルバー `FUN_14025b000` から
復元）を再現したユーザーデータフォルダーを作成します：`Translations`、`Templates\AV`、
`EnvironmentMaps`、`ExportPresets`、`PluginPresets`、`Plugins`、`Presets`、`Textures`、
`Templates\Workspaces`、`Objects`、`Tutorials`。そのうち 2 つは `Templates` の下にあり、
リファレンスは単独の `Templates` フォルダーを持ちません。Windows でのルートは
`%LOCALAPPDATA%\OpenVegas\OpenVegasEffects` です。

リファレンスとの違いは意図的なものです。リファレンスはフォルダーを確認するだけで、
なければ空のパスを返します（ツリーはインストーラーが作成します）。一方このポートは、
インストール版でもポータブル版でも自分でフォルダーを作成します。

ユーザーの `Plugins` フォルダーにはポート独自のエフェクト定義（`.vfx`、JSON）も置け
ます。ネイティブの `.hfpl` モジュールは、上の「ネイティブプラグイン」で説明したとおりに
見つけられます。

---

## リポジトリの構成

```
OpenVegas_Effects/
├── CMakeLists.txt / OpenVegasEffects.pro / CMakePresets.json
├── src/
│   ├── main.cpp          # WinMain 風のエントリ → UTF-8 の argv → QApplication
│   ├── core/             # Identifier、Result、Version、Log
│   ├── app/              # AppMain、データパス、設定、翻訳、テンプレート
│   ├── license/          # LicenseManager + 無料のマネージャー
│   ├── plugin/           # プラグインのレジストリとネイティブ .hfpl モジュールのホスト
│   ├── media/            # メディアの読み込み、libVLC、オーディオ、プロキシ、フレームキャッシュ
│   ├── composition/      # コンポジットショット、レイヤー、クリップ、エフェクト、キーフレーム
│   ├── project/          # .vegfx の読み書き、エフェクトのプリセット
│   ├── model3d/          # 3D モデルの読み込みと 3D レンダラー
│   ├── cache/            # SQLite キャッシュ（QtSql）
│   ├── render/           # フレームのレンダリング、テキスト、エクスポート、プリレンダー
│   └── ui/               # MainWindow とパネル、ダイアログ、オーバーレイ
├── ui/                   # Qt Designer のフォーム（メインウィンドウ、パネル、ダイアログ）
├── resources/            # アイコン、Windows のリソースファイル、macOS 用ファイル
├── translations/         # .ts カタログ（インターフェイスとプラグインの文字列）
├── tests/                # 回帰テスト（独立した CMake プロジェクト）
├── tools/                # ビルドスクリプト、インストーラー、パッケージ、RE 用ツール
├── MARKDOWN/             # アーキテクチャ、チェックリスト、リバースエンジニアリングのノート
├── docs/                 # スクリーンショット
├── thirdparty/           # VLC のヘッダー、OpenEXR、Alembic（任意）
└── SAMPLES/              # リファレンスのバイナリ、RE 資料、サンプルプロジェクト（ビルド対象外）
```

`SAMPLES/` にはリファレンスのバイナリ、リバースエンジニアリングの資料、サンプル
プロジェクト（`SAMPLES/vegfx_projects`）が含まれ、オープンなビルドには含まれません。

---

## ドキュメント

1. **アーキテクチャと計画** — [`MARKDOWN/ARCHITECTURE.md`](MARKDOWN/ARCHITECTURE.md)
2. **未完了の項目** — [`MARKDOWN/CHECKLIST.md`](MARKDOWN/CHECKLIST.md)
3. **開発ルール** — [`MARKDOWN/INIT.MD`](MARKDOWN/INIT.MD)
4. **VEGAS Effects リファレンスのリバースエンジニアリング** — [`MARKDOWN/RE_VegasEffects.md`](MARKDOWN/RE_VegasEffects.md)
5. **ネイティブ `.hfpl` プラグインとそのホスト** — [`MARKDOWN/RE_HFPL_Plugins.md`](MARKDOWN/RE_HFPL_Plugins.md)
6. **リファレンスの配布物の分析** — [`MARKDOWN/RE_VegasEffects_Distribution.md`](MARKDOWN/RE_VegasEffects_Distribution.md)
7. **テスト** — [`tests/README.ja.md`](tests/README.ja.md)、**翻訳** — [`translations/README.ja.md`](translations/README.ja.md)、**パネルのフォーム** — [`ui/panels/README.ja.md`](ui/panels/README.ja.md)

---

## ライセンス

このリポジトリのコードは、特に断りのない限り **GNU GPL v3** でライセンスされています。
全文：[`LICENSE`](LICENSE)。

`SAMPLES/` にあるリファレンスのバイナリは各権利者の所有物であり、学習目的でのみ保管して
います。
