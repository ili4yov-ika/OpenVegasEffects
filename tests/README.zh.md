# 回归测试

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

回归测试是 `tests/` 中独立的 CMake 项目。它把所需的应用程序源代码构建为六个 QtTest 目标，
全部注册到 CTest：

| 目标 | 覆盖内容 |
|------|----------|
| `textpanel_regression` | Text 面板：控件与渲染文本的对照、描边、颜色对话框、输入和拖动、撤销／重做、通过 `.vegfx` 序列化器保存的文本样式 |
| `timeline_regression` | 应用程序的大部分：时间轴、项目、媒体、3D、查看器、Layout、原生插件、Behavior 与跟踪、导出、缓存、面板的翻译 |
| `options_regression` | 选项对话框：全部 14 个分类、参考对象名、设置、Restore Defaults、快捷键、插件文件夹、已翻译的选项值 |
| `ui_stubs_regression` | 画外音录制的输出、Export 面板及其预设 |
| `audio_regression` | libVLC 主混音、采样率、实时原生音频特效 |
| `translation_regression` | 编译后的 ru/ja/zh_CN 翻译目录 |

## 构建与运行

在仓库根目录下，使用 MSVC 版 Qt（其他套件同理）：

```powershell
cmake -S tests -B build/ci-local-test -DCMAKE_PREFIX_PATH=C:/Qt/6.9.3/msvc2022_64
cmake --build build/ci-local-test --config Release --parallel
ctest --test-dir build/ci-local-test -C Release --output-on-failure --timeout 120
```

CTest 在 `offscreen` 平台上运行测试套件。在 CTest 下，`timeline_regression` 还会把日志写入
构建目录旁的 `timeline_ctest.txt`：它的标准输出不会进入 CTest 的日志，而该文件会指出是哪个
函数失败。单个测试以 `timeline_regression.exe <测试名>` 运行。

运行原生 `.hfpl` 模块的测试需要 OpenGL 4.1 上下文，在 offscreen 下会自动跳过。请在
`windows` 平台上运行它们（不会显示窗口）：

```powershell
$env:QT_QPA_PLATFORM = 'windows'
build/ci-local-test/Release/timeline_regression.exe nativeEffectsMoveWithTime everyShippedModuleRuns
```

### 部分测试的依赖

- 原生模块的测试需要 `SAMPLES/VEGAS_Effects/Plugins` 中的**参考插件**（没有时跳过）。
- 导出、代理、波形和转换后音频的测试需要 `PATH` 中的 **FFmpeg 和 ffprobe**。
- 音频和媒体音轨的测试需要 **libVLC**（`OPENVEGAS_VLC_DIR` 或已安装的 VLC）；
  `OPENVEGAS_VLC_AUDIO_OUTPUT=dummy` 可在没有声音设备的情况下播放，与 CI 相同。
- 把 `OPENVEGAS_HFPL_BALANCE` 指向参考程序的 `Balance.hfpl`，即可启用 `audio_regression`
  中的原生音频测试以及嵌套音频的测试。
- `OPENVEGAS_PROJECT3_OUT` 指定 `buildProject3` 写入 `SAMPLES/vegfx_projects/project_3` 的
  文件夹（否则跳过）；配合 `OPENVEGAS_PROJECT3_PREVIEW`，它还会把预览帧渲染到那里。

### 产物

供查看的图像以 `openvegas-<名称>.png` 写入临时文件夹（`timeline_regression`），或写在测试
可执行文件旁边（`text-panel.png`、`text-panel-narrow.png`、`color-dialog.png`、
`options-general.png`、`options-proxies.png`、`options-export.png`）。测试使用固定尺寸的
夹具以便目视检查；它们不断言与 VEGAS 逐像素一致。

完整构建应用程序：`tools\msvc_build.cmd build`。

---

## Text 面板（`textpanel_regression`）

该测试使用 offscreen 平台，并显式加载 Windows 的 Arial 字体。它检查控件与渲染图像的对照、
描边、颜色对话框、取色取消、输入和拖动、撤销／重做，以及通过实际的 `.vegfx` 序列化器保存
文本样式。桌面像素抓取需要真实的显示器，此 offscreen 测试不做验证。面板快照为
`text-panel.png`（400 px）和 `text-panel-narrow.png`（327 px）。

`referencePointTextStandsOnItsBaseline`（timeline）在 15 秒处渲染 project_1 的两行点文本
（`project_1-text.png`），检查无论垂直对齐如何，每一行都站在图层位置的基线上、去掉末尾空格
后居中，并且保存的 `<TextBox>` 与参考程序的范围一致；`paragraphTextBoxKeepsItsOffset` 保存
并读回一个偏离中心的段落框。
`textFrameFollowsItsParent` 把文本图层的父级顺时针旋转四分之一：文本框随之旋转，画布上的
移动变成以父级单位表示的移动。

## 时间轴、关键帧与合成镜头

`timeline_regression` 目标覆盖树和画布行的对齐、搜索、可编辑参数、颜色与预设、关键帧与插值、
图层控件、稳定的图层 ID 与父子关系、Layer 检查器、可撤销的剪辑移动／切片、可撤销／取消的
合成属性、`.vegfx` 保存、内置 Blur 和 Color Correction Wheels 的全部参数、播放缓存失效、
带七个视图预设和等距柱状投影的 Viewer 360 面板、Trimmer 持久的 In/Out 范围和 Insert/Overlay
信号，以及参考程序 MenuBar 的 29 个操作及其对象名。面板快照：`timeline-panel.png`、
`timeline-blur.png`、`timeline-color-wheels.png`、`controls-panel.png`、
`controls-color-wheels.png`、`layer-panel.png`、`viewer360-panel.png` 和 `trimmer-panel.png`。

包含多个合成镜头的项目（`projectKeepsEveryCompositeShot`、`nestedShotsCannotLoop`）会加载每个
CompositionAsset，链接嵌套的 AssetLayer，保留 `<IsPrimary>` 和 `<OpenCompositeShots>`，拒绝
嵌套循环，并以原生格式写回每个镜头；`mediaPanelListsCompositeShots` 检查 Media 面板中的镜头
行。`compositeShotFilesRoundTrip` 写入并读取 `.vegfxcs` 文件（只包含镜头使用的媒体，拒绝嵌套
镜头，替换已占用的 ID，连同嵌套镜头一起导入），`importCompositionDialogPicksShots` 操作镜头
选择对话框（`import-composition-dialog.png`）。`mediaRowsDropOntoTimeline` 检查 Media 面板的
拖动数据，以及时间轴在任意位置接收它们并报告放置的图层和时间。
`compositionTemplatesAndFormats` 覆盖帧率分数、PAR 表、Composite Shot Properties 对话框
（模板、Custom、Match Timeline、输入的帧率）、用户 `.hft` 模板以及 PAR／采样率／NTSC 的保存
（`composition-settings.png`）。时间轴回归测试还会检查移动后的相对路径项目，包括多个剪辑、
嵌套合成、离线媒体和内嵌的停靠布局。

`valueGraphFollowsTheReference` 检查数值曲线图的网格步长（标尺的时间步长及其细分、不小于
40 px 的数值步长）、通道颜色，以及在所选属性的所有通道上、两侧各留出十分之一范围的 Auto Zoom。
`keyFramesBetweenFrames` 计算帧之间的关键帧值（分段曲线、关键帧叠在关键帧上、Hold 保持）。
`keyTimesAreRelativeToTheLayer` 保存一个从 2 秒开始、在 2 秒和 3 秒处有关键帧的平面：文件中
保存的是 0 和 1000 毫秒（`OpenVegasKeyTimes="layer"`），读取时恢复到第 60 和第 90 帧，没有该
属性的保存（移植版较早的保存）按镜头时间读取，`shiftLayerKeys` 保留关键帧的标识。
`movedLayerTakesItsKeys` 把剪辑主体向后拖动一秒：图层的关键帧和其特效的关键帧移动相同的帧数，
作为一次撤销步骤。`trimmedLayerKeepsItsKeys` 把剪辑的开头向后拖动剪辑长度的四分之一：结尾
不变，源入点随之移动，图层及其特效的关键帧停留在各自的帧上（其中一个现在位于图层开始之前），
保存和加载后仍然如此。
`layerTransformsBecomeKeys` 检查变换如何成为关键帧或静态值。
`layersWithoutClipsKeepTheirSpan` 保存一个跨越 1–2 秒的摄像机（StartFrame 30、EndFrame 60）
和一个覆盖整个镜头的灯光（EndFrame = FrameCount，读回时为开放区间）；旧保存中的空区间按整个
镜头读取。`camerasAndLightsTakePartWithinTheirSpan` 在第二个摄像机开始时切换活动摄像机，灯光
只在其区间内照明。`cameraBarMovesAndTrimsItsSpan` 拖动摄像机在时间轴上的条：主体移动区间和
关键帧，尾部修剪区间并保留关键帧，各为一次撤销步骤。
`sliceAndStretchCutAndTrimACameraSpan` 把摄像机的条切成两段（从切口往后是副本，两者都保留
关键帧），并对其末端做速率拉伸，从而修剪它。`multiClipLayersGoOutAsSlices` 把带两个剪辑的平面
保存为两个图层——第二个标记为 OpenVegasSliceOf，范围是其剪辑的范围——读回时成为带两个剪辑的
一个图层，没有标记时则与参考程序一样读成两个图层。
`fadeToColorDrawsItsSide`（OpenGL）把编辑点的哪一侧（RenderContext +0x08）以及以毫秒为单位的
窗口告诉 Fade to Color：红色在编辑点之前渐变到黑色，绿色在之后从黑色中出现；从无到有时，剪辑
在整个窗口中从黑色淡入。
`buildProject3` 在 `OPENVEGAS_PROJECT3_OUT` 指定文件夹时写出 `SAMPLES/vegfx_projects/project_3`
（否则跳过），并把预览帧渲染到 `OPENVEGAS_PROJECT3_PREVIEW`。

## 父子关系、对齐与场景

`childrenFollowTheirParents` 检查一个位于隐藏、旋转并缩小一半的 2D 父级之下的 2D 平面——它在
场景中的位置、画布变换、Layout 框和渲染结果（`parented-2d.png`）——还检查去掉平面父级的锚点、
父级循环、摄像机站在其父级点所放的位置，以及 3D 父级下的 2D 图层只在该父级可见处跟随。
`parentedLayersFitAndPlaceTheirFootage` 把一个位于旋转 90 度并放大到 200% 的点图层之下的平面
适配到一个框中（框落在指定位置），并检查实例的素材被放置在其父级之下（向右 30 px，向上 20）。
`towardsLayerTakesTheOtherLayersUp` 检查 Towards Layer 的第二次旋转：看向绕 Z 旋转四分之一的
点的摄像机采用该点的上方向（点倒置时旋转半圈），其上方的片和灯光也一样，而行进方向沿该点 Y
轴的灯光采用该点的 -Z。
`layersTurnByTheirAlignment` 让灯光和摄像机朝向 Towards Target Position（正下方时上方向保持
(-1, 0, 0)；灯光自身的方向在瞄准后进一步旋转它），让点图层的 +Z 指向其目标，让灯光朝向
Towards Layer，让 3D 平面朝向 Towards Active Camera 并像摄像机一样旋转、在本应侧对的位置完整
绘制，检查 Transform 行（Target 在 Position 之后、参考程序按类型提供的 Alignment 选项、按要求
出现的 Alignment Axis 和 Alignment Layer、一次撤销步骤；`timeline-alignment.png`），以及
`<AutoOrient>`、`<AutoOrientLayer>` 和 `target` 的保存与读回。
`layersFollowTheirMotionAndOtherLayers` 检查 2D 条上的 Along Motion Path（由对角运动得出的
X 轴和 Y 轴角度、停止后的自身 Rotation、沿对角线的 Layout 框和渲染；`align-motion-2d.png`），
3D 片和聚光灯上的同样情况，片（正面朝向图层、绕中心旋转）和摄像机上的 Towards Layer，以及
查看器中的 Target 控柄：有自己的连线和控柄，在灯光不动的情况下平行于视图拖动。
`threeDLayersAreSeenThroughTheCameraWithFog` 检查 Flux 的 `ComputeFoggedFragment` 的雾因子、
z = 0 处的 3D 图层与 2D 图层落在同一位置、随距离缩小、按每个像素的距离变淡（绕 Y 旋转的平面
会出现渐变），以及 2D 图层不受影响（`fog-3d-plane.png`、`fog-3d-turned.png`）；
`compositionProperties` 也会操作 Fog 分组。`threeDLayersShareOneScene` 检查 3D 场景内的深度
顺序，以及 2D 图层会把场景分开（`scene-depth.png`）。
`bodiesHideWhatIsBehindTheirSurface` 把一张红色 3D 片放在立方体模型内部、其中心与前表面之间，
检查前表面会遮住它（物体按其表面所在位置逐像素判定），而放在立方体前方的同一张片则可见。
`sceneIsMultisampled` 检查九种抗锯齿模式的采样数及其采样模式：多重采样三角形的边缘像素被
部分覆盖，其覆盖率在 4/8/16/32 采样下与真实面积一致，而内部的着色与单采样相同；被另一个面
挡住的面保持隐藏；旋转的 3D 片边缘是柔和的，而 2D 图层不是；该模式只进入 3D 镜头的缓存键
（`msaa-triangle-4x.png`、`msaa-3d-sheet.png`）。

## 3D 模型

`modelTexturesAreDrawn` 加载一个带 2x2 `map_Kd` 的 OBJ 四边形，检查每个四分之一都显示其纹素、
V 朝上，以及 Flip UV Coordinates 会把它翻转（`model-texture.png`）。
`modelTexturesFollowTheProjectLimit` 在模型上绘制细条纹：在默认上限下可见，当 Model Texture
Max Size 为 2 px 时变成灰色。`layoutBoxesFrameModels` 把模型图层的 Layout 框与其渲染像素对照，
把它适配到输入的框中，并确认其动画会带动该框。
`modelImportFollowsTheReference` 检查参考程序的导入选项：单位列表及其代码、单位对应的像素
（毫米除以 2.8352）、Flip YZ 为 (-x, z, y)、Auto Normalize 到对角线一半为 500 像素且被 Model
Scale 抵消、居中、对没有法线的文件生成最多 80 度的平滑法线、OBJ 的 `DefaultMaterial` 以及
Assimp 的材质默认值。`modelImportSettingsSurviveSave` 保存一个以所有导入设置都已修改的方式导入
的模型，并检查这些设置和节点轴心都能恢复。
`fbxModelsAreRead` 把同一个场景（一个包含经过旋转、缩放并贴图的四边形的 null，其平移带动画）
分别写成二进制 FBX 7.4、使用压缩数组的 7.5 和 ASCII，并检查每种格式读出的结果相同：层级、
边界、法线、UV、材质和纹理、动画，以及对非 FBX 和 FBX 6 的拒绝。`threeDsModelsAreRead` 和
`lightWaveModelsAreRead` 对 3DS 对象（带一个大写命名的纹理）和 LWO2 图层（被 VMAD 打断的 UV
贴图、带 UV 映射片段的表面、从 LightWave 左手坐标系镜像过来后仍然面向摄像机）做同样的检查；
它们还检查参考程序读取这些格式时遵循的 Assimp 规则：带细节多边形的 LWOB、LightWave 的光泽度、
高光和按表面的平滑、仅在 Phong 着色时使用的 3DS 光泽度、场景的环境光、`%%%DEFAULT` 材质、平滑
组以及关键帧器的节点层级。`lightWaveProjectedTexturesGetUvs` 写入一个带沿 Z 的平面贴图和在六边
形管上绕 Y 缠绕两次的圆柱贴图的 LWO2，以及一个绕 Y 的“Planar Image Map”的 LWOB，检查 UV 与
Assimp 的 GenUVCoords 结果一致——包括跨越圆柱接缝、两侧按 RemoveUVSeams 的方式交换的两个面。
`gltfModelsAreRead` 在测试中构建一个 glTF 2.0 四边形——缓冲区和纹理放在 data: URI 中，同样的
内容也写成带二进制块的 .glb——检查节点层级、父级的平移、UV（glTF 中 v 朝下）和屏幕上的基础
颜色纹理（`gltf-*.png`），以及 glTF 1.0 会被拒绝并给出提示。
`fbxPivotsAndTimeFollowTheReference` 绕轴心旋转和缩放一个 FBX 模型，并按文件的 TimeMode（30）
把动画计数到堆栈的 LocalStop。
`skinnedModelsFollowTheirJoints` 构建一个绑定到两个关节的 glTF 条带，检查静止姿态、骨骼旋转时
的弯折、蒙皮节点自身的运动被忽略，以及图层按 Strength 和导入缩放摆出的姿态；
`fbxSkinsFollowTheirBones` 用 FBX 蒙皮簇检查同样的弯折。`morphTargetsFollowTheirWeights` 构建
一个 glTF 四边形，带一个静止权重为 0.5、关键帧从 0 到 1 的变形目标，检查静止姿态、剪辑的权重、
变形后的网格、Strength 和导入缩放；`fbxBlendShapesFollowTheirWeights` 用 FBX 混合形状通道
（DeformPercent 50，0..100 曲线）做同样的检查，包括形状的法线偏移。
`modelAnimationsPlayOnTheLayer` 构建一个带两个动画的 glTF 手臂，检查线性、阶梯和三次样条关键帧、
slerp、子节点跟随带动画的父节点、导入设置的缩放和坐标轴会带动运动、另一个文件的动画在共享节点
路径上播放、参考程序的帧计算（Loop、Custom Playback、帧率覆盖、精确的 NTSC）、模型图层只在有
实例时按其 Strength 运动，以及实例在保存和加载后保留。`modelAnimationRowsEditTheInstance` 从
时间轴插入动画，编辑 Strength、Loop、Custom Playback 和 Playback（播放头处的关键帧值），用关键帧
按钮切换 Strength 的关键帧，在该行的轨道上找到它并沿画布拖动，操作“3D Model Animation
Properties”，删除实例并撤销。`modelAnimationAssetsLiveInMedia` 把文件中的模型动画作为与同一
文件的模型分开的 Media 资源导入，拒绝没有动画的文件，在 Media 面板及其 Import 菜单中找到它，
并从项目的 `Model3DAnimationAsset` 中读回。
`modelNodesAreSelectedMovedAndSaved` 导入一个包含两个对象的 OBJ，检查其节点和轴心、
`Mesh::posed`、查看器选中单击处的节点、为其描边并拖动到指针所到之处（渲染器在那里绘制它）、
每个节点带 Transform 的 Models 行（编辑、关键帧、撤销），以及节点在保存后保留
（`model-node-outline.png`、`model-node-moved.png`）。

## 灯光、材质、阴影与反射

`lightsShadeTheSceneAsFluxDoes` 把 `model3d::illuminate` 与 Flux 的 `Illuminant` 对照：没有灯光
时不受光、环境光项、点光源在 2.5 x Intensity / 100 时的漫反射和镜面项、Linear 和 Curve 衰减以及
Reach 之外没有光照、聚光灯带羽化的光锥、自发光颜色、不受光照的图层，以及阴影混合；
`render::layerRotationAt` 按负角度旋转；`render::sceneLightsOf` 把方向为 X 90 的聚光灯放在
(0, 400, 0) 并朝下，且排除隐藏的灯光；还有在 800 外、硬边 20 度聚光灯下的红色 3D 平面——中心
141 以内被照亮，之外为黑色，30 % 时向边缘逐渐变暗，超出 Linear 范围为黑色，不受光照时保持原样
（`light-spot-plane.png`）。
`shadowsFallFromLights` 在点光源和红色地面之间放一张投射阴影的小卡片：阴影在预期位置为黑色，
其他地方不变，卡片不会给自己投影，80 % 的不透明度会留下五分之一的光，Shadow Color 为阴影着色，
窄聚光灯（一张贴图）投出相同的阴影，Shadow Diffusion 使边缘变软，不接收阴影的地面或不投射阴影
的卡片则没有阴影（`shadow-point.png`、`shadow-spot.png`）。
`billboardedLightsAndHiddenCasters` 用掠射光分别在有无 Billboarded Lights 的情况下照亮一张片
（先是昏暗，然后是完整的漫反射和镜面反射），并检查隐藏的卡片只有在 Cast Shadows If Visibility
Is Off 时才投射阴影，并且仍然不会被绘制（`shadow-hidden-caster.png`）。
`modelMapsShadeTheSurface` 用其他每种贴图渲染一个四边形：在不提供任何光照的灯光下发光的自发光
贴图、去掉高光的黑色镜面贴图、作用于环境光的遮蔽贴图、不改变任何东西的平坦法线贴图和倾斜 45 度
得到 cos 45 的法线贴图、-bm 越大越暗的凹凸斜坡，以及通过 Override Materials 从隐藏图层获取的
自发光贴图。
`modelMaterialsFollowTheirIlluminationModel` 按照明模型把模型材质的光照与 Flux 的着色器对照
（使用材质 Shininess 和完整镜面颜色的 Phong、正面观察的 Metallic-Roughness 和
Specular-Glossiness、Cook-Torrance 的 Beckmann 峰值、作用于拆分颜色的环境光），像参考程序的
导入器那样读取带 alpha 蒙版、遮蔽强度和 Specular-Glossiness 材质的 glTF（sRGB 颜色），并检查
蒙版会裁掉透明的一半。
`modelMaterialsAreEditedAndSaved` 在模型属性的 Materials 标签页中把材质切换为 Cook-Torrance
（其行随之变化），编辑其 Specular Reflectance，经过 Update Preview 后保留修改
（`model-materials-tab.png`），在 Media 中覆盖文件自带的材质（再次导入后也是如此），按参考程序
的 `<Material Version="7">` 保存并读回。`modelMaterialsTakeLayers` 通过时间轴行把模型材质的
Diffuse Map Source 设为使用隐藏洋红色平面的 Use Layer，检查渲染器在模型上绘制该平面的画面
（`model-material-layer.png`）、None 时保留材质颜色、保存与读回以及撤销。
`lightAndMaterialPropertiesAreEditedAndSaved` 在时间轴中编辑灯光的 Type、Reach 和 Cast
Shadows，以及 3D 平面的 Illuminated 和 Diffuse（各为一次撤销步骤；2D 图层没有 Material 分组），
并以参考程序的属性名保存和加载。
`lightAndMaterialPropertiesAnimate` 检查灯光的颜色和数值行、摄像机的 Lens 以及平面的 Material
上的关键帧按钮（Cast Shadows 上没有，模型的 Ambient..Emissive 上也没有，模型改为有 Self Shadow
Bias；平面上有 Billboarded Lights and Shadows），在播放头处切换 Color 和 Reach 的关键帧（一次
撤销步骤，在关键帧上编辑会设置该关键帧），场景灯光中插值后的颜色和 Reach，带关键帧的 Diffuse、
Emissive 和 Blur，模型完整的 ADSS 及其偏移，随灯光颜色从红色变为紫色的渲染
（`timeline-light-keys.png`），以及关键帧的保存——颜色以完整的 `<cl>` 保存在 `Type="0"` 之下——
和读回。
`ambientOcclusionDarkensWhereSurfacesMeet` 检查 Flux 在 G 缓冲上的 SSAO（平坦表面是开放的，凸起
方块旁的地面沿方块变暗、离远后开放，方块顶面开放，Intensity 为 0 或权重过小时没有效果）、
G 缓冲 `renderMesh` 的记录（两个网格中较近的那个，其法线转向视点）、一个镜头中接收遮蔽的地面
只有在卡片投射遮蔽且地面接收遮蔽时才会在其前方的卡片周围变暗（`ambient-occlusion-card.png`）、
Material 行（Receives/Casts Ambient Occlusion，以及只有模型才有的带 Include Semi Transparent
Triangles 的 Ambient Occlusion 分组，一次编辑及其撤销），以及保存与读回（静态属性、`<i>` 计数、
`<fl>` 值）。
`reflectionsMirrorTheSceneInSheets` 检查经纬度查找（`ConvertVec2LatLong`，以及它对 x 或 z 为 0
的方向的特殊处理）、两极处的环境颜色以及白色和半红色环境的辐照度，一张黑色镜面片在绿色卡片
镜像落下的位置显示它——完全强度、Specular Reflectivity 50 时为四分之一、没有 Receives 或 Casts
Reflections 时不显示、隐藏的卡片只有在 Cast Reflections If Visibility Is Off 时才显示、镜子后面
的卡片永远不显示（`planar-reflection.png`）——3D 文本采用其 Environment Map 图层的颜色
（`environment-map-layer.png`）或在 Use Composite Shot 时采用下方镜头的颜色，按类型的 Material 行
（选择 Use Layer 后出现 Environment Map 的 Layer 行），以及保存与读回。
`modelsReflectTheirSurroundings` 检查反射贴图的 mip 级别，一个带白色镜面颜色的灰色立方体在使用
红色图层的 Environment Map 时变红，并在接收反射且卡片投射反射时被摄像机后方的隐藏卡片映成绿色
（`model-reflection-cube.png`），材质为 Planar 的平坦模型在其平面中显示镜像的卡片
（`model-reflection-planar.png`）而 Spherical 时不显示，Reflection Mode 保存为
`<ReflectionType>` 并读回，以及模型上的资源环境。
`clearCoatBrightensGrazingReflections` 在白色环境中从 80 度和正面渲染一个粗糙的
Metallic-Roughness 四边形：光滑的清漆层会使掠射视角变亮，几乎不改变正面视角。
`textGeometryReflectsItsEnvironment` 给带（仅注册的）Geometry 特效的 3D 文本一个红色资源作为
Environment Map（类型 1003）：字母变红。

## 摄像机与查看器

`cameraFovProjectRoundTrip` 按参考程序 CameraLayer 的方式保存摄像机带动画的缩放、对焦距离、
光圈、模糊、景深和裁剪，并检查由缩放得出的垂直视角；`cameraLensAndClippingAreEdited` 在时间轴的
Lens 分组中编辑缩放，在 Layer 面板中编辑 Near/Far Clip Distance。
`cameraDepthOfFieldBlursAwayFromFocus` 检查薄透镜的模糊项（焦点处为零，远处平面的弥散圆约
16 像素，Blur 50 % 时减半），以及在 HasDoF 的镜头中远处平面边缘变软而焦点处的平面保持清晰，
没有它时则没有变化（`camera-depth-of-field.png`）。
`viewerViewsSeeTheScene` 通过查看器的视图摄像机渲染位于绿色 2D 平面之上的红色 3D 平面：活动
摄像机看到两者，Front 只以每单位一像素看到 3D 场景（缩放 2 时为两倍），Top 和 Left 侧对平面
（Top 带地面网格），Perspective 从 (0, 100, 1000) 观察；它还检查每个轴向视图把点放在哪里，缩放、
平移和环绕如何移动视图的摄像机，以及参考程序工具模式下的视图导航：绕原点和绕另一个轴心环绕
（保持距离和视图、保持竖直、永不越过顶部），Dolly、Rotate 和 Zoom Camera 的拖动（透视视角随其
缩放变窄），哪个按钮和按键选择哪种操作（`ui::viewDragFor`），以及在查看器中用中键拖动旋转透视
视图的摄像机、用右键拖动平移它，而在镜头自身的视图上不会移动任何东西。
`viewerCameraMenusChooseTheView` 检查各视图的摄像机菜单，以及拥有自己摄像机的视图绘制自己的
画面；`viewerStateSurvivesSave` 保存并读回镜头的 `ViewerState`。
`orbitTurnsAboutTheClickedPoint` 检查射线下的场景点（有内容处最近的可见 3D 片、模型的三角形、
被穿过的隐藏图层、它们旁边则没有，透视视图的中心在视图高出 100 时与镜头平面的交点），以及
Orbit 按钮的列表——选择 Orbit Around Clicked Point 会选中 Orbit 工具，在透视视图上按下会报告该
模式和按下的位置，Alt 加左键则针对所选图层。
`sceneObjectsShowInTheViewer` 检查 `SceneObjectOverlay` 绘制的内容：透过镜头的摄像机看时没有其
自身的视锥，灯光的控柄位于其投影处，Reach 显示为 60 像素的圆环，还有聚光灯和点光源；从 Top 视图
还能看到摄像机的视锥和控柄；控柄可选中其图层，拖动会平行于视图移动它（透过摄像机为向右／向下，
从顶部为向前），作为一次修改；查看器以参考程序的蓝色绘制它（`viewer-scene-objects.png`）。
`viewerFramesFootage` 在绕锚点旋转的静止图像上拖动查看器边框的控柄：角点在固定对角的情况下等比
缩放，边缩放一个轴，旋钮绕锚点旋转，渲染器在边框所在处绘制；在 Text 工具下不显示边框，选中多个
图层时在边框内拖动属于查看器（`viewer-footage-frame.png`）。
`previewSizesScaleTheWholeFrame` 以镜头尺寸的 1/4、1/2、1 和 2 倍渲染一个平面，检查它落在同一
位置；`pixelAspectSqueezesTheFrameAndTheViewerUnsqueezes` 检查 PAR 为 2:1 的镜头（400x100 的方形
空间被压缩到 200x100 像素，查看器以 4:1 显示，以及 `setsar` 分数）。
`reducedPreviewMatchesTheFullFrame` 以方形和 2:1 像素渲染绕锚点旋转、带内置模糊和羽化蒙版的
素材、平面、文本和嵌套镜头的 Half 预览，并与缩小后的完整帧比较（`half-preview-*.png`、
`half-expected-*.png`）。`reducedPreviewDrawsTheSceneReduced` 对 3D 场景做同样的比较——在雾中
沿深度旋转的平面和素材、一个 OBJ 立方体、其上的 2D 文本（`half-scene-*.png`、
`half-scene-expected-*.png`）。`reducedPreviewBlursMotion` 渲染带运动模糊的移动并旋转的平面的
Half 预览，并与缩小后的完整帧比较（`half-motion-blur*.png`）。
`viewer360DrawsThroughEnvironmentMapViewer`（OpenGL）让 360 查看器通过参考程序的
EnvironmentMapViewer 模块绘制，并就两种镜头、组合旋转、Scale 和 Scale Ratio、镜头的双向映射，
把自己的 CPU 投影与之对照，还检查 Top 和 Left 看向与参考程序相同的方向。
`layerPanelShowsTheSourceAndItsMasks` 检查 Layer 面板绘制的内容与参考程序的 RenderLayerViewJob
相同：在与其自身尺寸相同的画布上显示资源图层的源及其蒙版，不含其特效、变换和不透明度，文本
图层则不显示任何内容。
`projectFilesGetTheirIcon` 把 .vegfx 的 ProgID 注册到代替 Software\Classes 的 ini 中，检查其
DefaultIcon（可执行文件的图标 1）、打开命令、OpenWithProgids 和 Applications 条目，扩展名自身
的默认值保持不变，以及没有变化的注册不会写入任何内容。

## Layout 面板

Layout 回归测试覆盖恢复出的方向／锚点／对齐表单、资源图标的各状态、空选择、字段获得焦点时参考
点的变化、联动尺寸、分布、动画关键帧 ID 和控制柄的保留，以及图层重新排序后多图层的撤销／重做。
请运行 `timeline_regression` 中的三个 `layout*` 测试；快照为临时文件夹中的
`openvegas-layout-panel*.png`。`layoutBoxesFollowTheRenderedLayer` 把图层的 Layout 框
（`render::layerGeometry`）与渲染器实际绘制的像素对照——缩放后的静止图像、绕锚点旋转的同一图像、
2:1 像素宽高比、作为 3D 片——并检查在面板中输入的框（`render::fitLayerToBox`）正是静止图像、
旋转后的文本和 3D 片最终所在的位置；摄像机没有框。`layoutFieldsScrubLikeTheReference` 检查参考
程序的 `DoubleScrubberSpinBox`：拖动每移动一个像素增加一个步长，按住 Shift 时每五个像素吸附到
10 的倍数，在范围两端停住，松开按钮算一次编辑，单击可编辑文本，Escape 撤回；Layout 面板的字段
就是这种拖动框。

## 原生 `.hfpl` 插件

`everyShippedModuleRuns`（OpenGL）按照每个模块的类型渲染的方式（2D 特效、视频和音频转场、音频
特效、图层和逐字 Behavior、Geometry），以默认值让 `SAMPLES/VEGAS_Effects/Plugins` 中的每个模块
通过宿主运行，并期望没有一个失败。
`nativeControlsFollowTheModule`（OpenGL；模块无法响应时跳过）检查以选项 0x2 注册的控件初始为
隐藏，并且 Animated Lasers 在被告知某个属性改变时，会按每个特效显示所要求数量的光束控件，而 Glow
旧的整数半径保持隐藏。
`nativeEffectsMoveWithTime`（SAMPLES 中的 LinearWipe，OpenGL）检查 2D 模块能获得帧的时间：第 0
帧和第 30 帧不同，同一帧可重复，超过图层末尾的帧保持为最后一帧。`nativeEffectsSeeYUp`（OpenGL）
检查原生模块看到的 Y 轴朝上，这与 Flux 先存底行的纹理一致：4PointRamp 在 +54 处的点为画面顶部
着色。
`effectsGrowTheirTarget`（OpenGL）以渲染上下文询问 Drop Shadow 需要扩展的范围，在扩展后的目标中
绘制再裁回：图层保持原位、未被缩放，阴影在其旁边。`outerGlowStaysAroundItsLayer`（OpenGL）在扩展
后的目标上——目标尺寸在 RenderContext +0x60/+0x64，边距通过 +0x78..+0x90——围绕中间的方块绘制
Outer Glow：方块保留，光晕在每一侧同样强。
`nativeEffectsDrawReducedPreviews`（OpenGL）通过移动了中心的 RadialBlur（它在上下文中读取缩放，
宿主缩小该点）、半径 12 px 的 BoxBlur（宿主缩小其 px 值）以及以完整尺寸绘制后再缩小的 Magnify，
渲染一个平面的 Half 预览，并把每个结果与缩小后的完整帧比较（`half-native-*.png`）；它还检查
BoxBlur 的半径以“px”注册，并且缩小它会改变画面。
`nativeGlyphBehaviorsDrawReducedPreviews`（OpenGL，参考程序的 DropInByChar）以 Half 和完整尺寸
渲染下落的字母，并把字母的位置（亮框和中心）与缩小后的完整帧比较；它还检查 DropInByChar 文本的
Layout 框：在字母落下的过程中，它就是没有逐字 Behavior 的同一文本的框，因为参考程序只按世界变换
为图层取框。
`nativeBehaviorsKnowTimeAndLayer`（SAMPLES 中的 FadeBehavior 和 DownInsert）检查 Behavior 能获得
毫秒数并返回其不透明度（在图层的前 25 % 内淡入），以及被告知图层边界和镜头尺寸的 DownInsert 让
图层从上边缘开始并停在原位；有 OpenGL 时，它还把 DownInsert 的 Half 预览与缩小后的完整帧比较
（`half-behavior-*.png`）。
`textGeometryFillsGlyphsAndRunsGeometryModules`（OpenGL）在文本上运行 Geometry 模块，把 Extrude
的 Half 预览与缩小后的完整帧比较（`half-glyphs-*.png`、`half-geometry.png`），并把挤出的“O”在
红色地面前侧对灯光，检查其侧壁在那里投下阴影（`geometry-shadow.png`），不投射阴影时则没有。
`nativeMotionBlurReadsKeyframesBetweenFrames`（OpenGL）让开启 Motion Blur 的 EnvironmentMapViewer
在其子采样处读取带关键帧的旋转：条纹被拖尾，而静止的旋转绘制得与无模糊时相同。
`viewerCustomUiReachesNativeModule`（OpenGL，SAMPLES 中的 MotionTrack）检查通往模块自身查看器
界面的桥接：它传递的按键和光标表，MotionTrack 报告有自定义界面、对其进行设置、接收 Ctrl/Alt 和
鼠标按下并请求重绘。
`nativePluginControlsAreTranslated` 在安装俄语插件翻译目录的情况下扫描参考程序 Glow.hfpl 的副本，
检查界面显示的规格：翻译后的名称、标签和单位，以及以俄语显示的下拉项，而保存的值仍是模块自己的值。
`effectPresetsAreWrittenReadAndApplied` 以参考程序的格式写入和读取 `.hfpr` 特效预设，以参考程序的
措辞拒绝更新的版本和 3D 预设，在 Effects 面板中列出 2D/3D Presets 并支持导入（空闲的“名称 (N)”）、
导出、重命名和删除，并通过时间轴把预设应用到剪辑上，把其关键帧移到剪辑开头，作为一次可撤销的编辑。
`nativeEffectsGoInTheReferencesLists` 把原生特效写入参考程序自己的列表（带
`<Instance Version="8">` 和类型化 PropertyManager 的 `<Effects>` / `<BehaviorEffects>`），从不带
移植版扩展的文件中读回，原样保留未知插件的实例，并合并到 project_1 中而不重复其 `<Effects>`。
`effectsFavoritesAndRecentsAreKept` 按参考程序的设置格式（Plugins 分组、int 0 的 QDataStream、数量
和 ID）检查 Effects 面板的收藏和最近使用，五个最近使用项中最新的在最后，以及面板中位于各分类之上
的 Favorites/Recents 行。

## Behavior、跟踪与稳定

`simulatedBehaviorsMoveInTheWorld`（OpenGL，参考程序的 Gravity.hfpl）把一个下落的平面设为一个
旋转了四分之一的点的子级：它沿画面向下落而不是横向，框和像素都如此；下落平面的子级随之下落，
带 Drag 且关键帧横穿画面的平面会落后于其关键帧；从较晚的帧读回的运行结果与重新运行的一致，两帧
之间的物体位于两者之间，带 Drag 的子物体下落的距离与父级（其受力）大致相同，没有关键帧的下落
平面会产生运动模糊。
`behaviorsBlurAtTheirSubFrames` 用 MotionTrack 的矩阵让一个没有关键帧的平面每帧移动 8 像素：
运动模糊沿其路径围绕其框产生拖尾，模块的矩阵在帧之间读取。
`behaviorsMultiplyInTheReferencesOrder` 给平面一个四分之一旋转再加一个向右的位移：平面向上移动
（最后一个 Behavior 最先作用），父级的位移把子级带动同样的距离；框和像素一致。
`layerBoxFollowsItsBehaviors` 给平面 MotionTrack 矩阵（第 1 帧向右 50、向上 20）：其布局框随之
移动，关闭 Behavior 后则不再移动。`sheetBoxFollowsItsBehaviors` 给一个旋转过、尺寸减半的 3D 平面
MotionTrack 矩阵：其片矩阵变为位移乘以静止矩阵（父级 x Behavior x 自身），其框与渲染器绘制的红色
像素一致。`behaviorsActInsideTheParent` 把平面的父级顺时针旋转四分之一：MotionTrack 向右的位移
使平面向下移动，框和像素都如此。
`motionTrackInstanceMovesItsLayer`（需要 SAMPLES 中的 `MotionTrack.hfpl`，无需 OpenGL）检查
`NativeInstanceHost` 提供给模块的视图（素材像素的放置方式与素材图层相同）、模块运动到画布的转换、
渲染器把烘焙的四分之一旋转按顺时针方向旋转，以及 `<InstanceBytes>` 经过保存和加载。
`motionTrackReadsNestedShotsAndWorlds`（offscreen）检查作为类型 3 素材的嵌套镜头、其渲染出的帧、
图层的世界空间，以及从模块的应答中去掉该世界空间。
`motionTrackHostTracksFootage`（OpenGL）在 30 帧的图像序列上运行 MotionTrack 的完整流程——分析、
套索、定时器上的后台调用、矩阵，以及仅凭保存的数据得到的相同矩阵；然后在素材图层被移动和旋转的
情况下得到相同的运动（模块在 `GetLayerInfoV2` 给出的世界空间中应答，宿主再把它去掉），以及从一个
打开的镜头只嵌套了被跟踪镜头的宿主运行。
`stabilizersSteadyTheirOwnLayer`（OpenGL）在抖动最多 6 px 的 30 帧图像序列上运行
AutomaticStabilizer 和 MotionLock：宿主告诉每个特效它被放在了自己的图层上（`Effect_Parent_Layer`），
素材在后台分析，渲染器的帧——由特效自己的渲染实例根据其保存的数据绘制——必须保持在素材逐帧抖动的
三分之一以下；然后是 AutomaticStabilizer 的“Create Point Layer”（在顶部创建带关键帧的点图层，
“Apply To”指向它）和“Apply Transform”，以及 MotionLock 的“Apply Transform”（为其自身图层打上其
运动的关键帧），各为一次 `layersEdited` 步骤。

## 媒体、缓存、代理与预渲染

Photoshop 文档：`photoshopDocumentReadsLayers` 按文件格式规范写出文档
（`tests/PsdTestWriter.h`）并读回其图层：未压缩、RLE、ZIP 及带预测的 ZIP 通道，混合模式键、
不透明度、组、剪贴、栅格与矢量蒙版、Unicode 名称、曝光度调整图层、嵌入 PNG 的智能对象以及
合并图像。`photoshopDocumentImportsAsCompositeShot` 将其导入为合成镜头（图层自上而下、组的
不透明度、Grade 图层、图层样式中的外发光与投影、按四角放置的源图作为智能对象，以及转为
Exposure Pro 的 Camera Raw）并进行绘制。`photoshopSamplesImportByLayers` 导入
`SAMPLES/vegfx_projects/project_1` 中的 PSD（由 Photoshop 制作，不在 git 中），并与
Photoshop 自身的合并图像比较（`psd-<名称>.png`、`psd-<名称>-photoshop.png`）。
`photoshopLayersSurviveSave` 以绝对路径和相对路径保存并重新打开。具备 OpenGL 和参考插件时，
`psdDropShadowFallsAwayFromTheLight` 检查光源角度到 Drop Shadow 角度的换算，
`psdCameraRawRendersWithExposurePro` 检查 +0.5 EV 会提亮智能对象的源图。

`mediaOverridesReinterpretFrames` 检查 Media Properties 的其余部分：把 30 fps 文件覆盖为 15 fps
（源时间和长度）、NTSC 帧率、把白色压缩到 235 的 Computer（Full）、对 HD 强制使用 Rec. 601 矩阵、
从静止图像中除去预乘 alpha、其对话框，以及通过项目保存静止图像的 alpha 覆盖。
`mediaPixelAspectFromFileAndOverride` 在把一张 200x100 的静止图像覆盖为 Anamorphic 2:1 后以 400 宽
绘制，通过项目保存并读回该覆盖，操作 Media Properties（`media-settings.png`），并在有 ffmpeg 和
libVLC 时把 SAR 为 10/11 的 720x480 剪辑读作 25 fps 的 DV NTSC，保存并读回其帧率、电平、色彩空间和
硬件解码覆盖，并在对话框中显示（`media-settings-video.png`）。
`multipleMediaAssetsSurviveImportAndProjectLoad` 导入八张不同的图像，在追加／分离过程中保留共享的
资源快照，重新导入时不产生重复，并把生成的项目重新加载三次，检查路径、尺寸和剪辑 ID。
`mediaAudioStreamsMetadataSelectionAndRoundTrip` 构建一个带两条 32/44.1 kHz PCM 音轨的视频，检查
libVLC 的编解码器／采样率／声道元数据、Media Properties、撤销／重做、原生文档的反复合并／保存／
加载以及离线时选择的保留。它检查两条音频导出路径，包括从完成的 MOV 解码出的 PCM，以及两条音轨
在内存和磁盘上各自独立的波形条目。`selectedAudioStreamReachesMasterPcm` 通过比较两个不同信号的
实际主 PCM，确认 VLC 播放选择了所请求的音轨。两者都需要 FFmpeg 和 libVLC 运行时。
`mediaCacheKeepsWaveformPeaks`（需要 FFmpeg）在存储位置设为媒体缓存文件夹和数据库的情况下生成
波形，在没有解码器的情况下读回，并检查记录的使用时间、DaysToKeep 清理和彻底清除（会同时删除文件）。
`mediaCacheKeepsSlowDecodedFrames` 保留解码较慢的帧而不保留较快的帧，在另一个缓存中不经解码读回，
检查键（位置、尺寸、文件标识）、损坏文件时的未命中、数据库中记录的大小、`pruneToBytes`（最久未
使用的优先，连同其文件）、同时打开的两个数据库，以及为旧数据库添加的 `bytes` 列。
`mediaAudioIsConformedOnce` 在媒体缓存中把 44.1 kHz 单声道 WAV 转换为 48 kHz 立体声 PCM：在后台
生成一次并记录，再次从缓存中读取，换一个采样率或替换文件后会重新生成。
`timelineDiskCacheSurvivesManagersAndExpires` 检查跨管理器的帧磁盘缓存及其过期；它还检查查找某帧
不会创建其文件（以前一旦文件夹存在就会留下一个空文件，此后该帧永远不会被保存），以及损坏的条目会
被删除并重新保存。缓存测试渲染的是 Plane；它们不涉及通过 VLC 的实际视频解码。
代理回归测试覆盖旧的质量／预览值、FFmpeg 生成、已有代理时的转码失败／启动，以及通过 QSaveFile
发布。大于 2 MiB 的替换文件检查分块复制；缺失／空的输入、被占用的目标，以及（在 Windows 上）拒绝
重命名／删除的句柄，用于检查旧字节得以保留。请运行 `legacyProxyChoicesReachEncoderAndPreview`、
`proxyPublicationPreservesPreviousFile` 和 `proxyMediaGenerationAndRoundTrip`。
`preRendersWaitTheirTurn` 在 PreRenderManager 中排入两个镜头，检查第二个会等待第一个完成，两者最终
都为每一帧生成一个帧文件，被编辑的镜头会失去其帧并在编辑停止后重新生成，Remove 会删除文件夹，
取消会清空队列。`preRenderCarriesTheShotsAudio` 预渲染一个带音调的镜头：其预渲染包含镜头混音的
WAV，导出时把它用于嵌套镜头，而被编辑过的镜头则不再与之匹配。

## 导出

`exportJobFollowsPresets`（需要 FFmpeg 和 ffprobe）使用预设导出并探测文件：640x360、20 fps、
Main 配置的 MP4；带 alpha 的 ProRes 4444；带前缀的 120x120 JPEG 序列；96 kHz 的 WAV 和不含视频流
的 AAC；以及对没有声音的镜头拒绝仅音频预设。`exportRendersFramesAtOnce` 分别用一个和三个渲染器
导出一个移动的平面，检查帧相同、进度仍逐一递增、存在由播放缓存（Turbo）生成的时间轴缓存时每一帧
都取自该缓存，以及被编辑过的镜头会重新渲染。
`exportedMoviesKeepTheShotsColours`（FFmpeg）把红色平面导出为 MP4，检查视频流被标记为 BT.709／
有限范围并能解码回镜头的红色——此前 FFmpeg 未标记的 BT.601 默认值会让把 HD 视频视为 BT.709 的
播放器显示得比查看器更亮。`exportQueueRunsTasksFromSnapshots` 覆盖 Time Format 字符串、排队的
快照、任务文件、复制／删除，并通过 `render::ExportJob` 把两个任务运行为 PNG 序列，检查之后的编辑
不会影响已排队的任务。
在 `ui_stubs_regression` 中，`exportPresetsBuiltInAndFiles`、`exportPresetArguments` 和
`exportPresetManagerAndPage` 检查内置预设、`.hfxp` 文件（包括一个参考程序格式的文件）、FFmpeg
参数和滤镜、管理器、Presets 标签页、Edit Preset 以及 Export 面板的列表（`export-presets.png`）。
同一目标还验证画外音的 RIFF/PCM 输出、不完整的采样帧、被拒绝的格式、保留已有文件的取消、下拉框
滚轮偏好、导出名称的处理，以及使用影片预设导出图像；它不会从真实麦克风录音。

## 音频

合成音频采样率的回归测试覆盖 32/44.1/48/96 kHz。音频测试检查重采样后主混音的块大小、偏移／速度／
增益的时序、定位／停止、带动画的原生 Balance，以及原生 AudioReverse 较短的前瞻。设置
`OPENVEGAS_HFPL_BALANCE` 指向参考程序的 Balance.hfpl 即可启用原生测试；单独的 setSource 仍使用
48 kHz。超出 8..192 kHz 范围的无效项目采样率会回退为 48 kHz。
`compositionSampleRateReachesMovieAndProcessedPcm` 检查项目的保存与读回、直接 PCM 分支和带动画
Level 的 PCM 分支、MOV 和 MP4 中输出的采样率／声道／时长，以及 MOV 中实际的 PCM 数量和增益。它能
发现把普通速度经过 atempo=1 处理所导致的尾部截断。音频转场导出也在同样的四种采样率下运行。这些
检查使用实际的 FFmpeg/VLC；它们不测量物理设备的格式，也不保证每种 AAC 编码器采样率。
嵌套音频导出：`nestedAudioOffsetsSpeedGainAndReuse` 检查两层嵌套、0.25x/2x 速度、修剪／偏移、
继承／带动画的电平、隐藏／静音图层、共享的子级、项目的保存与读回、根／子级关键帧的 fps、实例隔离，
以及在 32/44.1/96 kHz 下从部分 MOV 导出得到的实际 PCM。
`nestedAudioCycleDoesNotPublishPartialInputs` 检查在不修改现有输入的情况下失败、忽略静音分支，以及
打破循环后的恢复。`nestedAudioNativeDspAndTransitions` 需要 `OPENVEGAS_HFPL_BALANCE`，检查子级
AudioReverse 先于父级速度、带动画的父级 Balance、合成剪辑之间的原生 Fade，以及最终 MOV 的 PCM 与
预先准备的输入样本的对比。`audioMixUsesSampleDelaysAndPadding` 检查 0.5 毫秒的延迟在 44.1 kHz 下
恰好是 22 个样本，以及补齐到所请求的输出时长。嵌套的离线分组使用 PCM16。
`nestedShotsAreMixedBeforeTheirClipsProcessThem` 找出哪些合成剪辑要处理其混音（其上有启用的原生
音频特效），在后台以音调的电平把其镜头混音一次，读回，并重新混音被编辑过的镜头；实时播放随后会让
该混音经过剪辑的原生特效。
`clipsAreProcessedWholeForPlayback`（FFmpeg 和 `OPENVEGAS_HFPL_BALANCE`）像导出一样让一个 2 秒
剪辑经过 AudioReverse——剪辑的结尾先出现，增益留给播放处理——并检查 `ProcessedClipAudioCache`
对剪辑的每种状态只生成一次；audio_regression 中的 `nativeEffectsNeedingTheWholeClipAreReported`
检查播放会为此报告哪些剪辑（超出前瞻范围的 Reverse、位于另一个特效之后的特效），以及前瞻能覆盖的
剪辑不会被报告。

## 选项（`options_regression`）

该目标覆盖选项的全部 14 个分类、参考对象名、相互依赖的控件、INI 保存、Cancel/Restore 行为以及
可编辑的快捷键。它会写出 `options-general.png`、`options-proxies.png` 和 `options-export.png`。
选项迁移的回归测试使用真实的 ru/ja/zh_CN QM 翻译目录和英语。`localizedOptionValues` 检查切换语言
前后旧的已翻译代理选项、即使显示标签改变 INI 值也保持稳定，以及新项目的默认值。
`nativeAntialiasingModesPersist` 覆盖全部九种模式、Restore Defaults 以及旧的 Off/2x MSAA 回退；它
检查的是设置的传递，而不是渲染器对 MSAA 的支持。`gpuDriverWarningFollowsTheReference` 检查参考
程序的 Intel GPU 分类、每类所需的驱动版本、警告文本，以及在那里选择 Exit 会退出。
`pluginsPageChoosesFolders` 布置一个带 VEGAS Effects 和 HitFilm Pro 插件目录树的伪 Program Files：
找到并扫描安装版的插件，提供其他 FXhome 程序的插件，在选项 > 插件中选择的文件夹按 OK 后保存（只有
文件夹改变时才请求重新扫描），页面的默认值会清除它，并且在没有任何文件夹包含插件时，选项会自动
打开到该页面一次。`renderMapSizesReachNewProjects` 在选项 > Render 中设置阴影和反射贴图的尺寸，并
在新项目的渲染设置中找到它们。

## 翻译

`translation_regression` 目标使用 Qt LinguistTools 编译俄语、日语和简体中文翻译目录。它检查目录
的加载、切换回英语、取色器和启动画面的上下文、俄语的复数形式，以及插件翻译目录（`Effect_Glow` 中
Glow 的名称、一个标签和一个下拉项，以及俄语单位）。
`optionsShowNoEnglishInOtherLanguages`（options）和 `panelsShowNoEnglishInOtherLanguages`
（timeline）在加载 Qt 自身翻译目录的情况下，以俄语、日语和中文打开选项和各面板——选中各种类型图层
时的 Controls 和 Layer、带菜单的查看器工具栏——只要任何标签、按钮、列表、下拉框、标签页、表头、
占位文本、工具提示或操作仍显示英文单词就会失败（`tests/UntranslatedText.h`；格式、设备、文件的
名称以及项目自身的数据除外）。不经过 `tr()` 而由代码直接显示的文本永远不会进入翻译目录，因此会
检查所有有效 TS 消息中的空翻译、占位符、复数形式、标记和文件过滤器的
`python tools/validate_translations.py` 看不到它。
