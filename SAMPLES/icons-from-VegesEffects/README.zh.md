# VEGAS Effects 图标（VegasEffects.exe）——目录与说明

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

## 1. 来源与提取方法

- **文件：** `SAMPLES\VEGAS_Effects\VegasEffects.exe`（23,147,544 字节，x64，Qt5，MSVC）。
- **PNG：** 逐字节扫描二进制文件中的 `89 50 4E 47` 签名（IHDR/IDAT/IEND）提取，每个文件
  的边界根据 PNG 的块结构确定。按 SHA-1 去重。
- **资源名称：** 取自二进制文件数据段中的 UTF-16 名称池（即应用程序内部的图标目录：每条
  记录是一个资源文件名，按字母顺序排列）。
- 签名扫描找到了 **3379 个唯一的 PNG**。随后解析标准的 Qt RCC 树，恢复出 **3380 个具名
  PNG** 和两个 Style 文件；差异来自资源别名，即多个名称共用一个 PNG。旧的 UTF-16 正则清单
  漏掉了部分条目，已不再用于匹配。

所有提取出的文件位于 `png\`，列表在 `manifest_png.tsv`，完整的名称目录在
`icons_inventory.txt`。

精确的对应关系通过 `python _tools/map_vegas_icons.py` 从内嵌的 Qt RCC 树中恢复。具名资源
位于 `named_png/images/images/`，`named_manifest.tsv` 关联原始路径、偏移量和 SHA-1。原始的
`png/` 文件夹作为去重后的数据块的不可变集合保留：不同的原始名称有时指向同一个 PNG，简单
重命名会丢失这些别名。

## 2. 构成（图像尺寸）

| 尺寸 | 数量 | 备注 |
|-----:|-----:|------|
| 16×16  | 1409  | 1x 基本图标尺寸 |
| 32×32  | 1577  | @2x（HiDPI）版本 + 部分独立图标 |
| 64×64  | 186   | 大尺寸版本／素材 |
| 128×128| 16    | 大尺寸版本 |
| 20×20、10×10、40×15、80×30、32×64、64×32、10×26、5×13、38×16、76×32、206×32 | 79 | 辅助元素（插入符、滑块、条） |
| 106×60…1240×795 等 | 112 | 水印、启动画面、横幅、占位图 |

## 3. 命名规则（即“画的是什么”）

资源名称按以下模式描述图标：

```
[主题] <操作名> [-<状态>] [@2x] .png
```

- **主题（前缀）：**
  - `base` —— 主图标集（2549 个名称）；
  - `G...` —— 另一种样式／主题（410 个名称，例如 `Gundo`、`Gmask-square`）；
  - `'...`（撇号）—— 又一种样式（415 个名称，例如 `'file-video`、`'text-bottom-indent`）。
- **状态（后缀）：**
  - 无后缀 —— 普通状态（819）；
  - `-hover` —— 鼠标悬停（837）；
  - `-checked` —— 按下／激活（837）；
  - `-disabled` —— 不可用（836）；
  - `-menu` —— 菜单项（31）；
  - `-invalid` —— 无效（12）；
  - `-active` —— 活动（2）。
- **尺寸：** `@2x` —— HiDPI 版本（1671 个名称），没有后缀的是基本版本（1703）。
- 共有 **1288** 个唯一的“主题 + 名称”组合；主图标集中唯一的基本名称有 **727** 个。

## 4. 图标类别（按内容）

主要的功能分组；括号中为目录中的条目数（包括所有主题／状态／尺寸）。

| 类别 | 数量 | 内容（典型名称） |
|------|-----:|------------------|
| `mask*` / `gmask-` / `'mask` | 158+56+22 | 蒙版工具：矩形、椭圆、星形、多边形、添加／移动／删除点、曲线路径、对称控制柄 |
| `cursor-*` / `gcursor` | 108+52 | 编辑器光标：ripple、roll、slide、slip、slice、stretch、hand、pen、resize-left/right、track-backward/forward |
| `text*` / `gtext` / `'text` | 92+28+22 | 文本设置：对齐、缩进、大小写（uppercase/lowercase/capitalize）、下标／上标、字间距、填充／描边颜色 |
| `file-*` / `gfile` / `'file` | 85+24+17 | 文件和图层类型：video、audio、picture、3d、offline、transition、folder、new-composite |
| `layout-*` / `glayout` / `'layout` | 82+26+19 | 窗口和面板布局：canvas-left/right/top/bottom/middle、“1…n”格、分布 |
| `key-frame*` / `gkey` | 70+26 | 关键帧：prev/next、ease-in/out、easy-ease、hold、linear、on/off、frame-in/out、轨道对象 |
| `export-*` / `gexport` | 47+23 | 导出：queue、preset、content、canvas、tidy、hot-folder、layered |
| `camera-*` | 42 | 摄像机：dolly、orbit、pan、zoom、3D 视图、light |
| `checkbox-*` / `gcheckbox` / `radio-*` | 38+16+16 | 复选框和单选按钮的 normal/hover/checked/disabled、mixed |
| `effect-*` / `geffect` | 37+12 | 特效类别：2d、3d、ae-plugin、behavior、geometry、transition |
| `distribute-*` / `align-*` / `galign` | 36+30+14 | 对象对齐／分布：left/right/top/bottom/center、hor/vert |
| `frame-*` / `gframe` | 30+14 | 帧与传输控制：in/out、next/last、prev、ramp |
| `dialog-*` / `gdialog` | 23 | 对话框图标：critical、warning、question、information |
| `action-*` | 22 | 操作：copy、ignore、link、move |
| `folder-*` | 22 | 文件夹：open、locked、add-export、horizontal/vertical |
| `hud-*` | 22 | HUD 元素：move、textbox-overflow、toolbar |
| `font-*` | 21 | 字体：color、outline-color/size、size、line-height |
| `caret-*` | 20 | 插入符：up/down/left/right、black/gray、menu |
| `track-*` | 19 | 轨道：select-forward/backward、ripple、timeline |
| `graph-*` / `animation-graph` | 18 | 曲线图／动画：curve-up/down、value graph、带曲线图的勾选 |
| `margin-*` | 17 | 边距：left/right/top/bottom |
| `smooth-*` | 16 | 平滑：smooth-in/out/auto、ease |
| `clipboard-*` | 15 | 剪贴板：copy、cut、paste |
| `play*` / `pause` / `stop-ram` / `play-ram` | 14+ | 传输控制：play、play-backwards、pause、play-ram、stop-ram |
| `zoom-*` | 14 | 缩放：min、max、fit、histogram zoom |
| `browser-*` / `new-*` / `import-*` / `preset-*` | 13+ | 特效浏览器、新建、导入、预设 |
| `proxy-*` / `gproxy` | 13 | 代理文件：complete、queue、pause、generate、hot-folder |
| `sound-*` | 13 | 声音、音频图层 |
| `brush-*` / `freehand-*` | 12+12 | 画笔、自由形状（freehand-path、brush-remove） |
| `letter-*` | 12 | 字母参考标记 scaling-horizontal/vertical |
| `vegas*` | 12 | VEGAS Effects 品牌图标 |
| 其他 | | pointer、help、pipette、batch、ripple-menu、slice、straighten、stretch、auto、key-frame、blank、white-checked、global、missing 等 |

## 5. 显式的 Qt 图标路径 `:/images/images/...`

程序代码中还会通过以下 QIcon 路径直接请求图标：

```
:/images/images/clipboard-copy.png        :/images/images/clipboard-paste.png
:/images/images/clipboard-cut.png         :/images/images/cog-properties-information.png
:/images/images/exclamation.png           :/images/images/folder-open.png
:/images/images/name-creator.png          :/images/images/name-pro.png
:/images/images/name.png                  :/images/images/redo.png
:/images/images/save.png                  :/images/images/tier-lock-hover.png
:/images/images/undo.png                  :/images/images/vegas-effects-new-file.png
```

## 6. 与我们重新实现的图标的关系

我们的项目把图标保存在 `resources\icons\` 中，并打包进 `resources/icons.qrc`：

- 自有的 **134 个 SVG**：`toolbar/`（71）、`text/`（46）、`timeline/`（14），根目录中是
  应用程序、项目文件和启动画面的图像；
- 从 `named_png/images/images/` **原样复制的 220 个 PNG**：Layout 面板 26 个图标的所有状态
  和尺寸（`layout/`，208 个文件，参见
  [`resources/icons/layout/README.zh.md`](../../resources/icons/layout/README.zh.md)），以及
  查看器的图标（`viewer/`，12 个文件）。

我们的部分名称遵循自己的约定（`kf-*` —— 关键帧，`tool-*` —— 工具，`effect-*` —— 特效
类别），而参考程序中有大量我们尚未拥有的名称（`mask-*`、`cursor-*`、`gtext-*`、`font-*`、
`hud-*`、`caret-*` 等）——它们可以作为扩充图标集的基础。

## 7. 关于识别方法的说明

名称与 PNG 现在通过内嵌 Qt RCC 树的节点直接关联（`qRegisterResourceData`：tree
`0x1412792f0`、names `0x14128b5f0`、data `0x140d1d850`）。这是程序自身的权威对应关系，
包括主题、状态、HiDPI 版本和别名。为便于目视检查，还保留了联系表。

## 8. `SAMPLES\icons-from-VegesEffects\` 文件夹的内容

```
png\               3379 个提取出的 PNG（png_<offset>_<W>x<H>.png）
named_png\         按 Qt RCC 树中的确切名称存放的资源
svg\               空：二进制文件中没有 SVG 资源
manifest_png.tsv   sha1 / offset / length / width / height / file
manifest_svg.tsv   SVG 的相同列（只有表头）
named_manifest.tsv 原始名称 / 文件 / offset / 大小 / SHA-1
icons_inventory.txt 完整的按字母排序的名称目录（3374 条）
contact16\         所有 16×16 图标的 8 张联系表（放大 ×3）
contact32\         所有 32×32 图标的 8 张联系表（放大 ×2）
contact64\         所有 64×64 图标的 1 张表
contact_misc\      其他尺寸的 4 张表（水印、横幅、滑块）
README.md          本文件的原文（及其译文 README.*.md）
```
