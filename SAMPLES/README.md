# Samples and reference material

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

Material for development; nothing here is part of the build.

| Folder | What it holds |
|--------|---------------|
| `VEGAS_Effects/` | A local copy of the reference application (VEGAS Effects 5.0.2): its binaries, the native `.hfpl` plugins (`Plugins/`), translations, environment maps and decompiled sources (`decompile-src/`). It is **not in the repository** (`SAMPLES/.gitignore`); put an installed copy here to run the reverse-engineering tools and the tests that need the reference's plugins |
| [`vegfx_projects/`](vegfx_projects/) | Sample projects: `project_1` (a 2D project made in the reference), `project_2` (3D models), `project_3` (a cube with texts on its faces, keyframed turns, effects on everything and two music files with audio effects; written by the test `buildProject3`) |
| [`screenshots/`](screenshots/) | Screenshots of the reference with pixel breakdowns, the source of truth for the interface |
| [`icons-from-VegesEffects/`](icons-from-VegesEffects/) | The reference's icons extracted from its resources, with a catalog |

Media files of the sample projects (`*.mp3`, `*.mp4`, `*.png`, `*.psd`) are ignored by
`vegfx_projects/.gitignore` unless added on purpose.
