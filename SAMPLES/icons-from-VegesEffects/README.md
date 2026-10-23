# VEGAS Effects icons (VegasEffects.exe) — catalog and description

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

## 1. Source and extraction method

- **File:** `SAMPLES\VEGAS_Effects\VegasEffects.exe` (23,147,544 bytes, x64, Qt5, MSVC).
- **PNG:** extracted by scanning the binary byte by byte for the `89 50 4E 47` signature
  (IHDR/IDAT/IEND); the bounds of each file are found from the PNG chunk structure.
  Deduplicated by SHA-1.
- **Resource names:** taken from the UTF-16 name pool in the binary's data section (the
  application's internal icon catalog: each entry is a resource file name, sorted
  alphabetically).
- The signature pass found **3379 unique PNGs**. Parsing the regular Qt RCC tree
  afterwards recovered **3380 named PNGs** and two Style files; the difference comes from
  resource aliases, where several names use one PNG. The old UTF-16 regex inventory
  missed some entries and is no longer used for matching.

All extracted files are in `png\`, their list in `manifest_png.tsv`, the whole name
catalog in `icons_inventory.txt`.

The exact mapping was recovered from the embedded Qt RCC tree with
`python _tools/map_vegas_icons.py`. The named resources are in
`named_png/images/images/`, and `named_manifest.tsv` links the original path, offset and
SHA-1. The original `png/` folder is kept as an unchanged set of deduplicated blobs:
different original names sometimes point to one PNG, so a plain rename would lose the
aliases.

## 2. Contents (image sizes)

| Size | Count | Note |
|-----:|------:|------|
| 16×16  | 1409  | the base 1x icon size |
| 32×32  | 1577  | the @2x (HiDPI) variant + some standalone ones |
| 64×64  | 186   | large versions / material |
| 128×128| 16    | large versions |
| 20×20, 10×10, 40×15, 80×30, 32×64, 64×32, 10×26, 5×13, 38×16, 76×32, 206×32 | 79 | utility (carets, sliders, bars) |
| 106×60…1240×795 etc. | 112 | watermarks, splash, banners, placeholder images |

## 3. Naming scheme (this is "what is shown")

A resource name describes the icon by the pattern:

```
[theme] <action-name> [-<state>] [@2x] .png
```

- **Theme (prefix):**
  - `base` — the main set (2549 names);
  - `G...` — an alternative style/theme (410 names, e.g. `Gundo`, `Gmask-square`);
  - `'...` (apostrophe) — another alternative style (415 names, e.g. `'file-video`,
    `'text-bottom-indent`).
- **State (suffix):**
  - no suffix — the normal state (819);
  - `-hover` — mouse over (837);
  - `-checked` — pressed/activated (837);
  - `-disabled` — unavailable (836);
  - `-menu` — a menu item (31);
  - `-invalid` — invalid (12);
  - `-active` — active (2).
- **Size:** `@2x` — the HiDPI variant (1671 names), without it — the base one (1703).
- In total there are **1288** unique "theme + name" pairs; **727** unique base names in
  the main set.

## 4. Icon categories (by content)

The main functional families; in parentheses the number of entries in the catalog
(counting all themes/states/sizes).

| Category | Count | What is shown (typical names) |
|----------|------:|-------------------------------|
| `mask*` / `gmask-` / `'mask` | 158+56+22 | Mask tools: rectangle, ellipse, star, polygon, adding/moving/deleting points, curved paths, mirrored handles |
| `cursor-*` / `gcursor` | 108+52 | Editor cursors: ripple, roll, slide, slip, slice, stretch, hand, pen, resize-left/right, track-backward/forward |
| `text*` / `gtext` / `'text` | 92+28+22 | Text settings: alignment, indents, case (uppercase/lowercase/capitalize), sub/superscript, letter spacing, fill/stroke color |
| `file-*` / `gfile` / `'file` | 85+24+17 | File and layer types: video, audio, picture, 3d, offline, transition, folder, new-composite |
| `layout-*` / `glayout` / `'layout` | 82+26+19 | Window and panel layouts: canvas-left/right/top/bottom/middle, "1…n" cells, distribution |
| `key-frame*` / `gkey` | 70+26 | Keyframes: prev/next, ease-in/out, easy-ease, hold, linear, on/off, frame-in/out, track objects |
| `export-*` / `gexport` | 47+23 | Export: queue, preset, content, canvas, tidy, hot-folder, layered |
| `camera-*` | 42 | Camera: dolly, orbit, pan, zoom, 3D view, light |
| `checkbox-*` / `gcheckbox` / `radio-*` | 38+16+16 | Check boxes and radio buttons in normal/hover/checked/disabled, mixed |
| `effect-*` / `geffect` | 37+12 | Effect categories: 2d, 3d, ae-plugin, behavior, geometry, transition |
| `distribute-*` / `align-*` / `galign` | 36+30+14 | Aligning/distributing objects: left/right/top/bottom/center, hor/vert |
| `frame-*` / `gframe` | 30+14 | Frames and transport: in/out, next/last, prev, ramp |
| `dialog-*` / `gdialog` | 23 | Dialog icons: critical, warning, question, information |
| `action-*` | 22 | Actions: copy, ignore, link, move |
| `folder-*` | 22 | Folders: open, locked, add-export, horizontal/vertical |
| `hud-*` | 22 | HUD elements: move, textbox-overflow, toolbar |
| `font-*` | 21 | Font: color, outline-color/size, size, line-height |
| `caret-*` | 20 | Carets: up/down/left/right, black/gray, menu |
| `track-*` | 19 | Tracks: select-forward/backward, ripple, timeline |
| `graph-*` / `animation-graph` | 18 | Graphs/animation: curve-up/down, value graph, check with graph |
| `margin-*` | 17 | Margins: left/right/top/bottom |
| `smooth-*` | 16 | Smoothing: smooth-in/out/auto, ease |
| `clipboard-*` | 15 | Clipboard: copy, cut, paste |
| `play*` / `pause` / `stop-ram` / `play-ram` | 14+ | Transport: play, play-backwards, pause, play-ram, stop-ram |
| `zoom-*` | 14 | Zoom: min, max, fit, histogram zoom |
| `browser-*` / `new-*` / `import-*` / `preset-*` | 13+ | Effect browser, creating, importing, presets |
| `proxy-*` / `gproxy` | 13 | Proxy files: complete, queue, pause, generate, hot-folder |
| `sound-*` | 13 | Sound, audio layers |
| `brush-*` / `freehand-*` | 12+12 | Brush, freehand shape (freehand-path, brush-remove) |
| `letter-*` | 12 | Letter guide markers scaling-horizontal/vertical |
| `vegas*` | 12 | VEGAS Effects brand icons |
| others | | pointer, help, pipette, batch, ripple-menu, slice, straighten, stretch, auto, key-frame, blank, white-checked, global, missing, etc. |

## 5. Explicit Qt icon paths `:/images/images/...`

In the program's code icons are also requested directly by these QIcon paths:

```
:/images/images/clipboard-copy.png        :/images/images/clipboard-paste.png
:/images/images/clipboard-cut.png         :/images/images/cog-properties-information.png
:/images/images/exclamation.png           :/images/images/folder-open.png
:/images/images/name-creator.png          :/images/images/name-pro.png
:/images/images/name.png                  :/images/images/redo.png
:/images/images/save.png                  :/images/images/tier-lock-hover.png
:/images/images/undo.png                  :/images/images/vegas-effects-new-file.png
```

## 6. How they relate to the icons of our reimplementation

Our project keeps its icons in `resources\icons\` and packages them in
`resources/icons.qrc`:

- **134 SVGs** of its own: `toolbar/` (71), `text/` (46), `timeline/` (14) and the
  application, project file and splash images in the root;
- **220 PNGs copied unchanged** from `named_png/images/images/`: the Layout panel's 26
  icons in all states and sizes (`layout/`, 208 files, see
  [`resources/icons/layout/README.md`](../../resources/icons/layout/README.md)) and the
  viewer's (`viewer/`, 12 files).

Some of our names follow our own conventions (`kf-*` — keyframes, `tool-*` — tools,
`effect-*` — effect categories), while the reference has a large group of names we do
not have yet (`mask-*`, `cursor-*`, `gtext-*`, `font-*`, `hud-*`, `caret-*`, etc.) —
they are a convenient base for extending the set.

## 7. A note on the identification method

Names and PNGs are now linked directly through the nodes of the embedded Qt RCC tree
(`qRegisterResourceData`: tree `0x1412792f0`, names `0x14128b5f0`, data `0x140d1d850`).
This is the program's own authoritative mapping, including themes, states, HiDPI
variants and aliases. Contact sheets are kept as well for a visual check.

## 8. Contents of the `SAMPLES\icons-from-VegesEffects\` folder

```
png\               3379 extracted PNGs (png_<offset>_<W>x<H>.png)
named_png\         the resources under their exact names from the Qt RCC tree
svg\               empty: the binary holds no SVG resources
manifest_png.tsv   sha1 / offset / length / width / height / file
manifest_svg.tsv   the same columns for SVG (header only)
named_manifest.tsv original name / file / offset / size / SHA-1
icons_inventory.txt the full alphabetical name catalog (3374 entries)
contact16\         8 contact sheets of all 16×16 icons (enlarged ×3)
contact32\         8 contact sheets of all 32×32 icons (enlarged ×2)
contact64\         1 sheet of all 64×64 icons
contact_misc\      4 sheets of other sizes (watermarks, banners, sliders)
README.md          this file (and its translations README.*.md)
```
