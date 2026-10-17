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
| 0001 | Welcome wizard with mode selection | done | The welcome panel has a "Pick a tool" row (Studio, 3D Tools, Movie Maker, Audio) that opens the matching program; screen-checked 17 Oct 2026 |
| 0002 | One-click sample project | partial | Studio opens a template scene and has script template cards; `games/` holds BrokenBones, SkyGarden, DefaultWorld; no "new sample game" picker |
| 0003 | First-launch hardware and driver check | partial | the welcome panel shows the GPU, Vulkan version and a plain tip (`describeGpu`, `testGpuSummary`); not screen-checked, no audio or disk check |
| 0026 | Resizable docks with saved layouts | exists | ImGui docking, `imgui.ini`, hidden panels saved (`StudioApp::saveHiddenPanels`) |
| 0027 | Reset workspace to default | exists | Ribbon "Reset Layout" and menu item (`StudioRibbon.cpp`, `StudioApp.cpp`) |
| 0028 | Per-task workspace presets | missing | Movie Maker and Audio Maker are separate programs; Studio has one layout |

## Scene graph

| ID | Item | Status | Evidence / gap |
|---|---|---|---|
| 0051 | Virtualized hierarchy for big scenes | exists | `ExplorerPanel` skips rows outside the view (5,000-part folder scrolled on screen); no 100k-entity timing test yet (0075) |
| 0052 | Multi-select with a summary | exists | Select All, "N entities selected", multi-object Inspector |
| 0053 | Drag-and-drop parenting, no cycles | done | the Explorer drop calls `hierarchy::setParent` (self and cycles refused) and now pushes an undo step "Move in Hierarchy" (`testHierarchyReparentCanBeUndone`); the drag itself is not driven by my screen tools |

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
| 0402 | Autocomplete from reflected APIs | exists | `core/RobloxApiDefinitions` turns the class table into Luau `declare class` text; `workspace.` and `game:` complete members (screen-checked, 0 false errors on the Player greeting template); return values are still `any` |
| 0403 | API hover cards | done | resting the pointer on an API name in the script editor shows its doc line (`world.findByName` etc.) from `kronos_globals.d.lua`; screen-checked 17 Oct 2026; Roblox class members have no doc text yet |
| 0426 | Python in Studio Play | exists | 12 Oct 2026; `#`-first-line scripts run in Play (`testStudioPythonScripts`). The backlog preamble is out of date on this |
| 0427 | Python editor with diagnostics | partial | colours and templates; no autocomplete or error markers |
| 0428 | PocketPy version and capability report | exists | `engine_runtime --python-info` (probes run for real), `testPythonScripts` |
| 0451 | Document game scripts vs plugins | exists | `docs/LANGUAGES.md` |
| 0452 | Language capability matrix | exists | `docs/LANGUAGES.md` (16 Oct 2026) |
| 0453 | Stable plugin ABI versioning | exists | `KRONOS_PLUGIN_API_MAJOR/MINOR`, `struct_size` on every struct, capability bits (`kronos_plugin.h`); a conformance test binary is missing (0458) |

## Diagnostics

| ID | Item | Status | Evidence / gap |
|---|---|---|---|
| 0476 | Debug console with filtering | exists | level checkboxes plus a text filter (`core::logLineMatches`, `testLogFilter`); filter box not screen-checked |
| 0477 | Structured logs with categories | exists | `Logger` categories (`Script`, `Network`, `Plugin:<name>` ...), see `CRASH_TELEMETRY.md` |
| 0478 | Crash report with consent and redaction | partial | reports stay on the PC (nothing uploads them) and now have home folder, user name and e-mail removed (`redactPersonalInfo`, `testCrashReportRedaction`); no consent prompt because nothing is sent |

## Roblox bridge

| ID | Item | Status | Evidence / gap |
|---|---|---|---|
| 0501 | Compatibility score by class and API | exists | `kronos_compat`, import report |
| 0502 | Test corpus for common genres | exists | 10 places + Lab (`COMPAT_LAB.md`) |
| 0503 | Binary RBXL/RBXM hardening | partial | binary reader works (LZ4); a fixed-seed damage test passes (`testRobloxParsersSurviveMutations`); Zstd chunks and a real fuzzer missing |

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
| 0803 | Fresh project creation smoke test | partial | `testFreshProjectTemplate` loads the template scene and runs physics headless; no Studio-driven "File > New" test |
| 0826 | Threat model for Player and Studio | exists | `docs/THREAT_MODEL.md`; rows say what is tested and what is not |
| 0827 | Sandbox escape regression suite | done | `docs/THREAT_MODEL.md` "Sandbox escape tests" lists each escape attempt and the test that runs it; macOS plugins have no sandbox |
| 0828 | Least-privilege plugin profiles | partial | named profiles (`capabilityProfile`, `testPluginCapabilityProfiles`); Studio doesn't yet ask the user which profile to grant |
| 0851 | CPU frame-time breakdown | exists | `core::SystemTimers` (scripting, physics, pre-tick, post-physics, render) shown in Stats "Where the time goes"; not screen-checked |
| 0852 | GPU frame-time breakdown | partial | one GPU time for the whole frame (timestamp queries in `RendererFrame.cpp`, shown as "GPU frame" in Stats; screen-checked 17 Oct 2026); no per-pass split yet |
| 0853 | Per-system ECS timing | done | `core/SystemTimers` (scripting, physics, game logic, post-physics, render, plugins) shown in Stats under "Where the time goes"; wall-clock, not per ECS system |

## What this suggests for 4.7 ("first 15 minutes")

Cheap and high value: the Studio first-run flow (0001, 0002, 0003), the language and maturity
matrices (0452, 0428), a threat-model document (0826), a fresh-project smoke test (0803), the
hierarchy clipper plus a 100k-entity test (0051, 0075), the crash-report consent and redaction
(0478), hover cards and reflection-driven autocomplete (0402, 0403), and timing counters for the
profiler overlay (0851 to 0853).

## v5 batch 1 (17 Oct 2026): the easy documents

All of these are covered by [PROJECT_POLICY.md](PROJECT_POLICY.md) or [MATURITY.md](MATURITY.md).

| ID | Item | Status | Evidence / gap |
|---|---|---|---|
| 0976 | One-sentence product positioning | done | PROJECT_POLICY.md "In one sentence" |
| 0977 | Accurate feature maturity matrix | done | MATURITY.md |
| 0978 | Comparison page with qualified claims | done | PROJECT_POLICY.md comparison table |
| 0980 | Local-only versus cloud-hosted | done | PROJECT_POLICY.md table |
| 0982 | How Kronos differs from Unity | done | comparison table |
| 0983 | How Kronos differs from Roblox Studio | done | comparison table |
| 0984 | Target audience and non-goals | done | PROJECT_POLICY.md |
| 0985 | Compatibility guarantee levels | done | PROJECT_POLICY.md "old projects" |
| 0986 | Version compatibility policy for old projects | done | same |
| 0992 | Minimum supported hardware | partial | Vulkan 1.3 and OS stated; no RAM or disk figures measured |
| 0993 | Bug severity and response policy | done | PROJECT_POLICY.md |
| 0994 | Contributor decision-making process | done | PROJECT_POLICY.md |
| 0995 | Security disclosure expectations | partial | says what to do; a real contact address is the owner's choice |
| 0997 | Stable-versus-preview release criteria | done | PROJECT_POLICY.md |
| 0998 | Transparent cloud-cost policy | done | PROJECT_POLICY.md "Money" |
| 1000 | Success metrics around active creators | done | PROJECT_POLICY.md |
| 0824 | Release-blocking severity policy | done | bug severity table |

## v5 batch 2 (17 Oct 2026): small checks

| ID | Item | Status | Evidence / gap |
|---|---|---|---|
| 0882 | Text contrast validation | partial | `testPaletteTextContrast`: bright, muted, blue, green and amber text reach 4.5:1 on all panel colours; red on app background and panels; faint text 3:1. Known gaps: faint text and red text on raised controls (under 4.5:1) |
| 0005 | Clear pre-beta warning and crash-safe reset | done | Studio welcome panel shows an amber pre-beta note; View > Reset Layout to Default exists |
| 0881 | Color-blind friendly status palette | partial | Checked with deuteranopia/protanopia simulation: green, amber and blue status colours are nearly the same brightness (contrast 1.0-1.3:1), so status must never rely on colour alone. A post-process colour-blind mode exists. Gap: audit status UI for text/icon backup |

## v5 batch 3 (17 Oct 2026): 50 items

Most are small tested libraries that are not in the UI yet; the status column says so.

| ID | Item | Status | Evidence / gap |
|---|---|---|---|
| 0361 | Normalize and loudness analysis | partial | core/AudioAnalysis.hpp + tests; not in an audio UI yet |
| 0374 | Waveform cache and peaks generation | partial | waveformPeaks tested; no cache or drawing yet |
| 0392 | Audio asset compression presets | partial | preset table tested; no exporter uses it yet |
| 0114 | Normals display and recalculation | partial | core/MeshOps.hpp recalculateNormals tested; not in Studio UI |
| 0111 | Solidify and shell modifier | partial | MeshOps solidify tested; not in Studio UI |
| 0117 | Pivot and origin editing | partial | MeshOps recenter tested; not in Studio UI |
| 0124 | Mesh compare and diff view | partial | MeshOps compare tested; no diff view |
| 0171 | Sprite sheet slicing on import | partial | core/SpriteSheet.hpp tested; no import step |
| 0314 | Quaternion-safe rotation editing | partial | core/RotationEdit.hpp tested; Inspector not switched over |
| 0092 | Surface alignment placement tool | partial | alignToSurface tested; no placement tool |
| 0489 | Import warning aggregation | partial | migration/ImportWarnings.hpp tested; importer not switched over |
| 0611 | Join tokens with expiry and single use | partial | net/JoinToken.hpp tested; not used by the server yet |
| 0539 | Content reporting with reason codes | partial | core/ReportReasons.hpp tested; no report UI or backend |
| 0414 | Module dependency graph | partial | core/ModuleGraph.hpp finds require() and cycles, tested; no UI |
| 0574 | Safe prerelease migration backups | partial | core/MigrationBackup.hpp tested; not called by the loader yet |
| 0016 | Keyboard shortcut discovery overlay | done | F1 / View / Help; screen-checked |
| 0047 | Focus mode hides nonessential chrome | done | F10; screen-checked |
| 0048 | Presentation mode for screen recording | partial | Shift+F10 hides panels and ribbon, Esc leaves; not screen-checked |
| 0035 | Compact and spacious density modes | partial | View > Density, saved; not screen-checked |
| 0321 | Animation compression settings | partial | core/AnimationReduce.hpp reduceKeys tested; no setting yet |
| 0396 | Music state and snapshot transitions | partial | core/MusicStates.hpp tested; not wired to Audio |
| 0267 | Color grading LUT support | partial | core/ColorLut.hpp parses .cube, tested; renderer doesn't use it |
| 0146 | Splatmap import support | partial | splatWeights tested; no importer |
| 0136 | Procedural biome rules | partial | pickBiome tested; terrain doesn't call it |
| 0142 | Terrain layers with distance blending | partial | distanceBlend tested; terrain doesn't call it |
| 0261 | Height fog and distance fog controls | partial | heightFogAmount tested; shader not changed |
| 0339 | Camera shake library with envelopes | partial | ShakeEnvelope/shakeOffset tested; not in Application |
| 0289 | Wind and turbulence forces | partial | WindForce tested; particles don't use it |
| 0367 | Stereo and surround channel routing | partial | downmixToStereo tested; mixer unchanged |
| 0242 | Render backend capability report | done | engine_runtime --gpu-info; run on this PC |
| 0346 | Hardware ray-tracing capability probe | done | --gpu-info reports it; detectRayTracing tested |
| 0449 | Python API compatibility versioning | done | world.API_VERSION = 1, in --python-info; tested |
| 0189 | Thumbnail contact sheets | partial | contactSheetLayout tested; no renderer |
| 0186 | Metadata tags and custom fields | partial | AssetTagSet tested; not stored with assets |
| 0236 | GPU memory budget manager | partial | GpuMemoryBudget tested; renderer doesn't report to it |
| 0542 | Rejoin last server action | partial | RecentServers tested; no button |
| 0217 | Material batching compatibility hints | partial | batchingHints tested; no UI |
| 0071 | Component dependency warnings | partial | core/ComponentRules.hpp tested; Inspector doesn't show them |
| 0025 | Accessibility audit checklist | done | docs/ACCESSIBILITY.md |
| 0474 | Plugin API deprecation policy | done | docs/PLUGIN_API.md |
| 0471 | Native versus sandboxed plugins | done | table in PLUGIN_API.md; Resources panel already labels each plugin |
| 0616 | Bring-your-own-host deployment docs | done | docs/SERVER.md |
| 0617 | Kubernetes evaluation | done | docs/SERVER.md: not now, with the conditions |
| 0571 | CI-generated compatibility report | partial | Linux job writes it to the run summary; first run not seen yet |
| 0499 | Diagnostic mode toggles | partial | docs/TROUBLESHOOTING.md lists the commands |
| 0014 | In-app release notes and migration hints | partial | Help > What's New; hints are minimal |
| 0521 | Asset-license provenance report | partial | provenanceReport tested; nothing records licences yet |
| 0546 | Save-data status feedback | partial | saveStatusText tested; not shown in a status bar |
| 0549 | First-launch telemetry opt-in | partial | policy written (off until yes); nothing is collected |
| 0699 | Collaboration disabled fallback | done | PROJECT_POLICY.md |

Already existed (checked, not built): 0061 property filter, 0596 remote rate limit, 0589 loss simulator, 0514 tag import, 0023 accent, 0024 reduced motion, 0013 notification centre, 0010 command palette.

## v5 batch 4 (17 Oct 2026): docs and small helpers

| ID | Item | Status | Evidence / gap |
|---|---|---|---|
| 0846 | Third-party license inventory | done | THIRD_PARTY_LICENSES.md (NOTICE file in the download still to do) |
| 0845 | Secure dependency update process | done | SECURITY_PROCESS.md |
| 0847 | Security contact and disclosure process | partial | SECURITY_PROCESS.md; a real e-mail address is the owner's choice |
| 0850 | Security checklist for each release | done | SECURITY_PROCESS.md |
| 0849 | Permission prompt for external integrations | partial | rules written; nothing external to prompt for yet |
| 0840 | Anti-cheat authority boundary documentation | done | SECURITY_PROCESS.md table |
| 0844 | Data retention and deletion policy | partial | DATA_AND_PRIVACY.md proposals for the owner; deletion jobs not built |
| 0843 | Age-aware privacy design review | partial | DATA_AND_PRIVACY.md; under-13 rule is the owner's and needs legal advice |
| 0842 | User reporting and appeal workflow | partial | DATA_AND_PRIVACY.md workflow; report button not built |
| 0770 | Known limitations page with severity | done | LIMITATIONS.md |
| 0991 | Maintain public known limitations page | done | LIMITATIONS.md linked from README index |
| 0987 | Publish engine architecture overview | done | ARCHITECTURE_OVERVIEW.md |
| 0990 | Reproducible showcase benchmark methodology | done | BENCHMARKS.md |
| 0874 | Optimization before-after benchmark snapshots | partial | BENCHMARKS.md method + benchmarkVerdict tested; no results recorded |
| 0864 | Cold-start time benchmark | partial | method in BENCHMARKS.md; not measured yet |
| 0865 | Hot-start time benchmark | partial | method in BENCHMARKS.md; not measured yet |
| 0943 | Platform capability matrix | done | PLATFORMS.md |
| 0946 | Graceful unsupported-feature fallback | partial | rule + current fallbacks listed in PLATFORMS.md |
| 0937 | Console architecture assessment document | done | PLATFORMS.md |
| 0939 | Gamepad certification checklist draft | done | PLATFORMS.md |
| 0936 | Web build CI experiment | partial | plan only in PLATFORMS.md |
| 0949 | Browser cache versioning strategy | done | PLATFORMS.md |
| 0911 | Mobile asset compression profiles | partial | assetProfiles table tested; no exporter |
| 0924 | Mobile player onboarding | partial | notes only in PLATFORMS.md |
| 0950 | Portability risk register | done | PLATFORMS.md |
| 0686 | Shared undo history semantics | partial | design in COLLABORATION_DESIGN.md; collaboration not built |
| 0689 | Invite revocation and access expiry | partial | design only |
| 0692 | Follow collaborator camera mode | partial | design only |
| 0696 | Collaboration bandwidth simulator | partial | design only; reuses the network conditioning menu |
| 0774 | Learning path for programmers | done | LEARNING_PATH.md |
| 0775 | Sample asset licensing manifest | done | SAMPLES.md |
| 0721 | Plugin API sample gallery | done | SAMPLES.md |
| 0724 | API deprecation and migration guide | done | MIGRATION_GUIDE.md + PLUGIN_API.md policy |
| 0767 | Searchable offline docs | partial | docs/README.md index; plain Markdown is greppable; no in-app search |
| 0769 | Troubleshooting decision tree | done | TROUBLESHOOTING.md |
| 0897 | WCAG-inspired UI checklist | done | ACCESSIBILITY.md; boxes unticked until checked on screen |
| 0896 | Accessible error message wording | done | ACCESSIBILITY.md rules; existing messages not audited |
| 0871 | Scalable quality settings by GPU tier | partial | recommendedQuality tested; renderer doesn't use it |
| 0921 | Scaled physics budget for low-end CPUs | partial | physicsBudgetFor tested; physics doesn't use it |
| 0888 | Reduced flashing effect mode | partial | FlashLimiter tested; effects don't use it |
| 0889 | Motion-sickness camera options | partial | CameraComfort tested; camera doesn't use it |
| 0914 | Low-memory asset eviction | partial | LruAssetCache tested; loader doesn't use it |
| 0421 | Script profiler flame graph | partial | FlameNode aggregation and folded output tested; no sampler or drawing |
| 0856 | Script execution flame graph | partial | same as 0421 |
| 0399 | Audio profiler with active voice graph | partial | VoiceHistory tested; no panel |
| 0867 | GPU memory high-water-mark view | done | Stats panel shows 'Highest so far' |
| 0364 | Reverb delay chorus and saturation presets | partial | preset table and saturate() tested; mixer doesn't use them |
| 0861 | Entity count and component count stats | partial | Stats shows scene object count; component count not shown |
| 0010 | Searchable command palette | done | already built (Ctrl+K), verified in code |
| 0013 | Global notification center | done | already built (NotificationCenter) |
| 0021 | UI frame-time overlay | done | already built (F3 overlay, Stats) |
| 0023 | Customizable accent colors | done | already built (StudioAccent) |
| 0067 | Reparent and duplicate with undo | done | built this week (reparent undo, Ctrl+D) |
| 0564 | Project packaging manifest | done | already built (game package manifest) |
| 0589 | Packet loss and latency simulator | done | already built (ENetTransport conditioning) |
| 0461 | Rust panic containment at ABI boundary | done | already built (catch_unwind in sdk/rust/kronos) |
| 0464 | Rust plugin capability manifest | done | already built (capability bits in plugin info) |
| 0771 | Feature maturity labels | done | MATURITY.md |
| 0989 | Separate shipped features from prototypes | done | MATURITY.md |

## v5 batch 5 (17 Oct 2026): tested helper libraries

| ID | Item | Status | Evidence / gap |
|---|---|---|---|
| 0058 | Bulk rename with patterns | partial | assets::bulkRename ({name},{n},{nn}) tested; no Studio dialog |
| 0066 | Copy entity path or stable ID | partial | assets::entityPath tested; no menu item |
| 0159 | Duplicate asset detection by hash | partial | assets::findDuplicates tested; not run on import |
| 0188 | Content hash verification | partial | assets::verifyHash (FNV-1a 64, not cryptographic) tested |
| 0158 | Stable GUID assignment for imported assets | partial | assets::stableAssetId tested; importer doesn't use it |
| 0180 | Duplicate and orphan asset scanner | partial | assets::scanAssets + findDuplicates tested; no panel |
| 0181 | Dependency graph for assets | partial | transitiveDeps/dependents tested; no graph view |
| 0184 | Asset usage counts by scene | partial | assets::usageCounts tested |
| 0183 | Replace asset across project | partial | assets::replaceAsset tested on reference lists |
| 0198 | Missing asset relink wizard | partial | suggestRelink tested; no wizard |
| 0072 | Missing-reference repair panel | partial | repairSuggestions tested; no panel |
| 0172 | Texture channel packing presets | partial | packPresets + packChannels tested |
| 0173 | Unit scale and axis conversion preview | partial | convertPoint Z-up/Y-up with scale tested |
| 0105 | Vertex merge and weld cleanup | partial | meshops::weld tested |
| 0109 | Mirror modifier with clipping | partial | meshops::mirror tested (no clipping) |
| 0110 | Array and radial duplication modifiers | partial | arrayDuplicate/radialDuplicate tested |
| 0116 | Mesh validation for non-manifold geometry | partial | meshops::validate (holes, 3+ edges, degenerate) tested |
| 0118 | Mesh island selection and separation | partial | meshops::islands tested; no selection UI |
| 0119 | Mesh join with material preservation | partial | meshops::join tested; materials not tracked |
| 0125 | Export selected mesh with clean transforms | partial | meshops::applyTransform tested; no exporter |
| 0120 | LOD generation preview | partial | simplifyByGrid tested; no preview |
| 0115 | UV seam marking | partial | uvSeams tested; no UI |
| 0112 | Subdivision surface modifier | partial | linear subdivision only (no smoothing) |
| 0091 | Pivot editing and reset | partial | meshops::recenter tested |
| 0081 | Orthographic top front side and isometric views | partial | viewport::viewPresets table tested; camera doesn't use it |
| 0085 | Snap to grid and angular increments | partial | snapValue/snapAngleDegrees tested |
| 0093 | Measurement ruler and distance annotations | partial | distance/rulerLabel tested; no tool |
| 0083 | Viewport bookmarks and named cameras | partial | serialize/parseBookmarks tested; no UI |
| 0087 | Box lasso and paint selection | partial | pointInPolygon/pointInBox tested; no tool |
| 0088 | Occlusion-aware selection | partial | pickNearestVisible tested |
| 0090 | Transform gizmo coordinate-space toggle | partial | gizmoDeltaToWorld (yaw only) tested |
| 0098 | Gizmo hit-target accessibility | partial | gizmoHitRadius tested |
| 0096 | Camera clip-plane controls | partial | clampClipPlanes tested; no controls |
| 0097 | Near-camera clipping diagnostics | partial | nearClipped tested |
| 0909 | Safe-area layout support | partial | applySafeArea tested |
| 0134 | Spline-based road and river tools | partial | sampleSpline/roadEdges tested; no tool |
| 0135 | Landscape stamps and reusable brushes | partial | terrain::stamp tested |
| 0137 | Scatter vegetation along slope and masks | partial | terrain::scatter (seeded, height/slope/density) tested |
| 0138 | Foliage instancing with density controls | partial | scatter density tested; no instancing |
| 0139 | Runtime terrain carving API | partial | terrain::carve on a heightfield tested; not on the real Terrain or Luau |
| 0140 | Terrain collision mesh generation | partial | collisionMesh tested; physics doesn't use it |
| 0147 | Terrain export to heightmap and meshes | partial | 16-bit heightmap export/import tested |
| 0149 | Terrain boundary stitching | partial | stitchRightLeft tested |
| 0144 | Terrain streaming diagnostics | partial | streamingSummary tested |
| 0218 | Procedural noise and gradient nodes | partial | valueNoise/fbm/gradient tested; no shader nodes |
| 0209 | Triplanar mapping node | partial | triplanarWeights tested |
| 0210 | Vertex-color material blending | partial | heightBlend tested |
| 0219 | Water material preset with depth fade | partial | waterAt tested |
| 0220 | Toon and stylized shading templates | partial | toonRamp tested |
| 0265 | Automatic exposure controls | partial | adaptExposure tested; renderer unchanged |
| 0266 | Bloom and tone mapping presets | partial | reinhard/acesFilm/bloomPresets tested |
| 0244 | Dynamic resolution scaling option | partial | adjustRenderScale tested; renderer unchanged |
| 0245 | Render scale presets | partial | renderScalePresets tested |

## v5 batch 6 (17 Oct 2026): animation, camera, VFX and audio helpers

| ID | Item | Status | Evidence / gap |
|---|---|---|---|
| 0308 | Root-motion extraction and preview | partial | anim::rootDeltas/stripRootMotion tested; no preview |
| 0310 | Foot planting and slope correction | partial | solveTwoBone + pelvisDrop tested; not connected to the rig |
| 0312 | Timeline markers and events | partial | anim::MarkerTrack (fires once, handles loops) tested |
| 0313 | Animation curve editor | partial | bezier and keyframe evaluation tested; no editor UI |
| 0315 | Motion capture cleanup tools | partial | smoothSamples/removeSpikes tested |
| 0318 | Pose library and pose mirroring | partial | mirrorPose/mirrorBoneName tested; no library UI |
| 0320 | Ragdoll-to-animation blending | partial | recoverBlend tested |
| 0322 | Runtime animation LOD | partial | animationUpdateInterval tested; animator doesn't use it |
| 0323 | Retargeting validation report | partial | validateRetarget tested |
| 0330 | Lens focal length and aperture animation | partial | fovFromFocalLength/focalLengthFromFov tested |
| 0331 | Depth-of-field preview | partial | circleOfConfusion tested; no preview |
| 0338 | Camera rig presets for dolly orbit and crane | partial | dolly/orbit/crane poses tested |
| 0340 | Focus target and rack-focus tool | partial | rackFocus tested |
| 0341 | Marker and annotation tracks | partial | MarkerTrack tested |
| 0334 | Material and post-effect keyframes | partial | evaluateTrack (step/linear/smooth) tested |
| 0333 | Lighting and time-of-day tracks | partial | sunElevationDegrees tested |
| 0264 | Day-night timeline component | partial | sunElevationDegrees and skyPresets tested; TimeOfDay not changed |
| 0262 | Sky atmosphere presets | partial | skyPresets table tested |
| 0268 | Light linking and exclusion groups | partial | lightAffects mask tested; renderer doesn't use it |
| 0272 | Per-light performance budget | partial | pickShadowCasters tested |
| 0275 | Lighting regression comparison captures | partial | imageMse/imagePsnr tested; no capture flow |
| 0337 | Scene shot list and storyboard board | partial | shotListMarkdown tested |
| 0342 | Timeline nested sequences | partial | flattenSequences (depth-limited) tested |
| 0344 | Render queue with resumable jobs | partial | serialize/parse queue tested; no renderer |
| 0348 | EXR PNG and image-sequence export | partial | frameFileName only; no exporter |
| 0350 | Frame-accurate render tests | partial | frame/time/timecode maths tested |
| 0283 | Deterministic seeded particle playback | partial | vfx::SeededRng tested |
| 0284 | Particle LOD and distance culling | partial | particleLodScale tested |
| 0281 | Flipbook sprite particles | partial | flipbookFrame tested |
| 0286 | Sub-emitter support | partial | subEmitterSpawns tested |
| 0287 | Beam and laser effects | partial | beamPoints tested |
| 0294 | Effect pooling for runtime | partial | EffectPool tested |
| 0295 | VFX budget overlay | partial | vfxWarnings tested; no overlay |
| 0297 | GPU overflow diagnostics | partial | vfxWarnings covers particle counts; GPU buffers not covered |
| 0298 | Lightweight mobile particle profile | partial | vfxProfiles table |
| 0291 | Particle event hooks for scripts | partial | ParticleEvents tested; not exposed to Luau |
| 0296 | Asset presets for fire smoke and magic | partial | effectPresets table |
| 0355 | Clip gain and track gain controls | partial | combinedGain tested |
| 0356 | Loop and region playback | partial | audiotools::advance tested |
| 0358 | Marker and cue regions | partial | MarkerTrack tested |
| 0359 | Sample slicing and transient detection | partial | detectTransients tested; no slicing |
| 0363 | EQ and compressor effect rack | partial | peakingEq/highPass/compressorGainDb tested; no rack |
| 0366 | Bus and send routing | partial | busChainGain tested |
| 0368 | Audio clip crossfade editing | partial | crossfadeGains tested |
| 0369 | Sample-rate conversion quality report | partial | resampleLinear + resampleQualityNote tested |
| 0375 | Audio export stems and mixdown | partial | mixdown tested; no export |
| 0381 | Audio zones and reverb regions | partial | zoneReverbAt tested |
| 0385 | Audio ducking and sidechain control | partial | Ducker tested |
| 0390 | Audio pooling and voice stealing | partial | VoiceAllocator tested; mixer doesn't use it |
| 0391 | Priority-based voice limits | partial | VoiceAllocator tested |
| 0394 | Master limiter to prevent clipping | partial | softLimit tested; mixer unchanged |
| 0395 | Loudness meter and peak warning | partial | PeakMeter tested |
| 0386 | Low-latency voice buffer tuning | partial | periodFramesForLatency tested |
| 0389 | Sample-accurate scheduled playback | partial | secondsToFrames; engine cues already schedule by frame |

## v5 batch 7 (17 Oct 2026): network, script, input helpers; design notes; scripts

| ID | Item | Status | Evidence / gap |
|---|---|---|---|
| 0585 | Network interpolation buffers | partial | net/NetTools.hpp InterpolationBuffer tested; client doesn't use it |
| 0587 | Reliable ordered and unreliable message channels | partial | SequenceFilter tested (wrap-around); ENet already has channels |
| 0588 | Bandwidth budget per connection | partial | TokenBucket tested; not applied to connections |
| 0590 | Network congestion and backoff policy | partial | CongestionController (AIMD) tested |
| 0591 | Clock synchronization and drift measurement | partial | estimateClockOffset tested; drift not measured |
| 0594 | Disconnect and reconnect state recovery | partial | ReconnectTable tested; state not restored |
| 0595 | Replication schema compatibility checks | partial | checkSchemas tested |
| 0598 | Packet capture redaction and replay | partial | redactCapture tested; no capture tool |
| 0600 | Server-client desync detector | partial | firstDesync tested; peers don't exchange hashes |
| 0620 | Server version compatibility gate | partial | versionsCompatible tested; join doesn't check it |
| 0563 | Player Studio version compatibility checks | partial | same version functions |
| 0605 | Server health and readiness endpoints | partial | healthJson tested; server has no HTTP endpoint |
| 0593 | Networked physics ownership transfer | partial | grantOwnership tested; not wired |
| 0584 | Deterministic tick scheduler validation | partial | TickScheduler tested (60 ticks however frames split) |
| 0839 | Rate limits for chat and RPC | partial | TokenBucket tested; RemoteEvent rate limit already exists, chat not wired |
| 0580 | Entity ownership and authority rules | done | SECURITY_PROCESS.md and DESIGN_NOTES.md |
| 0618 | Secrets injection without committing credentials | done | DESIGN_NOTES.md; scripts/release-check.sh scans for key shapes |
| 0625 | Local-first deploy preview without cloud charges | done | DESIGN_NOTES.md docker steps |
| 0405 | Go to definition and references | partial | scripttools::findIdentifierUses tested; no editor UI |
| 0406 | Find all script usages | partial | same, per script |
| 0425 | Safe script refactoring tools | partial | renameIdentifier tested (refuses clashes); no UI |
| 0410 | Script formatting on save | partial | formatOnSave tested; editor doesn't call it |
| 0411 | Configurable lint rules | partial | scripttools::lint with switchable rules tested; no UI |
| 0409 | Script project tree and filter | partial | filterNames tested |
| 0565 | Exclude source files and editor-only assets on publish | partial | includeInPublish tested; publisher doesn't call it |
| 0430 | Python-to-Luau API parity matrix | done | DESIGN_NOTES.md |
| 0431 | Cross-language entity handle type | done | DESIGN_NOTES.md (entity ids are integers) |
| 0433 | Python coroutine and async design review | done | DESIGN_NOTES.md |
| 0434 | Deterministic Python execution constraints | done | DESIGN_NOTES.md |
| 0438 | Python package allowlist policy | done | DESIGN_NOTES.md (none allowed) |
| 0439 | Project-local pure-Python module packaging | partial | design only |
| 0440 | Dependency lock manifest for allowed packages | done | not needed while no packages are allowed |
| 0444 | Luau-to-Python data conversion rules | partial | rules written; no bridge |
| 0445 | Shared event bus between script languages | partial | design + JSON schema; not built |
| 0467 | Language-agnostic event bus | partial | same |
| 0446 | Cross-language call tracing | partial | trace id in the schema; not built |
| 0472 | Polyglot debugging correlation IDs | partial | same |
| 0455 | Rust SDK safe wrapper audit | partial | checklist written; audit not done |
| 0456 | Rust plugin build templates for Windows Linux and macOS | partial | commands documented; Windows and macOS builds not tried |
| 0466 | Shared JSON schema for cross-language messages | done | docs/schemas/message.schema.json |
| 0469 | Generated Rust bindings from API schema | partial | plan only |
| 0470 | Generated Python API stubs from API schema | partial | plan only |
| 0473 | C++ Rust Python Luau API conformance suite | partial | plan only |
| 0834 | Untrusted project safe-open mode | partial | design only |
| 0823 | Crash reproduction capture format | partial | format written; reports don't include all sections yet |
| 0825 | Test flake tracking dashboard | partial | table in DESIGN_NOTES.md; no dashboard |
| 0863 | Low-end reference hardware profile | partial | target written; not measured |
| 0880 | Hold versus toggle accessibility options | partial | HoldOrToggle tested; no setting |
| 0885 | Captions and subtitle style controls | partial | CaptionStyle clamp tested |
| 0535 | Accessible subtitles and captions | partial | style settings only; no caption system |
| 0886 | Scalable UI fonts | partial | clampUiScale/suggestedUiScale tested; fonts unchanged |
| 0006 | UI scale and text size preferences | partial | same helpers |
| 0887 | Large cursor and focus indicators | partial | FocusStyle clamp tested |
| 0890 | Controller glyph system by device | partial | glyphFor table tested |
| 0891 | Input action map per context | partial | ActionMap tested |
| 0892 | Multi-device simultaneous input handling | partial | DeviceTracker tested |
| 0893 | Virtual joystick template | partial | virtualStick maths tested; no touch UI |
| 0894 | Touch gesture customization | partial | classifyGesture tested |
| 0908 | Gesture input API | partial | same |
| 0895 | Accessibility options saved per user | partial | serialize/parseSettings tested; not stored in the profile |
| 0898 | Input latency measurement tool | partial | latencyStats tested; no capture tool |
| 0910 | Battery and thermal-aware quality hints | partial | qualityHint tested |
| 0912 | Background pause and audio policy | partial | shouldPauseAudio tested |
| 0925 | Battery-safe frame-rate caps | partial | frameRateCap tested |
| 0572 | Release checklist automation | done | scripts/release-check.sh (run; it found nothing wrong apart from uncommitted files) |
| 0573 | Changelog generation from labels | partial | scripts/changelog.sh groups by first word of the commit message |
| 0815 | Fuzz test asset parsers | partial | 150 damaged copies of a place parsed in tests; binary parser already had damage tests |
| 0523 | Safe handling of malformed place files | done | damage tests for XML and binary places |
| 0835 | Zip path traversal protection | done | hostile package archive tests (16 Oct) |
| 0838 | Server-side validation for remote calls | done | rate limit, NaN/inf refused, hidden Instance args become nil |
| 0448 | Python sandbox escape regression tests | done | capability report probes asserted blocked in testPythonScripts |
| 0462 | Versioned API structs with size fields | done | struct_size in every plugin API struct |
| 0475 | Example plugin proving end-to-end workflow | done | SampleCPlugin.c and engine tests run it sandboxed and trusted |
