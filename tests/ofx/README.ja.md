# OpenFX テストプラグイン

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

OpenFX ホストのテストがビルドする OpenFX プラグインと記述用サンドボックスです
（[`../README.ja.md`](../README.ja.md) の「OpenFX プラグイン」を参照）。

| ファイル | 内容 |
|----------|------|
| `TestOfxPlugins.cpp` | 素の OpenFX C API で書かれたエフェクト：OpenVegasTest バンドルの `org.openvegas.test.Gain`（フィルター）と `org.openvegas.test.Wipe`（トランジション）。`OPENVEGAS_OFX_CRASH` 付きでビルドすると、describe で異常終了するプラグイン 1 つだけの OpenVegasCrash バンドルになります |
| `OfxDescribeSandbox.cpp` | `ofx_describe_sandbox`：アプリケーションを `--ofx-describe <binary> <out.json>` で起動したときと同じ処理 |
