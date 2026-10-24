# Sources

<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.ru.md">Русский</a>
</p>

The application's C++17 / Qt 6 code, one folder per module. The module layout mirrors
the reference's high-level structure (see [`MARKDOWN/ARCHITECTURE.md`](../MARKDOWN/ARCHITECTURE.md)).

| Folder | What it holds |
|--------|---------------|
| [`main.cpp`](main.cpp) | The entry point: the command line as UTF-8, `QApplication`, translations, `app::AppMain` |
| [`core/`](core/) | Basic types: `Identifier`, `Result`, `Version`, the file log |
| [`app/`](app/) | Start-up (`AppMain`), user data folders, settings, translations, AV templates, project defaults, plugin folders, file association, GPU driver checks |
| [`license/`](license/) | The license interface and the free manager that always reports a valid license |
| [`composition/`](composition/) | The model: composite shots, layers, clips, effects, keyframes, masks, text styles, transitions, Behaviors and their simulation, motion tracking |
| [`project/`](project/) | Reading and writing `.vegfx` projects and `.vegfxcs` composite shots in the reference's format, merging projects, effect presets (`.hfpr`) |
| [`media/`](media/) | Media import and probing, libVLC (`VlcBackend`, `Vlc4Adapter`), video decoding, the realtime audio mixer (`AudioPlayer`), voiceover capture, waveforms, conformed audio, proxies, the decoded frame cache |
| [`plugin/`](plugin/) | The plugin registry and the host of the native `.hfpl` modules (`NativeEffectRender`, `NativePlugin`): 2D effects, transitions, audio, Behaviors, Geometry, custom viewer UI |
| [`model3d/`](model3d/) | 3D model readers (OBJ, FBX, glTF, 3DS, LWO, Alembic), meshes and the 3D renderer |
| [`render/`](render/) | Frame rendering (`RenderManager`), text, layer geometry, export and its presets, video and audio encoding through FFmpeg, pre-renders, the frame disk cache |
| [`cache/`](cache/) | The SQLite media cache database |
| [`ui/`](ui/) | The main window, panels (Timeline, Viewer, Controls, Layer, Layout, Media, Text, Effects, Export, ...), dialogs and viewer overlays |

The Qt Designer forms these classes load are in [`ui/`](../ui/) at the repository root.
