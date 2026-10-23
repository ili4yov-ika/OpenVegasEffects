# VEGAS Effects リファレンスのスクリーンショット

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

VEGAS Effects 5.0.2 のインターフェイスのスクリーンショットと、そのピクセル単位の分析です。
各 `N.png` には、パネルの座標レイアウト、内容、配色をまとめた `N.md` があります（分析は
ロシア語で書かれています）。UI を移植する際の正解の基準として使います —
[`MARKDOWN/RE_VegasEffects.md`](../../MARKDOWN/RE_VegasEffects.md) §5 を参照。

## 索引

| ファイル | 解像度 | 写っているもの | このショットだけにあるもの |
|----------|--------|----------------|----------------------------|
| [`0.md`](0.md) | 1317×816 | Effects 画面、メディアレイヤーの **Cartoon** エフェクト | Controls のエフェクトのパラメーター：Preset、Edge、Fill |
| [`1.md`](1.md) | 1920×1080 | **Controls** パネル | — |
| [`2.md`](2.md) | 1920×1080 | **Effects** パネル + 3D モデル `octop.obj` | エフェクトカテゴリーの完全なツリー（424 項目）、`[Model]` レイヤー、XYZ の 3D ギズモ、`Layout` タブ |
| [`3.md`](3.md) | 1920×1080 | モーダルの **Options** ダイアログ | 設定ウィンドウの構造 |
| [`4.md`](4.md) | 1920×1080 | **Options**、**Export** タブ | エクスポートのパラメーター |
| [`5.md`](5.md) | 1920×1080 | `octop.obj` の **Controls**、World Transform | 3D オブジェクトのトランスフォーム |
| [`crop_screenshot.md`](crop_screenshot.md) | 1280×720（×2 縮小） | Edit 画面、**Point レイヤーのアニメーション** | Position トラックのキーフレーム（約 60 個）、`Value Graph`、2D ギズモ、パネルタブの両グループ |

## その他のショット（分析なし）

| ファイル | 解像度 | 写っているもの |
|----------|--------|----------------|
| `6.jpg` | 1920×1036 | Controls の Color Correction Wheels とタイムライン上のキー付きパラメーター、Scopes パネル：Histogram、Parade、Vectorscope、Waveform |
| `wnd_Layout.png` | 324×374 | **Layout** パネル（`MARKDOWN/RE_wnd_Layout.md` を参照） |
| `wnd_Text-full.png` | 898×773 | **Text** パネル、すべてのグループ |
| `wnd_Text-paragraph.png` | 327×282 | **Text** パネル、Paragraph グループ |

## 解像度についての注意

- `1.png`～`5.png` はネイティブの 1920×1080 で撮影しています。
- `0.png` は 1317×816 です。`0.md` の Controls/Timeline/Status Bar の節の座標は
  1920×1080 のグリッドに対して書かれており、換算が必要です。補正方法はファイル冒頭の
  囲みに説明があります。
- `crop_screenshot.png` は約半分に縮小されています（元の約 2560×1440 から 1280×720）。
  `crop_screenshot.md` の座標はすべてファイルのピクセル単位で、ネイティブの値にするには
  2 倍してください。

## ショットから移植済みのもの

ショットに写っているものはすべてポートにあります：ビューアーのトランスフォームギズモ、
`Point` と `Model` の種類のレイヤー、キーフレーム（パラメーターのトラック上のひし形、
リファレンスの 6 種類の時間補間、トラック上の `Value Graph` の曲線、キーとそのハンドルの
マウスでのドラッグ）。まだ移植していないもの：リファレンスの空間（3D モーションパス）補間 —
実装されているのは時間の曲線だけです（`src/composition/KeyFrame.h`）。
