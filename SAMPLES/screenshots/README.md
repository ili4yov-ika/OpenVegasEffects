# VEGAS Effects reference screenshots

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

Screenshots of the VEGAS Effects 5.0.2 interface and their pixel breakdowns. Each
`N.png` has an `N.md` with the coordinate layout of the panels, their contents and the
color scheme (the breakdowns are written in Russian). They are used as the source of
truth when porting the UI — see
[`MARKDOWN/RE_VegasEffects.md`](../../MARKDOWN/RE_VegasEffects.md) §5.

## Index

| File | Resolution | What is shown | Unique to this shot |
|------|-----------|---------------|---------------------|
| [`0.md`](0.md) | 1317×816 | Effects screen, the **Cartoon** effect on a media layer | The effect's parameters in Controls: Preset, Edge, Fill |
| [`1.md`](1.md) | 1920×1080 | The **Controls** panel | — |
| [`2.md`](2.md) | 1920×1080 | The **Effects** panel + the 3D model `octop.obj` | The full tree of effect categories (424 entries), the `[Model]` layer, the XYZ 3D gizmo, the `Layout` tab |
| [`3.md`](3.md) | 1920×1080 | The modal **Options** dialog | The structure of the settings window |
| [`4.md`](4.md) | 1920×1080 | **Options**, the **Export** tab | Export parameters |
| [`5.md`](5.md) | 1920×1080 | **Controls** for `octop.obj`, World Transform | Transforming a 3D object |
| [`crop_screenshot.md`](crop_screenshot.md) | 1280×720 (reduced ×2) | Edit screen, **animating a Point layer** | Keyframes on the Position track (~60), `Value Graph`, the 2D gizmo, both groups of panel tabs |

## Other shots (no breakdown)

| File | Resolution | What is shown |
|------|-----------|---------------|
| `6.jpg` | 1920×1036 | Color Correction Wheels in Controls with keyed parameters on the timeline, and the Scopes panel: Histogram, Parade, Vectorscope, Waveform |
| `wnd_Layout.png` | 324×374 | The **Layout** panel (see `MARKDOWN/RE_wnd_Layout.md`) |
| `wnd_Text-full.png` | 898×773 | The **Text** panel, all groups |
| `wnd_Text-paragraph.png` | 327×282 | The **Text** panel, the Paragraph group |

## Notes on resolution

- `1.png`–`5.png` were taken natively at 1920×1080.
- `0.png` is 1317×816. The coordinates in the Controls/Timeline/Status Bar sections of
  `0.md` were written against a 1920×1080 grid and need converting; the correction is
  described in the box at the start of the file.
- `crop_screenshot.png` is reduced about twice: 1280×720 from about 2560×1440. All
  coordinates in `crop_screenshot.md` are in the file's pixels; multiply by 2 for the
  native ones.

## What has been ported from the shots

Everything the shots show is in the port: the transform gizmos in the viewer, layers
of the `Point` and `Model` kinds, the keyframes (diamonds on the parameter track, the
reference's six temporal types, the `Value Graph` curve on the track, dragging keys
and their handles with the mouse). Not ported yet: the reference's spatial (3D motion
path) interpolation — only the temporal curve is implemented
(`src/composition/KeyFrame.h`).
