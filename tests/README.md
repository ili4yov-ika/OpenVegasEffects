# Text, Timeline, Controls, Layer, Viewer 360, and Options regression tests

Standalone Qt 6 test target; does not require WebEngine, VLC or the full editor.
Run from the repository root in PowerShell (adjust Qt/toolchain paths if needed):

```powershell
$env:PATH = 'C:/Qt/6.9.3/mingw_64/bin;C:/Qt/Tools/mingw1310_64/bin;' + $env:PATH
$env:QT_PLUGIN_PATH = 'C:/Qt/6.9.3/mingw_64/plugins'
cmake -S tests -B build/textpanel-tests -G Ninja -DCMAKE_PREFIX_PATH=C:/Qt/6.9.3/mingw_64
cmake --build build/textpanel-tests --parallel 4
ctest --test-dir build/textpanel-tests --output-on-failure --timeout 25
```

The test uses the offscreen platform and explicitly loads Windows Arial fonts.
It checks controls against rendered images, outlines, color dialogs, picker
cancellation, typing and dragging, undo/redo, and text style persistence through
the actual `.vegfx` serializer. Desktop pixel capture requires a real display
and is not verified by this offscreen test.

Panel snapshots are written beside the test executable as `text-panel.png`
(400 px) and `text-panel-narrow.png` (327 px).

Full application build: `./tools/msvc_build.cmd build`.

The `ui_stubs_regression` target verifies RIFF/PCM output, incomplete sample
frames, rejected formats, cancellation preserving existing files, combo wheel
preferences, export name handling, and image export with a movie preset. It
does not record from a real microphone. The Timeline regression also checks
relative-path projects after moving them, including multiple clips, nested
compositions, offline media and embedded dock layout.

The `translation_regression` target compiles the Russian, Japanese and Simplified
Chinese catalogs with Qt LinguistTools. It checks catalog loading, switching back
to English, color-picker and splash contexts, and Russian plural forms. Run
`python tools/validate_translations.py` to check all active TS messages for empty
translations, placeholders, plural forms, markup and file filters.

The `timeline_regression` target covers aligned tree/canvas rows, search, editable
parameters, colors and presets, keyframes and interpolation, layer controls,
stable layer IDs and parenting, the Layer inspector, clip move/slice with undo,
composition properties with undo/cancel, `.vegfx` persistence, all parameters of
the built-in Blur and Color Correction Wheels, playback cache invalidation, and
the Viewer 360 panel with its seven view presets and equirectangular projection,
the Trimmer's persistent In/Out range and Insert/Overlay signals, and the 29
reference MenuBar actions/object names.
The cache test renders a Plane; real video decoding through VLC is not exercised
by this standalone target.

Panel snapshots: `timeline-panel.png`, `timeline-blur.png`,
`timeline-color-wheels.png`, `controls-panel.png`, `controls-color-wheels.png`,
`layer-panel.png`, `viewer360-panel.png`, and `trimmer-panel.png` beside the executable. The tests use
fixed-size fixtures for visual review; they do not assert pixel equivalence with
VEGAS. Latest verified results: 50 Timeline/Controls/Layer/Viewer 360/Trimmer/MenuBar and 48 Text
QtTest passes (including suite setup/cleanup); both targets also pass CTest.

The `options_regression` target covers all 13 Options categories, reference
object names, dependent controls, INI persistence, Cancel/Restore behavior, and
editable shortcuts. It writes `options-general.png`, `options-proxies.png`, and
`options-export.png`. Latest verified result: 6 QtTest passes; all three targets
pass CTest.
