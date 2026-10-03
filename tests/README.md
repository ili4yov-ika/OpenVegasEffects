# Text, Timeline, Controls, Layer, Viewer 360, and Options regression tests

Standalone Qt 6 test target; does not require VLC or the full editor.
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
reference MenuBar actions/object names. Projects with several composite shots
(`projectKeepsEveryCompositeShot`, `nestedShotsCannotLoop`) load every
CompositionAsset, link nested AssetLayers, keep `<IsPrimary>` and
`<OpenCompositeShots>`, refuse nesting loops and write each shot back natively;
`mediaPanelListsCompositeShots` checks the shots' rows in the Media panel.
`compositeShotFilesRoundTrip` writes and reads `.vegfxcs` files (only the media the
shot uses, nested shots refused, taken IDs replaced, nested shots imported along) and
`importCompositionDialogPicksShots` drives the shot selection dialog
(`import-composition-dialog.png`).
`mediaRowsDropOntoTimeline` checks the Media panel's drag payloads and that the
timeline takes them anywhere, reporting the layer and time of the drop.
`compositionTemplatesAndFormats` covers frame-rate fractions, the PAR table, the
Composite Shot Properties dialog (templates, Custom, Match Timeline, typed rates), user
`.hft` templates and PAR/sample-rate/NTSC persistence (`composition-settings.png`).
`threeDLayersAreSeenThroughTheCameraWithFog` checks the fog factors of Flux's
`ComputeFoggedFragment`, that a 3D layer at z = 0 lands where the 2D one does,
shrinks with distance, fades by each pixel's distance (a plane turned about Y gets
a gradient) and that 2D layers stay clear; artefacts `fog-3d-plane.png` and
`fog-3d-turned.png`. `compositionProperties` also drives the Fog group.
`referencePointTextStandsOnItsBaseline` renders project_1's two point-text lines at 15 s
(`project_1-text.png`) and checks that each stands on its baseline at the layer position and is
centred without its trailing space, whatever its vertical alignment, and that the saved
`<TextBox>` is the reference's extent; `paragraphTextBoxKeepsItsOffset` round-trips an
off-centre paragraph box. `previewSizesScaleTheWholeFrame` renders a plane at 1/4, 1/2, 1 and 2x the shot's size and
checks it lands in the same place; `pixelAspectSqueezesTheFrameAndTheViewerUnsqueezes` checks a
2:1 PAR shot (square space 400x100 squeezed into 200x100 pixels, the viewer showing it 4:1, the
`setsar` fraction). `mediaOverridesReinterpretFrames` checks the rest of Media Properties: a 15 fps override of a
30 fps file (source time and length), NTSC rates, Computer (Full) squeezing white to 235, a
forced Rec. 601 matrix on HD, premultiplied alpha divided out of a still, its dialog and the
still's alpha override through a project. `mediaPixelAspectFromFileAndOverride` draws a 200x100 still 400 wide once it is overridden to
Anamorphic 2:1, round-trips the override through a project, drives Media Properties
(`media-settings.png`) and, with ffmpeg and libVLC present, reads a 720x480 SAR 10/11 clip as DV
NTSC at 25 fps, round-trips its frame rate, levels, colour space and hardware decoding
overrides and shows them in the dialog (`media-settings-video.png`). `nativeEffectsMoveWithTime` (SAMPLES' LinearWipe, OpenGL, skipped offscreen) checks that a 2D
module gets the frame's time: frames 0 and 30 differ, a frame is repeatable and a frame past the
layer's end holds the last one. `nativeBehaviorsKnowTimeAndLayer` (SAMPLES' FadeBehavior and DownInsert, no OpenGL) checks that
Behaviors get milliseconds and give their opacity back (a fade over the first 25 % of the layer)
and that DownInsert, told the layer's bounds and the shot's size, starts the layer at the top
edge and leaves it in place. `motionTrackInstanceMovesItsLayer` (needs SAMPLES' `MotionTrack.hfpl`, no
OpenGL) checks the view `NativeInstanceHost` gives the module (footage pixels placed like the
footage layer), the conversion of the module's motion to the canvas, that the renderer turns a
baked quarter turn clockwise, and `<InstanceBytes>` through save and load.
`motionTrackHostTracksFootage` runs the whole MotionTrack cycle on a 30-frame image sequence -
analysis, the lasso, background calls on the timer, matrices, and the same matrices from the
saved data alone; it needs the OpenGL 4.1 context and skips offscreen (run it alone with
`QT_QPA_PLATFORM=windows`, it shows no window). `threeDLayersShareOneScene` checks depth order inside a 3D scene and that a 2D layer
splits it (`scene-depth.png`). `exportQueueRunsTasksFromSnapshots` covers the Time Format
strings, queued snapshots, the tasks file, duplicate/remove, and runs two tasks to PNG
sequences through `render::ExportJob`, checking that later edits do not reach a queued task.
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


Media stream selection: `mediaAudioStreamsMetadataSelectionAndRoundTrip` builds a
video with two PCM tracks at 32/44.1 kHz, checks libVLC codec/rate/channel metadata,
Media Properties, Undo/Redo, repeated native-document merge/save/load and offline
selection retention. It checks both audio export paths, including decoded PCM from
a finished MOV, and separate waveform memory/disk entries for both tracks.
`selectedAudioStreamReachesMasterPcm` confirms that VLC playback selects the requested
track by comparing the actual master PCM of two different signals. Requires FFmpeg
and a libVLC runtime; these checks run locally against VLC 3.0.24.

`multipleMediaAssetsSurviveImportAndProjectLoad` imports eight distinct images,
keeps shared asset snapshots across append/detach, reimports without duplicates,
and reloads the resulting project three times, checking paths, dimensions and clip IDs.

Layout regression checks cover the recovered orientation/anchor/alignment form,
resource icon states, empty selection, reference-point changes during field
focus, linked dimensions, distribution, preserved animation key IDs/handles,
and multi-layer Undo/Redo after layer reordering. Run the three `layout*` tests
in `timeline_regression`; snapshots are `openvegas-layout-panel*.png` in temp.
