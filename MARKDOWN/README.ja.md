# 開発者向けドキュメント

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

プロジェクトの計画、ルール、リバースエンジニアリングのメモです。リファレンス
アプリケーションに関するメモはロシア語で、[`ARCHITECTURE.md`](ARCHITECTURE.md) は英語で書かれています。

| ドキュメント | 内容 |
|--------------|------|
| [`ARCHITECTURE.md`](ARCHITECTURE.md) | リファレンスの動作がこのリポジトリのどのモジュールに対応するか |
| [`CHECKLIST.md`](CHECKLIST.md) | プロジェクトの未完了項目 |
| [`INIT.MD`](INIT.MD) | 開発ルール |
| [`RE_VegasEffects.md`](RE_VegasEffects.md) | リファレンスアプリケーション (VegasEffects.exe): 起動、ウィンドウ、パネル、設定 |
| [`RE_VegasEffects_Distribution.md`](RE_VegasEffects_Distribution.md) | リファレンスの配布物の構成と各ライブラリの役割 |
| [`RE_Project_dll.md`](RE_Project_dll.md) | Project.dll: オブジェクトモデル - プロジェクト、コンポジットショット、レイヤー、キーフレーム、`.vegfx` 形式 |
| [`RE_Flux_dll.md`](RE_Flux_dll.md) | Flux.dll: レンダリングエンジン |
| [`RE_Tannen_dll.md`](RE_Tannen_dll.md) | Tannen.dll: プラグインとエフェクトのホスト |
| [`RE_Plugin_Engine.md`](RE_Plugin_Engine.md) | ネイティブ `.hfpl` プラグインのエンジン |
| [`RE_HFPL_Plugins.md`](RE_HFPL_Plugins.md) | `.hfpl` モジュール: ファイル形式、ホストの API とコンテキスト、移植版で動くものとその検証方法。[`RE_HFPL_Plugins/hfpl_metadata.tsv`](RE_HFPL_Plugins/hfpl_metadata.tsv) に全モジュールの一覧 |
| [`RE_3D_Camera_Rule.md`](RE_3D_Camera_Rule.md) | 3D コンポジットショットにはカメラが必要というルール |
| [`RE_UI_Stubs.md`](RE_UI_Stubs.md) | インターフェースのプレースホルダーの監査と、それぞれの解消方法 |
| [`RE_wnd_Layer.md`](RE_wnd_Layer.md)、[`RE_wnd_Layout.md`](RE_wnd_Layout.md)、[`RE_wnd_Viewer360.md`](RE_wnd_Viewer360.md) | Layer、Layout、360 Viewer パネル |

メモ中のアドレス (`FUN_14…`、`0x18…`) は Ghidra で表示されるリファレンスのバイナリの
アドレスです。バイナリ自体はリポジトリに含まれていません ([`SAMPLES/README.ja.md`](../SAMPLES/README.ja.md) を参照)。
