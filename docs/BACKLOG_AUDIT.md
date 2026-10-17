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

## v5 batch 8

| ID | Item | Status | Evidence / gap |
|---|---|---|---|
| 0017 | Recent projects with thumbnails | partial | EditorTools.hpp, testV5Batch8; library only, not wired into Studio yet |
| 0018 | Project health dashboard on startup | partial | EditorTools.hpp, testV5Batch8; library only, not wired into Studio yet |
| 0019 | Recover unsaved work after crash | partial | EditorTools.hpp, testV5Batch8; library only, not wired into Studio yet |
| 0033 | Persistent workspace per project | partial | EditorTools.hpp, testV5Batch8; library only, not wired into Studio yet |
| 0055 | Filter hierarchy by component | partial | EditorTools.hpp, testV5Batch8; library only, not wired into Studio yet |
| 0056 | Favorites and pinned entities | partial | EditorTools.hpp, testV5Batch8; library only, not wired into Studio yet |
| 0063 | Numeric property scrub controls | partial | EditorTools.hpp, testV5Batch8; library only, not wired into Studio yet |
| 0065 | Copy and paste component values | partial | EditorTools.hpp, testV5Batch8; library only, not wired into Studio yet |
| 0068 | Lock hide and solo entities | partial | EditorTools.hpp, testV5Batch8; library only, not wired into Studio yet |
| 0069 | Scene tree diff after import | partial | EditorTools.hpp, testV5Batch8; library only, not wired into Studio yet |
| 0084 | Toggle grid axes and safe guides | partial | EditorTools.hpp, testV5Batch8; library only, not wired into Studio yet |
| 0095 | Viewport statistics overlay | partial | EditorTools.hpp, testV5Batch8; library only, not wired into Studio yet |
| 0100 | Viewport interaction recording tests | partial | EditorTools.hpp, testV5Batch8; library only, not wired into Studio yet |
| 0868 | Asset streaming hitch detector | partial | PerfTools.hpp, testV5Batch8; library only, not fed by the live engine yet |
| 0869 | Shader compile hitch detector | partial | PerfTools.hpp, testV5Batch8; library only, not fed by the live engine yet |
| 0873 | Frame pacing and missed-deadline report | partial | PerfTools.hpp, testV5Batch8; library only, not fed by the live engine yet |
| 0860 | Render pass cost waterfall | partial | PerfTools.hpp, testV5Batch8; library only, not fed by the live engine yet |
| 0858 | Network bandwidth profiler | partial | PerfTools.hpp, testV5Batch8; library only, not fed by the live engine yet |
| 0855 | Physics broadphase and contact metrics | partial | PerfTools.hpp, testV5Batch8; library only, not fed by the live engine yet |
| 0872 | Batch and instancing efficiency report | partial | PerfTools.hpp, testV5Batch8; library only, not fed by the live engine yet |
| 0875 | Performance regression budgets in CI | partial | PerfTools.hpp, testV5Batch8; library only, not fed by the live engine yet |
| 0862 | Large-scene benchmark suite | partial | PerfTools.hpp, testV5Batch8; library only, not fed by the live engine yet |
| 0156 | Reimport when source files change | partial | ContentTools.hpp, testV5Batch8; library only, not wired into the UI yet |
| 0160 | Import conflict resolution UI | partial | ContentTools.hpp, testV5Batch8; library only, not wired into the UI yet |
| 0196 | Asset pack creation wizard | partial | ContentTools.hpp, testV5Batch8; library only, not wired into the UI yet |
| 0197 | Reusable project-level asset aliases | partial | ContentTools.hpp, testV5Batch8; library only, not wired into the UI yet |
| 0205 | Material instance parameters | partial | ContentTools.hpp, testV5Batch8; library only, not wired into the UI yet |
| 0223 | Exportable material presets | partial | ContentTools.hpp, testV5Batch8; library only, not wired into the UI yet |
| 0212 | Shader include and reusable function library | partial | ContentTools.hpp, testV5Batch8; library only, not wired into the UI yet |
| 0215 | Per-platform shader variant selection | partial | ContentTools.hpp, testV5Batch8; library only, not wired into the UI yet |
| 0240 | Shader permutation budget enforcement | partial | ContentTools.hpp, testV5Batch8; library only, not wired into the UI yet |
| 0224 | Shader documentation generated from nodes | partial | ContentTools.hpp, testV5Batch8; library only, not wired into the UI yet |
| 0230 | GPU frustum culling pass | partial | ContentTools.hpp, testV5Batch8; library only, not wired into the UI yet |
| 0547 | Downloaded content cache manager | partial | ContentTools.hpp, testV5Batch8; library only, not wired into the UI yet |
| 0923 | Offline cache quota controls | partial | ContentTools.hpp, testV5Batch8; library only, not wired into the UI yet |
| 0709 | Plugin dependency resolution | partial | ContentTools.hpp, testV5Batch8; library only, not wired into the UI yet |
| 0723 | Plugin permission review UI | partial | ContentTools.hpp, testV5Batch8; library only, not wired into the UI yet |
| 0710 | Plugin configuration persistence | partial | ContentTools.hpp, testV5Batch8; library only, not wired into the UI yet |
| 0722 | Safe plugin update and rollback | partial | ContentTools.hpp, testV5Batch8; library only, not wired into the UI yet |
| 0533 | Keybind remapping menu | partial | ContentTools.hpp, testV5Batch8; library only, not wired into the UI yet |
| 0534 | Per-game audio settings | partial | ContentTools.hpp, testV5Batch8; library only, not wired into the UI yet |
| 0538 | Game discovery categories and tags | partial | ContentTools.hpp, testV5Batch8; library only, not wired into the UI yet |
| 0540 | Local parental and privacy settings | partial | ContentTools.hpp, testV5Batch8; library only, not wired into the UI yet |
| 0543 | Server region and ping display | partial | ContentTools.hpp, testV5Batch8; library only, not wired into the UI yet |
| 0544 | Join friend flow with permission controls | partial | ContentTools.hpp, testV5Batch8; library only, not wired into the UI yet |

## v5 batch 9

| ID | Item | Status | Evidence / gap |
|---|---|---|---|
| 0007 | Light dark and high-contrast themes | partial | UiTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0008 | Consistent spacing and typography tokens | partial | UiTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0009 | Unified icon library with clear metaphors | partial | UiTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0011 | Context-sensitive help panel | partial | UiTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0012 | Undo and redo status visibility | partial | UiTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0015 | Empty states with actionable next steps | partial | UiTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0024 | Reduced-motion UI option | partial | UiTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0030 | Draggable tabs with clear drop targets | partial | UiTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0031 | Dock preview before releasing panel | partial | UiTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0032 | Panel search and panel visibility manager | partial | UiTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0034 | Quick switch between Studio Player and Movie Maker | partial | UiTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0036 | Responsive layout for 720p displays | partial | UiTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0037 | No clipped text at common DPI scales | partial | UiTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0038 | DPI-aware icons and hit targets | partial | UiTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0039 | Consistent property row alignment | partial | UiTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0040 | Color tokens for status selection and warnings | partial | UiTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0041 | Shared style guide for all windows | partial | UiTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0042 | Empty panel onboarding | partial | UiTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0043 | Panel-level keyboard shortcuts | partial | UiTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0045 | Pop-out script editor window | partial | UiTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0050 | UI consistency lint rules | partial | UiTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0059 | Bulk component add remove and reset | partial | UiTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0060 | Inspector sections grouped by component | partial | UiTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0061 | Property search and quick focus | partial | UiTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0062 | Mixed-value display for multi-selection | partial | UiTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0064 | Vector and transform gizmos in inspector | partial | UiTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0070 | Entity reference picker | partial | UiTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0073 | Inspector schema generated from reflection metadata | partial | UiTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0074 | Saveable custom inspector layouts | partial | UiTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0075 | Hierarchy performance stress test with 100k entities | partial | UiTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0080 | Walkthrough camera with collision | partial | UiTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0086 | Vertex edge face and object selection modes | partial | UiTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0089 | X-ray and wireframe selection | partial | UiTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0094 | Screenshot capture without editor chrome | partial | UiTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0099 | Multi-viewport split layouts | partial | UiTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0121 | Procedural mesh from Luau parameters | partial | ImportTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0122 | Mesh operation history and replay | partial | ImportTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0123 | Gizmo-based deform brushes | partial | ImportTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0141 | Terrain sculpt undo history | partial | ImportTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0155 | Import report with warnings and skipped fields | partial | ImportTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0161 | glTF 2.0 import quality suite | partial | ImportTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0162 | OBJ and MTL import reliability | partial | ImportTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0166 | SVG and raster image import | partial | ImportTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0167 | HDR EXR and environment map import | partial | ImportTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0168 | Font import with shaping validation | partial | ImportTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0169 | Video clip import metadata extraction | partial | ImportTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0170 | Audio waveform and metadata import | partial | ImportTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0174 | Import sandbox for untrusted parsers | partial | ImportTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0175 | Batch import regression corpus | partial | ImportTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0185 | Version history for source assets | partial | ImportTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0187 | License and attribution metadata | partial | ImportTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0190 | Preview 3D models with lighting presets | partial | ImportTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0191 | Preview audio and video in browser | partial | ImportTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0192 | Preview animation clips on rig | partial | ImportTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0193 | Asset comparison view | partial | ImportTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0194 | Drag assets directly into scene | partial | ImportTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0195 | Download and cache remote assets | partial | ImportTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0199 | Asset database consistency repair | partial | ImportTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0200 | Asset browser keyboard navigation | partial | ImportTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0208 | UV channel preview | partial | ImportTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0211 | Decal authoring and projection bounds | partial | ImportTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0213 | Shader compile cache with diagnostics | partial | ImportTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0214 | Hot reload material graphs | partial | ImportTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0216 | Visualize normals roughness and overdraw | partial | ImportTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0222 | Shader graph version migration | partial | ImportTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0225 | Material graph unit tests | partial | ImportTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0233 | Persistent mapped upload buffer manager | partial | RenderMediaTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0234 | Async texture upload | partial | RenderMediaTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0235 | Async mesh upload | partial | RenderMediaTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0237 | Descriptor allocation diagnostics | partial | RenderMediaTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0238 | Render-thread command queue profiling | partial | RenderMediaTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0239 | Pipeline cache persistence | partial | RenderMediaTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0241 | RenderDoc-friendly debug markers | partial | RenderMediaTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0243 | Swapchain recreation reliability | partial | RenderMediaTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0246 | GPU timing queries with graceful fallback | partial | RenderMediaTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0249 | Renderer feature flags per driver | partial | RenderMediaTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0250 | Render correctness test scenes | partial | RenderMediaTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0263 | Procedural sky and cloud layers | partial | RenderMediaTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0269 | Light bake preview and invalidation | partial | RenderMediaTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0271 | Shadow caster diagnostics | partial | RenderMediaTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0274 | Environment profile assets | partial | RenderMediaTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0280 | Mesh particles | partial | RenderMediaTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0288 | Decal burst emitters | partial | RenderMediaTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0292 | VFX preview window | partial | RenderMediaTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0293 | VFX timeline authoring | partial | RenderMediaTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0299 | VFX capture and thumbnail | partial | RenderMediaTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0311 | Constraint stack for props and limbs | partial | RenderMediaTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0317 | Python animation utilities | partial | RenderMediaTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0319 | Facial blendshape timeline | partial | RenderMediaTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0325 | Animation regression corpus | partial | RenderMediaTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0335 | Audio waveform and sync tracks | partial | RenderMediaTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0336 | Animation and facial tracks | partial | RenderMediaTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0343 | Proxy preview for heavy scenes | partial | RenderMediaTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0349 | MP4 export through bundled or documented encoder | partial | RenderMediaTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0372 | Sound library browser | partial | RenderMediaTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0373 | Batch audio conversion | partial | RenderMediaTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0383 | Streaming long music tracks | partial | RenderMediaTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0393 | Runtime audio capture permission model | partial | RenderMediaTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0397 | Audio events exposed to Luau | partial | RenderMediaTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0398 | Python audio event API | partial | RenderMediaTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0400 | Audio loopback test harness | partial | RenderMediaTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0408 | Multi-file tabs and split panes | partial | DevTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0412 | Static type checking integration | partial | DevTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0413 | Type-aware autocomplete for Instances | partial | DevTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0418 | Evaluate expression in debug console | partial | DevTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0419 | Per-script CPU and memory budgets | partial | DevTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0435 | Per-script memory and time budget UI | partial | DevTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0420 | Hot-reload diff before applying | partial | DevTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0422 | Unit-test runner for Luau | partial | DevTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0442 | Python unit-test harness | partial | DevTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0423 | Script template gallery | partial | DevTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0424 | API compatibility annotations for Roblox imports | partial | DevTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0436 | Python stack traces linked to source lines | partial | DevTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0441 | Python formatter integration where feasible | partial | DevTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0443 | Python type hint documentation generation | partial | DevTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0459 | Ownership-safe scene handle wrapper | partial | DevTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0460 | Typed entity IDs with stale-handle checks | partial | DevTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0458 | Plugin ABI conformance test binary | partial | DevTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0465 | Rust plugin diagnostics and logging macros | partial | DevTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0468 | Language-neutral asset and scene schema | partial | DevTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0481 | Runtime variable watch window | partial | DevTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0483 | Frame-by-frame simulation stepping | partial | DevTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0484 | Physics contact visualization | partial | DevTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0485 | Network replication visualization | partial | DevTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0487 | Asset loading timeline | partial | DevTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0488 | Shader compilation error grouping | partial | DevTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0490 | Copyable diagnostics bundle | partial | DevTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0500 | One-click environment report export | partial | DevTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0491 | Searchable project-wide logs | partial | DevTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0492 | Error panel with source links | partial | DevTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0493 | Recoverable error notification queue | partial | DevTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0494 | Built-in command console with permission checks | partial | DevTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0496 | Server/client correlated logs | partial | DevTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0498 | Debug-only simulation overlays | partial | DevTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0720 | Plugin logging and diagnostic bundle | partial | DevTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0725 | Headless plugin test harness | partial | DevTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0505 | API shims for common services | partial | ProductTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0506 | GUI constraints and layout compatibility | partial | ProductTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0508 | MeshPart and texture preservation | partial | ProductTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0511 | Sound and music mapping report | partial | ProductTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0512 | Lighting and atmosphere mapping | partial | ProductTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0514 | CollectionService tag preservation | partial | ProductTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0515 | RemoteEvent network semantic parity tests | partial | ProductTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0516 | DataStore emulator with clear migration notes | partial | ProductTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0517 | Unsupported API call source report | partial | ProductTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0520 | Batch place conversion CLI | partial | ProductTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0522 | Roblox-version metadata handling | partial | ProductTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0525 | Compatibility report export for marketing claims | partial | ProductTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0536 | Avatar customization saved across games | partial | ProductTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0537 | Avatar preview performance mode | partial | ProductTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0541 | Clear offline mode behavior | partial | ProductTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0545 | Crash-safe game return to home | partial | ProductTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0548 | Player update rollback safety | partial | ProductTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0550 | User-facing privacy and safety center | partial | ProductTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0555 | Clean-machine installer smoke tests | partial | ProductTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0556 | Portable archive validation | partial | ProductTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0559 | Update channel for stable preview and nightly | partial | ProductTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0566 | Asset compression profile per target | partial | ProductTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0567 | Build cache reuse | partial | ProductTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0568 | Build progress and cancel controls | partial | ProductTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0569 | Build error summary with source paths | partial | ProductTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0570 | Headless command-line build mode | partial | ProductTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0575 | Installer uninstall cleanup and preserve-user-data choice | partial | ProductTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0592 | Late join world snapshot recovery | partial | ProductTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0596 | Secure remote-call rate limits | partial | ProductTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0597 | Network fuzzing harness | partial | ProductTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0816 | Fuzz test project manifest loader | partial | ProductTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0684 | Conflict-aware script merge UI | partial | CollabPluginTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0685 | Lock and lease policy for high-risk edits | partial | CollabPluginTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0688 | Permission roles for owner editor viewer | partial | CollabPluginTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0690 | Collaborative asset import status | partial | CollabPluginTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0693 | Change attribution timeline | partial | CollabPluginTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0694 | Session audit log | partial | CollabPluginTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0695 | Offline edits conflict review | partial | CollabPluginTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0697 | Large-scene sync benchmarks | partial | CollabPluginTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0698 | Multi-user test harness | partial | CollabPluginTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0706 | Per-plugin CPU and memory telemetry | partial | CollabPluginTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0708 | Plugin compatibility testing across versions | partial | CollabPluginTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0711 | Plugin command registration API | partial | CollabPluginTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0712 | Menu and toolbar extension points | partial | CollabPluginTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0713 | Inspector custom property editors | partial | CollabPluginTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0714 | Scene gizmo extension API | partial | CollabPluginTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0715 | Asset importer extension API | partial | CollabPluginTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0716 | Exporter extension API | partial | CollabPluginTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0717 | Custom viewport render-pass API | partial | CollabPluginTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0718 | Script language extension API | partial | CollabPluginTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0719 | Project validator plugin API | partial | CollabPluginTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0805 | Studio launch and clean exit test | partial | QaPlatformTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0806 | Import corpus regression runner | partial | QaPlatformTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0808 | GPU renderer golden-image tests | partial | QaPlatformTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0809 | Physics behavior scenario suite | partial | QaPlatformTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0810 | Network simulation integration suite | partial | QaPlatformTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0811 | Script language conformance tests | partial | QaPlatformTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0813 | Save reopen identity tests | partial | QaPlatformTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0814 | Data migration backward-compat tests | partial | QaPlatformTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0817 | Memory leak checks on exit | partial | QaPlatformTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0819 | Windows clean-machine smoke tests | partial | QaPlatformTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0820 | Linux clean-machine smoke tests | partial | QaPlatformTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0821 | Long-running soak test | partial | QaPlatformTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0822 | Automated packaged game launch test | partial | QaPlatformTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0831 | TLS and certificate failure handling | partial | QaPlatformTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0833 | Redaction of tokens from diagnostics | partial | QaPlatformTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0836 | Asset parser resource limits | partial | QaPlatformTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0837 | Backend authorization checks per game | partial | QaPlatformTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0841 | Moderation actions audit trail | partial | QaPlatformTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0859 | Audio voice and CPU meter | partial | QaPlatformTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0870 | UI layout performance trace | partial | QaPlatformTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0883 | Keyboard-only Studio navigation | partial | QaPlatformTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0884 | Screen-reader labels for controls | partial | QaPlatformTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0899 | Focus order regression tests | partial | QaPlatformTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0906 | Touch-first runtime controls | partial | QaPlatformTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0913 | App suspend and resume recovery | partial | QaPlatformTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0915 | Small download game packaging profile | partial | QaPlatformTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0918 | Mobile network handover resilience | partial | QaPlatformTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0920 | Device compatibility report | partial | QaPlatformTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0922 | Shader fallback for older GPUs | partial | QaPlatformTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0930 | Web runtime sandbox boundaries | partial | QaPlatformTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0931 | Browser save-data adapter design | partial | QaPlatformTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0933 | Browser audio autoplay compliance handling | partial | QaPlatformTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0934 | Pointer-lock and fullscreen UX | partial | QaPlatformTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0935 | Web crash and context-loss recovery | partial | QaPlatformTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0938 | Platform abstraction audit | partial | QaPlatformTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0940 | Platform-specific input adapter tests | partial | QaPlatformTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0941 | Cross-platform save migration format | partial | QaPlatformTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0942 | Cross-platform shader variant tests | partial | QaPlatformTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0944 | Shared project format conformance suite | partial | QaPlatformTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0945 | Per-platform packaging profiles | partial | QaPlatformTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0947 | Platform feature flags | partial | QaPlatformTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0948 | Web performance benchmark scenes | partial | QaPlatformTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0766 | API reference generated from reflection | partial | QaPlatformTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0768 | Versioned tutorial matching release | partial | QaPlatformTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0772 | Sample project automated smoke tests | partial | QaPlatformTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0979 | State licensing terms consistently across repository and website | partial | QaPlatformTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0981 | Publish data flow and privacy diagram | partial | QaPlatformTools.hpp, testV5Batch9; library only, not wired into the app yet |
| 0988 | Keep roadmap priorities measurable | partial | QaPlatformTools.hpp, testV5Batch9; library only, not wired into the app yet |

## v5 batch 10

| ID | Item | Status | Evidence / gap |
|---|---|---|---|
| 0076 | Orbit pan and zoom controls with presets | partial | WorldTools.hpp, testV5Batch10; library only. 0107 handles boxes only; 0113 is vertex clustering, not retopology |
| 0077 | Frame selected object shortcut | partial | WorldTools.hpp, testV5Batch10; library only. 0107 handles boxes only; 0113 is vertex clustering, not retopology |
| 0078 | Focus selected bounding box smoothly | partial | WorldTools.hpp, testV5Batch10; library only. 0107 handles boxes only; 0113 is vertex clustering, not retopology |
| 0079 | Fly camera with adjustable speed | partial | WorldTools.hpp, testV5Batch10; library only. 0107 handles boxes only; 0113 is vertex clustering, not retopology |
| 0082 | Perspective lens and field-of-view controls | partial | WorldTools.hpp, testV5Batch10; library only. 0107 handles boxes only; 0113 is vertex clustering, not retopology |
| 0054 | Search hierarchy by name type and tag | partial | WorldTools.hpp, testV5Batch10; library only. 0107 handles boxes only; 0113 is vertex clustering, not retopology |
| 0057 | Rename in place with validation | partial | WorldTools.hpp, testV5Batch10; library only. 0107 handles boxes only; 0113 is vertex clustering, not retopology |
| 0101 | Primitive mesh creation with editable parameters | partial | WorldTools.hpp, testV5Batch10; library only. 0107 handles boxes only; 0113 is vertex clustering, not retopology |
| 0102 | Extrude faces with inset and offset | partial | WorldTools.hpp, testV5Batch10; library only. 0107 handles boxes only; 0113 is vertex clustering, not retopology |
| 0103 | Inset and bevel mesh operations | partial | WorldTools.hpp, testV5Batch10; library only. 0107 handles boxes only; 0113 is vertex clustering, not retopology |
| 0104 | Loop cut and edge slide | partial | WorldTools.hpp, testV5Batch10; library only. 0107 handles boxes only; 0113 is vertex clustering, not retopology |
| 0106 | Knife and plane cut tools | partial | WorldTools.hpp, testV5Batch10; library only. 0107 handles boxes only; 0113 is vertex clustering, not retopology |
| 0107 | Boolean union subtract and intersect | partial | WorldTools.hpp, testV5Batch10; library only. 0107 handles boxes only; 0113 is vertex clustering, not retopology |
| 0108 | Non-destructive modifier stack | partial | WorldTools.hpp, testV5Batch10; library only. 0107 handles boxes only; 0113 is vertex clustering, not retopology |
| 0113 | Decimate and retopology assist | partial | WorldTools.hpp, testV5Batch10; library only. 0107 handles boxes only; 0113 is vertex clustering, not retopology |
| 0126 | Heightmap terrain import and export | partial | WorldTools.hpp, testV5Batch10; library only. 0107 handles boxes only; 0113 is vertex clustering, not retopology |
| 0127 | Editable heightmap terrain brush | partial | WorldTools.hpp, testV5Batch10; library only. 0107 handles boxes only; 0113 is vertex clustering, not retopology |
| 0128 | Smooth raise lower flatten and erosion brushes | partial | WorldTools.hpp, testV5Batch10; library only. 0107 handles boxes only; 0113 is vertex clustering, not retopology |
| 0148 | Erosion simulation with bake step | partial | WorldTools.hpp, testV5Batch10; library only. 0107 handles boxes only; 0113 is vertex clustering, not retopology |
| 0129 | Terrain layer painting with blend masks | partial | WorldTools.hpp, testV5Batch10; library only. 0107 handles boxes only; 0113 is vertex clustering, not retopology |
| 0130 | Runtime terrain chunk streaming | partial | WorldTools.hpp, testV5Batch10; library only. 0107 handles boxes only; 0113 is vertex clustering, not retopology |
| 0133 | Terrain chunk LOD and crack prevention | partial | WorldTools.hpp, testV5Batch10; library only. 0107 handles boxes only; 0113 is vertex clustering, not retopology |
| 0131 | Voxel terrain block editing | partial | WorldTools.hpp, testV5Batch10; library only. 0107 handles boxes only; 0113 is vertex clustering, not retopology |
| 0143 | Cave and overhang workflow | partial | WorldTools.hpp, testV5Batch10; library only. 0107 handles boxes only; 0113 is vertex clustering, not retopology |
| 0145 | World origin rebasing for large maps | partial | WorldTools.hpp, testV5Batch10; library only. 0107 handles boxes only; 0113 is vertex clustering, not retopology |
| 0154 | Import presets per target platform | partial | WorldTools.hpp, testV5Batch10; library only. 0107 handles boxes only; 0113 is vertex clustering, not retopology |
| 0157 | Asset dependency tracking | partial | WorldTools.hpp, testV5Batch10; library only. 0107 handles boxes only; 0113 is vertex clustering, not retopology |
| 0182 | Find references before delete | partial | WorldTools.hpp, testV5Batch10; library only. 0107 handles boxes only; 0113 is vertex clustering, not retopology |
| 0176 | Thumbnail generation cache | partial | WorldTools.hpp, testV5Batch10; library only. 0107 handles boxes only; 0113 is vertex clustering, not retopology |
| 0177 | Search by name tag type and usage | partial | WorldTools.hpp, testV5Batch10; library only. 0107 handles boxes only; 0113 is vertex clustering, not retopology |
| 0178 | Smart collections based on rules | partial | WorldTools.hpp, testV5Batch10; library only. 0107 handles boxes only; 0113 is vertex clustering, not retopology |
| 0179 | User-created asset folders and favorites | partial | WorldTools.hpp, testV5Batch10; library only. 0107 handles boxes only; 0113 is vertex clustering, not retopology |
| 0202 | Material graph validation and error paths | partial | WorldTools.hpp, testV5Batch10; library only. 0107 handles boxes only; 0113 is vertex clustering, not retopology |
| 0201 | Node-based material editor | partial | WorldTools.hpp, testV5Batch10; library only. 0107 handles boxes only; 0113 is vertex clustering, not retopology |
| 0203 | PBR base color metallic roughness pipeline | partial | WorldTools.hpp, testV5Batch10; library only. 0107 handles boxes only; 0113 is vertex clustering, not retopology |
| 0204 | Normal height and occlusion map support | partial | WorldTools.hpp, testV5Batch10; library only. 0107 handles boxes only; 0113 is vertex clustering, not retopology |
| 0207 | Texture sampler and tiling controls | partial | WorldTools.hpp, testV5Batch10; library only. 0107 handles boxes only; 0113 is vertex clustering, not retopology |
| 0226 | Frame graph with explicit resource lifetimes | partial | GfxAnimTools.hpp, testV5Batch10; library only, not wired into the renderer or Studio yet |
| 0227 | GPU resource leak tracking | partial | GfxAnimTools.hpp, testV5Batch10; library only, not wired into the renderer or Studio yet |
| 0228 | Bindless texture capability detection | partial | GfxAnimTools.hpp, testV5Batch10; library only, not wired into the renderer or Studio yet |
| 0229 | Multi-draw indirect path for compatible GPUs | partial | GfxAnimTools.hpp, testV5Batch10; library only, not wired into the renderer or Studio yet |
| 0231 | GPU occlusion culling prototype | partial | GfxAnimTools.hpp, testV5Batch10; library only, not wired into the renderer or Studio yet |
| 0232 | Draw-call and pipeline-state reduction report | partial | GfxAnimTools.hpp, testV5Batch10; library only, not wired into the renderer or Studio yet |
| 0247 | Mesh shader capability-gated experiment | partial | GfxAnimTools.hpp, testV5Batch10; library only, not wired into the renderer or Studio yet |
| 0248 | Clustered geometry culling prototype | partial | GfxAnimTools.hpp, testV5Batch10; library only, not wired into the renderer or Studio yet |
| 0251 | Directional point spot and area light improvements | partial | GfxAnimTools.hpp, testV5Batch10; library only, not wired into the renderer or Studio yet |
| 0252 | Shadow quality presets | partial | GfxAnimTools.hpp, testV5Batch10; library only, not wired into the renderer or Studio yet |
| 0253 | Cascaded shadow-map stability | partial | GfxAnimTools.hpp, testV5Batch10; library only, not wired into the renderer or Studio yet |
| 0256 | Light probe volume workflow | partial | GfxAnimTools.hpp, testV5Batch10; library only, not wired into the renderer or Studio yet |
| 0257 | Reflection probe capture and blending | partial | GfxAnimTools.hpp, testV5Batch10; library only, not wired into the renderer or Studio yet |
| 0258 | Screen-space ambient occlusion | partial | GfxAnimTools.hpp, testV5Batch10; library only, not wired into the renderer or Studio yet |
| 0260 | Volumetric fog prototype | partial | GfxAnimTools.hpp, testV5Batch10; library only, not wired into the renderer or Studio yet |
| 0276 | GPU particle emitter with CPU fallback | partial | GfxAnimTools.hpp, testV5Batch10; library only, not wired into the renderer or Studio yet |
| 0277 | Burst and continuous emission modes | partial | GfxAnimTools.hpp, testV5Batch10; library only, not wired into the renderer or Studio yet |
| 0278 | Particle collision with world and terrain | partial | GfxAnimTools.hpp, testV5Batch10; library only, not wired into the renderer or Studio yet |
| 0279 | Particle trails and ribbons | partial | GfxAnimTools.hpp, testV5Batch10; library only, not wired into the renderer or Studio yet |
| 0282 | Curve editors for size speed and color | partial | GfxAnimTools.hpp, testV5Batch10; library only, not wired into the renderer or Studio yet |
| 0301 | Animation clip browser and preview | partial | GfxAnimTools.hpp, testV5Batch10; library only, not wired into the renderer or Studio yet |
| 0302 | Skeleton hierarchy visualization | partial | GfxAnimTools.hpp, testV5Batch10; library only, not wired into the renderer or Studio yet |
| 0303 | Import humanoid rigs with retargeting | partial | GfxAnimTools.hpp, testV5Batch10; library only, not wired into the renderer or Studio yet |
| 0304 | Avatar retarget profile | partial | GfxAnimTools.hpp, testV5Batch10; library only, not wired into the renderer or Studio yet |
| 0305 | Animation state machine graph | partial | GfxAnimTools.hpp, testV5Batch10; library only, not wired into the renderer or Studio yet |
| 0306 | Blend trees for locomotion | partial | GfxAnimTools.hpp, testV5Batch10; library only, not wired into the renderer or Studio yet |
| 0307 | Animation layers and masks | partial | GfxAnimTools.hpp, testV5Batch10; library only, not wired into the renderer or Studio yet |
| 0309 | Inverse-kinematics target controls | partial | GfxAnimTools.hpp, testV5Batch10; library only, not wired into the renderer or Studio yet |
| 0327 | Track-based timeline with zoom and snapping | partial | GfxAnimTools.hpp, testV5Batch10; library only, not wired into the renderer or Studio yet |
| 0328 | Camera cuts and multi-camera tracks | partial | GfxAnimTools.hpp, testV5Batch10; library only, not wired into the renderer or Studio yet |
| 0329 | Transform keyframes with curve handles | partial | GfxAnimTools.hpp, testV5Batch10; library only, not wired into the renderer or Studio yet |
| 0332 | Motion blur quality controls | partial | GfxAnimTools.hpp, testV5Batch10; library only, not wired into the renderer or Studio yet |
| 0352 | Non-destructive waveform editing | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0353 | Region split trim fade and slip editing | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0354 | Envelope and automation lanes | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0357 | Tempo and time-signature grid | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0360 | Pitch and time-stretch evaluation | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0362 | Noise gate and denoise offline tools | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0365 | Sidechain routing prototype | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0371 | MIDI note editor prototype | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0376 | Audio mixer bus graph | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0377 | Per-category volume sliders | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0378 | 3D positional sound attenuation | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0379 | Directional cone emitters | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0380 | Occlusion and obstruction filtering | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0382 | Doppler shift option | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0387 | Spatial audio headphone mode | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0388 | HRTF capability evaluation | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0404 | Signature help for function calls | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0407 | Rename symbols across scripts | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0415 | Breakpoints and pause execution | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0417 | Call stack and coroutine inspector | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0429 | Typed world API stubs for editor autocomplete | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0432 | Python event subscriptions with cancellation | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0482 | Breakpoint support across script types | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0497 | Reproducible bug report recorder | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0504 | Round-trip preservation of unknown properties | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0507 | UDim and anchor-point edge-case tests | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0513 | Constraints and attachments translation | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0518 | Script rewrite suggestions for unsupported APIs | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0529 | Download resume and integrity verification | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0557 | Dependency manifest per release | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0562 | Rollback after failed update | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0530 | Controller-friendly navigation | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0531 | Gamepad-first UI navigation | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0532 | In-game graphics settings | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0576 | Authoritative server simulation mode | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0577 | State replication priority classes | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0578 | Delta-compressed component replication | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0579 | Interest management by spatial cell | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0581 | Client prediction for character movement | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0582 | Server reconciliation diagnostics | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0586 | Lag compensation for hitscan prototype | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0601 | Local dedicated-server launcher wizard | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0602 | Server process supervisor with graceful restart | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0678 | Presence avatars and cursor positions | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0687 | Session reconnect and snapshot recovery | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0701 | Formal plugin lifecycle documentation | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0702 | Manifest schema with permissions | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0703 | Plugin API capability negotiation | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0705 | Sandboxed plugin process health watchdog | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0707 | Crash isolation and automatic disable | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0832 | Secret scanning in source and logs | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0854 | Allocation hot-spot view | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0876 | Fully remappable keyboard controls | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0877 | Gamepad control remapping | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0878 | Mouse sensitivity presets | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0879 | Invert axis controls | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0901 | Device capability tier detection | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0902 | Dynamic resolution on sustained GPU load | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0903 | Texture memory budget profiles | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0904 | Reduced shadow and post-process modes | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0907 | Virtual thumbstick component | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0929 | Asset bundle size budget for web | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0804 | Player launch and clean exit test | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0807 | Screenshot comparison for editor UI | partial | AudioNetTools.hpp, testV5Batch10; library only, not wired in yet. 0388 is a head-delay model, not HRTF |
| 0004 | Missing dependency installer with repair mode | partial | MiscTools.hpp, testV5Batch10; library only |
| 0020 | First-five-minutes guided tutorial | partial | MiscTools.hpp, testV5Batch10; library only |
| 0022 | Localization-ready UI string catalog | partial | MiscTools.hpp, testV5Batch10; library only |
| 0029 | Multi-monitor window placement restore | partial | MiscTools.hpp, testV5Batch10; library only |
| 0044 | Layout migration between versions | partial | MiscTools.hpp, testV5Batch10; library only |
| 0046 | Multi-window viewport support | partial | MiscTools.hpp, testV5Batch10; library only |
| 0049 | UI screenshot regression suite | partial | MiscTools.hpp, testV5Batch10; library only |
| 0751 | Five-minute first game tutorial | partial | MiscTools.hpp, testV5Batch10; library only |
| 0919 | Mobile profiler counters | partial | MiscTools.hpp, testV5Batch10; library only |
| 0165 | Blender export workflow documentation | partial | docs/BLENDER_WORKFLOW.md, written guide |
| 0683 | Script editing collaboration design | partial | docs/COLLAB_DESIGN_EXTRAS.md, design notes, nothing built |
| 0691 | Voice or text session chat design | partial | docs/COLLAB_DESIGN_EXTRAS.md, design notes, nothing built |
| 0700 | Local-hosted collaboration option | partial | docs/COLLAB_DESIGN_EXTRAS.md, design notes, nothing built |
| 0270 | Emissive material indirect-light contribution research | partial | docs/LIGHTING_RESEARCH.md, research notes, nothing built |
| 0290 | Volumetric fog interaction prototype | partial | docs/LIGHTING_RESEARCH.md, research notes, nothing built |
| 0927 | Browser WebGPU capability probe | partial | docs/WEB_DECISIONS.md, decision records, no web build exists |
| 0928 | Browser fallback renderer decision record | partial | docs/WEB_DECISIONS.md, decision records, no web build exists |
| 0932 | Web networking transport evaluation | partial | docs/WEB_DECISIONS.md, decision records, no web build exists |
| 0762 | Python game scripting sample | partial | samples/python_game, sample script and README; not run by a test yet |

## v5 batch 11

| ID | Item | Status | Evidence / gap |
|---|---|---|---|
| 0606 | Autoscaling policy prototype with limits | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0607 | Region-aware server selection | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0608 | Party-aware matchmaking | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0609 | Skill or latency matchmaking plugin API | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0610 | Capacity-aware queue with timeout | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0612 | Server drain and rolling updates | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0613 | Crash restart budget and alerting | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0614 | Per-instance CPU memory and player metrics | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0615 | Cost estimator before cloud deployment | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0619 | Region failover behavior | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0621 | Matchmaking load-test simulator | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0622 | Queue visibility and estimated wait | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0623 | Reserved private servers | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0624 | Server browser for community-hosted games | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0626 | Versioned public backend API | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0627 | OpenAPI schema and generated clients | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0628 | Developer portal login hardening | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0629 | Per-game environment settings | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0630 | Staging and production environment separation | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0631 | Publish API with resumable uploads | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0632 | Release channel management | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0633 | API rate-limit dashboard | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0634 | Request tracing with correlation IDs | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0635 | Backend migration rollback plan | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0636 | Database schema migration tests | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0637 | Service health dashboard | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0638 | Object storage lifecycle policy | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0639 | Asset upload virus and type checks | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0640 | Per-project API credentials | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0641 | Credential rotation flow | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0642 | Webhook signature verification | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0643 | Idempotency keys for publish requests | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0644 | Audit log for creator actions | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0645 | Backend outage status page | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0646 | Local backend mock for tests | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0648 | Project ownership transfer workflow | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0649 | Export user data package | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0650 | Delete project and account flow with confirmation | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0651 | Versioned DataStore emulator | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0652 | Atomic update operations and conflict tests | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0653 | Per-key rate limits with backoff | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0654 | Persistent data schema migrations | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0655 | Backup and restore per game | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0656 | Cross-server session locking | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0657 | Server-side receipt validation interface | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0658 | Developer-defined currency service template | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0659 | Inventory API with unique item IDs | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0660 | Transaction idempotency protection | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0661 | Audit ledger for item grants and purchases | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0662 | Refund and reversal event handling | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0663 | Fraud and duplication detection hooks | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0664 | Entitlement checks that never trust the client | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0665 | Transparent platform fee policy controls | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0666 | BYO payment-provider adapter interface | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0667 | No payment details stored in the game client | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0668 | Test-mode purchase simulator | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0670 | Economy balance simulation spreadsheet export | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0671 | Developer analytics for purchases | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0673 | Gift and trade permission system | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0674 | Spending limits and parental controls evaluation | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0675 | Marketplace item ownership validation | partial | BackendLogicTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0704 | Signed plugin package metadata | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0726 | Curated featured plugin directory | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0727 | Signed plugin catalog metadata | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0728 | Search plugins by task and compatibility | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0729 | Plugin reviews with abuse controls | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0730 | Verified author badges criteria | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0731 | Plugin version pinning per project | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0732 | Dependency compatibility warnings | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0733 | Private team plugin registry | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0734 | One-click install update and remove | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0735 | Plugin uninstall cleanup preview | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0736 | Report malicious plugin workflow | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0740 | Plugin sandbox threat model review | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0741 | Automated package scanning | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0743 | Plugin changelog and screenshots | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0744 | Plugin download integrity verification | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0745 | Deprecation notices for abandoned plugins | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0749 | Plugin licensing metadata display | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0750 | Export and import plugin sets | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0776 | Opt-in explain selection assistant | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0777 | Generate script from described behavior with preview | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0778 | Explain existing script selection | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0779 | Script error explanation linked to line | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0780 | AI-generated code tests before insertion | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0782 | Procedural level seed suggestions | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0784 | AI-assisted asset tagging local-first option | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0785 | Natural-language scene search | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0787 | AI diff viewer before accepting changes | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0788 | Never silently overwrite user code | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0789 | Provider adapter architecture | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0791 | Cost and token usage meter | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0792 | Redact secrets from prompts | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0793 | AI feature opt-out and disable switch | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0794 | Generated content provenance metadata | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0795 | Asset rights and safety warnings | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0796 | Deterministic fallback when service unavailable | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0797 | Context window budgeting | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0798 | Prompt injection threat model for imported content | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0799 | Benchmark task success on fixtures | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0800 | AI feature telemetry opt-in only | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0951 | Public creator profile page | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0952 | Game page with screenshots and tags | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0953 | Search and filtering for published games | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0954 | Featured collections with transparent rules | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0955 | Recently updated games feed | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0957 | Public demo links and safe launch flow | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0959 | Player feedback survey widget | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0961 | Creator follower and update notifications | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0962 | Community showcase submission workflow | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0963 | Showcase video metadata and thumbnail | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0964 | Verified links to official project downloads | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0966 | Spam and impersonation reporting | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0967 | Developer forum category taxonomy | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0968 | Monthly challenge starter kits | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0970 | Contributor onboarding issue list | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0971 | Release announcement template generator | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0972 | Public compatibility badges backed by tests | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0973 | Creator analytics privacy controls | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0974 | Project attribution and credits page | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
| 0975 | Independent project health metrics dashboard | partial | CommunityAiTools.hpp, testV5Batch11; logic only, the live service or account part needs the owner |
