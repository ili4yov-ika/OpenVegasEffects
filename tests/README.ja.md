# 回帰テスト

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

回帰テストは `tests/` にある独立した CMake プロジェクトです。必要なアプリケーションの
ソースを 6 つの QtTest ターゲットにビルドし、すべて CTest に登録されています：

| ターゲット | 対象 |
|------------|------|
| `textpanel_regression` | Text パネル：描画されたテキストに対するコントロール、アウトライン、カラーダイアログ、入力とドラッグ、元に戻す／やり直し、`.vegfx` シリアライザーを通したテキストスタイル |
| `timeline_regression` | アプリケーションの大部分：タイムライン、プロジェクト、メディア、3D、ビューアー、Layout、ネイティブプラグイン、Behavior とトラッキング、エクスポート、キャッシュ、パネルの翻訳 |
| `options_regression` | オプションダイアログ：14 のカテゴリーすべて、リファレンスのオブジェクト名、設定、Restore Defaults、ショートカット、プラグインのフォルダー、翻訳されたオプション値 |
| `ui_stubs_regression` | ボイスオーバー録音の出力、Export パネルとそのプリセット |
| `audio_regression` | libVLC のマスターミックス、サンプルレート、ネイティブオーディオエフェクトのリアルタイム処理 |
| `translation_regression` | コンパイル済みの ru/ja/zh_CN カタログ |

## ビルドと実行

リポジトリのルートから、MSVC 用の Qt で（他のキットも同様）：

```powershell
cmake -S tests -B build/ci-local-test -DCMAKE_PREFIX_PATH=C:/Qt/6.9.3/msvc2022_64
cmake --build build/ci-local-test --config Release --parallel
ctest --test-dir build/ci-local-test -C Release --output-on-failure --timeout 120
```

CTest はスイートを `offscreen` プラットフォームで実行します。CTest の下では
`timeline_regression` がビルドの隣の `timeline_ctest.txt` にもログを書きます。標準出力は
CTest のログに届かず、このファイルでどの関数が失敗したかがわかります。個別のテストは
`timeline_regression.exe <テスト名>` で実行します。

ネイティブ `.hfpl` モジュールを実行するテストには OpenGL 4.1 のコンテキストが必要で、
offscreen では自動的にスキップされます。`windows` プラットフォームで実行してください
（ウィンドウは表示されません）：

```powershell
$env:QT_QPA_PLATFORM = 'windows'
build/ci-local-test/Release/timeline_regression.exe nativeEffectsMoveWithTime everyShippedModuleRuns
```

### 一部のテストに必要なもの

- ネイティブモジュールのテストには `SAMPLES/VEGAS_Effects/Plugins` の**リファレンスの
  プラグイン**（ない場合はスキップ）。
- エクスポート、プロキシ、波形、コンフォームしたオーディオのテストには `PATH` 上の
  **FFmpeg と ffprobe**。
- オーディオとメディアストリームのテストには **libVLC**（`OPENVEGAS_VLC_DIR` またはインス
  トール済みの VLC）。`OPENVEGAS_VLC_AUDIO_OUTPUT=dummy` を使うと、CI と同様に音声デバイス
  なしで再生します。
- リファレンスの `Balance.hfpl` を指す `OPENVEGAS_HFPL_BALANCE` で、`audio_regression` の
  ネイティブオーディオテストとネストしたオーディオのテストが有効になります。
- `OPENVEGAS_PROJECT3_OUT` は `buildProject3` が `SAMPLES/vegfx_projects/project_3` を書き
  込むフォルダーを指定します（指定しない場合はスキップ）。`OPENVEGAS_PROJECT3_PREVIEW` を
  付けるとプレビューのフレームもそこにレンダリングします。

### 成果物

確認用の画像は一時フォルダーに `openvegas-<名前>.png` として（`timeline_regression`）、
またはテスト実行ファイルの隣に書き出されます（`text-panel.png`、`text-panel-narrow.png`、
`color-dialog.png`、`options-general.png`、`options-proxies.png`、`options-export.png`）。
テストは目視確認用に固定サイズのフィクスチャを使い、VEGAS とのピクセル単位の一致は検証
しません。

アプリケーション全体のビルド：`tools\msvc_build.cmd build`。

---

## Text パネル（`textpanel_regression`）

このテストは offscreen プラットフォームを使い、Windows の Arial フォントを明示的に読み
込みます。描画された画像に対するコントロール、アウトライン、カラーダイアログ、ピッカーの
キャンセル、入力とドラッグ、元に戻す／やり直し、実際の `.vegfx` シリアライザーを通した
テキストスタイルの保存を確認します。デスクトップのピクセルの取得には実際のディスプレイが
必要で、この offscreen テストでは確認しません。パネルのスナップショットは
`text-panel.png`（400 px）と `text-panel-narrow.png`（327 px）です。

`referencePointTextStandsOnItsBaseline`（timeline）は project_1 の 2 行のポイントテキストを
15 秒でレンダリングし（`project_1-text.png`）、垂直方向の配置にかかわらず、各行がレイヤーの
位置でベースライン上に立ち、末尾の空白を除いて中央にそろうこと、保存された `<TextBox>` が
リファレンスの範囲であることを確認します。`paragraphTextBoxKeepsItsOffset` は中心から
ずれた段落ボックスを保存して読み戻します。
`textFrameFollowsItsParent` はテキストレイヤーの親を時計回りに 4 分の 1 回転させます：
テキストの枠も一緒に回転し、キャンバス上の移動は親の単位での移動になります。

## タイムライン、キーフレーム、コンポジットショット

`timeline_regression` ターゲットは、ツリーとキャンバスの行の整列、検索、編集できる
パラメーター、色とプリセット、キーフレームと補間、レイヤーのコントロール、安定した
レイヤー ID と親子関係、Layer インスペクター、元に戻せるクリップの移動／スライス、元に
戻す／キャンセルできるコンポジションのプロパティ、`.vegfx` への保存、組み込みの Blur と
Color Correction Wheels のすべてのパラメーター、再生キャッシュの無効化、7 つのビューの
プリセットと正距円筒図法を持つ Viewer 360 パネル、Trimmer の保持される In/Out 範囲と
Insert/Overlay のシグナル、リファレンスの 29 の MenuBar アクションとオブジェクト名を
カバーします。パネルのスナップショット：`timeline-panel.png`、`timeline-blur.png`、
`timeline-color-wheels.png`、`controls-panel.png`、`controls-color-wheels.png`、
`layer-panel.png`、`viewer360-panel.png`、`trimmer-panel.png`。

複数のコンポジットショットを持つプロジェクト（`projectKeepsEveryCompositeShot`、
`nestedShotsCannotLoop`）は、すべての CompositionAsset を読み込み、ネストした AssetLayer を
リンクし、`<IsPrimary>` と `<OpenCompositeShots>` を保ち、ネストのループを拒否し、各
ショットをネイティブの形式で書き戻します。`mediaPanelListsCompositeShots` は Media パネルの
ショットの行を確認します。`compositeShotFilesRoundTrip` は `.vegfxcs` ファイルを書き込んで
読み込み（ショットが使うメディアだけ、ネストしたショットは拒否、使用中の ID は置き換え、
ネストしたショットも一緒に読み込み）、`importCompositionDialogPicksShots` はショット選択
ダイアログを操作します（`import-composition-dialog.png`）。`mediaRowsDropOntoTimeline` は
Media パネルのドラッグのデータと、タイムラインがどこでもそれを受け取り、ドロップした
レイヤーと時刻を報告することを確認します。`compositionTemplatesAndFormats` はフレーム
レートの分数、PAR の表、Composite Shot Properties ダイアログ（テンプレート、Custom、Match
Timeline、入力したレート）、ユーザーの `.hft` テンプレート、PAR／サンプルレート／NTSC の
保存をカバーします（`composition-settings.png`）。タイムラインの回帰テストは、移動した後の
相対パスのプロジェクト（複数のクリップ、ネストしたコンポジション、オフラインのメディア、
埋め込まれたドックのレイアウトを含む）も確認します。

`valueGraphFollowsTheReference` は値グラフのグリッドの刻み（ルーラーの時間の刻みとその
分割、40 px 以上の値の刻み）、チャンネルの色、選択したプロパティのすべてのチャンネルに
対し両側に範囲の 10 分の 1 の余裕を取る Auto Zoom を確認します。
`keyFramesBetweenFrames` はフレーム間のキーを評価します（セグメントの曲線、キーの上の
キー、Hold の保持）。
`keyTimesAreRelativeToTheLayer` は 2 秒に始まり 2 秒と 3 秒にキーがあるプレーンを保存
します：ファイルには 0 と 1000 ms が入り（`OpenVegasKeyTimes="layer"`）、読み込むと
フレーム 60 と 90 に戻り、属性のない保存（ポートの古い保存）はショットの時刻として読まれ、
`shiftLayerKeys` はキーの同一性を保ちます。`movedLayerTakesItsKeys` はクリップ本体を 1 秒
後ろにドラッグします：レイヤーのキーとそのエフェクトのキーが同じフレーム数だけ、1 回の
元に戻す操作で移動します。`trimmedLayerKeepsItsKeys` はクリップの先頭をクリップの 4 分の 1
だけ後ろにドラッグします：終わりはそのまま、ソースのイン点がついていき、レイヤーと
そのエフェクトのキーはそれぞれのフレームにとどまり（1 つはレイヤーの開始より前になる）、
保存と読み込みの後もそのままです。
`layerTransformsBecomeKeys` はトランスフォームがキーや静的な値になる方法を確認します。
`layersWithoutClipsKeepTheirSpan` は 1～2 秒の範囲のカメラ（StartFrame 30、EndFrame 60）と
ショット全体にわたるライト（EndFrame = FrameCount、開いた範囲として読み戻す）を保存
します。古い保存の空の範囲はショット全体として読まれます。
`camerasAndLightsTakePartWithinTheirSpan` は 2 番目のカメラの開始でアクティブカメラを
切り替え、ライトはその範囲の中でだけ照らします。`cameraBarMovesAndTrimsItsSpan` はカメラの
タイムラインのバーをドラッグします：本体は範囲とそのキーを移動し、末尾は範囲をトリムして
キーを残します。それぞれ 1 回の元に戻す操作です。
`sliceAndStretchCutAndTrimACameraSpan` はカメラのバーを 2 つにスライスし（切り口から先が
コピー、どちらもキーを保持）、終わりをレートストレッチしてトリムします。
`multiClipLayersGoOutAsSlices` は 2 つのクリップを持つプレーンを 2 つのレイヤーとして
保存し（2 つ目は OpenVegasSliceOf の印付きで、範囲はそのクリップのもの）、両方のクリップを
持つ 1 つのレイヤーとして読み戻し、印がなければリファレンスと同様に 2 つのレイヤーとして
読みます。
`fadeToColorDrawsItsSide`（OpenGL）は Fade to Color に編集点に対する側（RenderContext
+0x08）とミリ秒単位のウィンドウを伝えます：赤は編集点の前に黒へ沈み、緑はその後で黒から
現れ、何もないところからはクリップがウィンドウ全体で黒から現れます。
`buildProject3` は `OPENVEGAS_PROJECT3_OUT` がフォルダーを指定したときに
`SAMPLES/vegfx_projects/project_3` を書き出し（そうでなければスキップ）、プレビューの
フレームを `OPENVEGAS_PROJECT3_PREVIEW` にレンダリングします。

## 親子関係、整列、シーン

`childrenFollowTheirParents` は、非表示で回転し半分に縮小した 2D の親の下にある 2D
プレーンについて、シーン内の位置、キャンバスのトランスフォーム、Layout のボックス、
レンダリング（`parented-2d.png`）を確認し、プレーンの親のアンカーの除去、親のループ、親の
ポイントが置く場所に立つカメラ、3D の親の下にある 2D レイヤーが親の見える場所でだけ
ついていくことを確認します。`parentedLayersFitAndPlaceTheirFootage` は 90 度回転して 200 %
に拡大したポイントレイヤーの下にあるプレーンをボックスに合わせ（ボックスは指定した場所に
置かれる）、インスタンスのフッテージがその親の下に配置される（右に 30 px、上に 20）ことを
確認します。
`towardsLayerTakesTheOtherLayersUp` は Towards Layer の 2 番目の回転を確認します：Z の
周りに 4 分の 1 回転したポイントを見るカメラはそのポイントの上向きを取り（ポイントが逆さ
なら半回転）、その上のシートとライトも同じで、進む方向がポイントの Y に沿うライトは
ポイントの -Z を取ります。
`layersTurnByTheirAlignment` はライトとカメラを Towards Target Position に向け（真下では
上向きは (-1, 0, 0) のまま。ライト自身の向きは照準の後にさらに回転させる）、ポイント
レイヤーの +Z をターゲットへ、ライトを Towards Layer に、3D プレーンを Towards Active
Camera にしてカメラと同じ向きに回転させ、真横から見える位置でも全体を描画し、Transform の
行（Position の後の Target、種類ごとのリファレンスの Alignment の選択肢、指定どおりの
Alignment Axis と Alignment Layer、1 回の元に戻す操作。`timeline-alignment.png`）と、
`<AutoOrient>`、`<AutoOrientLayer>`、`target` の保存と読み込みを確認します。
`layersFollowTheirMotionAndOtherLayers` は 2D のバーでの Along Motion Path（X 軸と Y 軸に
ついて対角線の動きから求めた角度、止まった後の自身の Rotation、対角線に沿った Layout の
枠とレンダリング。`align-motion-2d.png`）、3D のシートとスポットでの同じ確認、シート
（レイヤーに正面を向け、中心の周りに回転）とカメラでの Towards Layer、そしてビューアーの
Target ハンドル（専用の線とハンドル、ライトはそのままでビューと平行にドラッグ）を確認
します。
`threeDLayersAreSeenThroughTheCameraWithFog` は Flux の `ComputeFoggedFragment` のフォグの
係数、z = 0 の 3D レイヤーが 2D レイヤーと同じ場所に置かれ、距離とともに小さくなり、各
ピクセルの距離に応じて薄れる（Y の周りに回転したプレーンにはグラデーションがつく）こと、
2D レイヤーは影響を受けないことを確認します（`fog-3d-plane.png`、`fog-3d-turned.png`）。
`compositionProperties` は Fog グループも操作します。`threeDLayersShareOneScene` は 3D
シーン内の奥行きの順序と、2D レイヤーがシーンを分割することを確認します
（`scene-depth.png`）。
`bodiesHideWhatIsBehindTheirSurface` は赤い 3D シートを立方体モデルの中、中心と前面の間に
置き、前面がそれを覆う（物体はその表面のある場所でピクセル単位で判定される）こと、立方体の
前に置いた同じシートは見えることを確認します。
`sceneIsMultisampled` は 9 つのアンチエイリアスモードのサンプル数とパターン、マルチサンプル
した三角形の縁のピクセルが部分的に覆われ、その被覆率が 4/8/16/32 サンプルで実際の面積と
一致し、内部は 1 サンプルと同じように陰影がつくこと、他の面の後ろにある面が隠れたまま
であること、回転した 3D シートの縁は 2D レイヤーと違って柔らかいこと、モードが 3D
ショットのキャッシュキーにだけ含まれることを確認します（`msaa-triangle-4x.png`、
`msaa-3d-sheet.png`）。

## 3D モデル

`modelTexturesAreDrawn` は 2x2 の `map_Kd` を持つ OBJ の四角形を読み込み、4 分の 1 ずつが
それぞれのテクセルを表示し、V が上向きで、Flip UV Coordinates で反転することを確認します
（`model-texture.png`）。`modelTexturesFollowTheProjectLimit` はモデルに細い縞を描きます：
既定の上限では見え、Model Texture Max Size が 2 px になると灰色になります。
`layoutBoxesFrameModels` はモデルレイヤーの Layout のボックスを描画されたピクセルと比べ、
入力したボックスに合わせ、アニメーションがボックスを一緒に動かすことを確認します。
`modelImportFollowsTheReference` はリファレンスの読み込みオプションを確認します：単位の
一覧とコード、単位あたりのピクセル（ミリメートルを 2.8352 で割る）、(-x, z, y) としての
Flip YZ、Model Scale で打ち消される 500 ピクセルの対角線の半分への Auto Normalize、中央
寄せ、法線のないファイルに対する 80 度までのスムーズな法線、OBJ の `DefaultMaterial`、
Assimp のマテリアルの既定値。`modelImportSettingsSurviveSave` は読み込み設定をすべて変更
して読み込んだモデルを保存し、設定とノードのピボットが戻ることを確認します。
`fbxModelsAreRead` は 1 つのシーン（回転・拡大縮小しテクスチャを貼った四角形を持ち、その
移動がアニメーションする null）をバイナリ FBX 7.4、配列を圧縮した 7.5、ASCII で書き出し、
どれも同じに読めることを確認します：階層、範囲、法線、UV、マテリアルとテクスチャ、
アニメーション、そして FBX 以外と FBX 6 の拒否。`threeDsModelsAreRead` と
`lightWaveModelsAreRead` は 3DS オブジェクト（大文字の名前のテクスチャ付き）と LWO2 の
レイヤー（VMAD で分割された UV マップ、UV マップしたクリップを持つサーフェス、LightWave の
左手系から反転してもカメラを向いたまま）で同じことを行い、リファレンスがこれらの形式を
読むときの Assimp の規則も確認します：詳細ポリゴンを持つ LWOB、LightWave の光沢、
ハイライト、サーフェスごとのスムージング、Phong シェーディングのときだけの 3DS の光沢、
シーンの環境光、`%%%DEFAULT` マテリアル、スムージンググループ、キーフレーマーのノード
階層。`lightWaveProjectedTexturesGetUvs` は Z に沿った平面マップと、六角形の筒に 2 回巻いた
Y の周りの円筒マップを持つ LWO2、Y の周りの「Planar Image Map」を持つ LWOB を書き出し、
UV が Assimp の GenUVCoords のものであること（RemoveUVSeams と同じように辺が入れ替わる、
円筒の継ぎ目をまたぐ 2 つの面を含む）を確認します。
`gltfModelsAreRead` はテスト内で glTF 2.0 の四角形（data: URI のバッファとテクスチャ、
およびバイナリチャンク付きの .glb）を作り、ノード階層、親の移動、UV（glTF では v が下向き）、
画面上のベースカラーのテクスチャ（`gltf-*.png`）を確認し、glTF 1.0 がメッセージ付きで拒否
されることを確認します。
`fbxPivotsAndTimeFollowTheReference` は FBX モデルをピボットの周りに回転・拡大縮小し、
アニメーションをファイルの TimeMode（30）でスタックの LocalStop まで数えます。
`skinnedModelsFollowTheirJoints` は 2 つのジョイントにスキンした glTF の帯を作り、レスト
ポーズ、ボーンが回転したときの折れ曲がり、スキンしたノード自身の動きが無視されること、
Strength と読み込みスケールによるレイヤーのポーズを確認します。`fbxSkinsFollowTheirBones`
は FBX のスキンクラスターで同じ折れ曲がりを確認します。`morphTargetsFollowTheirWeights` は
ウェイト 0.5 で静止し 0 から 1 にキーを打った 1 つのモーフターゲットを持つ glTF の四角形を
作り、レストポーズ、クリップのウェイト、変形したメッシュ、Strength、読み込みスケールを確認
します。`fbxBlendShapesFollowTheirWeights` は FBX のブレンドシェイプのチャンネル
（DeformPercent 50、0..100 の曲線）で、シェイプの法線のオフセットも含めて同じことを行います。
`modelAnimationsPlayOnTheLayer` は 2 つのアニメーションを持つ glTF の腕を作り、線形、
ステップ、キュービックスプラインのキー、slerp、子がアニメーションする親についていくこと、
読み込み設定のスケールと軸が動きを運ぶこと、別のファイルのアニメーションが共通のノードの
パスで再生されること、リファレンスのフレーム計算（Loop、Custom Playback、フレームレートの
上書き、正確な NTSC）、モデルレイヤーがインスタンスがあるときだけその Strength で動くこと、
インスタンスが保存と読み込みを経ても残ることを確認します。
`modelAnimationRowsEditTheInstance` はタイムラインからアニメーションを挿入し、Strength、
Loop、Custom Playback、Playback（再生ヘッドでのキー付きの値）を編集し、キーボタンで
Strength のキーを切り替え、行のレーンでそれを見つけてキャンバス上でドラッグし、
「3D Model Animation Properties」を操作し、インスタンスを削除して元に戻します。
`modelAnimationAssetsLiveInMedia` はファイルのモデルアニメーションを、同じファイルのモデル
とは別の Media アセットとして読み込み、アニメーションのないファイルを拒否し、Media パネル
とその Import メニューで見つけ、プロジェクトの `Model3DAnimationAsset` から読み戻します。
`modelNodesAreSelectedMovedAndSaved` は 2 つのオブジェクトを持つ OBJ を読み込み、ノードと
ピボット、`Mesh::posed`、ビューアーがクリックしたノードを選び、輪郭を描き、ポインターの
行き先までドラッグすること（レンダラーはそこに描画する）、ノードごとの Transform を持つ
Models の行（編集、キー、元に戻す）、ノードが保存を経ても残ることを確認します
（`model-node-outline.png`、`model-node-moved.png`）。

## ライト、マテリアル、影、反射

`lightsShadeTheSceneAsFluxDoes` は `model3d::illuminate` を Flux の `Illuminant` と比べます：
ライトがなければ照明なし、環境光の項、2.5 x Intensity / 100 でのポイントライトの拡散と
鏡面の項、Linear と Curve の減衰と Reach より先は何もないこと、スポットのぼかした円錐、
発光色、照明を受けないレイヤー、影の混合。`render::layerRotationAt` が角度の符号を反転
して回転すること、`render::sceneLightsOf` が向き X 90 のスポットを (0, 400, 0) に下向きに
置き、非表示のライトを除外すること、そして 800 離れた硬い 20 度のスポットの下の赤い 3D
プレーン — 中心から 141 以内は照らされ、その外は黒、30 % では縁に向かって暗くなり、Linear
の到達範囲の外は黒、照明を受けない場合はそのまま（`light-spot-plane.png`）。
`shadowsFallFromLights` はポイントライトと赤い床の間に小さな影を落とすカードを置きます：
影は期待どおりの場所で黒く、他は何も変わらず、カードは自分自身に影を落とさず、不透明度
80 % では光の 5 分の 1 が残り、Shadow Color で色がつき、狭いスポット（マップ 1 枚）でも
同じ影を落とし、Shadow Diffusion で縁が柔らかくなり、影を受けない床や影を落とさない
カードでは影が出ません（`shadow-point.png`、`shadow-spot.png`）。
`billboardedLightsAndHiddenCasters` はかすめる角度のライトでシートを Billboarded Lights の
有無で照らし（暗い、次に拡散と鏡面の全体）、非表示のカードが Cast Shadows If Visibility
Is Off のときだけ影を落とし、それでも描画されないことを確認します
（`shadow-hidden-caster.png`）。
`modelMapsShadeTheSurface` は他の各マップで四角形をレンダリングします：何も与えないライトの
下で光る発光マップ、ハイライトを消す黒い鏡面マップ、環境光にかかるオクルージョンマップ、
何も変えない平らな法線マップと cos 45 を与える 45 度傾いた法線マップ、-bm が大きいほど
暗くなるバンプの傾斜、Override Materials で非表示のレイヤーから取った発光マップ。
`modelMaterialsFollowTheirIlluminationModel` はモデルのマテリアルの照明を、照明モデルごとに
Flux のシェーダーと比べ（マテリアルの Shininess と鏡面色全体を使う Phong、正面からの
Metallic-Roughness と Specular-Glossiness、Cook-Torrance の Beckmann のピーク、分割した色に
かかる環境光）、アルファマスク、オクルージョンの強さ、Specular-Glossiness マテリアルを
持つ glTF をリファレンスのインポーターと同じように（sRGB の色で）読み、マスクが透明な半分を
切り取ることを確認します。
`modelMaterialsAreEditedAndSaved` はモデルのプロパティの Materials タブでマテリアルを
Cook-Torrance に切り替え（行が変わる）、Specular Reflectance を編集し、Update Preview を
経ても編集を保ち（`model-materials-tab.png`）、Media でファイルのマテリアルの上に重ね
（別の読み込みの後も）、リファレンスの `<Material Version="7">` として保存して読み戻します。
`modelMaterialsTakeLayers` はタイムラインの行からモデルのマテリアルの Diffuse Map Source を、
非表示のマゼンタのプレーンを使う Use Layer に設定し、レンダラーがモデルにプレーンの絵を描く
こと（`model-material-layer.png`）、None ではマテリアルの色のままであること、保存と読み
込み、元に戻す操作を確認します。
`lightAndMaterialPropertiesAreEditedAndSaved` はタイムラインでライトの Type、Reach、Cast
Shadows と、3D プレーンの Illuminated と Diffuse を編集し（それぞれ 1 回の元に戻す操作。2D
レイヤーには Material グループがない）、リファレンスのプロパティ名で保存して読み込みます。
`lightAndMaterialPropertiesAnimate` はライトの色と数値の行、カメラの Lens、プレーンの
Material のキーボタンを確認し（Cast Shadows にはなく、モデルの Ambient..Emissive にもなく、
代わりに Self Shadow Bias がある。プレーンには Billboarded Lights and Shadows）、再生ヘッド
で Color と Reach のキーを切り替え（1 回の元に戻す操作、キーの上での編集はそのキーを設定
する）、シーンのライトでの補間された色と Reach、キー付きの Diffuse、Emissive、Blur、
モデルの ADSS 全体とそのバイアス、ライトの色とともに赤から紫に変わるレンダリング
（`timeline-light-keys.png`）、そしてキーの保存（色は `Type="0"` の下に完全な `<cl>` として）
と読み戻しを確認します。
`ambientOcclusionDarkensWhereSurfacesMeet` は G バッファー上の Flux の SSAO（平らな面は
開いている、盛り上がったブロックの隣の床はそれに沿って暗くなり離れると開いている、
ブロックの上面は開いている、Intensity 0 やウェイトが小さすぎると何もない）、G バッファーの
`renderMesh` のメモ（2 つのメッシュのうち近い方、目の方に向けた法線）、オクルージョンを
受ける床がその前のカードの周りで、カードがそれを落とし床が受けるときだけ暗くなる
ショット（`ambient-occlusion-card.png`）、Material の行（Receives/Casts Ambient Occlusion、
モデルにだけある Include Semi Transparent Triangles を持つ Ambient Occlusion グループ、編集と
その取り消し）、保存と読み込み（静的なプロパティ、`<i>` の数、`<fl>` の値）を確認します。
`reflectionsMirrorTheSceneInSheets` は緯度経度の参照（`ConvertVec2LatLong`、x または z が 0 の
方向でのその癖）、極での環境の色、白と半分赤の環境の放射照度、緑のカードの鏡像を、それが
映る場所に表示する黒い鏡のシート — 全強度、Specular Reflectivity 50 では 4 分の 1、Receives
または Casts Reflections がなければ映らず、非表示のカードからは Cast Reflections If
Visibility Is Off のときだけ、鏡の後ろのカードからは決して映らない（`planar-reflection.png`）
— Environment Map のレイヤーの色（`environment-map-layer.png`）または Use Composite Shot で
下のショットの色を取る 3D テキスト、種類ごとの Material の行（Use Layer を選ぶと出る
Environment Map の Layer の行）、保存と読み込みを確認します。
`modelsReflectTheirSurroundings` は反射マップのミップレベル、赤いレイヤーの Environment Map
で赤くなる白い鏡面色の灰色の立方体、反射を受け、カードが反射を落とすときにカメラの後ろの
非表示のカードで緑になる立方体（`model-reflection-cube.png`）、マテリアルが Planar の平らな
モデルがその平面に映ったカードを表示し（`model-reflection-planar.png`）、Spherical では表示
しないこと、`<ReflectionType>` として保存して読み戻す Reflection Mode、モデルでのアセットの
環境を確認します。
`clearCoatBrightensGrazingReflections` は白い環境の中の粗い Metallic-Roughness の四角形を
80 度と正面からレンダリングします：滑らかなクリアコートはかすめる角度の見え方を明るくし、
正面はほとんど変えません。`textGeometryReflectsItsEnvironment` は（登録のみの）Geometry
エフェクトを持つ 3D テキストに赤いアセットを Environment Map（タイプ 1003）として与えます：
文字が赤くなります。

## カメラとビューアー

`cameraFovProjectRoundTrip` はカメラのアニメーションするズーム、焦点距離、絞り、ぼかし、
被写界深度、クリッピングをリファレンスの CameraLayer と同じように保存し、ズームから導かれる
垂直の画角を確認します。`cameraLensAndClippingAreEdited` はタイムラインの Lens グループで
ズームを、Layer パネルで Near/Far Clip Distance を編集します。
`cameraDepthOfFieldBlursAwayFromFocus` は薄レンズのぼかしの項（焦点では何もなく、遠い
プレーンでは約 16 ピクセルの CoC、Blur 50 % ではその半分）と、HasDoF のショットでは遠い
プレーンの縁がぼけ、焦点のプレーンはシャープなままで、それがなければ何も変わらないことを
確認します（`camera-depth-of-field.png`）。
`viewerViewsSeeTheScene` はビューアーのビューのカメラを通して、緑の 2D プレーンの上の赤い
3D プレーンをレンダリングします：アクティブカメラは両方を見て、Front は 3D シーンだけを
1 単位 1 ピクセルで（スケール 2 では 2 倍）、Top と Left はプレーンを真横から（Top は床の
グリッド付き）、Perspective は (0, 100, 1000) から見ます。さらに、各軸のビューが点を置く
場所、ズーム、パン、オービットがビューのカメラを動かす方法、リファレンスのツールモードの
ビュー操作を確認します：原点と別のピボットの周りのオービット（距離とビューを保ち、直立し、
真上を越えない）、Dolly、Rotate、Zoom Camera のドラッグ（パースペクティブの画角はスケール
とともに狭まる）、どのボタンとキーでどれを選ぶか（`ui::viewDragFor`）、そしてビューアー
では中ボタンのドラッグでパースペクティブのビューのカメラを回し、右ボタンのドラッグで
パンし、ショット自身のビューでは何も動かないこと。`viewerCameraMenusChooseTheView` は
ビューごとのカメラメニューと、独自のカメラを持つビューが独自の絵を描くことを確認し、
`viewerStateSurvivesSave` はショットの `ViewerState` を保存して読み戻します。
`orbitTurnsAboutTheClickedPoint` は光線の下のシーンの点（内容がある場所での最も近い可視の
3D シート、モデルの三角形、通り抜ける非表示のレイヤー、それらの横では何もなし、
パースペクティブのビューの中央が 100 上に立つビューでショットの平面と交わる点）と、Orbit
ボタンの一覧を確認します — Orbit Around Clicked Point を選ぶと Orbit ツールが選ばれ、
パースペクティブのビューの上で押すとそのモードと押した場所が報告され、Alt と左ボタンでは
選択したレイヤーになります。
`sceneObjectsShowInTheViewer` は `SceneObjectOverlay` が描くものを確認します：ショットの
カメラを通すと自身の視錐台はなく、ライトのハンドルが投影される場所にその Reach を 60
ピクセルのリングで、スポットとポイント。Top ビューからはカメラの視錐台とハンドルも。
ハンドルでそのレイヤーを選び、ドラッグでビューに平行に動かし（カメラ越しでは右／下、上から
では前方へ）、1 回の変更になります。ビューアーはそれをリファレンスの青で描きます
（`viewer-scene-objects.png`）。
`viewerFramesFootage` はアンカーポイントの周りに回転した静止画で、ビューアーの枠の
ハンドルをドラッグします：角は反対の角を固定して均等に拡大縮小し、辺は 1 軸を拡大縮小し、
ノブはアンカーの周りに回転させ、レンダラーは枠のある場所に描きます。Text ツールでは枠は
なく、複数のレイヤーを選択しているときは内側のドラッグはビューアーのものです
（`viewer-footage-frame.png`）。
`previewSizesScaleTheWholeFrame` はショットのサイズの 1/4、1/2、1、2 倍でプレーンを
レンダリングし、同じ場所に置かれることを確認します。
`pixelAspectSqueezesTheFrameAndTheViewerUnsqueezes` は PAR 2:1 のショット（正方形の空間
400x100 を 200x100 ピクセルに圧縮、ビューアーは 4:1 で表示、`setsar` の分数）を確認します。
`reducedPreviewMatchesTheFullFrame` はアンカーの周りに回転し、組み込みのぼかしとぼかした
マスクを持つフッテージ、プレーン、テキスト、ネストしたショットの Half プレビューを正方形と
2:1 のピクセルでレンダリングし、縮小したフルフレームと比べます（`half-preview-*.png`、
`half-expected-*.png`）。`reducedPreviewDrawsTheSceneReduced` は 3D シーン（フォグの中で奥行き
方向に回転したプレーンとフッテージ、OBJ の立方体、その上の 2D テキスト）で同じことを行い
ます（`half-scene-*.png`、`half-scene-expected-*.png`）。`reducedPreviewBlursMotion` は
モーションブラーをかけた、動いて回転するプレーンの Half プレビューをレンダリングし、縮小
したフルフレームと比べます（`half-motion-blur*.png`）。
`viewer360DrawsThroughEnvironmentMapViewer`（OpenGL）は 360 ビューアーにリファレンスの
EnvironmentMapViewer モジュールで描画させ、両方のレンズ、組み合わせた回転、Scale と Scale
Ratio、レンズの双方向の写像について CPU の投影をそれと比べ、Top と Left がリファレンスと
同じ方向を見ることを確認します。
`layerPanelShowsTheSourceAndItsMasks` は Layer パネルがリファレンスの RenderLayerViewJob と
同じものを描くことを確認します：アセットレイヤーのソースとマスクを自身のサイズの
キャンバスに、エフェクト、トランスフォーム、不透明度なしで描き、テキストレイヤーでは何も
描きません。
`projectFilesGetTheirIcon` は .vegfx の ProgID を Software\Classes の代わりの ini に登録し、
その DefaultIcon（実行ファイルのアイコン 1）、開くコマンド、OpenWithProgids、Applications
のエントリ、拡張子自体の既定がそのままであること、変更のない登録は何も書かないことを確認
します。

## Layout パネル

Layout の回帰テストは、復元した向き／アンカー／整列のフォーム、リソースのアイコンの状態、
空の選択、フィールドにフォーカスがある間の基準点の変更、リンクした寸法、分布、アニメー
ションのキーの ID とハンドルの保持、レイヤーの並べ替え後の複数レイヤーの元に戻す／
やり直しをカバーします。`timeline_regression` の 3 つの `layout*` テストを実行してください。
スナップショットは一時フォルダーの `openvegas-layout-panel*.png` です。
`layoutBoxesFollowTheRenderedLayer` はレイヤーの Layout のボックス（`render::layerGeometry`）
を、レンダラーが実際に描くピクセルと比べ（拡大縮小した静止画、アンカーの周りに回転した
同じもの、2:1 のピクセルアスペクト、3D シート）、パネルに入力したボックス
（`render::fitLayerToBox`）が静止画、回転したテキスト、3D シートの行き着く場所であることを
確認します。カメラにはボックスがありません。`layoutFieldsScrubLikeTheReference` は
リファレンスの `DoubleScrubberSpinBox` を確認します：ドラッグは 1 ピクセルにつき 1 ステップ
加え、Shift では 5 ピクセルごとに 10 の倍数にスナップし、範囲の端で止まり、ボタンを離すと
1 回の編集になり、クリックでテキストを編集し、Escape で元に戻ります。Layout パネルの
フィールドはこのようなスクラバーです。

## ネイティブ `.hfpl` プラグイン

`everyShippedModuleRuns`（OpenGL）は `SAMPLES/VEGAS_Effects/Plugins` のすべてのモジュールを、
その種類がレンダリングされるとおりに（2D エフェクト、ビデオとオーディオのトランジション、
オーディオエフェクト、レイヤーと文字単位の Behavior、Geometry）既定値でホストに通し、
どれも失敗しないことを期待します。
`nativeControlsFollowTheModule`（OpenGL。モジュールが応答できない場合はスキップ）は、
オプション 0x2 で登録されたコントロールが非表示で始まること、プロパティが変わったと伝え
られた Animated Lasers が、エフェクトごとに、要求された数のビームのコントロールを表示する
こと、Glow の古い整数の半径は非表示のままであることを確認します。
`nativeEffectsMoveWithTime`（SAMPLES の LinearWipe、OpenGL）は 2D モジュールがフレームの
時刻を受け取ることを確認します：フレーム 0 と 30 は異なり、フレームは再現でき、レイヤーの
終わりを過ぎたフレームは最後のフレームを保ちます。`nativeEffectsSeeYUp`（OpenGL）は、Flux
の下の行が先のテクスチャが与えるとおりに、ネイティブモジュールが Y を上向きに見ることを
確認します：4PointRamp の +54 の点はフレームの上部を色付けします。
`effectsGrowTheirTarget`（OpenGL）は Drop Shadow にレンダリングのコンテキストで拡張量を
尋ね、拡張したターゲットに描画して切り戻します：レイヤーは拡大縮小されずにその場に残り、
隣に影がつきます。`outerGlowStaysAroundItsLayer`（OpenGL）は Outer Glow を拡張した
ターゲット（RenderContext +0x60/+0x64 のターゲットのサイズと、+0x78..+0x90 経由の余白）で、
中央の正方形の周りに描画します：正方形は残り、光彩はどの辺でも同じ強さです。
`nativeEffectsDrawReducedPreviews`（OpenGL）は、中心を動かした RadialBlur（コンテキストの
スケールを読み、ホストが点を縮小する）、半径 12 px の BoxBlur（ホストが px の値を縮小
する）、フルサイズで描いて縮小する Magnify を通したプレーンの Half プレビューを
レンダリングし、それぞれを縮小したフルフレームと比べます（`half-native-*.png`）。また
BoxBlur の半径が「px」で登録されていることと、縮小すると絵が変わることも確認します。
`nativeGlyphBehaviorsDrawReducedPreviews`（OpenGL、リファレンスの DropInByChar）は落ちてくる
文字を Half とフルサイズでレンダリングし、文字の位置（明るいボックスと中心）を縮小した
フルフレームと比べます。また DropInByChar のテキストの Layout のボックスも確認します：
文字が落ちてくる間、それは文字単位の Behavior のない同じテキストのボックスであり、
リファレンスはレイヤーをそのワールドトランスフォームだけで枠取りします。
`nativeBehaviorsKnowTimeAndLayer`（SAMPLES の FadeBehavior と DownInsert）は、Behavior が
ミリ秒を受け取り不透明度を返すこと（レイヤーの最初の 25 % でのフェード）と、レイヤーの
範囲とショットのサイズを伝えられた DownInsert がレイヤーを上端から始め、その場に残すことを
確認します。OpenGL があれば、DownInsert の Half プレビューも縮小したフルフレームと比べます
（`half-behavior-*.png`）。
`textGeometryFillsGlyphsAndRunsGeometryModules`（OpenGL）はテキストに Geometry モジュールを
適用し、Extrude の Half プレビューを縮小したフルフレームと比べ（`half-glyphs-*.png`、
`half-geometry.png`）、押し出した「O」を赤い床の前でライトに対して真横に向け、その壁が
そこに影を落とすこと（`geometry-shadow.png`）、影を落とさない設定では影がないことを確認
します。
`nativeMotionBlurReadsKeyframesBetweenFrames`（OpenGL）は Motion Blur を有効にした
EnvironmentMapViewer に、キーを打った回転をサブサンプルで読ませます：縞はにじみ、静止した
回転はブラーなしと同じに描かれます。
`viewerCustomUiReachesNativeModule`（OpenGL、SAMPLES の MotionTrack）はモジュール独自の
ビューアー UI への橋渡しを確認します：渡すキーとカーソルの表、MotionTrack が独自の UI を
報告し、それを設定し、Ctrl/Alt とマウスの押下を受け取り、再描画を求めること。
`nativePluginControlsAreTranslated` はロシア語のプラグインカタログを入れた状態で
リファレンスの Glow.hfpl のコピーをスキャンし、インターフェイスが表示する仕様を確認します：
翻訳された名前、ラベル、単位、そしてロシア語で表示されるコンボボックスの項目（保存される
値はモジュール自身のまま）。
`effectPresetsAreWrittenReadAndApplied` はリファレンスの形式で `.hfpr` エフェクトプリセット
を書き込んで読み込み、新しいバージョンと 3D プリセットをリファレンスの文言で拒否し、
Effects パネルに 2D/3D Presets を読み込み（空いている「名前 (N)」）、書き出し、名前の変更、
削除とともに一覧表示し、タイムラインからクリップにプリセットを適用して、キーをクリップの
開始に移した 1 回の元に戻せる編集にします。`nativeEffectsGoInTheReferencesLists` は
ネイティブエフェクトをリファレンス自身のリスト（`<Instance Version="8">` と型付きの
PropertyManager を持つ `<Effects>` / `<BehaviorEffects>`）に書き込み、ポートの拡張のない
ファイルから読み戻し、未知のプラグインのインスタンスをそのまま保ち、`<Effects>` を重複
させずに project_1 にマージします。`effectsFavoritesAndRecentsAreKept` は Effects パネルの
お気に入りと最近使った項目を、リファレンスの設定の形式（グループ Plugins、int 0 の
QDataStream、件数と ID）で確認し、最新を最後にした 5 件の最近使った項目と、カテゴリーの上に
あるパネルの Favorites/Recents の行を確認します。

## Behavior、トラッキング、スタビライズ

`simulatedBehaviorsMoveInTheWorld`（OpenGL、リファレンスの Gravity.hfpl）は落下する
プレーンを 4 分の 1 回転したポイントの子にします：横ではなくフレームの下へ落ち、ボックスも
ピクセルも同様です。落下するプレーンの子は一緒に落ち、Drag を持ちフレームを横切るキーの
あるプレーンはキーより遅れます。後のフレームから読み戻した実行は新しい実行と一致し、
フレーム間の物体はその間にあり、Drag を持つ子の物体は親とほぼ同じ距離を落ち（その力）、
キーのない落下するプレーンにはモーションブラーがかかります。
`behaviorsBlurAtTheirSubFrames` は MotionTrack の行列でキーのないプレーンを 1 フレームに
8 ピクセル動かします：モーションブラーはボックスの周りで進む方向ににじませ、モジュールの
行列はフレーム間で読まれます。`behaviorsMultiplyInTheReferencesOrder` はプレーンに 4 分の 1
回転と、その後の右への移動を与えます：プレーンは上へ行き（最後の Behavior が最初に作用
する）、親の移動は子を同じだけ運びます。ボックスとピクセルは一致します。
`layerBoxFollowsItsBehaviors` はプレーンに MotionTrack の行列（フレーム 1 で右に 50、上に
20）を与えます：レイアウトのボックスはそれとともに動き、Behavior をオフにすると動きません。
`sheetBoxFollowsItsBehaviors` は回転した半分のサイズの 3D プレーンに MotionTrack の行列を
与えます：シートの行列は移動と静止時の行列の積（親 x Behavior x 自身）になり、ボックスは
レンダラーが描く赤いピクセルと一致します。`behaviorsActInsideTheParent` はプレーンの親を
時計回りに 4 分の 1 回転させます：MotionTrack の右への移動はプレーンを下へ動かし、
ボックスもピクセルも同様です。
`motionTrackInstanceMovesItsLayer`（SAMPLES の `MotionTrack.hfpl` が必要、OpenGL 不要）は、
`NativeInstanceHost` がモジュールに与えるビュー（フッテージのピクセルをフッテージの
レイヤーと同じように配置）、モジュールの動きのキャンバスへの変換、レンダラーが焼き込んだ
4 分の 1 回転を時計回りに回すこと、保存と読み込みを通した `<InstanceBytes>` を確認します。
`motionTrackReadsNestedShotsAndWorlds`（offscreen）は、タイプ 3 のフッテージとしての
ネストしたショット、そのレンダリングしたフレーム、レイヤーのワールド、モジュールの応答から
そのワールドを取り除くことを確認します。
`motionTrackHostTracksFootage`（OpenGL）は 30 フレームの連番画像で MotionTrack の全サイクル
を実行します — 解析、なげなわ、タイマーでのバックグラウンド呼び出し、行列、保存した
データだけから得る同じ行列、次にフッテージのレイヤーを移動・回転した同じ動き（モジュールは
`GetLayerInfoV2` が与えるワールドで応答し、ホストがそれを取り除く）、そして開いている
ショットが追跡するショットをネストしているだけのホストからの実行。
`stabilizersSteadyTheirOwnLayer`（OpenGL）は最大 6 px 揺れる 30 フレームの連番画像で
AutomaticStabilizer と MotionLock を実行します：ホストはそれぞれに自身のレイヤーに置かれた
ことを伝え（`Effect_Parent_Layer`）、フッテージはバックグラウンドで解析され、レンダラーの
フレーム（保存したデータからエフェクト自身のレンダーインスタンスが描く）はフッテージの
フレーム間の揺れの 3 分の 1 未満に収まる必要があります。続いて AutomaticStabilizer の
「Create Point Layer」（キー付きのポイントレイヤーを上に作り、「Apply To」をそれに向ける）と
「Apply Transform」、MotionLock の「Apply Transform」（自身のレイヤーにその動きのキーを打つ）
を、それぞれ 1 回の `layersEdited` として実行します。

## メディア、キャッシュ、プロキシ、プリレンダー

Photoshop ドキュメント：`photoshopDocumentReadsLayers` はファイル形式の仕様どおりに
ドキュメントを書き出し（`tests/PsdTestWriter.h`）、そのレイヤーを読み戻します。非圧縮、
RLE、ZIP、予測付き ZIP のチャンネル、描画モードのキー、不透明度、グループ、クリッピング、
ラスターマスクとベクトルマスク、Unicode の名前、露光量の調整レイヤー、PNG を埋め込んだ
スマートオブジェクト、統合画像を確認します。`photoshopDocumentImportsAsCompositeShot` は
それをコンポジットショットとして読み込み（上からのレイヤー順、グループの不透明度、Grade
レイヤー、レイヤースタイルの光彩（外側）とドロップシャドウ、四隅で配置されたソースとしての
スマートオブジェクトと Exposure Pro としての Camera Raw）、描画します。
`photoshopSamplesImportByLayers` は `SAMPLES/vegfx_projects/project_1` の PSD（Photoshop で
作成、git 外）を読み込み、Photoshop 自身の統合画像と比較します（`psd-<名前>.png`、
`psd-<名前>-photoshop.png`）。`photoshopLayersSurviveSave` は絶対パスと相対パスで保存して
開き直します。OpenGL とリファレンスのプラグインがあれば、`psdDropShadowFallsAwayFromTheLight`
が光源の角度と Drop Shadow の角度の対応を、`psdCameraRawRendersWithExposurePro` が +0.5 EV で
スマートオブジェクトのソースが明るくなることを確認します。

`mediaOverridesReinterpretFrames` は Media Properties の残りを確認します：30 fps のファイルの
15 fps への上書き（ソースの時刻と長さ）、NTSC のレート、白を 235 に圧縮する Computer
（Full）、HD での Rec. 601 の行列の強制、静止画から取り除くプリマルチプライドアルファ、その
ダイアログ、プロジェクトを通した静止画のアルファの上書き。
`mediaPixelAspectFromFileAndOverride` は Anamorphic 2:1 に上書きした 200x100 の静止画を幅
400 で描き、プロジェクトを通して上書きを保存・読み戻し、Media Properties を操作し
（`media-settings.png`）、ffmpeg と libVLC がある場合は SAR 10/11 の 720x480 クリップを
25 fps の DV NTSC として読み、そのフレームレート、レベル、色空間、ハードウェアデコードの
上書きを保存・読み戻してダイアログに表示します（`media-settings-video.png`）。
`multipleMediaAssetsSurviveImportAndProjectLoad` は 8 つの異なる画像を読み込み、追加／切り
離しを通して共有アセットのスナップショットを保ち、重複なく再読み込みし、できたプロジェクト
を 3 回読み直して、パス、寸法、クリップ ID を確認します。
`mediaAudioStreamsMetadataSelectionAndRoundTrip` は 32/44.1 kHz の 2 つの PCM トラックを
持つビデオを作り、libVLC のコーデック／レート／チャンネルのメタデータ、Media Properties、
元に戻す／やり直し、ネイティブドキュメントの繰り返しのマージ／保存／読み込み、オフラインでの
選択の保持を確認します。完成した MOV からデコードした PCM を含む両方のオーディオ
エクスポートの経路と、両方のトラックについて別々のメモリ／ディスク上の波形のエントリも確認
します。`selectedAudioStreamReachesMasterPcm` は、2 つの異なる信号の実際のマスター PCM を
比べて、VLC の再生が要求したトラックを選ぶことを確認します。どちらにも FFmpeg と libVLC の
ランタイムが必要です。
`mediaCacheKeepsWaveformPeaks`（FFmpeg が必要）は、保存先をメディアキャッシュのフォルダーと
データベースにして波形を作り、デコーダーなしで読み戻し、レコードの使用時刻、DaysToKeep に
よる整理と消去（ファイルも削除する）を確認します。
`mediaCacheKeepsSlowDecodedFrames` はデコードに時間のかかるフレームは保持し、速いものは
保持せず、別のキャッシュでデコードなしに読み戻し、キー（位置、サイズ、ファイルの同一性）、
破損したファイルでのミス、データベースに記録されたサイズ、`pruneToBytes`（最も長く使われて
いないものからファイルごと）、同時に開いた 2 つのデータベース、古いデータベースに追加した
`bytes` 列を確認します。`mediaAudioIsConformedOnce` はメディアキャッシュで 44.1 kHz モノの
WAV を 48 kHz ステレオの PCM にコンフォームします：バックグラウンドで一度作って記録し、
キャッシュから読み直し、別のレートや置き換えたファイルでは作り直します。
`timelineDiskCacheSurvivesManagersAndExpires` はマネージャーをまたいだフレームのディスク
キャッシュとその期限切れを確認します。またフレームを探してもそのファイルを作らないこと
（以前はフォルダーができると空のファイルが残り、そのフレームは保存されなくなっていた）と、
破損したエントリが削除されて保存し直されることも確認します。キャッシュのテストは Plane を
レンダリングし、VLC による実際のビデオのデコードはこれらでは確認しません。
プロキシの回帰テストは、古い品質／プレビューの値、FFmpeg による生成、既存のプロキシがある
状態でのトランスコードの失敗／起動、QSaveFile による公開をカバーします。2 MiB を超える
置き換えでブロックコピーを確認し、入力がない／空、書き込み先のブロック、（Windows では）
名前の変更／削除を拒むハンドルで、古いバイトが保たれることを確認します。
`legacyProxyChoicesReachEncoderAndPreview`、`proxyPublicationPreservesPreviousFile`、
`proxyMediaGenerationAndRoundTrip` を実行してください。
`preRendersWaitTheirTurn` は PreRenderManager に 2 つのショットをキューに入れ、2 つ目が 1 つ目
の終了を待つこと、どちらもフレームごとにフレームファイルを持って終わること、編集した
ショットはフレームを失い、編集が落ち着くと作り直されること、Remove でフォルダーが削除される
こと、キャンセルでキューが空になることを確認します。`preRenderCarriesTheShotsAudio` は
トーンを持つショットをプリレンダーします：そのプリレンダーにはショットのミックスが WAV と
してあり、エクスポートはネストしたショットにそれを使い、編集したショットはもう一致
しません。

## エクスポート

`exportJobFollowsPresets`（FFmpeg と ffprobe が必要）はプリセットでエクスポートしてファイルを
調べます：640x360、20 fps、Main プロファイルの MP4、アルファ付きの ProRes 4444、プレフィックス
付きの 120x120 の JPEG 連番、96 kHz の WAV とビデオストリームのない AAC、そして音のない
ショットに対するオーディオ専用プリセットの拒否。`exportRendersFramesAtOnce` は動くプレーンを
1 つと 3 つのレンダラーでエクスポートし、フレームが同じであること、進捗が 1 つずつ増える
こと、再生キャッシュ（Turbo）が作ったタイムラインキャッシュがあるとすべてのフレームがそこ
から来ること、編集したショットは新たにレンダリングされることを確認します。
`exportedMoviesKeepTheShotsColours`（FFmpeg）は赤いプレーンを MP4 にエクスポートし、
ストリームに BT.709／リミテッドレンジのタグが付き、ショットの赤にデコードし戻ることを確認
します。以前は FFmpeg のタグなしの BT.601 の既定のせいで、HD ビデオを BT.709 とみなす
プレーヤーではビューアーより明るく表示されていました。
`exportQueueRunsTasksFromSnapshots` は Time Format の文字列、キューに入れたスナップショット、
タスクファイル、複製／削除をカバーし、`render::ExportJob` で 2 つのタスクを PNG 連番に実行
して、後の編集がキューにあるタスクに届かないことを確認します。
`ui_stubs_regression` では `exportPresetsBuiltInAndFiles`、`exportPresetArguments`、
`exportPresetManagerAndPage` が組み込みのプリセット、`.hfxp` ファイル（リファレンスの形式の
ものも含む）、FFmpeg の引数とフィルター、マネージャー、Presets タブ、Edit Preset、Export
パネルの一覧を確認します（`export-presets.png`）。同じターゲットは、ボイスオーバーの
RIFF/PCM 出力、不完全なサンプルフレーム、拒否される形式、既存のファイルを保つキャンセル、
コンボボックスのホイールの設定、エクスポート名の扱い、ムービープリセットでの画像の
エクスポートを確認します。実際のマイクからは録音しません。

## オーディオ

コンポジションのオーディオレートの回帰テストは 32/44.1/48/96 kHz をカバーします。
オーディオのテストは、リサンプリングしたマスターのブロックサイズ、オフセット／速度／ゲインの
タイミング、シーク／停止、アニメーションするネイティブの Balance、短いネイティブの
AudioReverse の先読みを確認します。ネイティブのテストを有効にするには
`OPENVEGAS_HFPL_BALANCE` をリファレンスの Balance.hfpl に設定します。単独の setSource は
引き続き 48 kHz を使います。8..192 kHz の範囲外の無効なプロジェクトのレートは 48 kHz に
戻ります。
`compositionSampleRateReachesMovieAndProcessedPcm` は、プロジェクトの保存と読み込み、直接の
PCM とアニメーションする Level の PCM の分岐、MOV と MP4 での出力のサンプルレート／
チャンネル／長さ、MOV での実際の PCM の数とゲインを確認します。通常の速度を atempo=1 に
通すことで起きる末尾の切り捨てを検出します。オーディオトランジションのエクスポートも同じ
4 つのレートで実行します。これらの確認は実際の FFmpeg/VLC を使い、物理デバイスの形式を
測ったり、すべての AAC エンコーダーのレートを保証したりはしません。
ネストしたオーディオのエクスポート：`nestedAudioOffsetsSpeedGainAndReuse` は 2 段階、
0.25x/2x の速度、トリム／オフセット、継承／アニメーションするレベル、非表示／ミュートの
レイヤー、共有する子、プロジェクトの保存と読み込み、ルート／子のキーフレームの fps、
インスタンスの分離、32/44.1/96 kHz での MOV の部分エクスポートからの実際の PCM を確認
します。`nestedAudioCycleDoesNotPublishPartialInputs` は、既存の入力を変えない失敗、無視
されるミュートの分岐、循環を断った後の回復を確認します。`nestedAudioNativeDspAndTransitions`
は `OPENVEGAS_HFPL_BALANCE` を必要とし、親の速度の前の子の AudioReverse、アニメーション
する親の Balance、コンポジットクリップ間のネイティブの Fade、用意した入力サンプルに対する
最終的な MOV の PCM を確認します。`audioMixUsesSampleDelaysAndPadding` は、0.5 ms の遅延が
44.1 kHz でちょうど 22 サンプルになることと、要求した出力の長さへのパディングを確認します。
ネストしたオフラインのグループは PCM16 を使います。
`nestedShotsAreMixedBeforeTheirClipsProcessThem` は、どのコンポジットクリップがそのミックス
を処理するか（有効なネイティブオーディオエフェクトがある）を見つけ、そのショットを
バックグラウンドでトーンのレベルで一度ミックスし、読み戻し、編集したショットをミックスし
直します。リアルタイム再生はそのミックスをクリップのネイティブエフェクトに通します。
`clipsAreProcessedWholeForPlayback`（FFmpeg と `OPENVEGAS_HFPL_BALANCE`）は 2 秒のクリップを
エクスポートと同じように AudioReverse に通し（クリップの終わりが先に来て、ゲインは再生に
任せる）、`ProcessedClipAudioCache` がクリップの状態ごとに一度だけそれを作ることを確認
します。audio_regression の `nativeEffectsNeedingTheWholeClipAreReported` は、再生がその
ためにどのクリップを報告するか（先読みを超える Reverse、別のエフェクトの後のエフェクト）と、
先読みで足りるクリップは報告しないことを確認します。

## オプション（`options_regression`）

このターゲットは、オプションの 14 のカテゴリーすべて、リファレンスのオブジェクト名、依存
するコントロール、INI への保存、Cancel/Restore の動作、編集できるショートカットをカバー
します。`options-general.png`、`options-proxies.png`、`options-export.png` を書き出します。
オプションの移行の回帰テストは、実際の ru/ja/zh_CN の QM カタログと英語を使います。
`localizedOptionValues` は、言語を変える前後での古い翻訳済みのプロキシの選択肢、表示の
ラベルが変わっても安定した INI の値、新しいプロジェクトの既定値を確認します。
`nativeAntialiasingModesPersist` は 9 つのモードすべて、Restore Defaults、古い Off/2x MSAA の
フォールバックをカバーします。これは設定の伝わり方を確認するもので、レンダラーの MSAA の
サポートを確認するものではありません。`gpuDriverWarningFollowsTheReference` はリファレンスの
Intel GPU のクラス、それぞれに必要なドライバーのビルド、警告の文言、そこで Exit を選ぶと
終了することを確認します。
`pluginsPageChoosesFolders` は VEGAS Effects と HitFilm Pro のプラグインツリーを持つ偽の
Program Files を用意します：インストールのプラグインが見つかってスキャンされ、他の FXhome
製品のものが提案され、オプション > プラグインで選んだフォルダーは OK で保存され（フォルダーが
変わったときだけ新しいスキャンを求める）、ページの既定値でそれが消え、どのフォルダーにも
プラグインがないときはオプションが一度だけ自動でそのページを開きます。
`renderMapSizesReachNewProjects` はオプション > Render で影と反射のマップのサイズを設定し、
新しいプロジェクトのレンダー設定でそれを見つけます。

## 翻訳

`translation_regression` ターゲットは Qt LinguistTools でロシア語、日本語、簡体字中国語の
カタログをコンパイルします。カタログの読み込み、英語への切り戻し、カラーピッカーと
スプラッシュのコンテキスト、ロシア語の複数形、プラグインのカタログ（`Effect_Glow` の Glow
の名前、ラベル、コンボボックスの項目、ロシア語の単位）を確認します。
`optionsShowNoEnglishInOtherLanguages`（options）と `panelsShowNoEnglishInOtherLanguages`
（timeline）は、Qt 自身のカタログとともにロシア語、日本語、中国語でオプションとパネル
（すべての種類のレイヤーを選択した Controls と Layer、メニュー付きのビューアーのバー）を
開き、英語の単語が残っているラベル、ボタン、リスト、コンボボックス、タブ、ヘッダー、
プレースホルダー、ツールチップ、アクションがあれば失敗します（`tests/UntranslatedText.h`。
形式、デバイス、ファイルの名前とプロジェクト自身のデータは除外）。`tr()` を通さずにコードで
表示したテキストはカタログに入らないため、すべての有効な TS メッセージの空の翻訳、
プレースホルダー、複数形、マークアップ、ファイルフィルターを確認する
`python tools/validate_translations.py` にはそれが見えません。
