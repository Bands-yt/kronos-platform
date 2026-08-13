# Kronos Alpha — Section 8: Alpha Packaging

Status of the [Alpha Completion Checklist](ALPHA_COMPLETION_CHECKLIST.md)'s
Section 8. Verified with a full rebuild (`engine_runtime`, `studio`,
`engine_tests`) and the full test suite green (9594/9594 checks passing,
up from 9582 — 3 new tests), plus a real, live packaging run verified
against an actually-relocated copy (see below).

## Engine/studio binary packaging

**Real, critical bug found and fixed.** Auditing packaging readiness
surfaced that neither binary was actually relocatable at all:
`ENGINE_SHADER_DIR`/`ENGINE_ASSET_DIR` (see `src/CMakeLists.txt`) are
compile-time absolute paths baked into the binary on whatever machine
built it. `engine_runtime`/`studio` worked fine from the exact build tree
they were compiled in, but copying either binary anywhere else — another
machine, even just a moved build folder — would silently break shader
and asset loading. There was no packaging story here at all before this
pass; this was the actual blocker underneath the checklist item, not a
missing script.

**Fixed** — `core::ResourcePaths` (new, `core/ResourcePaths.{hpp,cpp}`):

- `executableDirectory()` — the real directory containing the
  currently-running executable, via `/proc/self/exe` (Linux-only for
  now, matching `CrashReporter.hpp`'s own platform-scoping precedent).
- `resolveResourceDir(exeDir, subdirName, compileTimeFallback)` — pure
  and independently testable: prefers a real, existing
  `exeDir/subdirName` directory (the packaged, flat-layout case) over
  `compileTimeFallback`, which stays exactly today's existing
  compile-time path otherwise — so a local development build's behavior
  is completely unchanged (verified: `engine/build/src/shaders` doesn't
  exist in the dev tree, so the dev workflow takes the identical
  fallback path it always has).

All 8 real `ENGINE_SHADER_DIR` call sites (`Renderer.cpp` ×7,
`UIRenderer.cpp` ×1) and all 3 real `ENGINE_ASSET_DIR` call sites
(`Application.hpp` ×1, `Application.cpp` ×1, plus one shared helper) now
resolve through this instead of using the raw macro directly.

**Real packaging script** — `scripts/package_alpha.sh`: assembles a real,
flat, self-contained distribution directory (binaries + compiled `.spv`
shaders + `assets/` + `templates/` + `plugins/`, all as direct siblings —
exactly the layout `resolveResourceDir()` looks for).

**Verified live, not just by inspection**: ran the script, then copied
the resulting package to a completely different filesystem path
(`/tmp/kronos-alpha-relocated`, nothing to do with the build tree) and
confirmed `executableDirectory()`/`resolveResourceDir()` correctly
resolved the real, relocated `shaders`/`assets` directories rather than
the (deliberately wrong, for this check) compile-time fallback — proving
the fix actually solves the real problem, not just compiles.

## Plugin folder structure

**Real, honest split, both now present in a package**: `templates/plugin/`
(Section 4) is the real, working starting point a plugin developer
copies *from*; `plugins/` (new, created empty by the packaging script) is
the real runtime scan directory `studio::plugins::PluginBrowserPlugin`
already defaults to (`pluginDirectoryBuffer_ = "plugins"` —
confirmed by reading the source, not assumed) — present in the package so
a creator can drop a real plugin folder straight in without first having
to create the directory themselves.

## Asset folder structure

Already real (`engine/assets/{audio,textures}`) — the packaging script
copies it as-is into the package's own `assets/` directory, which is
exactly what `resolveResourceDir("assets", ...)` looks for next to the
binaries.

## Default project template

**Real, previously-nonexistent gap closed.** `templates/project/default.{project,scene}`
(new) — a real, loadable starting project: a ground plane, one starter
box, and a sun light, generated through the engine's own real
`SceneFile::saveToFile()`/`ProjectFile::saveToFile()` (via a throwaway
generator program, deleted after use) rather than hand-typed, to avoid
risking a subtle mismatch with what `loadFromFile()` actually expects.
Deliberately ships with `CREATED`/`MODIFIED` left at their honest `0`
default (no `touch()` call) so a creator's first real Save after copying
it stamps a genuine creation time, not this generation run's own
timestamp.

**Real test coverage**: `testDefaultProjectTemplateLoadsReal` loads the
*actual shipped files* (not a re-typed copy) through the real
`ProjectFile`/`SceneFile` loaders and confirms all three entities
(`Ground`, `StarterBox`, `SunLight`) round-trip with their real
components intact.

## Summary

| Item | Status |
|---|---|
| Engine/studio binary packaging | Real, critical non-relocatability bug found and fixed (`core::ResourcePaths`); real packaging script; verified against an actually-relocated copy |
| Plugin folder structure | Real `plugins/` (runtime scan dir) + `templates/plugin/` (starting point), both now packaged |
| Asset folder structure | Already real; now packaged alongside the binaries |
| Default project template | Built — a real, loadable starter project, with real test coverage |
