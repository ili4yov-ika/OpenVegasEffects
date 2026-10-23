# VEGAS Effects 参考程序截图

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

VEGAS Effects 5.0.2 界面的截图及其像素级分析。每个 `N.png` 都有一个对应的 `N.md`，记录
面板的坐标布局、内容和配色方案（分析以俄语撰写）。它们是移植界面时的事实依据——参见
[`MARKDOWN/RE_VegasEffects.md`](../../MARKDOWN/RE_VegasEffects.md) §5。

## 索引

| 文件 | 分辨率 | 显示内容 | 该截图独有的内容 |
|------|--------|----------|------------------|
| [`0.md`](0.md) | 1317×816 | Effects 界面，媒体图层上的 **Cartoon** 特效 | Controls 中的特效参数：Preset、Edge、Fill |
| [`1.md`](1.md) | 1920×1080 | **Controls** 面板 | — |
| [`2.md`](2.md) | 1920×1080 | **Effects** 面板 + 3D 模型 `octop.obj` | 完整的特效类别树（424 项）、`[Model]` 图层、XYZ 3D 操纵器、`Layout` 标签页 |
| [`3.md`](3.md) | 1920×1080 | 模态 **Options** 对话框 | 设置窗口的结构 |
| [`4.md`](4.md) | 1920×1080 | **Options**，**Export** 标签页 | 导出参数 |
| [`5.md`](5.md) | 1920×1080 | `octop.obj` 的 **Controls**，World Transform | 3D 对象的变换 |
| [`crop_screenshot.md`](crop_screenshot.md) | 1280×720（缩小 ×2） | Edit 界面，**Point 图层动画** | Position 轨道上的关键帧（约 60 个）、`Value Graph`、2D 操纵器、两组面板标签页 |

## 其他截图（无分析）

| 文件 | 分辨率 | 显示内容 |
|------|--------|----------|
| `6.jpg` | 1920×1036 | Controls 中的 Color Correction Wheels 及时间轴上带关键帧的参数，以及 Scopes 面板：Histogram、Parade、Vectorscope、Waveform |
| `wnd_Layout.png` | 324×374 | **Layout** 面板（参见 `MARKDOWN/RE_wnd_Layout.md`） |
| `wnd_Text-full.png` | 898×773 | **Text** 面板，全部分组 |
| `wnd_Text-paragraph.png` | 327×282 | **Text** 面板，Paragraph 分组 |

## 关于分辨率的说明

- `1.png`–`5.png` 以原生 1920×1080 拍摄。
- `0.png` 为 1317×816。`0.md` 中 Controls/Timeline/Status Bar 各节的坐标是按 1920×1080
  网格写的，需要换算；修正方法见文件开头的说明框。
- `crop_screenshot.png` 大约缩小了一半：从约 2560×1440 缩小为 1280×720。
  `crop_screenshot.md` 中的所有坐标都以该文件的像素为单位，换算为原生坐标时请乘以 2。

## 已从截图中移植的内容

截图中显示的所有内容都已在移植版中实现：查看器中的变换操纵器、`Point` 和 `Model` 类型的
图层、关键帧（参数轨道上的菱形、参考程序的六种时间插值类型、轨道上的 `Value Graph` 曲线、
用鼠标拖动关键帧及其控制柄）。尚未移植：参考程序的空间（3D 运动路径）插值——目前只实现了
时间曲线（`src/composition/KeyFrame.h`）。
