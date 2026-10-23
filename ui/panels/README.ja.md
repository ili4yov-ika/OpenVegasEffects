# パネルウィンドウの *.ui フォームへの移行

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

このドキュメントでは、パネルの Qt Designer フォームと、それらが動作するクラスとどう
つながっているかを説明します。ここに挙げたフォームはすべて CMake と qmake のビルドに
含まれており、パネルのコンストラクターは `setupUi()` を呼び出します。動的なリスト、
メニュー、描画、シグナルは C++ に残っています。`ui/panels/Export.ui` は参照用の
コピーとして残しています。実際の Export パネルは、すでに接続されていたフォーム
`ui/ExportPanel.ui` を使います。

## 1. 調べたもの

| ソース | 得られたもの |
|--------|--------------|
| `src/ui/MainWindow.cpp`（`buildUi`） | 共有ドックグループ Viewer / Trimmer / Export / Layer / 360 Viewer、`viewerPage` → キャンバス + インラインのトランスポートバー |
| `src/ui/ViewerWidget.{h,cpp}` | キャンバス本体（`graphicsViewViewer`）、レイアウトマネージャー（Single/Row/TwoOverOne/Four/RowFour）、ツールストリップ（`viewer-tool-strip`、ボタン）、`createViewOptionsBar()` → `toolButtonView`、`toolButtonOption`、`toolButtonPlaybackQuality` |
| `src/ui/ViewerTransportBar.{h,cpp}` | `ViewerPlaybackWidget` という命名、進捗表示 `progressBarRender`、スクラブ行（`spinBoxCurrentTime`、`sliderCurrentFrame`、Undo/Redo 付きで編集できる `spinBoxFrameCount`／Timeline Duration）、ボタン行（クラスターの順序）、表示オプション用の `addTrailingWidget(...)` |
| `src/ui/ViewScaleButton.{h,cpp}` | ズームの段階（Fit、12%…200%）：「−」、ラベル `viewScaleLabel`、「+」。objectName は `toolButtonZoom` に上書き |
| `resources/icons.qrc` | 利用できるアイコン `/icons/*` の一覧 |
| `ui/ExportPanel.ui`、`ui/MainWindow.ui` | プロジェクトの .ui の書き方：`<class>` = 所有クラス、ルートウィジェット名、明示的な margins/spacing、翻訳しないテキストには `<string notr="true">` |

## 2. 移行したもの

`Viewer.ui` は中央タブのレイアウトを持ち、プロモートした 2 つの実ウィジェットを使います：

```
ViewerPanel（QWidget、クラス openvegas::ui::ViewerPanel のフォーム）
└── rootLayout（QVBoxLayout、margins 0、spacing 0）
    ├── graphicsViewViewer（ViewerWidget、最小 320×180）
    └── ViewerPlaybackWidget（ViewerTransportBar）
```

フォームのクラスは `openvegas::ui::ViewerPanel` です。内側の `ViewerWidget` と
`ViewerTransportBar` は、それぞれのリファレンスのオブジェクト名、ツールチップ、専用の
スタイル、実際の接続を保ちます。`ViewerPanel` はフォームから両方のポインターを受け取り、
Viewer のオプションバーを `ViewerTransportBar` の末尾に追加します。

## 3. コードに残したもの（動的な部分）

以下は静的なツリーではないため、**意図的に** .ui に移していません：

- **キャンバス** — プロモートした `ViewerWidget`。フレーム、モーションパス、タイムコード、
  マウス座標はその `paintEvent` で描画します。
- **ツールストリップ** — 幅 24 px と左側の位置はコードで設定します（`toolStripRect`）。
  22×22 px のボタンは 1 ピクセルの余白で共通の軸にそろえ、ローカルスタイルで一般的な
  `QToolButton` の余白から切り離しています。カーソルのショートカット（V/H/T/R/E/F/B）、
  `toolButtonMaskShape` の図形フライアウト（Rec/RRect/Ellipse/Polygon/Star）、
  ドロップダウンメニューもコードです。
- **トランスポート／オプションのボタンメニュー**：「View」（レイアウト）、「Options」
  （Show Motion Path、Background Color、Checkerboard、Color Channels、Full Screen Preview、
  Scale/Fit/In/Out）、Quality（4 プロファイル × 再生中／停止中）、ズームの段階。
- **アイコン** — コード（`makeButton`）で設定し、SVG がラスタライズできない場合は
  テキストにフォールバックします。フォームはリソースを参照しないため、`icons.qrc` を
  取り込みません。
- **シグナル／スロット** — すべての接続（`scrubRequested`、`playPauseRequested` など）。

## 4. 接続のしかた

1. `ViewerWidget` と `ViewerTransportBar` はフォームの `<customwidgets>` で宣言されて
   います。
2. `ViewerPanel` が `setupUi()` を呼び出し、実際のポインターを `MainWindow` に渡します。
3. `CMAKE_AUTOUIC_SEARCH_PATHS` に `ui/panels` が含まれ、フォームは
   `OpenVegasEffects.pro` にも列挙されています。
4. Timeline はプロモートした `TimelineTree`、`TimelineCanvas`、
   `TimelineValueGraphView` を使います。Trimmer、Preview360、Meters は `setupUi()` の
   後で、描画用のプレースホルダーだけを実際のカスタムウィジェットに置き換えます。

## 5. 気づいた点

- `audioMeterIcon` — ラベルのない、描画によるコンパクトな 2 チャンネルの LED
  インジケーター。`AudioPlayer` からマスターの RMS を受け取り、Options の前にあり、
  クリックすると Audio Meters パネル全体を開きます。
- `toolButtonZoom` — 複合ウィジェット。コードでは `ViewScaleButton` がまず自分の名前
  `ViewScaleButton` を持ち、その後 `toolButtonZoom` に上書きされます。テーマやテストが
  壊れないよう、フォームには最終的な（リファレンスの）名前を残しています。
- `sliderCurrentFrame` は `duration == 0` の間、コードで無効になっています
  （`setDuration` で有効化）。フォームの `enabled=false` は新しいプロジェクトの初期状態に
  対応します。
- 左側と中央のクラスターのボタン順はリファレンスのコンストラクターの順序
  （Export Frame → Loop → In → Out → Meters | First → Prev → Next → Play）で、新たに
  考えたものではありません。

---

# 6. MainWindow のその他のパネル

メインウィンドウの他のドックパネルとタブもすべて同様のフォームを使います。フォームの
クラスはツリーを所有するクラス（`openvegas::ui::…`）、ウィジェット名は
**リファレンスのオブジェクト名**（テーマとテストがこれで検索します）で、動的な部分
（paintEvent、ポップアップ、リスト、アイコン）は静的な骨組みの上にコメントで記述して
います。**Export** については `ui/panels/` に仕様用のコピー（`Export.ui`）を作りましたが、
実際の実装は既存の接続済みフォーム `ui/ExportPanel.ui` のままです（6.15 を参照）。

`src/ui/MainWindow.cpp` のドックツリー、画面「Effects」：

- **左：** Media（`mediaPanel`）+ Text（`textPanel`、どちらも左のドック）
- **中央：** 共有ドックグループ Viewer / Trimmer / Export / Layer / 360 Viewer
- **右：** Controls / Effects / Layout / Track / Library / History + Learn（ネイティブの
  ドック、非表示）
- **下：** Timeline（`timelinePanel`）と共有の Start／コンポジションのストリップ +
  AudioMeters（`AudioMetersPanel`、既定では非表示）。Start のフォームは Timeline の 2 番目
  のページとして埋め込まれているため、別のシステムドックのストリップはありません。

| # | ソース | フォーム | ルートウィジェット / objectName |
|---|--------|----------|---------------------------------|
| 6.1 | `MediaPanel.cpp` | `Media.ui` | `MediaPanel` / `media-panel`（QWidget） |
| 6.2 | `TextPanel.cpp` | `Text.ui` | `TextPanel` / `textPanel`（QWidget） |
| 6.3 | `EffectsPanel.cpp` | `Effects.ui` | `EffectsPanel` / `effectsPanel`（QWidget） |
| 6.4 | `EffectInspector.cpp` | `Controls.ui` | `EffectInspector` / `controlsPanel`（QDockWidget） |
| 6.5 | `LayoutPanel.cpp` | `Layout.ui` | `LayoutPanel` / `Layout`（QDockWidget） |
| 6.6 | `TrackPanel.cpp` | `Track.ui` | `TrackPanel` / `trackPanel`（QDockWidget） |
| 6.7 | `LayerPanel.cpp` | `Layer.ui` | `LayerPanel` / `layerPanel`（QDockWidget） |
| 6.8 | `HistoryPanel.cpp` | `History.ui` | `HistoryPanel` / `History`（QDockWidget） |
| 6.9 | `LibraryPanel.cpp` | `Library.ui` | `LibraryPanel` / `libraryPanel`（QDockWidget） |
| 6.10 | `Preview360VideoPanel.cpp` | `Preview360.ui` | `Preview360VideoPanel`（QDockWidget） |
| 6.11 | `AudioMetersPanel.cpp` | `AudioMeters.ui` | `AudioMetersPanel` / `AudioMetersWidget`（QDockWidget） |
| 6.12 | `StartPanel.cpp` | `Start.ui` | `StartPanel` / `StartPanelWidget`（QDockWidget） |
| 6.13 | `TrimmerPanel.cpp` | `Trimmer.ui` | `TrimmerPanel` / `trimmer-panel`（QWidget） |
| 6.14 | `TimelineWidget.cpp` | `Timeline.ui` | `TimelineWidget` / `timelinePanel`（QDockWidget） |
| 6.15 | `ExportPanel.{h,cpp}` + `ui/ExportPanel.ui` | `Export.ui`（仕様） | `ExportPanel` / `export-panel`（QWidget） |

## 6.1 Media — `Media.ui`

`media-panel` はリファレンスのスクリーンショットの縦の並びに従います。上の行に
`toolButtonImport` / `MediaPanelNewFolderButton` と 2 つの表示ボタン、
`media-panel-search`（「Search in Project Media」）、`mediaArrangeButton` /
`mediaGroupButton` の独立した行、`QListWidget listView`（ドラッグ＆ドロップ）、そして
`MediaPanelFolderButton` / `MediaPanelCompositeShotButton` / `MediaPanelTrashButton` と
カウンター `media-panel-count` の下の行。並べ替え、グループ化、表示のメニューはすべて
接続され、フォルダー作成の 2 つのコマンドは動作し、Composite Shot は選択したレイヤーから
ネストしたコンポジションを作る、検証済みの共通処理を使います。コンテキストメニューと
ドラッグ＆ドロップはコードに残っています。

## 6.2 Text — `Text.ui`

`textPanel` には 3 つのグループがあります：`groupBoxCharacter`（フォント、
`spinBoxFontSize`、`QColorDialog` の色ボタン、太字／斜体／下線／取り消し線、ストロークの
距離・ぼかし・方向・広がり、`strokeColor`、`strokeWidth`、メトリクス
`tracking`/`leading`）、`groupBoxParagraph`（配置 `toolButtonAlignLeft/Center/Right`、
垂直方向の配置、`left/right/top/bottom` の余白スピンボックス）、`groupBoxBackground`
（`checkBoxBackground`、色、半径）。フォームは `setupUi()` で接続されています。コードは
抽出した SVG をすべてのトグルボタンに割り当て、デザイナーのスピンボックスに横方向の
スクラブ操作を追加し、追加のストロークとカラーピッカーの行を作ります。ルートの
レイアウトは、隠れた横スクロール範囲を作らずにドックの幅まで縮められます。

## 6.3 Effects — `Effects.ui`

`effectsPanel` — `effects-panel-search`「Search field」、「show all types」のチェック
ボックス `effects-panel-show-all`、`QTreeWidget`（2 列、ヘッダー非表示：エフェクト／
トランジション）、`effects-panel-count`。ツリーは `EffectsPanel::EffectsListModel` から
埋められ、検索行の `toolButtonOptions` の表示はコードです。

## 6.4 Controls（EffectInspector）— `Controls.ui`

QDockWidget `controlsPanel` とルートウィジェット `ControlsPanel`（インスペクターの
スペーサー。ピンをクリックするとパネルが隠れます）。中身：`controlsSelectionTitle` +
`controlsPin`、`controlsCurrentTime`（クリックできるタイムコード）+ 時計アイコン、
`controlsSearch` + `controlsCollapseAll` + `controlsExpandAll`、`controlsTree` —
`QTreeWidget`（2 列、ヘッダー非表示、インデント 14）。インスペクターは選択した項目の
プロパティ行をその場で作成・削除し、フォームには骨組みだけがあります。

## 6.5 Layout — `Layout.ui`

QDockWidget `LayoutPanel`：フォームは `LayoutPanel.cpp` に接続されています。スクロール
領域の中は `TransformWidget`：2 つの反転ボタン、間隔、2 つの 90° 回転、元のアイコンを
使った 3×3 の基準点、X/Y/Width/Height とサイズのリンク。続いて `AlignmentWidget`：
Alignment の見出し、Selection/Timeline、3 つずつの間に間隔を置いた 6 つの整列と 6 つの
分布。元の PNG の各状態と @2x は `resources/icons/layout` にあります。選択したロックされて
いないレイヤーはすべて 1 回の Undo 操作で編集され、アニメーションするプロパティは現在の
フレームでキーを保ったまま変更されます。復元の正確な範囲と Ghidra のアドレスは
`MARKDOWN/RE_wnd_Layout.md` にあります。

## 6.6 Track — `Track.ui`

QDockWidget `trackPanel`：`widgetLayerHeader`（ボタン `toolButtonNewLayer`、
`toolButtonMoveLayerUp`、`toolButtonMoveLayerDown`、`toolButtonRemoveLayer` を持つ
QFrame）と `composition-layers-header` — 列 Name / Visible / Muted / Blend / Opacity
（PosX/PosY はオプション）の QTreeWidget。フォームには静的なヘッダーのグリッドがあり、
レイヤーの行は別のクラスが作ります。

## 6.7 Layer — `Layer.ui`

QDockWidget `layerPanel` → `stackedWidget`：`labelBreadCrumbTrail`、独立したプレビュー
`layerPreview`、フォーム `layerPropertiesWidget`（行：読み取り専用の `layerType`、
`layerName`、`layerVisible`、`layerMuted`、`layerLocked`、`layerBlendMode`、
`layerOpacity`、`layerDimension`、`layerParent`、`layerLabelColor`、`layerPlaneColor`、
`layerCameraFov`、`layerID`）を持つ `pageLayer` と、`pageNoLayer`（`dummyWidget` —
「no layer selected」）。伸びるフォームのプロパティと選択に応じた変更はコードです。
`layerPreview` は、リファレンスの `RenderLayerViewJob` と同様に、レイヤーのソースとその
マスクを、エフェクト・トランスフォーム・不透明度なしで表示します
（`MARKDOWN/RE_wnd_Layer.md`）。テキストレイヤーにはプレビューがありません。

## 6.8 History — `History.ui`

QDockWidget `History` → `HistoryDockWidget`（ルート）：ツールバー
`toolButtonUndo`/`toolButtonRedo`/`toolButtonClear` + `listViewHistory`（最近の操作、
クリックで戻す）。履歴は `MainWindow` にあり、フォームは骨組みです。

## 6.9 Library — `Library.ui`

QDockWidget `libraryPanel` → ルート `LibraryPanel`：`libraryStatus`（QLabel
「Library」）、`toolButtonReloadLibrary`、`libraryList`（QListWidget）。フォルダーからの
読み込みはコードです。

## 6.10 Preview360 — `Preview360.ui`

QDockWidget `Preview360VideoPanel` → `Preview360VideoPanelWidget`（`360-viewer` の
ラッパー）：`comboBoxCurrentView`（7 方向：Front/Back/Left/Right/Top/Bottom/Custom）を
持つツールバー、`toolButtonProperties` は Yaw/Pitch/Roll/FOV、カメラ、ラップの編集を
開きます。`glWidget` は `Viewer360View` で、リファレンスの EnvironmentMapViewer
モジュールでフレームを描画し、モジュールがない場合は CPU 上の独自の透視投影で描画します
（`MARKDOWN/RE_wnd_Viewer360.md`）。描画と操作はコードです。

## 6.11 AudioMeters — `AudioMeters.ui`

QDockWidget `AudioMetersPanel` → `AudioMetersWidget`：`AudioMetersHoldPeaks`（QCheckBox
「Hold Peaks」、QSettings から）、`toolButtonResetPeaks`、`InputLevels` /
`OutputLevels` の列（LED の列、`AudioMeterDisplay`、最小 32×90。色は
`Theme/meterBackgroundColor` #111111、ホールドピーク #eeeeee）。列は paintEvent で描画し、
フォームにはプレースホルダーのスロットがあります。レベルは `AudioPlayer` の共有 PCM
オブザーバーから届き、`MainWindow` は 1 つの `levelsChanged` シグナルで両方の列と
Viewer 内のインジケーターを更新します。

## 6.12 Start — `Start.ui`

QDockWidget `StartPanel` → `StartPanelWidget`：アクションボタン「New Composite Shot」
（`toolButtonNewComp`）、「New Composite Shots From Footage」
（`toolButtonNewCompsFromMedia`）、「Import File」（`AddMedia`）、「Edit Screen」
（`toolButtonEditScreen`）— いずれも minHeight 46、テキストのみ。右側に「Jump back in」
（`toolButtonNew`、`toolButtonOpen`、`toolButtonClearRecents`）と
`listViewRecentProjects` を持つ `ProjectSideBarWidget`、下に学習用のストリップ。最近使った
項目のリストはコードです。

## 6.13 Trimmer — `Trimmer.ui`

共有 Viewer グループのドック `TrimmerDock` の中のウィジェット `trimmer-panel`（QWidget、
ドロップ可能）。ツールバー「trimmer-toolbar-top」：`trimmerSetIn`/`trimmerSetOut`、
`trimmerPosition`（タイムコード）、`trimmerInsert`、`trimmerOverlay`。フレーム
`trimmer-frame`（クラス `TrimmerWidget`、描画ストリップ）。「trimmer-toolbar-bottom」：
`TrimmerSlider`（0..1000）、`trimmerRange`。`trimmerSourceList`（QListWidget、
maxHeight 90）。キャンバスは paintEvent で、フォームにはプレースホルダーがあります。

## 6.14 Timeline — `Timeline.ui`

QDockWidget `timelinePanel` → `TimelinePanelWidget`（ダークな QSS #272727）。
`timelineToolStrip`（23 px、ツール Select/Hand/Slice/Rate Stretch + Slip/Slide/Ripple/
Roll/TrackSelect の伸縮メニュー + `timelineSnap` + `timelineSettings`）。ヘッダー：
`timelineClock`、`timelineTimecode`、`timelineNewLayer`、`toolButtonMakeCompositeShot`、
キーフレームバー（`timelinePreviousKeyframe`、`timelineToggleKeyframe`、
`timelineNextKeyframe`、すべての補間に対応する `timelineKeyFrameType*`）、
`timelineValueGraph`、`timelineGraphAutoZoom`、`timelineRenderCache`、`timelineExport`。
本体：`QSplitter` → `leftColumn`（`timelineSearch` の行 + `timelineLayerTree` +
`timelineZoomOut/Slider/In`）と `stackedWidgetTimelines`（ツリーキャンバス／値グラフ）、
右側に `timelineVScroll`（ミラー）。外側の `timelinePanelPages` が Editor/Start を切り替え、
1 本の 25 px のフッターに `timelineStartTab` と `timelineCompositionTabs` が並び、右端に
パネルメニューがあります。フッターの下に 2 行目ができないよう、ネイティブの QDockWidget
の枠と QStatusBar は隠しています。レイヤーツリー、キャンバス、値グラフはプロモートした
`TimelineTree`、`TimelineCanvas`、`TimelineValueGraphView` で、行の構造と
`timelinePreset_*` / `timelineTrackEnabled_*` / `timelineMask*` はコードで生成されます。

## 6.15 Export — `Export.ui`（仕様用のコピー）

共有 Viewer グループのドック `ExportDock` の中のウィジェット `export-panel`。
**重要：** 実際のフォームはすでに存在し、ビルドに**接続されています** —
`ui/ExportPanel.ui`。クラス `ExportPanel` は `Ui::ExportPanel`（`setupUi`）でこれを
組み立て、コンストラクターは `objectName "export-panel"`、余白 0 の外側のウィジェットで
包みます。残りの動的な処理は `ExportPanel.cpp` にあります：ディレクトリの選択、
QSettings からの `Options/ExportDirectory` の復元、フレームのパスの `plainTextProgress` への
出力、シグナル `exportFrameRequested` / `exportContentsRequested`。

`ui/panels/Export.ui` は**仕様であり、接続も読み込みもされません**。他のフォームとの
一貫性のために作りました。ルートは他のパネルウィンドウと同じく `QDockWidget`
（`objectName "export-panel"`）で、枠に標準のドックボタン、つまり**独立したウィンドウへの
切り離し**（`QDockWidget::DockWidgetFloatable`）と**閉じる**（`DockWidgetClosable`、
`features` プロパティで `DockWidgetMovable` と一緒に指定）があります。中にはボディの
ウィジェット `ExportPanel` があり、そのツリーはリファレンスのオブジェクト名で接続済みの
フォームと 1 対 1 で一致します：`lineEditExportDirectory`、`toolButtonExportDirectory`
（「…」）、`comboBoxPreset`（H.264 (.mp4) / PNG / JPEG / OpenEXR / ProRes）、
`pushButtonExportFrame`（「Export Frame」）、`pushButtonExportContents`（「Export
Contents」）、`plainTextProgress`（読み取り専用）。今後のルール：両方のフォームを
objectName で同期させる（テストは接続済みフォームの名前で検索します）か、乖離したら仕様を
削除して、リファレンスの名前が重複しないようにします。なお実際のコードでは ExportPanel は
作業用の `ExportDock` の中の QWidget で、移動・切り離し・閉じる操作は MainWindow の
QDockWidget の外枠が提供し、ボディのフォームは変わりません。

## 7. パネル全般についての所見

- すべてのフォームの**リファレンスのオブジェクト名**は、接尾辞付きのもの（たとえば
  `timelineKeyFrameType` + キーフレームのアイコン名）も含めてコンストラクターと 1 対 1 で
  一致します。
- **独自描画**（`TimelineCanvas`、`TimelineValueGraphView`、`TrimmerWidget`、
  `AudioMeterDisplay`、360 キャンバス、チェッカーボード）は `paintEvent` です。フォームでは
  これらのウィジェットは実際のクラスにプロモートされる（Timeline、Viewer）か、
  `setupUi()` の後でそれに置き換えられます（Trimmer、Preview360、AudioMeters）。
- **アイコン**はテキストへのフォールバック付きでコードで設定します（`makeButton`）。
  フォームにはアイコンがなく、リソースも参照しません（`icons.qrc` はフォームに取り込み
  ません）。
- **動的な部分**（リストの充填、メニュー、ドラッグ＆ドロップ、シグナル／スロット、選択との
  同期）はコードに残り、フォームは静的な骨組みだけを記述します。
- **ビルド：** `uic` は接続された 15 のフォーム（`Export.ui` を除く `panels/*.ui`、CMake
  では AUTOUIC、qmake では `FORMS`）をコンパイルします。`Export.ui` は仕様でありビルド
  されません。
- `timeline_regression.cpp` や他のテストは、レイアウト、ボタン、ツールストリップの
  ボタンの objectName で検索します。フォームを編集するときはこれらの名前を保ってください。

## 8. 既定のレイアウト（2026 年 9 月 27 日）

中央のタブの並び：**Viewer / Trimmer / Export / Layer / 360 Viewer**。5 つのパネルはすべて
1 つのネイティブなドックグループにあり、タブは下側にあります。Layer と 360 Viewer はすぐに
使え、Viewer タブがアクティブです。左に Media/Text、右に Effects/Controls/Layout/Track/
Library/History、すべてのグループの下に Timeline があります。QMainWindow は独立した
centralWidget なしで動作し、ネイティブのドラッグ、分割／タブのターゲット、フロート／
ドック、パネルのメニューを保ちます。レイアウトの状態はバージョン 4 で、古い保存内容では
以前の右側の配置は復元されません。

メインメニューの下の `widgetQuickActions` バーは既定で表示されます：
Logo/Open/Save/Options/Cut/Copy/Paste/Undo/Redo。`Options/QuickActionsLayoutVersion=1` は
以前の非表示を一度だけ修正し、その後の `ShowMenuBarQuickActions` の選択はリセットなしで
適用されます。`timelineGraphAutoZoom` ボタンは IconOnly を使い、ツールチップ、アクセシブル
名、既存の toggled ハンドラーを保ちます。
