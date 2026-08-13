# Kronos Platform — Phase 8: Asset Pipeline

Status of the Alpha Roadmap's Phase 4 ("Asset Pipeline") — see
[ALPHA_ROADMAP.md](ALPHA_ROADMAP.md) for the full roadmap.

All work below was verified with a full rebuild (`engine_runtime`,
`studio`, `engine_tests`) and the full test suite green after every change
(9427/9427 checks passing, up from 9410 — 17 new tests for
`core::AssetRegistry`).

## Audit finding

Four of the five checklist items were already real: a model importer
(`core::loadObj()` — real `.obj` parsing, used by `SceneManager`/
`ScriptedPlugin` alike), a texture loader (`core::Texture::loadFromFile()`,
stb_image-backed), material presets (`studio::kMaterialPresets`, shared
by `MaterialPlugin` and the Asset Browser), and hot-reload (`core::
AssetCache<Handle>` — a real mtime-keyed cache already wired into
`TexturePreviewPlugin`/`ModelImporterPlugin`, so re-visiting a changed
file in Studio re-loads it instead of serving a stale GPU resource).

**One real gap: "Asset registry."** `studio::plugins::
CreatorAssetBrowserPlugin` (its own header comment says so plainly) browses
the engine's *built-in* catalogue — prop kinds, material/particle/terrain
presets — not files a creator has actually imported. `core::AssetCache`
is a load-dedup cache, not a browsable list. `core::ProjectFile` tracks
`scenePaths` only, nothing about assets. There was genuinely nowhere a
creator could see "here is what this project has imported."

## Asset registry

**Built — `core::AssetRegistry`** (`core/AssetRegistry.{hpp,cpp}`): a
real, persisted list of imported assets. `importAsset(path)` runs the
already-real `extractAssetMetadata()` probe (Mesh/Texture/Audio, real
per-kind inspection — vertex/triangle counts, pixel dimensions, duration/
sample rate) and stores the result; re-importing an already-registered
path replaces its entry rather than duplicating it. `removeAsset()`,
`contains()`, `entries()`, and `saveToFile()`/`loadFromFile()` (the same
hand-rolled `KEY value` text format every other `core/` save/load struct
already uses) round out the class. Deliberately a thin wrapper — it never
re-implements mesh/texture/audio parsing itself, only calls the real
probe that already existed.

**Wired into Studio** — `CreatorAssetBrowserPlugin` gained a sixth
category, "Imported": an Import path field + button (calling
`importAsset()`, showing the real error on failure), and a list of every
registered entry with its real per-kind metadata and a Remove button —
distinct from, and alongside, the five built-in-preset categories that
were already there.

**Tests:** `testAssetRegistryImportSaveLoadRoundTrip` — real import of a
real `.obj` and a real `.png`, a failed import for a missing file (and
confirmation it doesn't pollute the registry), re-import replacing rather
than duplicating, `removeAsset()` (including the no-op case for an
unregistered path), and a full save/load round-trip verifying both the
entry list and its real per-kind metadata survive.

## Summary

| Item | Status |
|---|---|
| Model importer | Already real (pre-existing), confirmed |
| Texture loader | Already real (pre-existing), confirmed |
| Material presets | Already real (pre-existing), confirmed |
| Asset registry | Built — `core::AssetRegistry`, wired into the Asset Browser's new "Imported" category, 17 new tests |
| Hot-reload for assets | Already real (pre-existing `AssetCache`), confirmed |
