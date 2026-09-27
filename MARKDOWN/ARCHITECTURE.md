# Architecture of OpenVegas Effects

This document maps the behaviours discovered by reverse-engineering the
reference application (`SAMPLES/VEGAS_Effects/MARKDOWN/RE_VegasEffects.md`)
onto the fresh, original implementation in this repository. It is the working
plan; concrete decisions live in code.

## Goals

- Provide a clean-room, open, GNU GPL v3-licensed re-implementation of a
  compositing / effects application with a Vegas-Effects-like workflow.
- Keep module boundaries aligned with the reference layout so that workflow
  equivalents can be added incrementally.

## Reference behaviours we mirror (architectural, not code)

1. **Entry chain.** The reference binary boots from `WinMain` through a
   console/GUI dispatcher (render-server mode vs GUI mode) to `main()`,
   converting the wide command line to UTF-8 `argv`. We mirror this in
   `src/main.cpp` via `CommandLineToArgvW` → UTF-8 `QStringList`.

2. **Crash reporting.** The reference hooks BugSplat handlers early. We keep
   the responsibility as a separate concern (future `crash/` module); for now a
   file log is used.

3. **User data folders.** `FUN_14025b000(dir, idx, mustExist)` resolves eleven
   folder indexes: 0 `Translations`, 1 `Templates\AV`, 2 `EnvironmentMaps`,
   3 `ExportPresets`, 4 `PluginPresets`, 5 `Plugins`, 6 `Presets`, 7 `Textures`,
   8 `Templates\Workspaces`, 9 `Objects`, 10 `Tutorials`. Two of them are nested:
   indexes 1 and 8 share the base "Templates" and get "\AV" / "\Workspaces"
   appended, so no index yields a bare `Templates`.

   The third argument is **not** a mkdir flag, as this document previously said.
   When set, the resolver builds a `QDir` and returns an *empty* string if the
   directory is missing - it never creates one. The reference relies on its
   installer to lay the tree down, which is what its "please run Setup to repair
   the installation" message is about. `src/app/UserDataPaths.{h,cpp}` reproduces
   the layout from scratch with `enum class UserDataFolder` but does create the
   folders on first run, since this port ships no installer.

4. **Plugin subsystem.** The reference uses a `PluginManager` whose
   `Create`-style factory takes a license manager, paths, edition, a callbacks
   struct, log mode and extra flags; plugin folders are checked for existence
   and a "Plugin log:" line is written. We provide:
   - `plugin::PluginManager` (abstract host interface),
   - `createPluginManager()` factory,
   - `plugin::Callbacks` (folder resolver + log sink) wired in `AppMain`,
   - native discovery of `.vfx` effect definitions in `Plugins/`.
   The next step is an OFX host: loading `.ofx/.dll` plugins found in the
   `Plugins` folder and exposing their effects in the Effects panel and the
   timeline.

5. **License subsystem.** Reference checks license state and edition early.
   Our `license::LicenseManager` is an interface; `OpenLicenseManager` is a
   free/local implementation (no activation, no vendor servers).

6. **Composition model.** Reference keeps a project/composition object model
   (media assets, layers, clips, effect instances). `src/composition/*` and
   `src/media/*` provide the first pass.

7. **Cache.** Reference maintains SQLite-backed media/timeline/OFX caches
   (`CacheDBManager`). `src/cache/CacheDB.cpp` opens `cache.db` under
   `Presets/` and keeps a `cache_entries` table.

8. **Render.** Reference runs rendering workers and a pipeline for frame
   output. `src/render/RenderManager` moves a placeholder worker to a worker
   thread and mirrors `requestFrame`/`frameReady` flow.

## Module map

| Area      | Reference notion        | Open module                          |
|-----------|-------------------------|--------------------------------------|
| entry     | WinMain → main          | `src/main.cpp`                       |
| app init  | main() orchestrator     | `src/app/AppMain.{h,cpp}`            |
| paths     | folder resolver         | `src/app/UserDataPaths`              |
| settings  | global settings         | `src/app/Settings` (QSettings ini)   |
| license   | license manager         | `src/license/*`                      |
| plugins   | PluginManager, Callbacks| `src/plugin/*`                       |
| media     | media manager           | `src/media/*`                        |
| project   | composition object model| `src/composition/*`                  |
| cache     | CacheDBManager          | `src/cache/CacheDB`                  |
| render    | render workers          | `src/render/RenderManager`           |
| UI        | main window + panels    | `src/ui/*`                           |

## Next steps

- [ ] OFX plug-in host: load `.ofx` shared libraries from `Plugins/`, read
      `OfxGetNumberOfPlugins`/`OfxGetPlugin` and register effects.
- [~] Effect instance pipeline: the five built-in colour effects now run in the
      render path - `RenderWorker::applyClipEffects` resolves each parameter
      through `Effect::parameterAt` (so keyframed parameters use their curve)
      and calls `plugin::applyEffectToImage`. Still open: `EffectInstance::render`
      for plugin-provided effects via an OFX host.
- [x] Serialization of project document (native `.vegfx` XML schema via
      `src/project/VegfxSerializer`, wired into File → New/Open/Save/Save As).
- [x] Timeline editing: add clips/layers via a whole-media Import; playhead
      scrubbing drives rendering (`TimelineWidget::timeScrubbed`).
- [ ] Render server mode: command-line flag to run headless worker (mirrors
      the reference console/render dispatch).
- [ ] Crash reporter module (PIE-style) logging context to `Presets/app.log`.
- [ ] Unit tests for core (Identifier, Version, UserDataPaths, CacheDB).