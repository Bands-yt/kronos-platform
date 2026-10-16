# Backlog audit: P0 items (16 Oct 2026)

First pass over the P0 items of the 1,000-item backlog (IDs `KRN-xxxx`). Each is checked against the
code and docs, not against the backlog's wording. "Exists" means the feature is in the repository
today; "verify" means I found it but haven't tested it for the claim in the backlog. This is a
reading audit: nothing here was run on a clean machine.

| Status | Meaning |
|---|---|
| exists | built and covered by a test or a doc |
| partial | some of it exists; the gap is named |
| missing | nothing found |

## First run and UI

| ID | Item | Status | Evidence / gap |
|---|---|---|---|
| 0001 | Welcome wizard with mode selection | missing | Player shows a one-time "Welcome to Kronos" notification (`RuntimeShell.cpp`); Studio has no first-run flow |
| 0002 | One-click sample project | partial | Studio opens a template scene and has script template cards; `games/` holds BrokenBones, SkyGarden, DefaultWorld; no "new sample game" picker |
| 0003 | First-launch hardware and driver check | partial | `core/GpuFeatures.*` detects capabilities; `classifySdlFailure` gives honest Vulkan messages; no visible first-run report |
| 0026 | Resizable docks with saved layouts | exists | ImGui docking, `imgui.ini`, hidden panels saved (`StudioApp::saveHiddenPanels`) |
| 0027 | Reset workspace to default | exists | Ribbon "Reset Layout" and menu item (`StudioRibbon.cpp`, `StudioApp.cpp`) |
| 0028 | Per-task workspace presets | missing | Movie Maker and Audio Maker are separate programs; Studio has one layout |

## Scene graph

| ID | Item | Status | Evidence / gap |
|---|---|---|---|
| 0051 | Virtualized hierarchy for big scenes | exists | `ExplorerPanel` skips rows outside the view (5,000-part folder scrolled on screen); no 100k-entity timing test yet (0075) |
| 0052 | Multi-select with a summary | exists | Select All, "N entities selected", multi-object Inspector |
| 0053 | Drag-and-drop parenting, no cycles | partial | Explorer has a drag source (`EXPLORER_ENTITY`); verify the drop target reparents with undo and refuses cycles |

## Assets

| ID | Item | Status | Evidence / gap |
|---|---|---|---|
| 0151 | Import queue with progress and cancel | partial | `core/AssetImportQueue.*` exists; verify progress and cancel in the UI |
| 0152 | Drag-and-drop multi-file import | verify | not checked |
| 0153 | Import profile per asset type | missing | importers have fixed settings |

## Scripting

| ID | Item | Status | Evidence / gap |
|---|---|---|---|
| 0401 | Syntax-highlighted Luau editor | exists | Script Editor, Luau checker, error line markers |
| 0402 | Autocomplete from reflected APIs | partial | editor completions exist; the Roblox class table (`InstanceTree`) is not yet used for them (4.5 "reflection-driven autocomplete") |
| 0403 | API hover cards | missing | |
| 0426 | Python in Studio Play | exists | 12 Oct 2026; `#`-first-line scripts run in Play (`testStudioPythonScripts`). The backlog preamble is out of date on this |
| 0427 | Python editor with diagnostics | partial | colours and templates; no autocomplete or error markers |
| 0428 | PocketPy version and capability report | missing | |
| 0451 | Document game scripts vs plugins | partial | README and `PYTHON_SCRIPTS.md` explain it; no single page |
| 0452 | Language capability matrix | missing | |
| 0453 | Stable plugin ABI versioning | exists | `KRONOS_PLUGIN_API_MAJOR/MINOR`, `struct_size` on every struct, capability bits (`kronos_plugin.h`); a conformance test binary is missing (0458) |

## Diagnostics

| ID | Item | Status | Evidence / gap |
|---|---|---|---|
| 0476 | Debug console with filtering | partial | Debug Console + Engine Log; no filter box |
| 0477 | Structured logs with categories | exists | `Logger` categories (`Script`, `Network`, `Plugin:<name>` ...), see `CRASH_TELEMETRY.md` |
| 0478 | Crash report with consent and redaction | partial | `CrashReporter` writes `crash_report_*.txt`; consent prompt and redaction not found |

## Roblox bridge

| ID | Item | Status | Evidence / gap |
|---|---|---|---|
| 0501 | Compatibility score by class and API | exists | `kronos_compat`, import report |
| 0502 | Test corpus for common genres | exists | 10 places + Lab (`COMPAT_LAB.md`) |
| 0503 | Binary RBXL/RBXM hardening | partial | binary reader works (LZ4); Zstd chunks and fuzzing missing |

## Player, builds, QA, security, performance

| ID | Item | Status | Evidence / gap |
|---|---|---|---|
| 0526 | Instant launch page | verify | Home screen exists; "instant" not measured |
| 0527 | Recent and favourite games | verify | |
| 0528 | Per-game update notes | missing | |
| 0551 | One version number | exists | `engine/CMakeLists.txt` generates the header and installer file |
| 0552 | Reproducible Windows build | partial | CI builds it; dependencies are not pinned by hash, so not reproducible |
| 0553 | Reproducible Linux build | partial | same; a container build now exists for the server only |
| 0801 | Test coverage map by subsystem | missing | 21,400+ checks in one file, no map |
| 0802 | Core smoke suite | exists | `remote_network_smoke.sh`, package smoke tests in CI |
| 0803 | Fresh project creation smoke test | missing | |
| 0826 | Threat model for Player and Studio | missing | no document |
| 0827 | Sandbox escape regression suite | partial | Luau and Python sandbox tests exist (memory, time, stack, blocked imports); not framed as an escape suite |
| 0828 | Least-privilege plugin profiles | partial | capability bits exist; no named profiles |
| 0851 | CPU frame-time breakdown | partial | Stats panel and frame pacing telemetry; no per-system split |
| 0852 | GPU frame-time breakdown | missing | no timestamp queries found |
| 0853 | Per-system ECS timing | missing | |

## What this suggests for 4.7 ("first 15 minutes")

Cheap and high value: the Studio first-run flow (0001, 0002, 0003), the language and maturity
matrices (0452, 0428), a threat-model document (0826), a fresh-project smoke test (0803), the
hierarchy clipper plus a 100k-entity test (0051, 0075), the crash-report consent and redaction
(0478), hover cards and reflection-driven autocomplete (0402, 0403), and timing counters for the
profiler overlay (0851 to 0853).
