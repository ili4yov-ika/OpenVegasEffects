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

Under CTest `timeline_regression` writes its log to `timeline_ctest.txt` beside the build as well:
its standard output does not reach CTest's log, and the file says which function failed.

`nativeGlyphBehaviorsDrawReducedPreviews` (OpenGL, reference DropInByChar) renders falling letters
at Half and full size and compares where the letters are (bright box and centre) with the full frame
reduced. `nativeBehaviorsKnowTimeAndLayer` and `textGeometryFillsGlyphsAndRunsGeometryModules` also
compare a Half preview of DownInsert and Extrude with the full frame reduced
(`half-behavior-*.png`, `half-glyphs-*.png`, `half-geometry.png`).

`reducedPreviewBlursMotion` renders a Half preview of a moving, turning plane with motion blur and
compares it with the full frame reduced (`half-motion-blur*.png`). `mediaCacheKeepsWaveformPeaks`
(needs FFmpeg) builds a waveform with the store set to a media cache folder and database, reads it
back with no decoder, and checks the record's use time, DaysToKeep pruning and purging, which
remove the files too. `mediaCacheKeepsSlowDecodedFrames` keeps a slowly decoded frame, but not a
quick one, reads it back in another cache without decoding, checks the key (position, size, file
identity), the damaged-file miss, the size recorded in the database, `pruneToBytes` (least recently
used first, with their files), two databases open at once, and the `bytes` column added to an
older database.

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
to English, color-picker and splash contexts, Russian plural forms, and the plug-in
catalogues (Glow's name, a label and a combo item in `Effect_Glow`, Russian units).
`modelTexturesAreDrawn` loads an OBJ quad with a 2x2 `map_Kd` and checks each quarter shows its
texel, V up, and that Flip UV Coordinates turns it over (`model-texture.png`).
`exportRendersFramesAtOnce` exports a moving plane with one and with three renderers and checks
the frames are the same, that progress still counts up one by one, that with a timeline cache made
by the playback cache (Turbo) every frame comes from it, and that an edited shot is rendered afresh.
`timelineDiskCacheSurvivesManagersAndExpires` also checks that looking a frame up does not create
its file (an empty one used to be left behind once its folder existed, and the frame was then never
stored) and that a damaged entry is removed and stored again.
`gpuDriverWarningFollowsTheReference` (options_regression) checks the reference's Intel GPU classes,
the driver build each needs, the warning text and that Exit there leaves.
`modelTexturesFollowTheProjectLimit` draws fine stripes on a model: they show at the default limit
and turn grey once Model Texture Max Size is 2 px.
`layoutBoxesFrameModels` checks a model layer's Layout box against its rendered pixels, fits it to a
typed box and sees its animation carry the box along.
`skinnedModelsFollowTheirJoints` builds a glTF strip skinned to two joints and checks the rest pose,
the fold when the bone turns, that the skinned node's own motion is ignored, and the layer's posing
by Strength and import scale; `fbxSkinsFollowTheirBones` checks the same fold from FBX skin clusters.
`modelImportFollowsTheReference` checks the reference's import options: the unit list and codes, a
unit's pixels (millimetres over 2.8352), Flip YZ as (-x, z, y), Auto Normalize to half a diagonal of
500 pixels with Model Scale cancelling, centring, and smooth normals up to 80 degrees for a file
without them. `lightWaveModelsAreRead` and `threeDsModelsAreRead` also check the Assimp rules the reference reads
those formats by: an LWOB with a detail polygon, LightWave's shininess, highlights and per-surface
smoothing, 3DS shininess only for Phong shading, the scene's ambient light, the `%%%DEFAULT` material,
smoothing groups and the keyframer's node hierarchy; `modelImportFollowsTheReference` checks OBJ's
`DefaultMaterial` and Assimp's material defaults.
`viewerViewsSeeTheScene` renders a red 3D plane over a green 2D one through the Viewer's view cameras:
the active camera sees both, Front only the 3D scene at a pixel a unit (twice at scale 2), Top and
Left the plane edge on (Top with the floor grid), Perspective from (0, 100, 1000); it also checks
where each axis view puts points and how zoom, pan and orbit move a view's camera. `viewerCameraMenusChooseTheView` checks the per-view camera menus
and that a view with its own camera draws its own picture; `viewerStateSurvivesSave` round-trips a
shot's `ViewerState`.
`modelAnimationAssetsLiveInMedia` imports a file's model animations as a Media asset apart from
the same file as a model, refuses a file without animations, finds it in the Media panel and its
Import menu, and reads it back from a project's `Model3DAnimationAsset`.
`fbxPivotsAndTimeFollowTheReference` turns and scales an FBX model about its pivots and
counts its animation in the file's TimeMode (30) to the stack's LocalStop.
`morphTargetsFollowTheirWeights` builds a glTF quad with one morph target resting at weight 0.5 and
keyed from 0 to 1, and checks the rest pose, the clip's weights, the deformed mesh, Strength and import
scale; `fbxBlendShapesFollowTheirWeights` does the same with an FBX blend shape channel (DeformPercent 50,
a 0..100 curve), its shape's normal offsets included.
`fbxModelsAreRead` writes one scene (a null holding a turned, scaled, textured quad, its translation
animated) as binary FBX 7.4, as 7.5 with deflated arrays and as ASCII, and checks each reads the same:
hierarchy, bounds, normals, UVs, material and texture, the animation, and the refusals for non-FBX
and FBX 6. `threeDsModelsAreRead` and `lightWaveModelsAreRead` do the same for a 3DS object (with a
texture named in capitals) and an LWO2 layer (UV map broken by VMAD, surface with a UV-mapped clip,
mirrored out of LightWave's left-handed space and still facing the camera).
`modelAnimationsPlayOnTheLayer` builds a glTF arm with two animations and checks linear, step and
cubic-spline keys, slerp, that a child follows its animated parent, that the import settings' scale
and axes carry the motion, that another file's animation plays on shared node paths, the reference's
frame arithmetic (Loop, Custom Playback, frame rate override, exact NTSC), that a model layer moves
only with an instance and by its Strength, and that instances survive save/load.
`modelAnimationRowsEditTheInstance` inserts an animation from the Timeline, edits Strength, Loop,
Custom Playback and Playback (keyed values at the playhead), toggles a Strength key with the key
button, finds it on the row's lane and drags it along the canvas, drives "3D Model Animation
Properties", removes the instance and undoes it.
`preRendersWaitTheirTurn` queues two shots in the PreRenderManager and checks the second waits
until the first is done, both end with a frame file per frame, an edited shot loses its frames and
is made again once the edits settle, Remove deletes the folder, and cancelling empties the queue.
`gltfModelsAreRead` builds a glTF 2.0 quad in the test - buffer and texture in data: URIs, and the
same as a .glb with a binary chunk - and checks the node hierarchy, the parent's translation, the
UVs (v down in glTF) and the base colour texture on screen (`gltf-*.png`), and that glTF 1.0 is
refused with a message.
`modelMapsShadeTheSurface` renders a quad with each of the other maps: an emissive map glowing
under a light that gives nothing, a black specular map taking the highlight away, an occlusion map
on the ambient light, a flat normal map changing nothing and one leaning 45 degrees giving cos 45,
bump slopes darkening more with a larger -bm, and an emissive map taken from a hidden layer through
Override Materials.
`lightsShadeTheSceneAsFluxDoes` checks `model3d::illuminate` against Flux's `Illuminant`: unlit
without lights, the ambient term, a point light's diffuse and specular terms at 2.5 x Intensity /
100, Linear and Curve falloff and nothing beyond Reach, a spot's feathered cone, emissive colour, a
layer that is not illuminated, and the shadow mix; `render::layerRotationAt` turning by minus the
angles; `render::sceneLightsOf` placing a spot at (0, 400, 0) pointing down with orientation X 90
and leaving out a hidden light; and a red 3D plane under a hard 20 degree spot 800 out - lit within
141 of the middle, black beyond, darker towards the rim at 30 %, black past a Linear reach, and as
it is when not illuminated (`light-spot-plane.png`).
`shadowsFallFromLights` puts a small casting card between a point light and a red floor: its
shadow is black where expected and nothing else changes, the card does not shadow itself, 80 %
opacity leaves a fifth of the light, Shadow Color tints it, a narrow spot (one map) casts the same
shadow, Shadow Diffusion softens the edges, and a floor that does not receive shadows or a card
that does not cast them shows none (`shadow-point.png`, `shadow-spot.png`).
`modelMaterialsFollowTheirIlluminationModel` checks a model material's lighting by its
illumination model against Flux's shader (Phong with the material's Shininess and its specular
colour whole, Metallic-Roughness and Specular-Glossiness head on, Cook-Torrance's Beckmann peak,
the ambient light on the split colour), reads a glTF with an alpha mask, occlusion strength and a
Specular-Glossiness material as the reference's importer does (sRGB colours), and checks the mask
cutting the clear half away.
`modelMaterialsAreEditedAndSaved` switches a material to Cook-Torrance in the Materials tab of a
model's properties (its rows change), edits its Specular Reflectance, keeps the edit through
Update Preview (`model-materials-tab.png`), lays it over the file's material in Media (also after
another import), saves it as the reference's `<Material Version="7">` and reads it back.
`sceneObjectsShowInTheViewer` checks what `SceneObjectOverlay` draws: through the shot's camera no
frustum of its own, a light's handle where it projects with its Reach as a 60-pixel ring, a spot and
a point; from the Top view the camera's frustum and handle too; a handle picks its layer, a drag
moves it parallel to the view (right/down through the camera, towards the front from the top), one
change; and the Viewer draws it in the reference's blue (`viewer-scene-objects.png`).
`cameraFovProjectRoundTrip` saves a camera's animated zoom, focus distance, aperture, blur, depth
of field and clipping as the reference's CameraLayer keeps them and checks the vertical angle
following from the zoom; `cameraLensAndClippingAreEdited` edits the zoom in the timeline's Lens
group and the Near/Far Clip Distance in the Layer panel.
`cameraDepthOfFieldBlursAwayFromFocus` checks the thin-lens blur term (nothing at the focus, a CoC of
about 16 pixels for a far plane, half with Blur 50 %) and that in a shot with HasDoF a far plane
goes soft at its edges while one at the focus stays sharp, and nothing changes without it
(`camera-depth-of-field.png`).
`lightAndMaterialPropertiesAreEditedAndSaved` edits a light's Type, Reach and Cast Shadows and a 3D
plane's Illuminated and Diffuse in the timeline (one undo step each; a 2D layer has no Material
group) and saves and loads them under the reference's property names.
`lightAndMaterialPropertiesAnimate` checks the key buttons on the rows of a light's colours and
numbers, a camera's Lens and a plane's Material (none on Cast Shadows, none of Ambient..Emissive on a
model, which has Self Shadow Bias instead; Billboarded Lights and Shadows on a plane), toggles keys on
Color and Reach at the playhead (one undo step, an edit at a key sets it there), the blended colour and
Reach in the scene's lights, a keyed Diffuse, Emissive and Blur, a model's whole ADSS and its bias,
the render turning from red to purple with the light's colour (`timeline-light-keys.png`), and the
keys saved - a colour's as whole `<cl>`s under `Type="0"` - and read back.
`billboardedLightsAndHiddenCasters` lights a sheet from a grazing light with and without Billboarded
Lights (dim, then the whole diffuse and specular) and checks that a hidden card casts its shadow only
with Cast Shadows If Visibility Is Off and is still not drawn (`shadow-hidden-caster.png`).
`ambientOcclusionDarkensWhereSurfacesMeet` checks Flux's SSAO over a G-buffer (a flat surface open, a
floor beside a raised block darkened along it and open further off, the block's top open, nothing with
Intensity 0 or too little weight), the G-buffer `renderMesh` notes (the nearer of two meshes, its
normal turned to the eye), a shot where a floor that receives occlusion darkens round a card in front
of it only while the card casts it and the floor receives it (`ambient-occlusion-card.png`), the
Material rows (Receives/Casts Ambient Occlusion, the Ambient Occlusion group with Include Semi
Transparent Triangles on a model only, an edit and its undo) and the save/load round trip (static
props, `<i>` counts, `<fl>` values).
`reflectionsMirrorTheSceneInSheets` checks the lat-long lookup (`ConvertVec2LatLong`, its quirk for a
direction with x or z 0), the environment colour at the poles and the irradiance of a white and a
half-red environment, a black mirror sheet showing a green card's mirror image where it falls - at
full strength, at a quarter with Specular Reflectivity 50, not without Receives or Casts Reflections,
from a hidden card only with Cast Reflections If Visibility Is Off, never from a card behind the
mirror (`planar-reflection.png`) - a 3D text taking the colour of its Environment Map's layer
(`environment-map-layer.png`) or of the shot beneath with Use Composite Shot, the Material rows per
kind (Environment Map's Layer row once Use Layer is chosen) and the save/load round trip.
`childrenFollowTheirParents` checks a 2D plane under a hidden, turned and halved 2D parent - its
position in the scene, the canvas transform, the Layout box and the render (`parented-2d.png`) - a
plane parent's anchor taken off, a loop of parents, a camera standing where its parent point puts it,
and a 2D layer under a 3D parent following only where that parent shows.
`towardsLayerTakesTheOtherLayersUp` checks Towards Layer's second turn: a camera looking at a point
turned a quarter about Z takes that point's up (half a turn when the point is upside down), a sheet and
a light above it the same, and a light whose way lies along the point's Y takes the point's -Z.
`orbitTurnsAboutTheClickedPoint` checks the point of the scene under a ray (the nearest visible 3D sheet
where its content is, a model's triangle, a hidden layer passed through, nothing beside them, the
middle of a perspective view meeting the shot's plane where the view stands 100 up) and the Orbit
button's list - Orbit Around Clicked Point chosen picks the Orbit tool, a press over a perspective
view reports that mode and the press's place, Alt with the left button the selected layers.
`modelsReflectTheirSurroundings` checks a reflection map's mip levels, a grey cube with a white
specular turning red with an Environment Map of a red layer, and green from a hidden card behind the
camera once it receives reflections and the card casts them (`model-reflection-cube.png`), a flat
model whose material is Planar showing a card mirrored in its plane (`model-reflection-planar.png`)
and not when Spherical, and Reflection Mode saved as `<ReflectionType>` and read back.
`layersTurnByTheirAlignment` aims a light and a camera Towards Target Position (straight down keeps
(-1, 0, 0) as up; the light's own orientation turns it further after the aim), a point layer's +Z at
its target, a light Towards Layer, a 3D plane Towards Active Camera turned as the camera is and drawn
whole where it would be edge-on, checks the Transform rows (Target after Position, the reference's
Alignment choices per kind, Alignment Axis and Alignment Layer as asked for, one undo step;
`timeline-alignment.png`) and the save/load round trip of `<AutoOrient>`, `<AutoOrientLayer>` and
`target`.
`layersFollowTheirMotionAndOtherLayers` checks Along Motion Path on a 2D bar (its angle from the
diagonal motion for the X and Y axis, its own Rotation once it stops, the Layout frame and the render
along the diagonal; `align-motion-2d.png`), on a 3D sheet and a spot, Towards Layer on a sheet (front
to the layer, turned about its centre) and a camera, and the Viewer's Target handle: a line and a
handle of its own, dragged parallel to the view while the light stays.
`textGeometryFillsGlyphsAndRunsGeometryModules` (OpenGL) also turns an extruded "O" edge-on to a
light in front of a red floor and checks its walls throw a shadow there (`geometry-shadow.png`), none
once it does not cast.
`viewerViewsSeeTheScene` also checks the view navigation of the reference's tool modes: the orbit
about the origin and about another pivot (distance and view kept, upright, never over the top), the
Dolly, Rotate and Zoom Camera drags (the perspective field of view narrowing with its scale), which
button and keys pick which (`ui::viewDragFor`) and, in a Viewer, a middle drag turning a perspective
view's camera, a right drag panning it and nothing moving over the shot's own view.
`lightWaveProjectedTexturesGetUvs` writes an LWO2 with a planar map along Z and a cylindrical one
round Y wrapped twice on a hexagonal tube, and an LWOB "Planar Image Map" about Y, and checks the UVs
are Assimp's GenUVCoords ones - including the two faces across the cylinder's seam with their sides
swapped as RemoveUVSeams does.
`modelMaterialsTakeLayers` sets a model material's Diffuse Map Source to Use Layer with a hidden
magenta plane from the timeline rows, checks the renderer draws the plane's picture on the model
(`model-material-layer.png`), that None leaves the material's colour, the save/load round trip
and undo.
`nativeControlsFollowTheModule` (OpenGL; skipped where the module cannot answer) checks that
controls registered with option 0x2 start hidden and that Animated Lasers shows the controls of
as many beams as asked for when told a property changed, per effect, while Glow's old integer
radius stays hidden.
`nativePluginControlsAreTranslated` (timeline) scans a copy of the reference Glow.hfpl
with the Russian plug-in catalogue installed and checks the spec the interface shows:
the translated name, labels and unit, and combo items shown in Russian while the
stored values stay the module's own. Run
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
`fog-3d-turned.png`. `sceneIsMultisampled` checks the samples of the nine antialiasing modes
and their patterns, that a multisampled triangle has partly covered edge pixels whose coverage
matches its true area at 4/8/16/32 samples while its inside is shaded as with one, that a face
behind another stays hidden, that a turned 3D sheet's edges are soft where the 2D layer's are
not, and that the mode is in the cache key of a 3D shot only (`msaa-triangle-4x.png`,
`msaa-3d-sheet.png`). `modelNodesAreSelectedMovedAndSaved` imports an OBJ with two objects and
checks its nodes and pivots, `Mesh::posed`, the Viewer picking the node under a click, outlining it
and dragging it to where the pointer went (the renderer drawing it there), the Models rows with a
Transform per node (edit, key, Undo) and the nodes surviving a save (`model-node-outline.png`,
`model-node-moved.png`). `modelImportSettingsSurviveSave` saves a model imported with every
import setting changed and checks they and the node pivot come back.
`exportJobFollowsPresets` (needs FFmpeg and ffprobe) exports with presets and probes the files: an
MP4 at 640x360, 20 fps, Main profile; ProRes 4444 with alpha; a JPEG sequence of 120x120 with its
prefix; WAV at 96 kHz and AAC with no video stream, and an audio-only preset refused for a shot
without sound. In `ui_stubs_regression`, `exportPresetsBuiltInAndFiles`, `exportPresetArguments` and
`exportPresetManagerAndPage` check the built-in presets, `.hfxp` files (also one in the reference's
shape), FFmpeg arguments and filters, the manager, the Presets tab, Edit Preset and the Export
panel's list (`export-presets.png`).
`nativeEffectsDrawReducedPreviews` (needs the OpenGL 4.1 context, `QT_QPA_PLATFORM=windows`)
renders a Half preview of a plane through RadialBlur with its centre moved (it reads the scale in
its context, the host reduces the point), BoxBlur with a 12 px radius (the host reduces its px
values) and Magnify, which is drawn full and reduced, and compares each with the full frame reduced
(`half-native-*.png`); it also checks BoxBlur's radius is registered in "px" and that reducing it
changes the picture. `compositionProperties` also drives the Fog group.
`referencePointTextStandsOnItsBaseline` renders project_1's two point-text lines at 15 s
(`project_1-text.png`) and checks that each stands on its baseline at the layer position and is
centred without its trailing space, whatever its vertical alignment, and that the saved
`<TextBox>` is the reference's extent; `paragraphTextBoxKeepsItsOffset` round-trips an
off-centre paragraph box. `previewSizesScaleTheWholeFrame` renders a plane at 1/4, 1/2, 1 and 2x the shot's size and
checks it lands in the same place; `pixelAspectSqueezesTheFrameAndTheViewerUnsqueezes` checks a
2:1 PAR shot (square space 400x100 squeezed into 200x100 pixels, the viewer showing it 4:1, the
`setsar` fraction). `reducedPreviewMatchesTheFullFrame` renders a Half preview of footage turned about its anchor
with a built-in blur and a feathered mask, a plane, text and a nested shot, with square and 2:1
pixels, and compares it with the full frame reduced (`half-preview-*.png`,
`half-expected-*.png`). `reducedPreviewDrawsTheSceneReduced` does the same for a 3D scene - a
plane and footage turned in depth with fog, an OBJ cube, 2D text over it
(`half-scene-*.png`, `half-scene-expected-*.png`). `layoutBoxesFollowTheRenderedLayer` compares Layout's box of a layer (`render::layerGeometry`)
with the pixels the renderer actually draws - a scaled still, the same turned about an anchor,
with a 2:1 pixel aspect, as a 3D sheet - and checks that a box typed into the panel
(`render::fitLayerToBox`) is where a still, a turned text and a 3D sheet end up; a camera has
no box. `viewerFramesFootage` drags the Viewer frame's handles on a still turned about its anchor
point: a corner scales it evenly with the opposite corner held, an edge scales one axis, the knob
turns it about the anchor, and the renderer draws it where the frame is; under the Text tool it is
not framed, and with several layers selected a drag inside it is the viewer's
(`viewer-footage-frame.png`). `mediaOverridesReinterpretFrames` checks the rest of Media Properties: a 15 fps override of a
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
`textFrameFollowsItsParent` turns a text layer's parent a quarter clockwise: the text frame turns with it
and a canvas move becomes a move in the parent's units.
`clearCoatBrightensGrazingReflections` renders a rough Metallic-Roughness quad in a white environment
seen at 80 degrees and square on: a smooth clear coat brightens the grazing view and hardly changes the
square one. `textGeometryReflectsItsEnvironment` gives 3D text with a (registered-only) Geometry effect a
red asset as its Environment Map (type 1003): the letters turn red. `modelsReflectTheirSurroundings` also
checks the asset environment on a model.
`parentedLayersFitAndPlaceTheirFootage` fits a plane under a point layer turned 90 degrees and scaled
200% to a box (the box lands where asked) and checks that an instance's footage is placed under its
parent (30 px right, 20 up).
`layerBoxFollowsItsBehaviors` gives a plane MotionTrack matrices (50 right, 20 up on frame 1): its
layout box moves with them, and not once the Behavior is off.
`layoutFieldsScrubLikeTheReference` checks the reference's `DoubleScrubberSpinBox`: a drag adds one
step a pixel, Shift snaps to multiples of 10 every five pixels, the range's ends hold, the button going
up is one edit, a click edits the text and Escape reverts; the Layout panel's fields are such scrubbers.
`keyTimesAreRelativeToTheLayer` saves a plane starting at 2 s with keys at 2 s and 3 s: the file holds
0 and 1000 ms (`OpenVegasKeyTimes="layer"`), reading puts them back at frames 60 and 90, a save without
the attribute (older saves of the port) is read as shot times, and `shiftLayerKeys` keeps key
identities. `movedLayerTakesItsKeys` drags a clip body a second later: the layer's keys and its effect's
keys move by as many frames, in one undo step. `sheetBoxFollowsItsBehaviors` gives a turned, half-size 3D plane MotionTrack matrices: its sheet matrix
becomes the shift times the still one (parent x Behaviors x own), and its box matches the red pixels the
renderer draws. `behaviorsActInsideTheParent` turns a plane's parent a quarter clockwise: MotionTrack's
shift to the right moves the plane down, its box and its pixels alike.
`simulatedBehaviorsMoveInTheWorld` (GL, the reference Gravity.hfpl) parents a falling plane under a point
turned a quarter: it falls down the frame, not sideways, its box and pixels alike; a child of the falling
plane falls with it, and a plane keyed across the frame with Drag lags behind its keys; a run read back
from a later frame matches a fresh one, a body between frames lies between them, a child body with
Drag falls about as far as its parent (its forces), and a falling plane without keys is motion blurred.
`behaviorsBlurAtTheirSubFrames` moves a plane without keys 8 pixels a frame with MotionTrack's matrices:
motion blur smears it along its way about its box, and the module's matrices are read between frames.
`behaviorsMultiplyInTheReferencesOrder` gives a plane a quarter turn and then a shift to the right: the
plane goes up (the last Behavior acts first), and a parent's shift carries its child as far; boxes and
pixels agree. `trimmedLayerKeepsItsKeys` drags a clip's head a quarter
of the clip on: the end stays, the source in-point follows, the layer's and its effect's keys stay on
their frames (one now before the layer's start) and still do after a save and a load.
`layersWithoutClipsKeepTheirSpan` saves a camera spanning 1-2 s (StartFrame 30, EndFrame 60) and a light
over the whole shot (EndFrame = FrameCount, read back open); an older save's empty span reads as the whole
shot. `camerasAndLightsTakePartWithinTheirSpan` switches the active camera at the second camera's start
and lights a light only within its span. `cameraBarMovesAndTrimsItsSpan` drags a camera's timeline bar:
the body moves the span and its keys, the tail trims it and leaves them, each one undo step.
`stabilizersSteadyTheirOwnLayer` runs AutomaticStabilizer and MotionLock on a 30-frame image sequence
shaking up to 6 px: the host tells each it was put on its layer (`Effect_Parent_Layer`), the footage is
analysed in the background, the renderer's frames - drawn by the effect's own render instance from its
saved data - must keep under a third of the footage's frame-to-frame jitter; then AutomaticStabilizer's
"Create Point Layer" (a keyed point layer on top, "Apply To" pointed at it) and "Apply Transform", and
MotionLock's "Apply Transform" (its own layer keyed with its motion), each one `layersEdited` step
(OpenGL, `QT_QPA_PLATFORM=windows`). `layerTransformsBecomeKeys` checks how transforms become keys or
static values.
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

Options migration regressions use the real ru/ja/zh_CN QM catalogues and English.
`localizedOptionValues` checks legacy translated proxy choices before/after changing
language, stable INI values even when display labels change, and new-project defaults.
`nativeAntialiasingModesPersist` covers all nine modes, Restore Defaults, and old
Off/2x MSAA fallback. This checks settings propagation, not renderer MSAA support.

Proxy regressions cover legacy quality/preview values, FFmpeg generation, failed
transcodes/startup with an existing proxy, and QSaveFile publication. A replacement
larger than 2 MiB checks block copying; missing/empty input, a blocked target, and
(on Windows) a handle denying rename/delete check preservation of old bytes.
Run `legacyProxyChoicesReachEncoderAndPreview`, `proxyPublicationPreservesPreviousFile`,
and `proxyMediaGenerationAndRoundTrip` in `timeline_regression`.

Composition audio-rate regressions cover 32/44.1/48/96 kHz. Audio tests check
resampled master block sizes, offset/speed/gain timing, seek/stop, animated native
Balance and short native AudioReverse look-ahead. Set OPENVEGAS_HFPL_BALANCE to
the reference Balance.hfpl to enable native tests; standalone setSource still
uses 48 kHz. Invalid project rates outside 8..192 kHz fall back to 48 kHz.

`compositionSampleRateReachesMovieAndProcessedPcm` checks project round-trip,
direct and animated-Level PCM branches, output sample rate/channels/duration in
MOV and MP4, and actual PCM count/gain in MOV. It catches the tail truncation
caused by routing ordinary speed through atempo=1. Audio transition export runs
at the same four rates. These checks use actual FFmpeg/VLC; they do not measure
the physical device format or guarantee every AAC encoder rate.

Nested audio export regressions: `nestedAudioOffsetsSpeedGainAndReuse` checks two
levels, 0.25x/2x speed, trim/offset, inherited/animated levels, hidden/muted layers,
shared children, project round-trip, root/child keyframe fps, instance isolation,
and actual PCM from a partial MOV export at 32/44.1/96 kHz.
`nestedAudioCycleDoesNotPublishPartialInputs` checks failure without modifying
existing inputs, ignored muted branches and recovery after breaking a cycle.
`nestedAudioNativeDspAndTransitions` requires OPENVEGAS_HFPL_BALANCE and checks
child AudioReverse before parent speed, animated parent Balance, native Fade
between composite clips and final MOV PCM against prepared input samples.
`audioMixUsesSampleDelaysAndPadding` checks a 0.5 ms delay as exactly 22 samples
at 44.1 kHz and padding to the requested output duration. Nested offline groups
use PCM16; realtime still flattens groups and omits parent DSP/transitions.

`options_regression::pluginsPageChoosesFolders` lays out a fake Program Files with VEGAS Effects and
HitFilm Pro plugin trees: the installation's plugins are found and scanned, other FXhome programs' are
offered, a folder chosen on Options > Plugins is saved with OK (and asks for a new scan only when the
folders changed), the page's defaults clear it, and Options opens there by itself once when no folder
has any plugin.
`nativeGlyphBehaviorsDrawReducedPreviews` also checks the Layout box of the DropInByChar text: while its
letters drop in, it is the box of the same text without the glyph Behavior, as the reference frames a
layer by its world transform only.
`mediaAudioIsConformedOnce` conforms a 44.1 kHz mono WAV to 48 kHz stereo PCM in the media cache: made
once in the background and recorded, read again from the cache, another rate or a replaced file made
again. `preRenderCarriesTheShotsAudio` pre-renders a shot with a tone: its pre-render has the shot's mix
as a WAV, export takes it for the nested shot, and an edited shot no longer matches it.
`options_regression::renderMapSizesReachNewProjects` sets the shadow and reflection map sizes in
Options > Render and finds them in a new project's render settings.
