# VEGAS Effects のアイコン（VegasEffects.exe）— カタログと説明

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

## 1. 出所と抽出方法

- **ファイル：** `SAMPLES\VEGAS_Effects\VegasEffects.exe`（23,147,544 バイト、x64、Qt5、MSVC）。
- **PNG：** バイナリを 1 バイトずつ走査して `89 50 4E 47` のシグネチャ（IHDR/IDAT/IEND）を
  探し、各ファイルの境界は PNG のチャンク構造から求めました。SHA-1 で重複を除いています。
- **リソース名：** バイナリのデータセクションにある UTF-16 の名前プールから取得しました
  （アプリケーション内部のアイコンカタログで、各エントリはリソースファイル名、アルファ
  ベット順）。
- シグネチャによる走査では **3379 個の一意な PNG** が見つかりました。その後、通常の Qt
  RCC ツリーを解析して **3380 個の名前付き PNG** と 2 つの Style ファイルを復元しました。
  差はリソースのエイリアス（複数の名前が 1 つの PNG を使う）によるものです。古い UTF-16
  正規表現による一覧は一部のエントリを取りこぼしていたため、照合には使っていません。

抽出したファイルはすべて `png\` にあり、一覧は `manifest_png.tsv`、名前のカタログ全体は
`icons_inventory.txt` にあります。

正確な対応関係は、埋め込まれた Qt RCC ツリーから `python _tools/map_vegas_icons.py` で
復元しました。名前付きのリソースは `named_png/images/images/` にあり、
`named_manifest.tsv` が元のパス、オフセット、SHA-1 を結び付けます。元の `png/` フォルダー
は、重複を除いたブロブの変更しない集合として残しています。異なる元の名前が 1 つの PNG を
指すことがあるため、単純に名前を変えるとエイリアスが失われるからです。

## 2. 内容（画像サイズ）

| サイズ | 数 | 備考 |
|-------:|---:|------|
| 16×16  | 1409  | 1x の基本アイコンサイズ |
| 32×32  | 1577  | @2x（HiDPI）版 + 一部の単独のもの |
| 64×64  | 186   | 大きいバージョン／素材 |
| 128×128| 16    | 大きいバージョン |
| 20×20、10×10、40×15、80×30、32×64、64×32、10×26、5×13、38×16、76×32、206×32 | 79 | 補助的なもの（キャレット、スライダー、バー） |
| 106×60…1240×795 など | 112 | 透かし、スプラッシュ、バナー、プレースホルダー画像 |

## 3. 命名規則（これが「何が描かれているか」）

リソース名は次のパターンでアイコンを表します：

```
[テーマ] <操作名> [-<状態>] [@2x] .png
```

- **テーマ（接頭辞）：**
  - `base` — メインのセット（2549 名）；
  - `G...` — 別のスタイル／テーマ（410 名、例：`Gundo`、`Gmask-square`）；
  - `'...`（アポストロフィ）— もう 1 つの別スタイル（415 名、例：`'file-video`、
    `'text-bottom-indent`）。
- **状態（接尾辞）：**
  - 接尾辞なし — 通常の状態（819）；
  - `-hover` — マウスオーバー（837）；
  - `-checked` — 押された／有効化された状態（837）；
  - `-disabled` — 使用不可（836）；
  - `-menu` — メニュー項目（31）；
  - `-invalid` — 無効（12）；
  - `-active` — アクティブ（2）。
- **サイズ：** `@2x` — HiDPI 版（1671 名）、なし — 基本版（1703）。
- 一意な「テーマ + 名前」の組は **1288**、メインセットの一意な基本名は **727** です。

## 4. アイコンのカテゴリー（内容別）

主な機能別のグループです。括弧内はカタログ中の出現数（すべてのテーマ／状態／サイズを
含む）。

| カテゴリー | 数 | 描かれているもの（代表的な名前） |
|------------|---:|----------------------------------|
| `mask*` / `gmask-` / `'mask` | 158+56+22 | マスクツール：長方形、楕円、星、多角形、点の追加／移動／削除、曲線パス、対称ハンドル |
| `cursor-*` / `gcursor` | 108+52 | エディターのカーソル：ripple、roll、slide、slip、slice、stretch、hand、pen、resize-left/right、track-backward/forward |
| `text*` / `gtext` / `'text` | 92+28+22 | テキストの設定：配置、インデント、大文字小文字（uppercase/lowercase/capitalize）、下付き／上付き、字間、塗り／線の色 |
| `file-*` / `gfile` / `'file` | 85+24+17 | ファイルとレイヤーの種類：video、audio、picture、3d、offline、transition、folder、new-composite |
| `layout-*` / `glayout` / `'layout` | 82+26+19 | ウィンドウとパネルのレイアウト：canvas-left/right/top/bottom/middle、「1…n」セル、分布 |
| `key-frame*` / `gkey` | 70+26 | キーフレーム：prev/next、ease-in/out、easy-ease、hold、linear、on/off、frame-in/out、トラックオブジェクト |
| `export-*` / `gexport` | 47+23 | エクスポート：queue、preset、content、canvas、tidy、hot-folder、layered |
| `camera-*` | 42 | カメラ：dolly、orbit、pan、zoom、3D 表示、light |
| `checkbox-*` / `gcheckbox` / `radio-*` | 38+16+16 | チェックボックスとラジオボタンの normal/hover/checked/disabled、mixed |
| `effect-*` / `geffect` | 37+12 | エフェクトのカテゴリー：2d、3d、ae-plugin、behavior、geometry、transition |
| `distribute-*` / `align-*` / `galign` | 36+30+14 | オブジェクトの整列／分布：left/right/top/bottom/center、hor/vert |
| `frame-*` / `gframe` | 30+14 | フレームとトランスポート：in/out、next/last、prev、ramp |
| `dialog-*` / `gdialog` | 23 | ダイアログのアイコン：critical、warning、question、information |
| `action-*` | 22 | 操作：copy、ignore、link、move |
| `folder-*` | 22 | フォルダー：open、locked、add-export、horizontal/vertical |
| `hud-*` | 22 | HUD の要素：move、textbox-overflow、toolbar |
| `font-*` | 21 | フォント：color、outline-color/size、size、line-height |
| `caret-*` | 20 | キャレット：up/down/left/right、black/gray、menu |
| `track-*` | 19 | トラック：select-forward/backward、ripple、timeline |
| `graph-*` / `animation-graph` | 18 | グラフ／アニメーション：curve-up/down、value graph、グラフ付きのチェック |
| `margin-*` | 17 | 余白：left/right/top/bottom |
| `smooth-*` | 16 | スムージング：smooth-in/out/auto、ease |
| `clipboard-*` | 15 | クリップボード：copy、cut、paste |
| `play*` / `pause` / `stop-ram` / `play-ram` | 14+ | トランスポート：play、play-backwards、pause、play-ram、stop-ram |
| `zoom-*` | 14 | ズーム：min、max、fit、histogram zoom |
| `browser-*` / `new-*` / `import-*` / `preset-*` | 13+ | エフェクトブラウザー、新規作成、読み込み、プリセット |
| `proxy-*` / `gproxy` | 13 | プロキシファイル：complete、queue、pause、generate、hot-folder |
| `sound-*` | 13 | サウンド、オーディオレイヤー |
| `brush-*` / `freehand-*` | 12+12 | ブラシ、フリーハンドの形状（freehand-path、brush-remove） |
| `letter-*` | 12 | 文字のガイドマーカー scaling-horizontal/vertical |
| `vegas*` | 12 | VEGAS Effects のブランドアイコン |
| その他 | | pointer、help、pipette、batch、ripple-menu、slice、straighten、stretch、auto、key-frame、blank、white-checked、global、missing など |

## 5. 明示的な Qt のアイコンパス `:/images/images/...`

プログラムのコードでは、次の QIcon のパスでもアイコンを直接要求しています：

```
:/images/images/clipboard-copy.png        :/images/images/clipboard-paste.png
:/images/images/clipboard-cut.png         :/images/images/cog-properties-information.png
:/images/images/exclamation.png           :/images/images/folder-open.png
:/images/images/name-creator.png          :/images/images/name-pro.png
:/images/images/name.png                  :/images/images/redo.png
:/images/images/save.png                  :/images/images/tier-lock-hover.png
:/images/images/undo.png                  :/images/images/vegas-effects-new-file.png
```

## 6. 私たちの再実装のアイコンとの関係

私たちのプロジェクトはアイコンを `resources\icons\` に置き、`resources/icons.qrc` に
まとめています：

- 独自の **134 個の SVG**：`toolbar/`（71）、`text/`（46）、`timeline/`（14）、ルートには
  アプリケーション、プロジェクトファイル、スプラッシュの画像；
- `named_png/images/images/` から**無変更でコピーした 220 個の PNG**：Layout パネルの
  26 個のアイコンのすべての状態とサイズ（`layout/`、208 ファイル、
  [`resources/icons/layout/README.ja.md`](../../resources/icons/layout/README.ja.md) を参照）と
  ビューアーのアイコン（`viewer/`、12 ファイル）。

私たちの名前の一部は独自の規則です（`kf-*` — キーフレーム、`tool-*` — ツール、`effect-*` —
エフェクトのカテゴリー）。一方、リファレンスには私たちがまだ持っていない名前の大きな
グループ（`mask-*`、`cursor-*`、`gtext-*`、`font-*`、`hud-*`、`caret-*` など）があり、
セットを拡張する土台として便利です。

## 7. 識別方法についての注意

名前と PNG は、埋め込まれた Qt RCC ツリーのノードを通じて直接結び付けています
（`qRegisterResourceData`：tree `0x1412792f0`、names `0x14128b5f0`、data `0x140d1d850`）。
これはプログラム自身の権威ある対応関係で、テーマ、状態、HiDPI 版、エイリアスを含みます。
目視での確認用にコンタクトシートも残しています。

## 8. `SAMPLES\icons-from-VegesEffects\` フォルダーの内容

```
png\               抽出した 3379 個の PNG（png_<offset>_<W>x<H>.png）
named_png\         Qt RCC ツリーの正確な名前で置いたリソース
svg\               空：バイナリには SVG リソースがない
manifest_png.tsv   sha1 / offset / length / width / height / file
manifest_svg.tsv   SVG 用の同じ列（ヘッダーのみ）
named_manifest.tsv 元の名前 / ファイル / offset / サイズ / SHA-1
icons_inventory.txt アルファベット順の名前カタログ全体（3374 件）
contact16\         すべての 16×16 アイコンのコンタクトシート 8 枚（×3 拡大）
contact32\         すべての 32×32 アイコンのコンタクトシート 8 枚（×2 拡大）
contact64\         すべての 64×64 のシート 1 枚
contact_misc\      その他のサイズのシート 4 枚（透かし、バナー、スライダー）
README.md          このファイルの原文（と翻訳 README.*.md）
```
