# インターフェイスの翻訳

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

カタログ `openvegaseffects_ru.ts`、`openvegaseffects_ja.ts`、
`openvegaseffects_zh_CN.ts` には、ロシア語、日本語、簡体字中国語の翻訳が入っています。
`.qm` は CMake または qmake によってビルドされ、アプリケーションに埋め込まれます。

翻訳対象の文字列を追加したら、Qt とコンパイラーのある環境で次を実行します：

```powershell
lupdate OpenVegasEffects.pro -no-obsolete
python tools/validate_translations.py
```

ビルドの前に新しい文字列を埋めてください。プレースホルダー `%1`・`%n`、改行、HTML
リンク、ファイルパターン `(*.ext)` は保持します。`%n` を含むロシア語のメッセージには
3 つの形が、日本語と中国語には 1 つの形が必要です。`.ui` のリソース名やスタイル用の
キーには `notr="true"` を付けてください。

インターフェイスに表示するテキストは `tr()` を通す必要があります（オプションの
カテゴリーやブレンドモードのように英語のキーとして保持するリストには
`QT_TRANSLATE_NOOP` と `QCoreApplication::translate`）。直接表示した文字列はカタログに
入らず、`validate_translations.py` にも見えません。テスト
`optionsShowNoEnglishInOtherLanguages` と `panelsShowNoEnglishInOtherLanguages` は、
各言語でオプションとパネルを開き、英語の単語が残っているテキストがあれば失敗します。

## プラグインの文字列

`openvegaseffects_plugins_<言語>.ts` には、リファレンスと同じ `Effect_<ShortName>`
コンテキストで、ネイティブプラグイン自身の文字列が入っています：名前、カテゴリー、
コントロールのラベル、単位、リストの項目、グループ、検索キーワード、モジュールが実行中に
作るメッセージ。これらの文字列はソースにないため、lupdate はこれらのカタログを更新
しません。カタログはコンパイルして埋め込まれるだけで（CMake では `qt_add_lrelease`、
qmake では `EXTRA_TRANSLATIONS`）、2 つ目のトランスレーターで読み込まれます
（`app::Translations::install`）。

カタログは `tools/plugin_translations.py` が書き出します：

```powershell
python tools/plugin_translations.py [--ru-dictionary ru.json]
```

コンテキストと原文、および日本語と中国語の翻訳はリファレンスのカタログ
（`SAMPLES/VEGAS_Effects/Translations/<言語>/VegasEffects_<言語>.qm`）から取ります。
リファレンスで改行が失われていた 3 つの翻訳はスクリプト内で修正しています。リファレンス
にはロシア語のカタログがないため、スクリプトは `openvegaseffects_plugins_ru.ts` に
既にある翻訳を保持し、JSON 辞書 `{原文: 翻訳}` から新しい翻訳を追加します。翻訳のない
文字列は unfinished として書き出され、`validate_translations.py` が報告します。ロシア語
カタログの検索キーワードにはロシア語と英語の両方の単語が含まれ、どちらの言語でも検索
できます。

設定済みのテストビルドで、コンパイルされたカタログを確認するには：

```powershell
cmake --build build/Windows_MSVC-x64-Release-Tests --target translation_regression
ctest --test-dir build/Windows_MSVC-x64-Release-Tests -R translation_regression --output-on-failure
```

このテストは、3 つの言語すべての読み込み、英語への切り戻し、カラーピッカーのダイアログと
スプラッシュの状態表示のコンテキスト、複数形を確認します。
