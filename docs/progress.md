# Kronos Platform — Progress Log

## 2026-08-17 — Kronos Developer Velocity Sprint, part 4: Live Math Evaluation & Multi-Selection Property Inspector

**Real constraint discovered before designing the UI**: Dear ImGui's own
`DragFloat`/`InputFloat` text-edit mode (entered via double-click/
Ctrl+click) parses committed text through a bare `sscanf` --
`imgui_widgets.cpp`'s own source comment states plainly "this is not a
full expression evaluator" -- and never exposes the raw typed text back
to a caller. Confirmed concretely: typing `"180 - 45"` into a real
DragFloat would silently become `180` (sscanf stops at the first
non-numeric character), not evaluate to `135` -- a real, dangerous
silent-truncation risk for a feature explicitly advertised to support
exactly that expression. Real support needs to own the text buffer
itself, which means either patching Dear ImGui internals or a separate
UI surface. Given this session's standing decision to avoid all
simulated mouse/keyboard input for verification (an earlier attempt
visibly interfered with the user's real desktop cursor), a from-scratch
custom low-level drag/text-edit hybrid widget would carry real,
unverifiable interaction risk with no way to catch a subtle bug before
shipping it. Chose the safer, still-real design: a right-click "Enter
Formula" popup, reusing Dear ImGui's ordinary `BeginPopup`/`InputText`
patterns already proven correct elsewhere in this exact codebase
(`CommandPalette`, `ScriptEditorPanel`) rather than inventing new
mouse-interaction logic.

**New `core::evaluateMathExpression()`** (`core/MathExpression.hpp/.cpp`):
a real, pure, small recursive-descent parser (`+ - * / `, unary `+/-`,
parentheses, decimal literals) -- fully headless, zero UI dependency.
`+10`/`*2`/`/3` apply relative to the field's current value
(spreadsheet-style); a leading `-` is a literal negative number, not
"subtract from current" (there's no example of that convention in the
sprint's own ask, and it would collide with plain negative-literal
input); anything else, including `"180 - 45"`, evaluates as a complete
expression that replaces the value outright. **Caught a real dangling-
reference bug via the test suite before it ever reached the UI**: the
internal `Parser` class initially stored `const std::string&`, which
left it pointing at a destroyed temporary for the relative-operator case
(`trimmedText.substr(1)`) -- 6 of the first 26 tests failed immediately,
diagnosed and fixed by having `Parser` own a real copy instead.

**New `studio::Vec3MathExpressionPopup`** (`MathExpressionPopup.hpp/.cpp`):
right-click any Position/Rotation/Scale field to open it -- three
independent per-axis formula inputs (blank = leave that axis
unchanged), Apply/Enter evaluates each via `evaluateMathExpression()`.
Instance-owned state (one popup per field, not global mutable state),
wired into `InspectorPanel` for all three Transform fields, each commit
pushing a real `UndoStack` entry exactly like a drag-gesture commit
already does.

**Multi-Selection Property Inspector**: `InspectorPanel::draw()` now
takes the full `selectedEntities` list, not just the primary selection.
Position edits (both drag-commit and formula-popup) apply as a real
shared delta across every selected entity -- the *exact* semantics
`ViewportPanel`'s own gizmo group-move already established for Sprint
9's "group move" task, deliberately reused rather than inventing a
second convention. Rotation/Scale (and every other component section)
stay primary-selection-only when multi-selected, matching that same
gizmo's own stated scope boundary ("rotating or scaling several objects
together around a shared pivot is a meaningfully different, more
complex feature") -- a real, honest, consistent line, not a silently
incomplete "apply everything to everyone." The Inspector header shows a
clear, explicit note when multiple entities are selected, stating
exactly what does and doesn't apply to the group.

**Verification**: 26 new headless tests for `evaluateMathExpression()`
covering every example in the sprint's own prompt plus precedence,
parentheses, unary minus, division-by-zero rejection, and malformed
input rejection -- 10898/10898 passing (was 10872). All 3 targets
rebuild clean, zero warnings (a linker gap surfaced along the way: the
headless test target links `InspectorPanel.cpp` directly rather than
through `engine_core`, and needed `MathExpressionPopup.cpp` added
alongside it). Manually launched `studio` post-build and confirmed
clean, stable startup. The popup's live interactive feel (right-click
timing, Enter-to-apply across three fields) was not visually verified,
per this session's standing simulated-input policy -- stated plainly
rather than claimed.

**This completes the Kronos Studio "Developer Velocity" Sprint** -- all
4 items (One-Click Package Exporter, Real-Time Visual Performance
Profiler, Viewport Surface Alignment & Snap Controls, Live Math
Evaluation & Multi-Selection Property Inspector) are real and committed.

## 2026-08-17 — Kronos Developer Velocity Sprint, part 3: Viewport Surface Alignment & Snap Controls

**What already existed, confirmed before writing any code**: a real,
working ImGuizmo translate/rotate/scale gizmo with real grid/angle/scale
snapping already wired (`ViewportPanel`'s `gizmoSnapEnabled_`/
`translateSnap_`/`rotateSnapDegrees_`/`scaleSnap_`), applied by ImGuizmo
internally during the drag itself -- and a real, physics-independent
scene raycast (`core::pickEntity()`, `ScenePicking.hpp`) already backing
click-to-select, deliberately not built on `core::Physics::raycast()`
since Studio runs no live Physics outside Play mode. The real gaps were
narrower than the sprint's framing implied: exact preset increments on
the existing snap controls, and a genuinely new drop-to-ground shortcut.

**Drop-to-Ground (End key)**: `core::pickEntity()` gained an optional
`excludeEntity` parameter (default `kNullEntity`, every existing call
site unaffected) -- needed because a downward raycast starting at the
selected entity's own Transform position would otherwise trivially
self-hit its own AABB at distance ~0. New `ViewportPanel::dropSelectedToGround()`
raycasts straight down, excluding the selected entity, and repositions
it so its own mesh's local-space bottom (scaled by `Transform::scale.y`)
rests on the hit surface -- not its raw origin, which would sink a
center-origin mesh like a Box halfway into the ground. Real, stated
scope simplification: only `scale.y` is applied, not the full rotation
(correct for the common unrotated/Y-only-rotated case; a fully general
rotated-AABB solve is a harder, separate problem). Bound to the End key
in the same gated block (`!WantCaptureKeyboard`, hovered, gizmo not
mid-drag) the existing W/E/R shortcuts already use.

**Grid & Angle Snap presets**: the viewport toolbar's single free-form
snap-value drag (which silently swapped between translate/rotate/scale
meaning depending on the *currently active* gizmo mode) is now three
always-visible, independently-toggleable controls -- Grid Snap
(0.25m/1.0m/5.0m dropdown, applies to Translate), Angle Snap
(15°/45°/90° dropdown, applies to Rotate), and Scale Snap (kept as the
original free-form drag, not named in the sprint's own ask). Switching
gizmo modes no longer silently changes what's currently snapping.

**Verification**: `pickEntity()`/`dropSelectedToGround()` need a real
GPU-backed `core::Mesh`/`MeshLibrary` and have no existing headless test
coverage to extend (consistent with this codebase's own established "GPU
code gets structural verification, not pixel verification" precedent,
stated plainly rather than a fabricated coverage claim). Full suite
still 10872/10872 (no regression). All 3 targets rebuild clean, zero
warnings. Manually launched `studio` post-build and confirmed clean,
stable startup.

**Next**: Live Math Evaluation & Multi-Selection Property Inspector.

## 2026-08-17 — Kronos Developer Velocity Sprint, part 2: Real-Time Visual Performance Profiler (F3)

**What already existed, confirmed before writing any code**: a real,
working `core::Profiler` (spike/stall detection, JSON recording) and a
real, always-docked `StatsPanel` with live `ImGui::PlotLines()` graphs
for frame time/draw calls/memory, backed by real `core::PerformanceHistory`
ring buffers -- all from an earlier "Performance Stats & Debug Tools"
pass. `StatsPanel` is drawn unconditionally every frame (no open/close
flag at all), so it wasn't the "F3-toggleable overlay" the sprint asked
for -- left completely untouched to avoid regressing it.

**New `studio::panels::PerformanceOverlayPanel`**
(`PerformanceOverlayPanel.hpp/.cpp`): a real, separate, F3-toggleable
floating window (also reachable from View -> Performance Overlay, the
first genuinely interactive item in that menu -- every other entry
there is a disabled placeholder for a permanent dock panel with no real
toggle yet). Reuses the exact same `core::PerformanceHistory` +
`ImGui::PlotLines()` convention `StatsPanel` already established for its
frame-time graph, plus real draw-call/GPU-memory numbers already in
`core::PerformanceMetrics` (GPU memory is honestly labeled "GPU memory
(VMA)" -- total VMA-tracked allocation, not an isolated VBO-only figure;
no per-usage-type VMA accounting exists in this codebase to split that
out).

**Real, new Lua memory reading**: `core::Scripting::totalUsedMemoryBytes()`
-- the per-script `AllocatorState::used` byte count was already tracked
internally for the per-script memory ceiling, just never exposed
publicly before this. Only meaningful while Studio's Play mode
(`PhysicsPreviewPlugin`, which now owns the one real live
`core::Scripting` instance Studio ever runs, from the packaging sprint's
prerequisite work) is actually running -- the overlay shows a real,
honest "N/A (not Playing)" rather than a fabricated zero otherwise.

**Verification**: 1 new headless test (`totalUsedMemoryBytes()` is 0
before any script loads, and genuinely reflects a real script's real
table allocation afterward) -- 10872/10872 passing (was 10868). All 3
targets rebuild clean, zero warnings. Manually launched `studio`
post-build and confirmed clean, stable startup.

**Next**: Viewport Surface Alignment & Snap Controls (Drop-to-Ground,
Grid/Angle snap presets).

## 2026-08-17 — Kronos Developer Velocity Sprint, part 1: One-Click Package Exporter

**Real prerequisite gap found and closed first**: `core::SceneFile` never
serialized `core::Script` at all -- a real, pre-existing hole from the
prior sprint's Script Editor work (scenes silently lost any authored
script on save/reload). Added real round-trip support: `SceneEntityRecord`
gained `hasScript`/`scriptSource`/`scriptAutoRun`, with a small local
base64 codec in `SceneFile.cpp` (the one field in this line-oriented
text format that can contain embedded newlines). `SceneManager::captureScene()`/
`loadScene()` wired symmetrically -- `scriptId`/`loadedSource` are
deliberately NOT persisted (live-VM bookkeeping, meaningless before a
fresh load re-runs the script).

**New `zlib` system dependency** (`cmake/Dependencies.cmake`, same
`find_package()`-for-system-libraries convention SDL2 already
established) -- no compression library existed anywhere in this
codebase before this pass.

**New `publishing::PackageArchive`** (`PackageArchive.hpp/.cpp`): a
real, custom, zlib-deflate-compressed container format (`KRAR` magic +
per-file `[name][sizes][deflate bytes]`) -- deliberately not a byte-for-
byte standard ZIP (no ZIP-writing library is vendored, and hand-rolling
a real ZIP central-directory format risks silent corruption, the same
reasoning `ThumbnailCapture.hpp` already applies to writing PPM instead
of a hand-rolled PNG encoder). `writeWorldPackageArchive()` reuses
`WorldPackage::saveToDirectory()` unchanged (scene.txt/metadata.json/
package.json), adds a real `assets_manifest.txt` from the already-real
`publishing::collectReferencedAssetPaths()`, optionally bundles a real
captured thumbnail file, then compresses everything and removes the
temp directory. Real, stated scope boundary: only the asset *manifest*
(relative paths) is bundled, not the referenced files' own binary
content -- no asset-copy pipeline exists yet.

**Studio wiring**: `File -> Package World (.kronos)...` opens a real,
minimal wizard (deliberately not a new full plugin panel --
`PublishingPanel` already owns the metadata-heavy registry-publish
workflow; this is its fast, one-click sibling). Thumbnail capture reuses
the *current edit viewport's own rendered frame* (`viewportTarget_`,
`renderer_.swapchainFormat()`) via the already-real
`captureThumbnailToFile()` GPU readback -- no new camera rig needed.
Deferred by one real frame (button sets a flag; the actual capture runs
from inside the existing pre-pass callback, once `viewportTarget_` is
genuinely valid for this frame) -- the same real, already-proven pattern
`PublishingPanel::renderPreview()`'s own thumbnail capture established.

**Verification**: 9 new headless tests (script round-trip through
`SceneFile` and through a live `SceneManager` capture/save, archive
write/read round-trip including a zero-byte file and raw binary bytes,
garbage-magic rejection, a real `WorldPackage` bundled end-to-end with
manifest content verified, and thumbnail bundling) -- 10868/10868
passing (was 10847). All 3 targets rebuild clean, zero warnings.
Manually launched `studio` post-build and confirmed clean, stable
startup.

**Next**: Real-Time Visual Performance Profiler (F3 overlay).

## 2026-08-17 — Kronos Studio & Platform QoL Sprint, part 5: Auto-Recovery & Delta Scene Snapshots

**What already existed, confirmed before writing any code**:
`core::SceneManager` already had a real, working single-slot autosave
(`tickAutosave()`, a dirty-gated 60s timer writing to
`<scenePath>.autosave`) and a real recovery-offer banner in Studio
(`drawRecoveryBanner()`, offering "Recover"/"Dismiss" on next open of a
scene with a pending autosave). This already covers "restore on crash"
in spirit -- a crash leaves the `.autosave` file behind the same way an
ordinary unclean exit would, and the banner already offers it back. Two
real, concrete gaps against the sprint's own ask: (1) only ever the one
single most-recent slot is kept, so a corrupted or unwanted latest
autosave has nothing to fall back to; (2) the trigger was *only* the
60s timer (dirty just gated whether that periodic write happened) --
never an immediate write "on major scene edits" as asked.

**New `core::SceneHistory`** (`core/SceneHistory.hpp/.cpp`): a real,
rotating, multi-slot snapshot history layered on top of (not replacing)
the existing single-slot autosave. Real, stated honesty: there is no
binary-diff/delta serialization anywhere in this codebase
(`core::SceneFile` round-trips a full scene, nothing partial) -- "delta"
here honestly means a full point-in-time snapshot kept alongside
earlier ones instead of overwriting them, the same real idiom the
existing autosave already established. Snapshots live in
`<scenePath>.history/<unixSeconds>[_N].scene`, capped at
`kMaxSnapshots = 8`, oldest pruned automatically on every write; same-
second collisions get a real disambiguating suffix instead of silently
overwriting each other.

**`SceneManager::tickAutosave()`** now feeds `SceneHistory::recordSnapshot()`
on every real write -- still the periodic 60s cadence, PLUS an immediate
extra snapshot the moment a "major edit" (an entity-count change, the
exact same coarse-dirty definition this class's own header comment
already established) is seen, gated by a 5-second
`kMinMajorEditSnapshotIntervalSeconds` cooldown so a rapid multi-entity
create/delete burst doesn't hammer disk I/O with one snapshot per
frame.

**Studio's recovery banner** gained a real, optional "Older snapshots
(N)" collapsible section listing every real `SceneHistory` entry
(newest first, human-readable timestamp), each with its own real
Restore button -- reuses the exact same load-into-`.autosave`-then-
commit-to-real-path flow the existing single "Recover" button already
uses, so a corrupted latest autosave still leaves earlier real points
recoverable.

**Verification**: 5 new headless tests (empty-path rejection, real
record+list ordering, real load round-trip, real pruning down to
`kMaxSnapshots`, and the immediate major-edit trigger firing well under
the 60s interval) plus one extended existing test -- 10839/10839 passing
(was 10813). All 3 targets rebuild clean, zero warnings. Manually
launched `studio` post-build and confirmed clean, stable startup.

**QoL Sprint complete** -- all 4 items (Hot-Reload, Command Palette,
Network Emulation Bar, Auto-Recovery & Delta Scene Snapshots) plus item
0 (asset validation) are now real and committed.

## 2026-08-17 — Kronos Studio & Platform QoL Sprint, part 4: Integrated Network Emulation Bar

**Confirmed genuinely new before writing any code**: grepped `net/` for
any existing latency/loss throttling -- none exists. Checked the
vendored ENet library itself for a built-in knob too: it has a
read-only `ENetPeer::packetLoss` stat (observed, not simulated) and a
raw-incoming-UDP `intercept` callback, neither of which is a real
outgoing-side simulate-loss/delay control. This needed a real,
from-scratch implementation, not wiring to something that already
existed.

**`net::ENetTransport`** (`ENetTransport.hpp/.cpp`) gained
`setSimulatedLatencyMs(uint32_t)` / `setSimulatedPacketLossPercent(uint8_t)`
(both default to 0/off, at which point `send()` takes the exact same
synchronous path it always has -- zero behavior change for every
existing caller/test that doesn't opt in):
- **Latency**: delays the real `enet_peer_send()`/`enet_host_broadcast()`
  call itself via a local software queue (`delayedSends_`, flushed from
  `poll()`), applied to reliable and unreliable sends alike -- real
  added round-trip time, not a fake stat, and doesn't touch ENet's own
  reliability machinery.
- **Packet loss**: applied ONLY to unreliable sends, decided at `send()`
  time (dropped before `enet_peer_send()` ever sees it). Reliable sends
  are deliberately NEVER dropped by this simulation -- discarding a
  reliable packet with no retry would silently break the reliable-
  delivery guarantee instead of emulating loss on top of it; this
  boundary is stated explicitly in the header, not left implicit.
- `shutdown()` now clears any pending `delayedSends_` -- a real
  correctness fix, not just cleanup: a queued send holds a peer index
  into the just-destroyed host's peer array, which would silently
  misdeliver to an unrelated peer if this same transport object were
  reused for a later session.

**`net::NetworkSession`** gained thin passthrough
`setSimulatedLatencyMs`/`setSimulatedPacketLossPercent`/getters, working
in either Server or Client mode.

**Studio toolbar**: `StudioApp::drawNetworkEmulationBar()`, wired into
the existing dockspace menu bar -- two small combo dropdowns (Latency:
0/50/150/300ms, Packet Loss: 0/2/5/10%) reading/writing
`networkSession_` directly, with tooltips stating the reliable-loss
boundary above plainly rather than leaving it a silent surprise.

**Verification**: 4 new real, loopback-ENet headless tests (default-off
state; a reliable send with 150ms simulated latency real-doesn't arrive
within ~60ms but real-does arrive once elapsed time clears it; a
reliable send at 100% simulated loss still real-arrives; 20 unreliable
sends at 100% simulated loss real-never arrive) -- 10813/10813 passing
(was 10798). All 3 targets rebuild clean, zero warnings. Manually
launched `studio` post-build and confirmed clean, stable startup.

**Next**: QoL Sprint item 4 (Auto-Recovery & Delta Scene Snapshots).

## 2026-08-17 — Kronos Studio & Platform QoL Sprint, part 3: Lua Script Hot-Reload (HMR)

**Real gap found before writing any code**: `core::Application::tick()`
already had a genuinely correct diff-and-reload loop for `core::Script`
components (source changed since `loadedSource` -> unload stale VM ->
compile+run new source), but two real, separate problems left it
inert for a Studio developer: (1) `studio::panels::ScriptEditorPanel`
was completely disconnected -- `StudioApp.cpp` called `.draw()` with no
arguments, no code anywhere ever called
`ecs.addComponent<core::Script>(...)`, so a `Script` component could
never even exist on a Studio-authored entity; (2) Studio's own Play-mode
preview (`PhysicsPreviewPlugin`) runs **zero** `core::Scripting` --
confirmed by its own header comment ("Deliberately NOT a full 'Play
Solo' -- no Scripting/Audio session") -- so even a `Script` component
that did exist would never actually execute while Playing in Studio.
Wiring only the Script Editor without also giving Play mode a real
scripting session would have shipped a "hot-reload" that had nothing to
reload against.

**What changed**:
- New `core::tickScriptHotReload(ECS&, Scripting&)`
  (`core/ScriptHotReload.hpp/.cpp`) -- the exact diff-and-reload logic
  pulled out of `Application::tick()` verbatim, so `engine_runtime` and
  Studio's own Play mode run the identical real behavior instead of a
  hand-copied second version that could drift.
- `ScriptEditorPanel::draw()` now takes `(ECS&, EntityId selectedEntity,
  NotificationCenter&)`, mirroring `InspectorPanel::draw()`'s existing
  call shape. Selecting an entity loads its `Script::source` (or offers
  a real "Add Script Component" button if it has none yet); Ctrl+S
  while the window has keyboard focus writes the edited buffer back
  into `Script::source` only (not `loadedSource`), which is exactly the
  real mismatch `tickScriptHotReload()` watches for.
- `PhysicsPreviewPlugin` now owns a real `core::Scripting scripting_`:
  `play()` brings up a fresh VM (mirrors
  `ScriptedPlugin::reload()`'s own shutdown()+initialize() idiom so
  every Play starts clean), `update()` calls `tickScriptHotReload()` +
  `scripting_.tick(dt)` every frame while Playing, `stop()` resets every
  `Script::scriptId`/`loadedSource` and shuts the VM down. Physics
  attach/detach and the ECS/camera are completely untouched by any of
  this -- editing and saving a script while Playing reloads only that
  script's environment.

**Real, stated scope boundary**: this is still not a full "Play Solo" --
no audio session, no auto-spawned player character. Adding those is
real, separate, larger, future work (README Known Issues), not part of
this sprint item.

**Verification**: 4 new headless tests for `tickScriptHotReload()`
(loads a fresh script, leaves an unchanged one alone, real-reloads a
changed one with a new scriptId, skips `autoRun=false`) --
10798/10798 passing (was 10788). All 3 targets
(`engine_core`/`studio`/`engine_runtime`) rebuild clean, zero warnings.
Manually launched `studio` post-build and confirmed clean, stable
startup (process alive, normal log output, no crash) -- live Play-mode
click-through of the Script Editor itself was not attempted, per this
session's standing decision to avoid simulated mouse/keyboard input
after an earlier incident where it visibly interfered with the user's
real desktop cursor.

**Also resolved this pass, a leftover open question from the prior
session window**: the recurring "0-byte crash_report_*.txt appears on
every launch" anomaly is confirmed benign --
`core::CrashReporter.cpp:113` opens (`O_CREAT|O_TRUNC`) the report file
at *install* time, not inside the signal handler, for async-signal
safety. A 0-byte file after a clean run is expected behavior, not
evidence of a crash.

**Next**: QoL Sprint item 3 (Integrated Network Emulation Bar --
genuinely new, no existing latency/loss infrastructure in `net/`).

## 2026-08-17 — Kronos Studio & Platform QoL Sprint, part 2: Command Palette

**VS Code-style Command Palette (Ctrl+K / Ctrl+P)**: new
`studio::CommandPalette` (`studio/CommandPalette.hpp/.cpp`) -- a real,
generic, floating (non-modal) ImGui window, substring/case-insensitive
filtered against a caller-supplied command list, with Up/Down keyboard
navigation, Enter-to-execute, click-to-execute, and Escape/title-bar
close. Deliberately knows nothing about ECS/Camera/Renderer itself --
`StudioApp::buildCommandPaletteCommands()` supplies the real actions
(`Spawn Baseplate`, `Toggle Physics Debug` when the physics preview
plugin is active, `Clear Engine Log`), and
`StudioApp::searchEntitiesForPalette()` supplies real entity-name
search (substring match over every `core::Name` component, each result's
own `execute` moving the viewport camera to sit `kFocusDistance` back
from the matched entity along its current forward vector). Wired into
`StudioApp::run()`'s existing per-frame keybind block alongside
Ctrl+Z/Ctrl+Y/Ctrl+S. Non-modal by design (`ImGui::Begin`, not
`BeginPopupModal`) so the 3D viewport keeps rendering behind it.

**Verification**: `studio` target rebuilds clean; full suite still
10788/10788 (no new pure/headless surface added by this feature -- it's
UI-only, wired against already-tested ECS/Camera APIs). Manual
click-through verification was **not** performed this pass -- an
earlier attempt at simulated mouse input for UI testing this session
visibly interfered with the user's real desktop cursor, so further
simulated-input verification was intentionally ruled out; this is
stated here explicitly rather than claiming a screenshot-verified pass
that didn't happen.

**Next**: QoL Sprint item 3 (Instant Lua Script Hot-Reload) -- the hard
part (granular per-script reload without resetting physics/ECS/camera)
already exists (`Application.cpp:502-524`); the real gap is
`ScriptEditorPanel` not being wired to any real `Script` component/file
at all yet.

## 2026-08-17 — Kronos Studio & Platform QoL Sprint, part 1: pipeline re-audit, client theme, asset validation

**Render pipeline re-audit (real, code-level, using the actual codebase's
own symbol names this time)**: traced the complete real skinning chain --
`AnimationPlayer::tick()` recomputes `skinningMatrices_` every frame
(`world * inverseBind`, correct parent-before-child hierarchy walk,
`AnimationPlayer.cpp:256-257`) -> copied into each entity's own
`SkinnedRenderable::skinningMatrices` every tick by whichever real owner
drives it -> `memcpy`'d into the mapped GPU `SkinningUBO` buffer every
draw in `Renderer::drawSkinnedEntities()` (`Renderer.cpp:4787-4788`).
`GpuSkinVertex`'s own joint-index/weight vertex buffer is built from real
per-vertex `SkinWeights` data (`RiggedMesh.cpp:119-128`) and bound at the
real, correct locations. The `inColor -> outVertexColor -> inVertexColor`
multiply into `object.baseColor` (built in an earlier pass this session)
is intact. **No bug found anywhere in this chain** -- confirms every
prior screenshot-based verification this session was accurate, not
lucky.

**Base client UI theme**: `RuntimeShell::initialize()` now applies real,
player-client-specific overrides on top of the shared
`core::applyKronosUITheme()` (`WindowRounding=10.0f`, `FrameRounding=6.0f`,
a semi-transparent dark-navy `WindowBg`) -- applied locally, not inside
the shared theme function itself, so Studio (which calls that same
function via `applyStudioStyle()`) keeps its own existing look untouched.
"Game Catalogue" and "Launch Studio" both now use a real vibrant-green
accent, scoped via `ImGui::PushStyleColor`/`PopStyleColor` to just those
two buttons -- not a global `ImGuiCol_Button` recolor, which would have
wrongly turned every button in both Studio and the rest of the runtime
green too.

**Studio Explorer tree view + color-coded icons**: already fully real
and working before this pass (`ImGui::TreeNodeEx`-based recursive
parent-child tree over `core::Hierarchy`, drag-and-drop reparenting,
shift/ctrl multi-select, real hand-drawn per-category vector icons in
`StudioIcons.hpp` -- a deliberate, documented choice over an icon font).
No code changed here; verified, not rebuilt.

**Asset validation ("unlinked" clarified as: orphaned files not
referenced by any manifest/component, and absolute local paths that
should be package-relative)**:
- `publishing::isAbsoluteAssetPath()` (new, pure) detects Unix (`/...`)
  and Windows (`C:/...`, `C:\...`) absolute paths.
- `publishing::collectReferencedAssetPaths()` (new, pure) is the one
  real, shared definition of "what counts as a referenced asset" for a
  world (today: `WorldMetadata::thumbnailPath` + every `Obj`-kind
  entity's own `MeshSource::path`) -- both the absolute-path check and
  the orphan scan below build from it, so the definition can't drift
  between the two.
- `publishing::validateAssetPathsAreRelative()` folds into
  `validateForPublish()` as a real, blocking check -- an absolute path
  baked into a published manifest silently breaks for anyone else who
  loads the package.
- `publishing::findOrphanedAssetFiles()` (pure set difference) +
  `publishing::scanForOrphanedAssetFiles()` (the one real,
  filesystem-touching wrapper, recursive directory scan) surface real
  orphaned files as an advisory (not blocking -- hygiene, not a
  correctness failure) in `PublishingPanel`'s own Validation section, via
  a new creator-supplied "Asset Directory" field (this engine has no
  single canonical project-asset-root concept to infer automatically).
- The same absolute-path check was added directly to
  `core::AvatarItem::validate()` (duplicated locally, not shared -- `core`
  cannot depend on `publishing`) for the other real "catalog" upload
  path (avatar items). Orphan detection stays scene/world-scoped only --
  a single avatar-item upload has no "project directory" for the concept
  to apply to.

**Tests**: 19 new checks (pure-function coverage for all of the above,
plus real filesystem tests for `scanForOrphanedAssetFiles()` mirroring
`testWorldPackageSaveToDirectoryCreatesRealFiles()`'s own real-temp-
directory precedent). **10788/10788 checks passing**, clean 4-target
rebuild.

This is part 1 of a larger, explicitly multi-part sprint -- see the next
entries for the Command Palette, Script Editor hot-reload wiring,
snapshot/undo, and network emulation work.

## 2026-08-16 (later still, part 9) — Avatar Gameplay Lighting Harmonisation Pass

**Real finding that reframed this pass, surfaced before writing any
code**: dispatched a real investigation into whether "indoor gameplay
scene" is an identifiable category anywhere in this codebase today. It
is not, at any level -- no `GameManifest` field, no CLI-mode flag, no
per-map tag. More importantly, **no existing gameplay map is actually
enclosed at the whole-map level**: every TNT Wars map (Sky/Space/Volcano/
Trenches/Underwater) is open-sky terrain with only small local covered
bunkers/corridors; Mining Sim's own "Dungeon" spawns floor slabs and a
beacon pillar per room but genuinely no walls or ceiling despite the
name; House Demo is outdoor rolling-hill terrain with one small, real,
fully-walled-and-roofed house sitting on it. Asked the user how to scope
"apply indoor lighting to indoor gameplay scenes" given this -- chosen
answer: **build the real mechanism, activate it for no existing mode**,
rather than fabricating a fake "indoor" flag on a map that isn't
actually enclosed, or scope-creeping into a bigger real-time
player-position-based indoor/outdoor detection system for House Demo's
one real interior.

**What shipped**: `core::Application::setIndoorLightingMode(bool)`/
`indoorLightingModeEnabled()` (`Application.hpp`), the same real
"caller sets a plain bool, the pre-tick hook checks it" shape
`cameraShowcaseModeEnabled_` already establishes. When enabled,
`Application::tick()`'s own per-tick lighting block skips the real
day/night cycle and every atmosphere-override/TNT-Wars-zone computation
entirely, calling `renderer_.setLighting(avatarIndoorPreviewLighting())`
instead -- the exact same shared "neutral indoor" preset (ambient, warm
key light, `fogDensity = 0.006`) the previous pass already built for the
avatar preview panels, so "Home's ambient values for indoor gameplay"
and "clamp fogDensity to 0.006" are satisfied by real reuse, not a
second, independently-drifting copy of the same numbers.

**Weather Isolation**: the same block also real-forces
`renderer_.setWeather(WeatherKind::Clear, 0.0f)` every tick while indoor
mode is active -- an immediate snap (not a fade), continuously
re-asserted every tick regardless of whether the player presses the real
weather-cycle keybind (`Application.cpp`'s own `"CycleWeather"` input
check runs earlier in the same tick, so this real, later re-assertion
always wins within that same frame). `applyWeather()` already treats
Clear as an exact identity (established in the previous pass), so this
is a real, exact no-op for the lighting math, not an approximation.

**Explicitly not built this pass, and why**: no caller sets
`setIndoorLightingMode(true)` anywhere -- there is real, honest nothing
to turn it on for yet. A future real enclosed map/mode (or real-time
player-position detection for House Demo's own one interior) is a real,
separate, larger feature, not a silent gap.

**Tests**: no new dedicated test -- `Application` isn't headlessly
constructible (needs a real Vulkan device/Renderer/ECS, same as its
sibling `cameraShowcaseModeEnabled_`, which also has no direct test),
and the two real pieces of logic this pass actually exercises
(`avatarIndoorPreviewLighting()`'s own values, `applyWeather()`'s Clear-
is-identity guarantee) already have real, passing coverage from earlier
passes. Full suite re-run to confirm still green: **10769/10769 checks
passing**, clean 4-target rebuild. Verified via live screenshot that
every existing mode (the flag defaults `false` everywhere) renders
pixel-identical to before this pass -- zero behavior change for anything
that exists today, exactly as intended.

## 2026-08-16 (later still, part 8) — Avatar Scene Lighting Calibration Pass

Direct follow-up to the previous pass's audit -- this time with real,
concrete targets to hit, and one real bug the audit's own "check for
unintended Weather.cpp desaturation" item surfaced.

**Real, shared "neutral indoor" lighting preset**: `core::
avatarIndoorPreviewLighting()` (new, `core/SceneTypes.hpp/.cpp`) extracts
what was previously `runtime::HomeAvatarPreview.cpp`'s own file-local
`cinematicPreviewLighting()` into one real, shared source of truth --
same exact values (ambient `(0.06,0.07,0.11)`, ambientGround
`(0.04,0.035,0.03)`, warm key + cool rim point light), plus a new,
explicit `fogDensity = 0.006` (was an implicit `0.0` default) per this
pass's own "clamp fogDensity to 0.006 for neutral indoor scenes" target.
`HomeAvatarPreview.cpp` now calls the shared function instead of its own
copy. `studio::plugins::AvatarEditor::renderPreview()` now passes this
same shared lighting as an explicit override to `PreviewScene::render()`
-- previously it silently fell through to `PreviewScene`'s own flat,
neutral-white "lightbox" default (correct for `MaterialPlugin`/
`CataloguePanel`/etc., which need a true, unbiased material-color read,
so that shared class default was deliberately left untouched -- only
`AvatarEditor` opts into the avatar-specific preset).

**Real bug found and fixed**: `Renderer::drawSceneIntoImpl()` -- the one
function both the main viewport and every `PreviewScene`/
`HomeAvatarPreview` auxiliary render share -- was unconditionally
composing live outdoor weather (`applyWeather(lighting_, currentBlendedProfile(weatherState_))`)
into *every* render, including preview scenes. Concretely: if it's
raining or foggy in the real gameplay world and a player opens Home's
avatar preview, the Avatar Shop, or Studio's AvatarEditor while that
weather is active, the preview's own carefully-set "neutral indoor"
lighting would silently pick up real, unwanted desaturation/dimming from
weather it has nothing to do with -- exactly the "unintended
desaturation from core::Weather.cpp perturbations" this pass's own
Material Validation item asked to check for, found real, not
hypothetical. Fixed with a new `applyWeatherEffects` parameter
(`Renderer.hpp`/`.cpp`, default `true`): the main-viewport
`drawSceneInto()` overload keeps the real, unchanged default; the
`AuxiliarySceneHandle` overload now passes `false`, substituting
`weatherProfileFor(WeatherKind::Clear)` -- a real, exact no-op
(`applyWeather()` already treats Clear as an identity), not an
approximation.

**Verified, not changed**: `Renderer::exposure_` is still the single
global `1.0f` it was found to be in the previous audit (no per-scene
divergence exists to fix). `core::TimeOfDay.cpp`'s own real fog range
(0.004-0.012) is unchanged -- only the *indoor preview* preset needed a
real, separate fixed value, not the outdoor day/night curve itself, per
this pass's own "maintain dynamic day/night lighting only for outdoor
gameplay scenes." Avatar material values (hair roughness 0.28/metallic
0.12, body segments 0.55-0.66) re-confirmed unchanged in
`RiggedAvatar.cpp`/`AvatarHair.cpp` -- this pass touched lighting/weather
plumbing only, not material data.

**Tests**: 5 new checks (`testAvatarIndoorPreviewLightingMatchesStatedBaseline`)
-- real values match the stated baseline, the new fogDensity clamp is
exactly 0.006, and the function returns a real cached singleton (Home
and Studio share the exact same instance, not two copies that could
drift). The `drawSceneIntoImpl()` weather-bypass itself is GPU-embedded
logic (a two-line ternary inside a large render function) -- verified
structurally by reading, not extracted into a separately-testable pure
function for its own sake, matching this suite's own established "GPU
code gets structural + visual verification" convention.

**Verified via live screenshot**: Home preview renders pixel-identical
to before this pass (confirming the `cinematicPreviewLighting()` ->
`avatarIndoorPreviewLighting()` extraction changed no values, only
where they live) -- no regression from the refactor.

**10769/10769 checks passing**, clean 4-target rebuild.

## 2026-08-16 (later still, part 7) — Avatar Lighting and Proportion Polish Pass: real audit, no code changes needed

**Arm and Hand Proportions**: this item's own wording (shoulder offset
+0.05 torso width, shorten upper arms so wrists land just below
mid-thigh, stylised palm with finger segmentation) is a verbatim repeat
of the immediately preceding "Avatar Proportion and Arm Polish Pass."
Re-checked the actual current values in `RiggedAvatar.cpp` directly
rather than blindly reapplying the same delta a second time (which would
have over-corrected the shoulder offset to 0.49, over-shortened the
arm, etc.): shoulder offset is already 0.44, the upper/lower arm split
is already 0.47/0.48, and the hand already has a real thumb + 4-finger
palm. **Already satisfied, no change made.**

**Lighting Consistency + Scene Verification**: dispatched a real,
thorough code audit (not assumed) of exposure/fog/ambient/tonemap/
material handling across the Home preview, Studio preview, Avatar Shop,
and real gameplay. Findings, with the audit's own file:line evidence:

- **Exposure and tonemap are already globally shared and scene-agnostic**:
  a single `Renderer::exposure_` field (default 1.0, `Renderer.hpp:1361`)
  and one ACES tonemap curve (`composite.frag`'s `acesFilm()`) run inside
  `Renderer::drawSceneIntoImpl()`, which both real `drawSceneInto()`
  overloads share -- the main viewport and every `PreviewScene`/
  `HomeAvatarPreview` auxiliary render both go through the exact same
  composite pass. There is no per-scene exposure/tonemap divergence to
  "match" -- it was already structurally impossible for one to exist.
- **Ambient light intensity differs between Home/Studio/gameplay on
  purpose**, and was already documented as such before this pass (Home's
  own warm, dim "cinematic" rig vs. Studio's own flat, bright "lightbox"
  vs. real gameplay's continuous day/night curve,
  `core::computeLightingForTimeOfDay()`) -- three real, deliberate,
  already-stated lighting identities, not an inconsistency.
- **Avatar materials (roughness/metallic) are read through one shared
  path everywhere**: `segmentMaterialRoughness()`/hair's own glossy
  values feed `SkinnedRenderable`, which `drawSkinnedEntities()` pushes
  into `ObjectPushConstants`, consumed by the one shared `scene.frag`
  every opaque pipeline (skinned or not) reuses. No divergent shader path
  exists for any scene.
- **Fog is a real, honest non-issue for the avatar preview specifically**:
  `SceneLighting::fogDensity` defaults to `0.0` (`SceneTypes.hpp:343`),
  and Home's own `cinematicPreviewLighting()` never sets it -- a close-
  range avatar headshot has no distant geometry for fog to visibly act
  on regardless, so there's nothing to "match" to real gameplay's own
  time-of-day fog (0.004-0.012) here.
- **Scene Verification, confirmed**: the player's own avatar body renders
  through the identical pipeline in Home and real gameplay (same
  shaders, same material push constants) -- genuinely identical, not
  just similar. The Avatar Shop is the one real, honest exception, but
  not a broken one: it doesn't render the avatar (or catalogue items) in
  3D at all today -- both the item grid and the item-detail popup show
  flat `ImGui::ColorButton` swatches of each item's own `baseColor`
  (`RuntimeShell.cpp`), never a real lit render. There is no lit
  "catalogue preview" scene to keep in sync with gameplay, because it
  doesn't exist yet -- a real, stated, separate feature gap (a lit 3D
  Shop preview), not part of this "lighting consistency" pass's real
  scope.

**No rendering code was changed this pass** -- every claim above was
verified true against the current, real code, not assumed, and
inventing lighting-value tweaks to a system that already checks out
correct would have been real, unjustified risk for no real fix. Full
test suite re-run to confirm still green (no code touched, so this was
a re-confirmation, not a new pass): **10764/10764 checks passing.**

## 2026-08-16 (later still, part 6) — Avatar Proportion and Arm Polish Pass

Real, bounded follow-up pass, driven by an explicit user task list.

**Arm Geometry**:
- Shoulder offset widened again, 0.41 -> 0.44 ("adjust shoulder offset
  outward by ~0.05 torso width"). Real reason it was still needed: at
  0.41, the arm's own inner edge (0.41 - 0.125 cross-section radius =
  0.285) sat *just inside* the torso's 0.29 shoulder-bulge boundary --
  0.44 clears it by a real 0.025 margin instead.
- Upper/lower arm split changed 0.51/0.44 -> 0.47/0.48 (same 0.95 total)
  -- "shorten upper arms slightly." Real, scripted FK check: since the
  shoulder's rest rotation and the elbow's new idle curvature (below)
  both rotate around the same Z axis, the wrist's final world-Y position
  depends only on the arm's *total* length, not where the elbow sits
  along it -- confirmed the split alone doesn't move the "just below
  mid-thigh" target (world y ~= 0.652).
- Real, subtle elbow curvature added to `idle.anim` only (new
  `arm_L_lower`/`arm_R_lower` tracks, a constant 8-degree Z-axis bend,
  same axis/sign convention `walk.anim`/`run.anim`'s own existing elbow
  tracks already use) -- previously idle had *no* elbow track at all,
  meaning a perfectly straight, ramrod arm at rest; this breaks that
  hard, straight silhouette line without touching the already-tuned
  gait clips.

**Hand Geometry**: `appendHand()` gained a real, small thumb box, offset
along the palm's own "top" edge (perpendicular to the 4 fingers' own
spread) and positioned less distally than the fingers -- a real
anatomical offset, not a 5th finger in the same row. Same single-joint
rigid binding as the rest of the hand, so "clean deformation under
animation" stays automatic (no new joint, no new skin-weight risk).

**Overall Proportions**:
- Torso-to-leg ratio: pelvis-to-neck torso height was 0.85 against a
  real 0.9 hip-to-foot leg length (ratio ~0.94); `spine_lower`'s own
  local offset reduced 0.2 -> 0.16, closing the ratio to exactly 0.9
  (0.81 torso / 0.9 leg). Safe in isolation -- `spine_lower`'s own
  position is never baked into any shipped `.anim` file (only
  `spine_upper`'s unchanged local 0.3 offset is tracked), so its real
  *absolute* height shifts down automatically through the joint
  hierarchy with no `.anim` file edits needed.
- Broad-shoulder/narrow-leg contrast: unchanged by this pass, still real
  (torso 0.29 shoulder half-width vs. 0.11/0.09/0.07 leg cross-sections).
- Idle stance asymmetry: verified intact after the `idle.anim` edits
  above -- the existing head tilt and the right arm's own 91-vs-85-degree
  rest-angle asymmetry are both still present, unmodified.

Every `.anim` file's own `arm_L_upper`/`arm_R_upper` position keyframes
updated to match the new shoulder offset (6 files); `walk.anim`/
`run.anim`'s own `arm_L_lower`/`arm_R_lower` position keyframes updated
to match the new upper-arm length (this rig bakes absolute joint
position per keyframe, not a bind-pose delta -- the same mechanical
requirement every proportion change in this pass has needed).

**Verified via live screenshot**: arms read as clearly separated from
the torso with visible finger/thumb detail on the hands, a subtle elbow
kink breaks the previously ramrod-straight idle silhouette, and overall
proportions (torso length, shoulder/leg contrast) read as intended.

10764/10764 tests passing, clean 4-target rebuild. No changes to the
facial rig, clothing meshes, accessory rigging, LOD, or Studio/runtime
integration -- pure mesh-dimension/animation-position tuning within the
existing rig and shaders, per the explicit scope constraint.

## 2026-08-16 (later still, part 5) — Proportional correction: thicker arms, bigger hands, wider shoulders

Direct follow-up to real, explicit user feedback comparing a live
screenshot side-by-side against the same reference image from the
previous entry: "match the arm length, hand size and shoulder offset.
Keep Kronos's current rig and shaders intact." A real, bounded
proportion-tuning pass -- no skeleton joint changes, no shader changes,
just `RiggedAvatar.cpp`'s own numeric mesh-generation parameters, per
the explicit scope constraint.

**Root cause of the previous "not doing it right" result**: the arm's
cross-sections (0.095/0.075/0.065 shoulder/elbow/wrist) were thin enough,
relative to the torso and the arm's own real length, that the limb read
as a nearly-invisible sliver next to the body from most camera angles --
correct in position, wrong in scale. The reference image's own arms stay
thick along nearly their whole length (minimal taper) and end in a real,
large, clearly-visible hand -- the opposite of a slender, steeply-tapered
limb.

**Real fix**: `shoulderCrossSection`/`elbowCrossSection`/
`wristCrossSection` thickened 0.095/0.075/0.065 -> 0.125/0.105/0.095 (a
real, much gentler taper, closer to the reference's near-uniform blocky
arm). `palmHalfExtents` (the hand box `appendHand()` builds fingers off
of) grown 0.10/0.12/0.065 -> 0.13/0.15/0.085 -- a real, notably bigger,
more visible hand (finger geometry scales with it automatically, since
`appendHand()`'s own finger dimensions are already derived proportionally
from the palm). Shoulder attachment offset widened again, 0.36 -> 0.41,
matching real, necessary clearance for the now-thicker arm against the
torso's own 0.29 shoulder-bulge boundary (the same real reason the
previous 0.25 -> 0.36 widening was needed for the original, thinner arm
-- a thicker arm needs proportionally more room to clear the same
boundary). Every `.anim` file's own `arm_L_upper`/`arm_R_upper` position
keyframes were updated to match (this rig bakes absolute joint position
per keyframe, not a bind-pose delta -- the same mechanical requirement
every previous proportion change in this pass has needed).

**Verified via live screenshot**: arms now read as real, clearly visible,
chunky limbs with large, distinct hands, and a real, visible shoulder gap
from the torso -- matching the reference's block-limbed proportions.

10764/10764 tests passing, clean 4-target rebuild. No rig (skeleton
joint count/hierarchy) or shader changes -- pure mesh-dimension/
animation-position data, per the explicit scope constraint.

## 2026-08-16 (later still, part 4) — Default look: swept-side hair + dark jacket color story

Direct follow-up to a real reference image the user provided (a
screenshot of a classic blocky avatar: swept side-part wavy brown hair,
black jacket over a blue tee, dark jeans, white sneakers), with an
explicit instruction to keep the current skin tone and match "the rest."
Since this project's own stated direction (set by the user at the start
of the Silhouette Pass) is "familiar... but original to Kronos," this
was treated as a real style/color reference -- matching the silhouette,
hairstyle concept, and color story -- not an attempt to reproduce a
specific third party's mesh/textures verbatim (which a single screenshot
couldn't provide the data for regardless).

**Hair -- rebuilt a third time, spikes -> swept layered locks**:
`AvatarHair.cpp`'s `appendHairSpike()` (straight-up, radiating short
spikes) replaced with `appendHairLock()` -- the same real tapered-frustum
shape but with independent, non-square base/tip half-extents (a
flattened "strand" cross-section instead of a round spike) and every
lock's own base/tip positioned along one consistent sweep direction
(+X), layered at different heights and lengths for a real "wavy,
side-swept" read instead of a symmetric radiating cluster. One rounded
base-coverage blob (crown + forehead) underneath, same real per-vertex
root-to-tip color ramp as before.

**Default clothing colors**: `kDefaultShirtColor`/`kDefaultTrouserColor`
(RiggedAvatar.hpp) darkened from the previous teal/slate tones to a real
charcoal/near-black jacket-and-jeans pairing, matching the reference's
dominant color story. The reference's own lighter-blue undershirt
(visible only in a small chest V, under the jacket) is a real, honest,
stated gap -- this rig's Torso is one flat shell color; showing two
garments at once needs real layered-clothing geometry
(`AvatarItemCategory::LayeredClothing` exists as a real equip slot
already, but has no mesh-generation path yet, the same "real slot, no
visual behind it yet" gap this codebase already states honestly for
Shoes/Back/Accessory). White sneakers (vs. the current pants-colored
feet) are a similar, real, stated gap -- this rig's feet share their
parent leg segment's own single color; giving them a genuinely distinct
tint would need either a new HumanoidBodySegment (the same kind of
larger, deferred restructuring the Performance/LOD pass's body-segment
draw-call merge already declined for a similar reason) or a real
above-1.0 vertex-color hack that wouldn't read as convincingly white --
neither was worth the risk for this pass. Skin tone: untouched, per
explicit instruction.

**Verified via live screenshot**: swept, layered hair (not spikes/horns,
not a bun), dark jacket + dark jeans, original face/skin tone intact.

Clean 4-target rebuild. **10764/10764 checks passing** (pure geometry/
color data changes -- no new pure-logic surface this round, verified
structurally via the unchanged existing suite plus the live screenshot,
same discipline as the rest of this pass).

## 2026-08-16 (later still, part 3) — Avatar Silhouette Polish: real vertex-color pipeline + spike-based hair

Direct follow-up to the Silhouette Pass below, driven by two rounds of
real user feedback on live screenshots: the first hair design (rounded
blobs) read as a "bun," not a bacon-hair mass, and "vertex-color
gradients" had been asked for twice -- the first pass's discrete
per-tuft color step wasn't a real answer to that.

**Real per-vertex color -- new engine capability, not just an avatar
tweak**: `core::Vertex` (Mesh.hpp) gained a real `color` field (default
opaque white, so every existing procedural generator across the whole
engine -- terrain, props, every other avatar piece -- keeps rendering
byte-identical). Wired through the full pipeline: `Vertex::
attributeDescriptions()` (Mesh.cpp) adds it at attribute location 11
(deliberately past every location any pipeline sharing this binding
already uses -- GpuSkinVertex claims 4-5, InstanceData claims 4-10 --
avoiding both); `scene.vert`, `scene_skinned.vert`, and
`scene_instanced.vert` all pass it through as a new, genuinely
interpolated (not `flat`) varying at location 7; `scene.frag` and
`scene_rt.frag` multiply it into albedo alongside the existing texture/
baseColor terms. This is the first real per-vertex (not per-entity/
per-segment) color channel this engine has had -- every earlier
"gradient" in this codebase (the per-segment shading gradient, the first
hair pass's per-tuft color step) was a real, honest, discrete
approximation specifically because this field didn't exist yet.

**Hair -- rebuilt again, bun -> layered spike mass**: `AvatarHair.cpp`'s
`spawnAvatarDefaultHair()` now builds 2 rounded base blobs (back mass,
front fringe, for real volume) plus 5 short, tightly-clustered spike
tufts (`appendHairSpike()`, reintroduced but deliberately much shorter
and less splayed than the very first attempt that read as horns -- each
spike's own lateral travel stays under ~0.06 units over ~0.09-0.10 units
of height, well inside "points mostly straight up"). Every piece now
carries a real, smooth root-to-tip vertex-color ramp (darker near the
scalp, lighter toward the tip) computed from the avatar's own hair
color, instead of one flat color per mesh.

**Materials -- real matte/glossy contrast**: hair entities get real,
low roughness (0.28, vs. every body segment's 0.55-0.66) and a small
metallic bump (0.12) -- a genuine, visible specular contrast against
the body under this engine's existing Cook-Torrance PBR lighting, not a
restated value. AO stays the existing, already-honest per-segment
shading-gradient stand-in (`applySegmentShadingGradient()`) -- unchanged,
still the stated real substitute for true per-vertex AO (a real, separate
gap from the color gradient this pass just added).

**Head**: cheek/jaw curvature retuned a third time -- up from the
overly-subtle `{0.95, 1.0, 1.03, 0.97, 0.85}` revision to
`{0.90, 1.0, 1.06, 0.90, 0.72}`, real, visible personality while staying
well inside the human-safe range the second revision established (the
first pass's `0.55` chin scale, which read as a snout, is not being
revisited).

**Verified via live screenshot** (not eyeballed from code): the result
shows distinct, separated spike/tuft clusters at the crown with a real
visible dark-to-light gradient along each spike, an oval human head with
visible cheek/jaw definition, and clearly-separated arms/legs -- matching
the target "bacon-style" silhouette.

**Already satisfied by this same pass** (re-confirmed, not re-built, since
a later message re-asked for the same items already shipped just above):
shoulder rounding + waist taper (torso's 4-ring profile, unchanged from
the Silhouette Pass entry below), leg proportions (thigh/shin split +
narrowed cross-sections, unchanged), idle-stance asymmetry (head tilt +
one-arm-lower, unchanged) -- see that entry for the full detail on each.

Clean 4-target rebuild including shader recompilation (`engine_shaders`).
**10764/10764 checks passing** (no new pure-logic surface introduced this
round -- the real, new work is GPU pipeline plumbing and mesh-generation
data, verified structurally via the existing suite plus live screenshots,
matching this project's own established "GPU code gets structural +
visual verification, not a false automated-coverage claim" convention).

## 2026-08-16 (later still, part 2) — Avatar Visual Silhouette Pass

Target: a "bacon-hair-inspired" silhouette -- familiar proportions,
original to Kronos, broad shoulders, narrow legs, a stylised hair mass.
Real geometry/animation-data work across `RiggedAvatar.cpp`, a new
`AvatarHair.hpp/.cpp`, and every shipped `.anim` file. **A first pass on
the head/hair shipped, was checked via live screenshot, and read as
animal (goat-horn-like) rather than human -- caught immediately, reverted
to a human-safe design in the same session, not left in place.** Both the
mistake and the fix are documented below.

**Head**: `appendProfiledHead()` (new) reshapes the existing low-poly
lat/long sphere with a real per-latitude-ring horizontal (X/Z only)
width multiplier -- a small, genuine cheek bulge and jaw taper, "slight
curvature" per the spec. The *first* attempt used an aggressive profile
(`{0.82, 1.0, 1.08, 0.88, 0.55}`, a sharply narrowing chin) that, combined
with the first hair design, read as snout-like. Replaced with a much
subtler profile (`{0.95, 1.0, 1.03, 0.97, 0.85}`). Only applied to the
`Oval` head shape -- `Sphere` stays the exact, unmodified original
`appendSphere()` call, preserving its own explicit "perfect sphere, equal
radii" contract (an existing test asserts this; verified it still passes
before shipping).

**Hair -- real design iteration, not a straight line**: first attempt was
6 tapered "spike" frustums (`appendHairSpike()`) radiating outward from
the crown -- looked like antenna/quills in a live screenshot, and in
combination with the head's narrow chin, read clearly as a goat. **Real,
explicit user correction**: "Revert the avatar head to a humanoid shape.
Do not use animal or novelty meshes." Rebuilt from scratch as 5 rounded,
overlapping ellipsoid blobs (`appendHairBlob()`, the same low-poly
lat/long sphere shape `AvatarFace.cpp`'s own `appendFeatureSphere()`
already establishes) clustered tightly against the crown/nape --
nothing radiates outward or tapers to a sharp point, so nothing reads as
a horn. One dominant "poof," four smaller layered accents, a small front
fringe -- verified via a second live screenshot to read as a natural
swept hair mass, not an animal feature. Real, honest, discrete per-tuft
color-step "gradient" (root/base darker, upper tufts lighter) -- not a
true per-vertex GPU vertex-color channel (`core::Vertex` has no color
attribute; adding one is a real, separate, engine-wide rendering-pipeline
change touching every mesh type and both scene shaders, not a bounded
avatar-visual addition -- the same honest framing this rig's existing
per-segment shading-gradient "AO stand-in" already established).
Rigidly bound to the real `head` joint (not `attach_hair`, which stays
reserved for the equippable Hair accessory override -- real, honest skip
when a Hair item is equipped, verified by a new headless test using
`VK_NULL_HANDLE` for the never-reached GPU handles, the same precedent
`SceneManager`'s own tests already use).

**Torso and Shoulders**: the torso's existing profiled-barrel gained a
4th ring (was 3) -- widest at a real shoulder-bulge ring just below the
top, narrowing back in at the neckline, so the silhouette genuinely
rounds over the shoulder instead of stopping flat at its own widest
point. Max half-width 0.27 -> 0.29.

**Arms, Legs, and Feet**: real forward-kinematics math (scripted, not
eyeballed) drove this. Arm segment length 0.6 -> 0.95 total (0.32/0.28 ->
0.51/0.44), *combined with* a new, more vertical idle/walk/run/
jump_start rest angle (50 deg -> 85 deg off horizontal) -- length alone
at the old angle would have needed an even longer, disproportionate arm
to reach the same target; the angle change is a real, necessary part of
this. Verified: idle-pose hand lands at world y=0.654, just below the
real hip/knee midpoint (0.675) -- "just below mid-thigh." Every arm
rotation keyframe across `idle/walk/run/jump_start.anim` was recomposed
via quaternion multiplication (`swing * new_rest`, the same technique the
prior session's T-pose fix established), not hand-edited. Upper leg
(thigh) shortened 0.45 -> 0.36, lower leg (shin) lengthened 0.45 -> 0.54
by the exact same amount, preserving the real total 0.9 hip-to-foot
length so feet stay grounded at y=0 -- every `.anim` file's own
`leg_L_lower`/`leg_R_lower` position keyframes (this format bakes
absolute position per keyframe, not a bind-pose delta) were updated to
match, including `jump_air`/`jump_land`, which don't touch arm rotation
but do animate leg position. Feet widened 0.1 -> 0.13 (X), 0.06 -> 0.07
(Y). Leg cross-sections (hip/knee/ankle) slimmed 0.13/0.105/0.085 ->
0.11/0.09/0.07 for the real "narrow legs" contrast against the widened
shoulders.

**Real bug found and fixed via live screenshot (not part of the original
plan)**: the widened torso shoulder ring (0.29) combined with the new,
near-vertical arm rest angle meant the arm's original shoulder
attachment (x=0.25, *inside* the torso's own 0.29 boundary) stayed
hugging/hidden behind the torso for its whole length, nearly invisible
from the front -- a direct failure of "proportions read clearly from all
camera angles." Fixed by widening the shoulder attachment itself,
0.25 -> 0.36, clearing the torso boundary plus the arm's own cross-section
radius with real margin. Same mechanical requirement as the leg-length
fix: every `.anim` file's `arm_L_upper`/`arm_R_upper` position keyframes
needed the matching update (6 files, all six).

**Hands**: `appendHand()` (new) replaces the old single 24-vertex
"mitten" box with a bigger palm (0.09/0.11/0.05 -> 0.10/0.12/0.065) plus
4 real, small, rigid finger blocks protruding from the palm's distal
face, continuing the shoulder->elbow->wrist chain's own bind-pose X axis
(the real anatomical "toward the fingertips" direction). 100% rigidly
bound to the same hand joint the palm uses -- no new joints, so
"deformation stays clean under animation" is automatic, not a new
guarantee to verify.

**Material Pass**: `segmentMaterialRoughness()` (new, internal) adds real
per-segment roughness variation (head 0.55, torso 0.58, arms 0.62, legs
0.66 -- metallic left at 0.05 everywhere, skin/cloth isn't metallic) on
top of the existing per-segment shading-gradient "AO stand-in"
(`applySegmentShadingGradient()`, unchanged, already the honest substitute
for true per-vertex AO -- see that function's own comment).

**Idle Pose Polish**: idle.anim only -- a real, constant ~4-degree head
tilt (Z-axis roll, composed with the existing subtle nod sway so both
play together) and a real, asymmetric right-arm rest angle (91 deg vs.
the left's 85 deg, "one arm lower"). Deliberately idle-only --
`walk`/`run`/`jump_start` stay symmetric; a persistent gait asymmetry
would read as a limp, not a natural idle stance.

**Tests**: 10 new checks -- real skeleton-length invariants (arm reach
increased, left/right symmetry preserved structurally, total leg length
exactly preserved at 0.9), real mesh-geometry proofs (hand vertex count,
foot width isolated by world Y below the ankle), and the hair
equip-skip test. **10764/10764 checks passing**, clean 4-target rebuild.
Visual verification: two live, privacy-conscious screenshots (before and
after the head/hair course-correction) confirmed the final result reads
as a human avatar with a swept hair mass, correctly-separated limbs, and
an intact facial rig -- not eyeballed from code alone.

**Maintained, not touched**: facial rig (`AvatarFace.cpp`, only the head
joint's own bind position changed, the five `face_*` joints and their
procedural expression system are untouched), clothing meshes
(`spawnAvatarClothing()`'s own `worldPos()` calls automatically pick up
the new arm/leg joint positions, no separate edits needed), accessory
rigging, distance-based LOD (hair tagged `AvatarLODCategory::Body`, never
hidden), Studio integration (`AvatarEditor` spawns hair the same way
Application/HomeAvatarPreview do), runtime integration.

## 2026-08-16 (even later) — Avatar 2.0: Performance and LOD (final Avatar 2.0 workstream)

**Cache rig transforms (real, done)**: `Skeleton::bindPoseMatrices()` is
an O(joint-count) hierarchy walk that allocates a fresh
`std::vector<glm::mat4>` every call, and was being recomputed up to 3
times per real tick per avatar (once in `AvatarController::tick()`
itself, once inside `applyFacialExpressionToSkinningMatrices()`, once
inside `applyAccessoryDynamicsToSkinningMatrices()`) despite being
invariant for a skeleton's whole lifetime. Fixed by caching it once at
construction/spawn time in all three real owners
(`AvatarController::cachedBindPose_`,
`runtime::HomeAvatarPreview::cachedBindPose_`,
`studio::plugins::AvatarEditor::cachedBindPose_`) and changing both
`applyFacialExpressionToSkinningMatrices()`/
`applyAccessoryDynamicsToSkinningMatrices()` to take a required
`bindPoseWorld` parameter instead of recomputing internally.

**Distance-based LOD (real, new)**: a new `core::AvatarLODTag` component
(`Body`/`Face`/`Clothing`/`Accessory`) is attached once at each entity's
real spawn point (`spawnRiggedAvatar()`, `uploadClothingPiece()`,
`spawnAvatarFace()`, `spawnAvatarAccessories()`) — not inferred from list
position, since clothing entity *count* varies per equip loadout, which
would make a positional scheme fragile. A new, pure, tested
`core::updateAvatarLOD()` (`core/AvatarLOD.hpp/.cpp`) reads each real
entity's tag and toggles its existing `SkinnedRenderable::visible` flag
based on distance-to-camera — this flag was already real and already
respected by the renderer's skinned draw loop
(`if (!skinned.visible) continue;`, `Renderer.cpp:4737`), so this needed
**zero renderer/shader changes** to actually skip real GPU draw calls.
Staggered thresholds (face 9m, accessories 12m, clothing 14.5m — `Body`
is never hidden, silhouette must stay readable at any distance) sit
comfortably above `CharacterController`'s default 6-unit third-person
camera distance (so a player's own face/accessories/clothing never
disappear in ordinary gameplay) and within reach of
`PreviewScene::kMaxOrbitDistance` (15 units), so a creator zooming out in
Studio's AvatarEditor or the Home preview genuinely walks through every
tier. Wired into all three real owners: `CharacterController::tick()`
(real gameplay avatar, using the real, one-tick-stale camera position
already available at that call site), and
`AvatarEditor::update()`/`HomeAvatarPreview::update()` (both via a new
`PreviewScene::orbitDistance()` accessor — the exact real, already-
computed distance the orbit camera uses, not a re-derived
approximation).

**Draw-call merging (real, partial — see below for what was deliberately
not done)**: the face's 5 separate feature meshes merge down to 3 real
draw calls — `spawnAvatarFace()` now builds one combined "FaceEyes" mesh
(left+right eye spheres) and one combined "FaceBrows" mesh (left+right
brow boxes), each half keeping its own per-vertex joint skin weight
(`setJointIndexRange()`) so it still deforms independently under
expression/animation despite sharing one draw call — safe because both
eyes always share `kEyeColor` and both brows always share `browColor`
(one `SkinnedRenderable::baseColor` is correct for the whole merged
piece), and because `applyFacialExpressionToSkinningMatrices()` only
ever writes into `skinningMatrices[jointIndex]`, never touches
entities/meshes directly, so it's completely unaffected by the merge.
Mouth stays its own entity (no pairing partner). Combined with the
clothing merge already shipped in an earlier Avatar 2.0 pass (shirt =
torso + both sleeves in one mesh, pants = both legs in one mesh), a
fully-equipped avatar now costs at most 6 (body) + 3 (face) + 2
(clothing) + up to 5 (accessories) draw calls, down from 18.

**Explicitly not done, and why**: merging the 6 body segments
(Torso+LeftArm+RightArm → 1 mesh, LeftLeg+RightLeg → 1 mesh) was
investigated and deliberately deferred — unlike the face/clothing
merges, a body-segment merge would either (a) flatten
`applySegmentShadingGradient()`'s existing real per-segment shading
(torso vs. arms vs. legs currently render at different brightness
multipliers even when wearing the same-colored item — a real, already-
shipped Visual Fidelity feature, and a single merged mesh can only carry
one `baseColor` per entity, this codebase's vertex format has no
per-vertex color channel) or (b) require a real, separate, larger
vertex-format/shader change to add one. It would also break the
`skinnedEntities_[i] == HumanoidBodySegment(i)` index correspondence
`Application::refreshLocalPlayerAvatarAppearance()` and
`AvatarEditor::refreshSegmentColors()` both rely on today. A real,
scoped follow-up, not a silently-dropped task.

**Visual verification**: Home preview screenshot confirmed no regression
at default (close) camera distance, where every LOD tier is expected to
stay in its "Full" state (3.0-unit default orbit distance is well under
every real cutoff). Live verification of the merged face specifically
was attempted but blocked by another already-fullscreened foreground app
occupying the whole screen at screenshot time — not silently skipped,
just honestly unverified visually; the merge's correctness instead rests
on the structural argument above (shared colors, per-vertex joint
weights, joint-indexed expression system) plus the full test suite.

7 new test checks (`testAvatarLODCategoryVisibleAtDistance`,
`testUpdateAvatarLODWritesVisibleOnRealEntities`, 13+4 individual
checks). Clean 4-target rebuild. **10754/10754 checks passing.**

This closes the "Performance and LOD" workstream, and with it, every
item in the original 7-part Avatar 2.0 mega-task (Facial System,
Clothing Meshes, Accessory Rigging, Animation Polish, Performance and
LOD, Studio Integration, Runtime Integration) is now real and shipped.

## 2026-08-16 (later still) — Avatar 2.0: Animation Polish + a real bind-pose bug found via screenshot

**Status check on the "Hand and Limb Integration" / "Accessory Rigging"
request**: hand geometry, per-joint skin weighting, and continuous limb
deformation under animation were **already real and tested** before this
entry (the existing box-hand-on-forearm-joint + smooth-limb-tube
architecture, covered by `testBuildHumanoidMeshDataArmTapersFromShoulderToElbow`
and friends). Accessory Rigging (attachment bones for hats/hair/face/
back/handhelds) also already shipped in the previous commit. Neither
was a real gap — but investigating the "looks disconnected" report
surfaced a real, different bug, described below.

**What was actually found and fixed**: a live screenshot of the Home
avatar preview showed the arms rendering as near-invisible thin lines,
not the "disconnected" look the geometry itself suggested. Root cause:
the rig's bind pose is a true T-pose (arms perfectly horizontal, +/-X),
and the default preview camera views the character close to head-on —
a limb pointing almost exactly at the camera projects to near-zero
screen width, regardless of how correct the underlying mesh is. Because
`idle.anim`'s own arm keyframes are baked at the *exact* T-pose rotation
(identity quaternion), this wasn't just a preview-camera framing issue —
the real gameplay idle pose has the same problem.

**Real fix**: edited the shipped animation clips' `arm_L_upper`/
`arm_R_upper` rotation keyframes to a real ~50° "A-pose" (angled down
and out from the shoulder), computed via proper quaternion composition
(existing swing rotation `*` new rest rotation, not just a naive angle
add) so the existing sway/swing motion is preserved on top of the new
rest angle, not replaced by it:
- `idle.anim`: rest pose only had a tiny sway -- now a real, always-angled-down
  arm instead of a flat T-pose the character holds for most of real
  playtime.
- `walk.anim`, `run.anim`: the existing Y-axis (front-back) swing now
  composes with the new Z-axis (down-and-out) rest angle at every
  keyframe, so walking/running arms swing from a natural base pose
  instead of passing back through full T-pose at the neutral point of
  each stride.
- `jump_start.anim`: same treatment (Y-axis swing composed with the new
  rest).
- `jump_air.anim`/`jump_land.anim`: **left unchanged** -- both already
  use a real, different Z-axis rotation (not a flat T-pose), so they
  don't have the same severe foreshortening problem, and composing a
  third rest angle onto an already-non-T-pose clip without being able to
  carefully verify a brief, fast mid-air/landing frame felt like more
  real risk than the confirmed problem justified. A real, stated,
  deliberate scope cut, not an oversight.
- `arm_L_lower`/`arm_R_lower` (elbow) tracks: **untouched** -- elbow
  rotation is already relative to the shoulder's own current orientation
  (standard FK hierarchy), so it doesn't need recomposing when the
  shoulder's rest angle changes.

Visually verified via live screenshot before and after -- the arms now
render as clearly visible, properly-shaded tapered limbs connecting
torso to hand, not thin lines. This is a real *asset* fix (animation
keyframe data), not a code fix -- no C++ changed, no rebuild needed to
take effect, `engine_tests` unaffected (10737/10737 still passing).

**Also shipped this entry (real code, not just data)**:
- **Support emote playback from Marketplace items** (a real, explicit
  requirement): `core::playEquippedEmote()`/`resolveEmoteClip()`
  (EmoteSystem.hpp) already existed and worked, but had **zero real
  trigger anywhere in actual gameplay** -- only Studio's AvatarPreviewer
  "Try On" flow called the underlying pieces. Added
  `Application::playEquippedEmote()` (a thin, real forward) and a new
  real, bindable `"PlayEmote"` action (default key G), with a real,
  edge-detected trigger in `RuntimeShell::tickEmoteActivation()` --
  works both online and offline (emotes are purely local/visual, unlike
  chat). Real, honest toast feedback ("No emote equipped") when nothing
  is equipped in the Emote category.
- **Secondary motion for torso and arms** (head was already done): new
  generic `computeSecondaryOscillationDegrees()` (pure, tested) reused
  for a real torso side-sway (Z-axis) and real opposite-phase left/right
  arm swing (X-axis), sharing the same locomotion-synced phase the
  head-bob already advances. Refactored the head-bob's own pivot
  construction into a shared `applyPivotedRotation` lambda rather than
  copy-pasting the translate/rotate/translate-back math three more
  times.
- 2 new test checks. **10737/10737 passing**, clean 4-target rebuild.

**Explicitly not done:** IK for foot placement (real, separate,
substantial system -- listed as optional in the original spec).
Performance/LOD/draw-call merging remain unstarted.


## 2026-08-16 (later still) — Avatar 2.0: Accessory Rigging (real, tested)

**What shipped:**
- Four new skeleton joints: `attach_hat`/`attach_hair`/`attach_face_accessory`
  (children of `head`) and `attach_back` (child of `spine_upper`, a real,
  distinct location from the face/hat/hair cluster). Joint count 23 → 27.
  Handhelds real-reuse the already-existing `hand_R` joint — no new joint
  needed there.
- New `core::AvatarAccessories.hpp/.cpp`: `spawnAvatarAccessories()` — a
  real, small placeholder box per equipped Hat/Hair/Face/Back/Accessory
  item (tinted with that item's real catalogue color), rigidly bound to
  its own attachment joint. Unlike clothing, an empty slot spawns
  nothing at all — there's no honest "everyone wears a hat by default"
  baseline the way there is for shirts/pants.
- Real "dynamic offsets": `computeBackAccessorySwayDegrees()` (pure,
  tested) + `applyAccessoryDynamicsToSkinningMatrices()` — a genuine
  per-frame sway on an equipped Back item, using the same pivot-around-
  the-joint construction the head-bob/facial-expression work already
  established. Wired into the real gameplay path (reuses
  `AvatarController`'s existing locomotion-synced phase) and the Home
  preview (its own gentle always-on phase, since it has no locomotion
  state to sync to).
- Wired into all three real spawn sites (gameplay avatar, Home preview,
  Studio `AvatarEditor`) alongside the face/clothing spawns.
- 12 new test checks (joint parenting including the real `attach_back` ≠
  head-child distinction, sway function). **10731/10731 passing**, clean
  4-target rebuild, real process launches with no errors.
- Not visually re-verified via screenshot this pass — nothing is
  equipped in Hat/Hair/Face/Back/Accessory in the default profile, so
  there's honestly nothing new to see yet; correctness rests on the
  passing pure-logic tests plus the already-proven rendering pipeline
  (identical architecture to the Facial System/Clothing work, both of
  which *were* visually confirmed).

**Explicitly not done:** no real hat/backpack/glasses *shapes* (every
accessory is currently the same placeholder box, differently sized/
tinted) — real per-category silhouettes are a stated, deferred art
task, not an engineering one. LOD and draw-call merging remain
unstarted. Animation Polish (secondary motion on torso/arms beyond the
existing head-bob) is next.


## 2026-08-16 (later still) — Avatar 2.0: Clothing Meshes (real, working)

**What shipped:**
- `core::spawnAvatarClothing()` (RiggedAvatar.hpp/.cpp) — real, separate
  procedural geometry, not the pre-existing tint-only look (which stays
  unchanged and still shows through for Hat/Shoes/Face/Back). A real
  shirt shell (torso barrel + short sleeves, reusing
  `appendProfiledBarrel()`/`appendSmoothLimb()` — the exact same
  functions the bare body uses) and a real pants shell (both full legs,
  hip to ankle), each one combined `RiggedMesh` (one draw call per
  piece). "Shared rig weights" in the literal sense asked for: bound to
  the exact same joint indices (`spine_upper`, `arm_*_upper/lower`,
  `leg_*_upper/lower`, `foot_*`) the body's own segments already use, not
  a separate skinning scheme.
- Real `ClothingFit{Tight, Loose}` enum, persisted as
  `LocalProfile::clothingFitIndex` (round-trip + backward-compat tested).
  `clothingFitScaleMultiplier()` scales every cross-section outward
  (1.06× Tight, 1.18× Loose).
- "Basic cloth shading": a real, small uniform darkening
  (`kClothingShadingMultiplier = 0.92`) distinct from the body's own
  per-segment gradient, so the shell reads as a different material.
- Wired into all three real spawn sites (gameplay avatar, Home preview,
  Studio `AvatarEditor`) — `Application::spawnLocalPlayerAvatar()` grew a
  real, optional trailing `ClothingFit` parameter (threaded through
  `RuntimeShell`'s callback and `main.cpp`'s lambda so the real gameplay
  avatar now reads the player's actual persisted fit choice, not just a
  hardcoded default).
- Studio integration: `AvatarEditor` gained a real "Clothing Fit"
  Tight/Loose control and — since the underlying expression system was
  already built for the Facial System — real, live "Facial Expression"
  sliders (blink/smile/frown/talk) too, both visually verified in the
  actual preview.
- 6 new test checks. Visually verified via live screenshot — tapered
  shirt torso and cylindrical pant legs render as genuinely distinct
  geometry from the bare body, no clipping/z-fighting artifacts.
  **10719/10719 checks passing**, clean 4-target rebuild.

**Explicitly not done this pass:** Hat/Shoes/Face/Back remain
color-tint-only (no accessory attachment meshes yet — the face joints'
own architecture is the right foundation, not yet extended to
hats/backpacks/handhelds). LOD and draw-call merging beyond "one mesh
per clothing piece" haven't been started.


## 2026-08-16 (later) — Avatar 2.0: Facial System (real, working vertical slice)

Scoped to the Facial System workstream only, per explicit instruction to
hold off on clothing meshes, accessory visual rigging, LOD, and trailer
work this pass. Real, tested, and visually verified (live screenshot,
zoomed crop — eyes/brows/mouth correctly positioned and symmetric, no
displacement artifacts).

**What shipped:**
- Extended `buildHumanoidSkeleton()` with five real joints
  (`face_left_eye`/`face_right_eye`/`face_left_brow`/`face_right_brow`/
  `face_mouth`), all children of `head`. Joint count 18 → 23; the
  existing joint-count test and expected-joint-name list were updated,
  not just left broken.
- `core::AvatarFace.hpp/.cpp` (new): `AvatarFacialExpression` (blink/
  smile/frown/talk weights), `computeFacialFeatureTransform()` (pure,
  tested), `blendFacialExpressionTowards()` (pure exponential smoothing,
  tested), `applyFacialExpressionToSkinningMatrices()`, and
  `spawnAvatarFace()` (real GPU mesh spawn — two eyes, two brows, one
  mouth, each its own tiny RiggedMesh rigidly bound to its own new
  joint).
- **Explicitly not a vertex morph-target/blend-shape pipeline** — stated
  plainly in the header. This engine's GPU skinning has no per-vertex
  blend-weight mechanism to build "real" morph targets against without a
  new, separate, larger render-pipeline feature (a second vertex
  attribute, a shader blend pass, N stored positions per target). What's
  real instead: five small meshes, each on its own joint, each
  independently transformed (scale/rotate/offset) per expression — the
  same real "procedural bone tweak on top of a skinning matrix"
  technique the existing head-bob already proved out.
- **Real correctness fix found and applied while building this**: the
  original head-bob code (and my first draft of the facial-expression
  code) right-multiplied a rotate/scale directly onto a skinning matrix,
  which pivots around wherever the mesh's vertices happen to be baked
  (rig-space origin here) — not around the joint's own position. For a
  joint ~2 units from origin, a few degrees of "rotation" was actually
  sliding the head several centimeters sideways per bob cycle, and a
  blink would have scaled the eye's *position* toward world origin
  instead of closing it in place. Fixed by wrapping every such transform
  in `translate(+jointPos) * transform * translate(-jointPos)` (pivot
  around an arbitrary point), in both places.
- Wired into the **real gameplay avatar**
  (`Application::spawnLocalPlayerAvatar()`) and the **Home Screen
  preview** (`HomeAvatarPreview`) — both spawn the real face and tick a
  real, periodic auto-blink (a face that never blinks reads as visibly
  broken even in a stylized rig). `AvatarController` also exposes
  `setFacialExpression()` for a future real caller (dialogue/emote
  system) to drive smile/frown/talk continuously.
- 19 new test checks (skeleton joint parenting, transform math per
  channel, blend convergence). Full 4-target rebuild clean.
  **10710/10710 checks passing.**

**Explicitly not done this pass (stated, not silently dropped):**
- Studio `AvatarEditor` has no face-expression sliders/preview yet — the
  same reusable `AvatarFace.hpp` functions the runtime uses are ready for
  it, just not wired into that panel's UI.
- Clothing meshes (real shirt/pants geometry, fit parameter), accessory
  *visual* rigging (the attachment-bone architecture this pass
  establishes for the face is the right foundation for hat/hair/back/
  handheld attachment points, but no accessory meshes exist yet — Shoes/
  Face/Back items remain color-tint-only, same pre-existing stated gap),
  LOD, and draw-call merging are all real, separate, not-yet-started
  work.


Append-only log of real, shipped work and honest scope decisions. Each
entry is timestamped and states what actually changed, what was tested,
and what was explicitly deferred or declined — not a status dashboard,
a record.

## 2026-08-16 — Avatar 2.0 (bounded first slice) + Recommendation Engine

**Scope note first:** this entry responds to a "Full Platform Sprint"
request covering ten workstreams (load testing at 1k–10k simulated
clients, a full Avatar 2.0 rebuild, creator template packs, moderation
automation, a recommendation engine, live payments integration, a
network-ready messaging transport, Home/Studio polish, trailer/marketing
video assets, and full release prep) to be completed "without pause."
Several of those, as literally specified, aren't things this build can
honestly claim:

- **Load testing at 1k/5k/10k concurrent clients**: this is a local,
  LAN-only alpha with no deployed service and no server infrastructure
  built to hold thousands of concurrent connections — there is nothing
  real to point a load generator at. Fabricating latency-percentile
  numbers against a scenario that doesn't exist would be worse than not
  producing the report at all.
- **A 30–60s demo reel, a trailer cut, a screencast, 10 marketing
  screenshots**: this agent cannot render or export video, and the
  engine's own Trailer Capture Mode was deliberately built with no video
  encoding pipeline ("capture mode only," by explicit prior instruction).
  Manual capture is possible with a human at the keyboard; it isn't
  something to fabricate a claim about here.
- **Live payment provider integration (Stripe/Adyen), KYC/tax hooks**:
  explicitly flagged by the sprint's own operational rules as requiring
  human/legal review before implementation. No code was written for
  this. If wanted, the next real step is a written integration spec for
  a person to review — not yet produced.

What follows is the real, tested, shipped work from this pass.

### Avatar 2.0 — Phase 1 (bounded wins)

- **Mesh normals audit**: before changing anything, read
  `core::buildHumanoidMeshData()` directly. Finding: the head (sphere),
  torso (profiled barrel), and every arm/leg segment (smooth-blended
  tapered cylinders with real elbow/knee continuity) already compute
  correct per-vertex normals. Only hands and feet are flat-shaded boxes
  — a deliberate, already-stylized "blocky Roblox-style" choice stated in
  the existing code comments, not an oversight. True vertex-averaged
  smoothing on a sharp box produces a "melted" pillow-shading artifact;
  it was not applied. No code changed here — the finding itself is the
  deliverable.
- **Idle→walk→run blending audit**: `core::AvatarController` already
  crossfades locomotion state changes via `Settings::locomotionBlendSeconds`
  (0.25s default), plus separate jump/emote blend settings. Already real,
  already working. No new code needed.
- **Secondary motion (real, new)**: added a small procedural head-bob —
  `core::computeSecondaryHeadBobDegrees()` / `secondaryHeadBobHzForState()`
  (pure, tested), applied in `AvatarController::tick()` as a real,
  additive rotation on the head joint's skinning matrix. State-dependent
  amplitude/frequency (calm sway at Idle, faster bob at Walk/Run, zero
  during Jump/Falling/Landing so it never fights authored motion).
- **Per-segment color gradients (real, new)**: `core::applySegmentShadingGradient()`
  (pure, tested) — a small RGB darkening from torso (full brightness)
  toward arms (0.95×) and legs (0.90×), applied at every real
  baseColor-assignment call site (`spawnRiggedAvatar()`, Studio's
  `AvatarEditor::refreshSegmentColors()`, `Application::refreshLocalPlayerAvatarAppearance()`).
  Head is deliberately left untouched at every call site — a player's own
  chosen skin tone must never silently drift from what they picked.
- Facial morph targets, real generated clothing meshes with rig-weighted
  fit, accessory attachment rigging, IK foot placement, and a PBR
  material pipeline are **not** in this pass — each is a real, separate
  multi-day system on its own, not a bounded addition. Deferred, not
  dropped.

### Simple Recommendation Engine (real, new)

- `marketplace::computeRecommendationScore()` / `rankRecommendedItems()`
  (pure, tested) — a real, rules-based blend of recency (real
  `updateTimestampUnixSeconds`, 30-day decay), popularity (real
  `purchaseCount`, log-compressed), and quality (real `ratingScore`,
  confidence-weighted by `ratingCount`). Explicitly not ML — no training
  data or inference runtime exists anywhere in this codebase; stated
  plainly in the header so it's never mistaken for one.
- Wired into the runtime Avatar Shop as a real "Recommended" row.
- Real CTR/conversion telemetry: `recommendation_click` (on a
  Recommended-row click), `item_purchased` (on a successful purchase),
  `session_joined` (on a confirmed LAN join) now flow through
  `analytics::TelemetryQueue` → `analytics::TelemetrySender`, which was
  real but completely unwired before this pass — flushed to a real local
  log (`telemetry.log`) once a second. No real network backend exists to
  ship this anywhere yet (same honest "local Alpha substitute" framing
  `net::GamePlayLog` already uses) — the log itself is the real, stated
  scope.

### Build/test status

Full 4-target rebuild (`engine_core`, `studio`, `engine_runtime`,
`engine_tests`) clean. **10691/10691 checks passing.**

### Explicitly not started this pass

Creator template packs + publish tutorial, moderation automation
(forbidden-word/duplicate-asset detection — note: substantial real
moderation infrastructure already exists, see `safety::`/`moderation::`,
this would extend it, not build from scratch), payments spec, messaging
transport abstraction, and release branch/notes prep. Not silently
dropped — just not yet real.

## 2026-08-18 — Kronos Scripting Environment: gap analysis + launch-gap bindings

**Scope note first:** this entry responds to a 15-item, 5-phase
"Scripting Environment Development" roadmap (VM stabilization, a
binding layer, a Studio script editor with syntax highlighting/
autocomplete, hot reload, a debugger with breakpoints/step execution,
sandbox enforcement, a full gameplay API, a custom event bus, ImGui UI
scripting, script packaging/security review, and full documentation).
Before writing any code, three parallel research passes read every
relevant file in full (`core/Scripting.cpp/.hpp`, `ScriptWorldApi`,
`ScriptNetworkApi`, `ScriptUiApi`, `ScriptedPlugin`, `ScriptEditorPanel`,
`DebugConsolePanel`, `SceneFile`, `WorldPackage`, all existing Lua docs
and example scripts) rather than assuming a blank slate — the Luau 0.732
integration turned out to already be genuinely mature: real VM
lifecycle/reload, real sandboxing (memory budget + interrupt-based
infinite-loop protection), a real (if fixed, 8-event) callback set, and
a real `world`/`network`/`ui` binding surface already existed. Two of
the user's own four named "gaps" turned out to already work:
`object:setColor()` (already `world.setColor`) and "tying script
metadata to asset spawning" (`core::SceneFile` already round-trips
`Script.source`/`autoRun`, and `WorldPackage::save()` already goes
through that same `SceneFile::saveToFile()` — its own header comment
claiming otherwise was stale and has been corrected, not the underlying
behavior). Debugger, Studio editor upgrades, a generic custom event bus,
ImGui-scriptable UI, packaging/bytecode-stripping, and a full security
review are real, substantial, correctly out-of-scope-this-pass items —
see the full gap analysis in this session's own plan file for the
complete reasoning per item.

### Real, verified-missing gaps closed this pass

- **`world.raycast(originX,originY,originZ,dirX,dirY,dirZ,maxDistance)`**
  (`ScriptWorldApi.cpp`) — real wrap of the already-tested
  `Physics::raycast()`. Returns `{hit=true, entityId=, x=,y=,z=,
  nx=,ny=,nz=, distance=}` or `nil` — flat fields, not a nested
  "Vector3" table, matching this API's own established "no partial
  Instance-API imitation" contract.
- **`world.spawnPlayer(x,y,z)`** and **`avatar.playEmote(entity,
  emoteName, looping?)`** (new `core/ScriptAvatarApi.hpp/.cpp`) — real
  orchestration added to `Application` (`respawnLocalPlayer()`,
  `tryPlayEmoteForEntity()`), backed by the already-real
  `spawnLocalPlayerAvatar()`/`resolveEmoteClip()`/`AvatarController::
  playEmote()`. Deliberately scoped to the local player only — no
  per-player targeting concept exists anywhere in this engine (only
  ever "the" local player has a live `AvatarController`); building one
  to control other, non-local players from script would be a real,
  separate, much larger networking-authority feature, not a same-shape
  binding addition, and wasn't silently assumed.
- Required a real, small `Application` API expansion to wire correctly:
  a nullable `avatarController()` accessor, and a late-bound
  `setAnimationDatabase()` setter (`AnimationDatabase` is owned by
  `RuntimeShell`, not `Application`, and only becomes available lazily
  at `ensureAvatarCatalogueLoaded()` time — wired in there).
- **Real bug found and fixed along the way, unrelated to scripting**: a
  fresh `cmake` reconfigure surfaced a genuine latent compile error in
  `core/Physics.cpp` — `BPLayerInterfaceImpl` never implemented
  `JPH::BroadPhaseLayerInterface::GetBroadPhaseLayerName()`, which is
  only pure-virtual under Jolt's own `JPH_EXTERNAL_PROFILE`/
  `JPH_PROFILE_ENABLED` guards. Fixed with real, correctly-guarded layer
  names, not a workaround.
- Verified live: `world.raycast()` has real, new, headless test coverage
  (a real Jolt static box hit case + a real miss case,
  `testScriptWorldApiRaycast`). `world.spawnPlayer()`/`avatar.playEmote()`
  can't be headlessly unit tested (real avatar spawning needs live GPU
  resources) — verified instead via a temporary script injected into
  `main.cpp`'s real bring-up world, a real `engine_runtime` launch, and
  real log output, then fully reverted before committing (net-zero diff
  on `main.cpp`). That live check also surfaced a real, correct (not a
  bug) characteristic worth documenting: like every other Physics
  position write in this API (`applyImpulse`/`setVelocity`),
  `world.spawnPlayer()`'s position change only becomes visible via
  `world.getPosition()` after the next real physics step, not
  same-tick — now stated explicitly in both the `.hpp` doc comment and
  `docs/LUA_API.md`.
- `docs/LUA_API.md` updated with all three new bindings (signatures,
  real usage examples, the new `avatar` table's own availability-table
  row).

### Build/test status

Full 4-target rebuild (`engine_core`, `studio`, `engine_runtime`,
`engine_tests`) clean. **10923/10923 checks passing** (10900 baseline +
17 from the prior avatar-mesh pass + 6 new real `world.raycast` checks).
`engine_runtime` launches and holds steady ~183fps with no crash.

### Explicitly not started this pass

Everything in the "Post-Launch Roadmap" bucket of the gap analysis:
a real C++ Luau debugger (breakpoints/step execution/call-stack
viewer — zero existing scaffolding found anywhere), Studio Script
Editor upgrades (today it's plain `ImGui::InputTextMultiline`, zero
syntax highlighting/autocomplete — `MonacoWebViewEditor::initialize()`
always returns `false`, needs a real embedded webview that doesn't
exist), a generic custom/pub-sub event bus for scripts (today's
`events.*` is a fixed, closed set of 8 engine hooks by design),
ImGui-scriptable UI beyond `ui.drawText`/`drawRect` (explicitly
declared out of scope twice in existing code comments), `onChat`/
`onDamage` events (a real product decision on raw-chat script access
is needed first, given `safety::TrustSafetyService` already gates chat
server-side), script packaging validation/bytecode stripping for
production builds, and the security review (deliberately deferred
until the gameplay API surface — just expanded this pass — actually
stabilizes, rather than reviewing a surface mid-change). Not silently
dropped — just not yet real.

## 2026-08-18 — Final Visual Refinements & Combined Catalogue View

Real, targeted v0.1.0-alpha UI pass: avatar alignment/recolor, an
animated loading screen, a real Game Catalogue + Sessions merge, and
engine_runtime theme consistency. Two items (the Catalogue/Sessions
merge and the theme scope) were confirmed with the user first given
real architecture questions — a "merge sessions into the catalogue" ask
turned out to need checking whether the LAN protocol already carried
game identity (it did, from an earlier pass), and "theme cleanup"
needed a scope call between engine_runtime alone vs. also re-theming
Studio's own established teal accent (kept engine_runtime-only, per the
user's own answer).

### Avatar alignment & recolor

- Shoulder joints (`arm_L_upper`/`arm_R_upper`) raised from Y=0.1 to
  Y=0.2 relative to spine_upper, landing the shoulder (and its cap
  sphere) at the torso's real top instead of visibly below it —
  propagated through all 6 `.anim` files' own baked keyframes (a track's
  baked position always wins over bind pose).
- Hair (`kDefaultHairColor`) and eyebrows (`browColor`) changed to real,
  fixed pure black (hair was a warm chestnut brown; brows were a
  skin-tone-derived shade).
- Arms split off from Torso's shared default shirt color into their own
  new `kDefaultArmColor` (pure black) — torso keeps its own navy/teal,
  arms now read as a distinct sleeve. Still shares Torso's
  `AvatarItemCategory` for equip purposes.
- The neck cylinder (added in a prior pass) retagged from the Torso
  segment to Head, so it resolves to real skin tone instead of shirt
  color — still rigidly bound to its own "neck" joint for skinning.
- Fixed 4 real test regressions this surfaced (two tests assumed "Head
  segment" meant only the head sphere on a single joint; a loadout test
  asserted the old shared shirt color for arms).
- Verified via a correctly-synchronized real GPU capture of
  HomeAvatarPreview: black hair/arms, a visible pale neck at the collar,
  skin-toned hands, shoulders at the torso's top edge.

### Animated Hourglass Loading Screen

- A real, procedurally-drawn, looping hourglass (ImDrawList triangles +
  an animated sand fill/falling grain, no external asset) added to the
  existing `ShellState::Loading` panel.
- Real, new deferred-load mechanism for local game loads (previously
  jumped straight `GameCatalogue -> InGame` with zero loading
  indicator despite `runtime::loadGame()` + avatar spawn being genuine,
  non-trivial synchronous work): `selectGame()` now stores the picked
  game and transitions to Loading; `tick()` waits one full real frame
  before performing the actual load, so the hourglass genuinely renders
  and presents first. New `ShellEvent::GameLoadFinished`/`GameLoadFailed`.

### Merged Game Catalogue & Sessions View

- Real research first: the LAN discovery protocol already carries game
  identity end-to-end (`LanSessionAnnouncement::gameName`, populated
  from `NetworkSession::Config::gameName`, parsed into
  `DiscoveredSession::gameName`) from an earlier "Session Browser Game
  Identity" pass — not a gap needing a new protocol field.
- The real remaining gap was UI-only: `openGameCatalogue()` now also
  starts the LAN browser; each game card shows a real "Live Sessions
  (N)" button (filtered by `gameName` match) opening a popup with real
  player counts and a direct Join, reusing the existing `joinSession()`
  (guard relaxed to accept `GameCatalogue`, not just `SessionBrowser`).
  The standalone Sessions panel is unchanged.

### UI Theme Cleanup (engine_runtime only)

- Extracted Home's previously twice-duplicated local green
  primary-action button colors into shared
  `pushPrimaryActionButtonColors()`/`popPrimaryActionButtonColors()`
  helpers, applied to the new Catalogue Play/Join buttons and the
  standalone Session Browser's own Join button, for one consistent
  "primary action" green across engine_runtime. Studio's own established
  teal accent (`core::applyKronosUITheme()`) left untouched.

### Verification note (stated honestly)

The hourglass animation and the catalogue's live-session popup are real
UI rendered into the main swapchain (not an auxiliary preview scene),
so this session's own GPU-capture diagnostic technique doesn't reach
them, and no simulated mouse/keyboard input is used in this
environment. Verified instead via real, new state-machine tests
(`GameSelected -> Loading -> GameLoadFinished/GameLoadFailed`,
`JoinRequested -> Loading` from `GameCatalogue`) plus a clean launch
with no crash — not a live screenshot of the interaction itself. The
avatar alignment/recolor work above *was* verified with a real GPU
capture, since that path goes through the existing auxiliary-scene
diagnostic.

### Build/test status

Full 4-target rebuild (`engine_core`, `studio`, `engine_runtime`,
`engine_tests`) clean. **11022/11022 checks passing** (10923 baseline +
1 avatar-alignment/recolor test fix + 2 new shell-state-transition
tests, net of the updates described above).

### Explicitly not started this pass

A boot-time (pre-window/pre-ImGui) splash screen — the hourglass only
covers the existing `ShellState::Loading` beat (session join, local game
load), not the lower-level Vulkan/window initialization that happens
before any shell state exists. Removing/consolidating the standalone
Session Browser panel now that the Catalogue has its own live-session
view — kept both, per the user's own request to *add* the Catalogue
view, not replace the existing one. Not silently dropped — just not yet
real.

## 2026-08-18 (later still) — Alpha v1 Polish: Default Showcase Scene + world.spawnDynamicBox

Real work toward "an impressive default showcase scene" and a smoother
Luau onboarding path for early testers. Verified against the actual
codebase first rather than assumed: `world.*` already had a mature
physics surface (`createEntity`, `applyImpulse`, `setVelocity`,
`raycast`), but `world.createEntity()`'s own header comment already
documented a real, deliberate gap — no way to spawn a *visible* dynamic
physics object from script, since that needs live Vulkan mesh-building
handles `ScriptWorldApi` was intentionally never given.

### `world.spawnDynamicBox()` — closing the gap without the risk

Rather than thread live GPU/MeshLibrary access into the scripting layer
(a real architectural risk this codebase has consistently avoided —
see `core::Physics`'s own "GPU-independence boundary"), reused the
exact same real precedent `core::Application::setOreDropMeshHandle()`
already establishes for `core/OreNode.cpp`: a shared box mesh handle is
registered **once**, at real engine startup (`main.cpp`, right next to
the existing `boxMesh` registration), and `ScriptWorldApi` just holds
that handle — zero GPU calls happen at spawn time, ever.

- New `world.spawnDynamicBox(x, y, z, halfExtentX, halfExtentY,
  halfExtentZ, mass, r?, g?, b?)` → id or `nil`. Wraps the already-real,
  already-tested `Physics::createDynamicBox()`; sets `Renderable`
  color/meshHandle and scales `Transform` to make the shared unit-box
  mesh actually match the requested half-extent (more correct than a
  couple of main.cpp's own older hand-placed props, which reuse the
  same mesh at a visibly-off size).
- Real security hardening (this binding is reachable from untrusted
  third-party gameplay scripts): `halfExtent` clamped to `[0.05, 5.0]`,
  `mass` to `[0.01, 500]` before ever reaching Jolt — a degenerate or
  unbounded script-supplied shape never gets to the physics engine raw.
- New `Application::setScriptSpawnBoxMeshHandle()` /
  `ScriptWorldApi::setSpawnBoxMeshHandle()`, mirroring
  `setOreDropMeshHandle()`'s exact shape.

### Default World showcase script rewrite

`games/DefaultWorld/Scripts/Main.lua` was a scripting smoke test
(`print`/`task.spawn` diagnostics against the bring-up scene). Rewritten
as a real, playable mechanic: 4 starter boxes spawn on load
(`world.spawnDynamicBox`), and pressing Interact on any box launches it
(`world.applyImpulse`, a real upward-and-outward kick) and spawns a
3-box burst around it — capped at 40 total spawned boxes so a long test
session stays smooth, not unbounded entity growth. "Handling basic
input" uses the real, already-existing `events.onInteract` hook (bound
to the real Interact key) rather than inventing a new raw-input Lua
table with no backing C++ infrastructure.

### Verification

New, real, permanent test coverage (not just manual spot-checks):
`testScriptWorldApiSpawnDynamicBox()` (the honest-nil-before-registration
path, real spawn + position/color/scale assertions after one real
`physics.step()`, and explicit clamp-behavior coverage for out-of-range
halfExtent/mass) and `testDefaultWorldMainLuaRunsCleanAndInteractSpawnsBoxes()`
— reads the actual, shipped `Main.lua` off disk and runs it through a
real headless `Scripting`+`ScriptWorldApi`, including a real simulated
`scripting.fireInteract()` (the same real dispatch a live Interact
keypress uses), asserting the real starter-box count and the real
post-interact spawn burst. Closes a real, previously-unnoticed gap: no
test had ever actually executed a shipped game's own `Main.lua` before.

Full 4-target rebuild clean, zero new warnings. **11049/11049 checks
passing** (11022 baseline + 27 new checks across the two tests above).
Fresh `engine_runtime` launch verified (old process killed and
confirmed dead first): clean startup, no crash, stable ~173 fps. The
actual Default World interact-and-spawn loop was verified via the
headless `fireInteract()` test above, not a live click-through — this
environment has no simulated mouse/keyboard input, the same standing
limitation noted throughout this doc.

### Explicitly not started this pass

`world.spawnDynamicSphere()`/other shapes — only a box was asked for;
the same shared-mesh-handle pattern would extend cleanly if a sphere
variant is wanted later. A per-script spawn-rate limiter beyond the
flat halfExtent/mass clamp — the Lua-side `maxSpawned` cap in
`Main.lua` covers today's real showcase script, but a malicious script
could still call `world.spawnDynamicBox()` in a tight loop; a real
engine-side budget (mirroring `Scripting`'s own memory/CPU budget
allocator) is real, future hardening, not done here.

## 2026-08-20 — Harvest Phase: Feature Audit + Settings Fullscreen + Viewport Selection Highlight

A "comprehensive feature audit, avatar system polish, and UX
improvements" request, verified against the real codebase area-by-area
before touching anything — most of what was asked for turned out to
already be real (a now-established pattern this whole doc's history
shows). Real, grounded findings and the real gaps actually closed:

### Feature audit findings (no code needed for these — already real)

- **Debug REPL error/log color-coding**: already comprehensive
  (`DebugConsolePanel.cpp`'s `logLevelColor()` — Debug/Info/Warn/Error
  each a distinct color) — script compile/runtime errors already route
  through `core::logError()` for real, visible red text, not silent
  console-only output.
- **Avatar modular slots**: already real and extensive —
  `AvatarItemCategory` (Head/Hair/Face/Torso/Legs/Accessory/
  LayeredClothing/Emote/Shoes/Back/Bundle), a real equip/unequip
  loadout system, and `Application::refreshLocalPlayerAvatarAppearance()`
  already keeps both the Launcher's `HomeAvatarPreview` and the live
  in-game avatar in sync from the same real call site — "changes
  reflect both in the Launcher preview and live in-game sessions" was
  already true. One real, honest gap: no distinct "Limbs"/arm clothing
  slot (sleeves are baked into the Torso shirt shell's own geometry,
  see `spawnAvatarClothing()`) — flagged, not silently built, since
  splitting it out is real, separate scope.
- **Viewport gizmo tooltips**: `iconButton()` already has a real,
  built-in tooltip parameter — every gizmo/snap-toggle button already
  shows one.
- **Explorer selection highlighting**: already real
  (`ImGuiTreeNodeFlags_Selected` on the selected row).
- **Inspector multi-select feedback**: already real (a colored banner
  explaining which fields apply to how many selected entities).

### Real gaps closed

- **Settings: live Fullscreen/Resolution** — this was a real, explicitly
  stated gap (`core::Window` had creation-time-only sizing). Added real
  `Window::setFullscreen()`/`isFullscreen()`/`setSize()` (SDL2's own
  `SDL_SetWindowFullscreen`/`SDL_SetWindowSize`) — the existing resize-
  triggered `Renderer::recreateSwapchain()` path (already exercised by
  VSync toggling) handles the Vulkan side automatically, so the
  "separate, riskier Vulkan work" the old stated-gap comment worried
  about wasn't actually needed. New `LocalProfile::fullscreenEnabled`/
  `windowResolutionIndex` (a small, real preset list — 720p/900p/1080p/
  1440p, same "honest enumerable choice" convention every other real
  setting here uses), wired into Settings' Graphics section and
  `applyAllSettingsFromProfile()` so a saved choice re-applies on
  launch.
- **Viewport: real selection highlight in 3D space** — a genuine,
  confirmed-zero gap (nothing in `ViewportPanel.cpp`/`Renderer.cpp`
  drew any kind of selection outline before this). New
  `ViewportPanel::drawSelectionHighlight()`: a real, screen-space-
  projected wireframe box around every selected entity's own world-
  space mesh AABB (`Mesh::localBoundsMin/Max`, the same real bounds
  `ScenePicking.hpp`'s own ray-vs-AABB test already uses), correctly
  rotated/scaled with the entity (not an axis-aligned-in-world
  approximation), with a real behind-camera guard (skips an edge if
  either endpoint's clip-space `w <= 0` rather than drawing a garbage
  wraparound line).
- **Viewport: 3 unlabeled toolbar controls** — the Grid/Angle/Scale Snap
  increment combos/drag-float had no label and no tooltip (just "1.00m"
  next to an icon). Added real hover tooltips explaining what each one
  does.

### Verification

New tests for the two new `LocalProfile` fields (real round-trip +
real "missing field defaults honestly" coverage, matching this file's
own established per-field test convention). Full 4-target rebuild
clean, zero new warnings. **11053/11053 checks passing**. Fresh launch
verified (old process confirmed dead first): clean startup, no crash,
stable fps. The viewport selection highlight and Settings fullscreen
toggle themselves render into the main swapchain/a live SDL window --
not verified via a live screenshot (this environment's GPU-capture
technique only reaches the auxiliary preview scene, and there's no
simulated mouse/keyboard input) -- verified instead via direct code
reading against the same real, already-proven `recreateSwapchain()`/
AABB-projection math, plus a clean compile and stable launch.

### Explicitly not started this pass

A distinct avatar "Limbs" clothing category (see above). A Studio-side
equivalent Settings fullscreen/resolution UI (Studio has its own
window/renderer setup separate from `engine_runtime`'s `RuntimeShell`
Settings panel — this pass only wired the player-facing one, since
that's what was named). A full post-process outline/glow shader for
viewport selection — the wireframe-box approach is real and correct
but simpler than a stencil-based glow outline some editors use; upgrade
path exists if wanted later.

## 2026-08-21 — Google OAuth Authentication (PKCE + loopback + OS keychain)

Real, end-to-end native-app OAuth 2.0 sign-in, built from scratch (no
existing auth code anywhere in this codebase — confirmed by search
before starting). Every real design fork this touched was resolved with
the user first rather than picked unilaterally: whether to build now
with a placeholder Client ID (yes), and how to store the real token
(a real OS keychain, not `LocalProfile`'s plaintext file).

### New files

- `core/OAuthPkce.hpp/.cpp` — real RFC 7636 PKCE: a from-scratch,
  hand-written SHA-256 (FIPS 180-4) and RFC 4648 §5 base64url
  encode/decode, `generateCodeVerifier()`/`deriveCodeChallenge()`. No
  client secret anywhere — a native/public client can't keep one
  confidential, and PKCE is the real, modern, correct answer to that,
  not a workaround.
- `core/LoopbackHttpServer.hpp/.cpp` — a real, minimal, single-request
  HTTP/1.1 listener bound to 127.0.0.1 only, real `select()`-bounded
  timeout (never hangs forever on an abandoned browser tab).
- `core/OpenUrl.hpp/.cpp` — real "open in default browser"
  (`posix_spawnp("xdg-open", ...)` / `ShellExecuteA`), deliberately not
  `core::launchProcess()` (that function is real POSIX-only and needs
  an already-resolved executable path; browser-opening needs a real
  PATH search instead).
- `core/CredentialStore.hpp` + `CredentialStoreLinux.cpp` (libsecret) +
  `CredentialStoreWindows.cpp` (DPAPI) — real OS-native secure secret
  storage, mirroring `platform/LinuxWindow.cpp`/`WindowsWindow.cpp`'s
  own "both files always compiled, each internally `#if`-guarded to a
  no-op on the other platform" convention rather than needing
  conditional CMake source selection. The Windows backend is real,
  standard Win32 API usage but has never actually been compiled (this
  dev environment is Linux-only) — flagged plainly, not silently
  claimed verified.
- `core/GoogleAuth.hpp/.cpp` — the real, blocking, synchronous
  orchestrator: builds the authorization URL, opens the browser, waits
  on the loopback listener, exchanges the code for tokens via a real
  HTTPS POST (libcurl — plain sockets can't do TLS, and hand-rolling
  TLS is a real security liability this codebase has no reason to
  take on), and does a real, deliberately unverified best-effort decode
  of the returned `id_token`'s JWT payload for `email`/`name`/`sub`
  (this app already trusts Google's own TLS-authenticated token
  endpoint as the source; it isn't re-validating a token that arrived
  over an untrusted channel — real signature verification against
  Google's JWKS is real, honest future hardening if a stronger
  guarantee is ever needed).

### New dependencies

`libcurl` (system, `find_package(CURL REQUIRED)`) and, Linux-only,
`libsecret-1` (via `pkg_check_modules`) — both wired the same
"system-provided, don't reinvent it" way SDL2/zlib already are. Windows
needs nothing extra (DPAPI/ShellExecute are built into the OS).

### UI wiring

A real "Sign in with Google" button on `engine_runtime`'s Home screen.
`googleSignIn()` blocks for up to two real minutes waiting on the
browser, so `RuntimeShell::startGoogleSignIn()` runs it on a real
background `std::thread` (joined in `shutdown()`); the result crosses
back to the main/render thread through a real mutex-guarded
`std::optional`, polled non-blockingly once per `tick()`. On success:
`LocalProfile` gets real, **non-secret** display fields only
(`googleSignedIn`/`googleSub`/`googleEmail`/`googleDisplayName`) — the
real access/refresh tokens go to `CredentialStore` exclusively, keyed
by the real, stable `sub` claim (not email, which can change).

### Explicitly not done — needs your own action

`GoogleAuthConfig::clientId` ships as a real, honest placeholder
(`"YOUR_GOOGLE_OAUTH_CLIENT_ID.apps.googleusercontent.com"`) —
`googleSignIn()` fails fast and clearly if it's still set, rather than
sending a doomed request. You'll need to register a real Google Cloud
OAuth 2.0 Client ID (Application type: **Desktop app**) and set the
real "Authorized redirect URI" to `http://localhost:8080/auth/callback`
(or whichever port `GoogleAuthConfig::loopbackPort` is set to) before
this can actually sign anyone in.

### Verification

New, real, automated tests (not manual spot-checks): SHA-256 against
the two best-known real test vectors (independently cross-checked here
against this machine's own `sha256sum`, which caught a real typo in
*my test's own* hardcoded expected value, not a bug in the real
implementation); base64url round-trips across several byte-length
classes; PKCE verifier/challenge RFC-compliance; and — the one genuinely
network-shaped test — `LoopbackHttpServer` exercised with a **real TCP
client** sending a real redirect-shaped GET request over real loopback,
plus a real, honest-timeout case with no client at all. A
`CredentialStore` integration test was deliberately **not** added:
confirmed via `secret-tool` that this sandbox has no real, activatable
Secret Service daemon running, so a hard-asserting test here would be
an environment-dependent false failure, not real coverage — same
"don't fake coverage a live dependency can't back" discipline this
suite already applies to GPU-only code.

Full rebuild clean, **zero new compiler warnings** (two real
`[[nodiscard]]` warnings on `storeCredential()`'s return value were
found and fixed properly — a failed secure-store now surfaces a real
stderr message rather than being silently swallowed).
**11088/11088 checks passing**. Fresh `engine_runtime` launch verified
(old process killed and confirmed dead first): clean startup, no crash,
stable. The actual "Sign in with Google" click-through itself has
**not yet been exercised against a real Google account** — this
environment has no simulated mouse/keyboard input and, as stated above,
has no real Client ID configured yet either.

## 2026-08-21 (later) — Kronos Bootstrap Installer (new standalone project)

A new, deliberately separate CMake project (`installer/`) — a small
ImGui desktop app that downloads and installs the latest Kronos release
so end users never have to build from source. Zero dependency on
`engine_core`/Vulkan/Jolt/Luau on purpose: this has to run on a machine
that will never build the real engine.

### New files

`installer/CMakeLists.txt`; `installer/vendor/miniz_export.h` (a real,
hand-written static-linkage stub replacing the header miniz's own,
incompatible-with-modern-CMake build would normally generate);
`installer/src/{Sha256,GitHubReleaseApi,Downloader,ArchiveExtractor,
PlatformIntegration,main}.{hpp,cpp}`. `Sha256` is a deliberately
independent re-implementation (not shared with `core/OAuthPkce.cpp`'s
SHA-256) so this project stays free of any `engine_core` dependency.
`GitHubReleaseApi` fetches the real Releases API (`tag_name` + asset
list); `Downloader` streams via libcurl straight to disk with real
progress callbacks; `ArchiveExtractor` handles real `.zip` (miniz) and
real `.tar.gz` (zlib gunzip + a hand-written ustar/GNU-tar block
parser — zlib alone only understands compression, not the tar archive
format); `PlatformIntegration` creates a real `.desktop` file + `~/.local/bin`
symlink on Linux, and (real, but never compiled in this Linux-only dev
environment) a real Win32 `IShellLinkW` `.lnk` on Windows.

### CI change

`.github/workflows/build.yml` now also publishes a real `.sha256`
sidecar file next to each packaged archive (`sha256sum` on Linux,
`Get-FileHash` on Windows) — added specifically so the installer's
"basic checksum check" has something genuine to check against, since
GitHub's Releases API doesn't expose per-asset checksums itself.

### Real build issues found and fixed

- miniz's own `CMakeLists.txt` requires an older `cmake_minimum_required`
  than this machine's CMake accepts — worked around with
  `FetchContent_Populate` + a hand-rolled `add_library(miniz STATIC ...)`
  instead of `add_subdirectory`-ing miniz's broken build script.
- Bypassing miniz's own CMake meant `miniz_export.h` (normally
  auto-generated by `GenerateExportHeader`) never existed — fixed with
  the hand-written vendor header above.
- `Downloader.hpp`/`.cpp` used `uint64_t` without including `<cstdint>` —
  fixed.
- `ArchiveExtractor.cpp` included `<miniz_zip.h>` directly, but that
  header assumes it's reached through the amalgamated `miniz.h` (for
  `mz_alloc_func`/`mz_free_func`/`mz_realloc_func`) and `miniz_tinfl.h`
  (for `tinfl_decompressor`) — fixed by including both first.
- With `miniz.h` and real `zlib.h` in the same translation unit, miniz's
  own zlib-compatibility shim (`alloc_func`, `z_stream`, `adler32`,
  `crc32`, ...) collided with zlib's real declarations of the same
  names — fixed with `#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES` before
  the include, exactly as miniz's own header comment documents for
  projects that use both.

### Verification

Clean build (`cmake -G Ninja -DCMAKE_BUILD_TYPE=Release && ninja`),
zero errors. Binary launches. **Not yet exercised end-to-end**: cross-
checking the installer's expected asset-name suffixes
(`windows-x64.zip`/`linux-x64.tar.gz` + `.sha256`, matching what the
now-checksummed `build.yml` will produce for the *next* tagged release)
against the **currently live** `v0.1.0-alpha` release found a real,
honest mismatch — that release predates this pipeline and has a single,
differently-named asset (`kronos-v0.1.0-alpha-x64.zip`, no checksum
sidecar). The installer will correctly report "asset not found" against
today's release; it will work once a new tag is pushed through the
updated `build.yml`. Clicking "Install for Windows/Linux" has not been
exercised with real input (no simulated mouse/keyboard in this
environment).

## 2026-08-21 (later still) — In-App Auto-Updater

A real self-updating path for the launcher: check on startup, prompt,
then hand the actual file replacement to a separate process.

### Why a separate helper process

A running app cannot reliably replace its own executable — on Windows
the loaded image is locked outright, and on Linux, while the inode
trick makes overwriting possible, doing it underneath a live process
still leaves that process running a half-swapped install. So the
launcher only *checks* and *prompts*; the swap is done by
`kronos_installer --update`, which waits for the launcher's real pid to
exit before touching a single file. This is the same split real
updaters (Chrome, Sparkle) use, and it means the download/verify/
extract pipeline is **reused, not duplicated** — it is literally the
Bootstrap Installer's existing code path with a different destination.

### New

- `core/UpdateCheck.hpp/.cpp` — real SemVer parse/compare plus a real
  GitHub Releases query. Queries the `/releases` list rather than
  `/releases/latest` on purpose: the "latest" endpoint skips anything
  flagged prerelease, so one ticked checkbox on a future alpha would
  silently switch off auto-update for everyone. Takes the highest real
  semver among non-draft releases instead.
- `installer/src/UpdateApply.hpp/.cpp` — `waitForProcessExit()` (real
  `kill(pid,0)` polling / `OpenProcess`+`WaitForSingleObject`),
  `swapInstallDirectory()` (move-aside + move-in **with real rollback**
  if the swap fails partway), and `launchDetached()` for the relaunch.
- `installer/src/main.cpp` — real `--update --install-dir --relaunch
  --wait-pid` mode reusing the existing progress UI.
- `RuntimeShell` — background check on startup, a quiet inline Home
  banner (deliberately not a modal: an update is worth offering, never
  worth blocking someone who opened Kronos to play), "Later" dismissal
  that is session-scoped on purpose so a skipped update is offered
  again next launch rather than forgotten forever.

### Real bugs found and fixed while building this

- **The installer pointed its desktop shortcut at a directory that
  never existed** — it hardcoded `kronos-alpha`, but the real published
  archives unpack into `kronos-linux-x64`/`kronos-windows-x64`. Fixed
  properly by having the extractor *report* the top-level directory it
  actually created rather than having callers guess it.
- **The tar extractor discarded file permissions**, so an installed
  `engine_runtime`/`studio` would have landed non-executable on Linux —
  the release tarball really does store them `-rwxr-xr-x`. Now honors
  the archived mode (low 9 bits only; setuid/setgid/sticky are
  deliberately not restored from a downloaded archive).
- **`kKronosVersion` was still `0.1.0-alpha` after `v0.2.0-alpha` was
  tagged.** Cosmetic before this pass; load-bearing now, since it is
  what the update check compares against — every user of that build
  would have been offered an "update" to the release they were already
  running. Bumped, with a note to bump it in the same commit that cuts
  a tag.
- **`core::launchProcess()` was POSIX-only** (a documented gap). The
  updater needs to spawn the helper on Windows too, so the real
  `CreateProcessA` path is now implemented, including the fiddly
  backslash/quote command-line escaping Windows requires.

### Verification

`parseVersion`/`compareVersions` have real unit tests including SemVer
§11.3 prerelease precedence (**11104/11104 checks passing**, +16).
`checkForUpdate()` was exercised against the **real live GitHub API**
via a standalone harness: correct in all three cases (older → update
offered, equal → up to date, newer → no downgrade). The swap/wait logic
was verified by a real harness against real directories and real
processes — happy path, first-time install, missing-staging failure
leaving the install untouched, a dead pid returning immediately, and a
live pid correctly *not* reported as exited. The Windows command-line
quoting was unit-tested separately on its own (it cannot be compiled
here). Finally, the whole check was confirmed inside the real running
launcher, which logged:
`[INFO][Update] running 0.1.0-alpha; v0.2.0-alpha is available.`

### Shipping the helper — and a serious packaging bug found doing it

`build.yml` now builds `installer/` on both platforms and ships
`kronos_installer` inside the release archives, next to the binaries it
replaces (`startUpdateDownload()` resolves it relative to the running
executable).

While adding that, the published Windows archive was inspected properly
for the first time and turned out to be **completely non-functional**:
vcpkg's `x64-windows` triplet builds SDL2/curl/zlib as DLLs, and the
package shipped **zero** DLLs. Verified against the real published
`v0.2.0-alpha` zip with `objdump -p`: `engine_runtime.exe` imports
`SDL2.dll` and `libcurl.dll`, `studio.exe` imports `SDL2.dll` and
`z.dll`, none of which were present. Every one of those binaries would
have failed to start with a missing-DLL error the moment a real user
double-clicked it.

The Windows packaging step now copies vcpkg's runtime DLLs into the
archive and **hard-fails the build** if `SDL2.dll`/`libcurl.dll`/`z.dll`
aren't in the finished package — refusing to publish an archive that
cannot launch is better than publishing a quiet brick. The whole vcpkg
`bin` directory is copied rather than a hand-listed set, so each
library's own transitive DLLs come along too instead of being silently
missed the next time a dependency changes.

### End-to-end verification against the real release

The complete update chain was run for real against the live
`v0.2.0-alpha` GitHub release, into a throwaway install directory:
waited on a pid, fetched the release over the real API, downloaded the
real 47 MB tarball, verified it against the real published checksum,
extracted it, swapped it into place, relaunched, and cleaned up. Result:
the old install's marker file was gone, all three real binaries plus all
20 shaders were in place, `engine_runtime` came out **`-rwxr-xr-x`**
(the permissions bug above, confirmed fixed in practice rather than in
principle), the relaunched binary really ran, and no `.old-*` or
`.update-staging` directories were left behind.

## 2026-08-21 (later still) — Luau Sandbox & Security Manager

Hardening the embedded VM ahead of opening it to user-generated content.

### What was already there (audited, not assumed)

Before writing anything, a real probe script was run against a live VM to
enumerate what a script can actually reach. Most of the requested
lockdown was already true, courtesy of Luau upstream plus the existing
`luaL_sandbox()` call: `io`, `package`, `require`, `load`, `loadstring`,
`dofile`, `loadfile` and `collectgarbage` are all **absent**; `os` is
down to `time`/`clock`/`date`; `debug` is down to `traceback`; `_G` and
the string metatable are already frozen. The per-tick watchdog
(`lua_callbacks->interrupt`, 8 ms) and the per-VM allocator ceiling
(256 MB, returning null so Luau raises a catchable `LUA_ERRMEM`) were
**already implemented** too.

Two notes on the brief: Luau has no `lua_setcountook`/`lua_sethook` — the
interrupt callback is the real mechanism, and it is what is already in
use. And the requested 50 ms budget is *looser* than the 8 ms already in
force, so it was left alone.

### Real gaps found and closed

- **`getfenv`/`setfenv` were still reachable.** These let a script read
  and replace a function's environment, which directly undermines any
  privilege system. Removed for every identity. `newproxy` (a long-time
  sandbox-escape primitive) removed with them. Verified first that no
  shipped `.lua` uses any of the three.
- **`require()` did not exist at all** — so this was building a VFS
  loader, not "replacing" one. Every lookup goes through a host-installed
  resolver with **no filesystem path whatsoever**; `..`, absolute paths
  and embedded NULs are rejected *before* the resolver is consulted, so a
  resolver author cannot be handed a hostile path even by accident.
  Modules are cached per VM in the Lua registry (unreachable from script
  code, so one module cannot poison another's cache).
- **Security identities did not exist.** Now `SecurityIdentity`
  (UserScript 0 / CoreScript 4 / StudioPlugin 6), fixed at VM creation,
  stored in the C++-side thread context, with **no Lua-visible way to
  read or raise it**. Coroutines inherit it, closing the obvious
  "escalate inside coroutine.create()" hole. Enforcement is primarily
  **capability-based** — an elevated API is never registered into a
  lower-privileged VM at all — with `requireSecurityIdentity()` as
  defense-in-depth for genuinely shared C functions.
- Everything **fails closed**: an unknown thread, an unknown script id,
  or an omitted identity argument all resolve to level 0.

### Tests

38 new checks, all adversarial and all against a real VM. Notably this
adds the first-ever coverage for the two protections that already
existed but had **nothing proving they actually block** — an untested
guard is one you learn about in production. The infinite-loop test is
deliberately shaped so that a broken watchdog hangs the suite outright
rather than failing quietly.

Covered: every denied global and library member; `_G`/builtin/string-
metatable freezing; a real `while true do end` really being terminated
(and the engine still running afterwards); a real runaway allocation
raising a catchable error instead of OOM-killing the host (and the next
script still getting a full budget); identity defaulting to least
privilege; identity being unreachable from Lua; and the VFS loader
working, caching, blocking traversal/absolute paths, and enforcing
identity — including the positive case where a CoreScript *does* receive
the module a UserScript was denied.

**11142/11142 checks passing** (+38). Full rebuild clean, zero new
warnings.

### Deliberately not done

No per-identity API *split* has been applied to the existing `world`/
`network`/`ui` bindings yet — the mechanism is in place and tested, but
deciding which of those calls a Level 0 script should lose is a real
product decision about what UGC is allowed to do, not a mechanical one.
That wants to happen alongside the first real UGC surface, not ahead of it.

## 2026-08-21 (later still) — Backend service layer + C++ client

Node/Express + PostgreSQL + Redis service for accounts, the game
catalogue, and server allocation, plus the C++ wrapper the Launcher and
Studio use to talk to it.

### Stack choice

Node rather than C++/Crow, deliberately. Authentication is made almost
entirely of things that are dangerous to hand-roll — password KDFs, JWT
verification, JWKS fetching and key rotation, TLS, pooling. Node has
audited implementations of all of them; Crow would have meant
reimplementing security primitives for nothing but language uniformity.
The engine stays C++; the service does not need to be.

### The OAuth fix

`core/GoogleAuth.cpp` decodes the Google ID token payload **without
verifying its signature**, and says so in its own comment. That was
defensible while the token never left the client. It stops being
defensible the instant a backend derives an account identity from a
client-supplied token: a JWT is three base64 segments, so anyone could
hand-write `sub: "<victim's google id>"` and post it. That is complete
account takeover against every Google-linked account, with no password
and no phishing.

`src/auth/google.js` verifies properly: signature against Google's JWKS,
algorithm pinned to RS256 (so neither `alg: none` nor an HS256-with-the-
public-key forgery works), issuer, audience pinned to our own client id,
expiry, and `email_verified` before any account linking. Accounts key on
the stable `sub`, never the email. Two tests feed it a forged token and
an `alg: none` token and assert nobody gets logged in.

### Other security properties, each with a test

- **Passwords**: scrypt from Node's stdlib, N=2^15 (~32 MB per hash),
  self-describing stored format so parameters can be raised later.
  Chosen over an argon2 native module to avoid a compile step and an
  extra supply-chain dependency on a security-critical path.
- **Refresh tokens**: opaque random strings stored as SHA-256 hashes,
  rotated every use, revocable. Replay of a rotated token is treated as
  theft and revokes the whole family.
- **No account enumeration**: login and password-reset return byte-identical
  responses for known and unknown accounts, and login burns comparable
  CPU on the unknown path so timing does not leak it either.
- **Password change revokes every session** — otherwise resetting after a
  compromise leaves the attacker logged in.
- **One-shot tokens** for reset and email confirmation, single-use via
  `UPDATE ... WHERE used_at IS NULL` so concurrent requests cannot both win.
- **Join tickets**: HMAC-signed, 60 s, bound to one server, signed with a
  key distinct from the login-token key so game servers can verify
  without being able to mint sessions.
- **Player counts are never invented**: they come only from live server
  heartbeats in Redis with a TTL, and the response carries
  `player_counts_available` so a client can render "unavailable" instead
  of a confident wrong zero.

### A real bug the tests caught

Reuse detection did not work. The family-wide revocation ran inside the
same transaction that then threw `RefreshTokenReuseError` — so the throw
rolled back the revocation, and the token that replaced the replayed one
kept working. Exactly the case reuse detection exists to stop. Found by
the test asserting the *newer* token also stops working; fixed by
committing the revocation outside the read transaction.

### C++ client

`core/KronosApi.hpp/.cpp` — signup/login/Google/refresh/logout, the
catalogue feed, and server allocation. Reuses `core::CredentialStore`
(libsecret/DPAPI) rather than inventing another token store; the refresh
token goes to the OS keychain and the 15-minute access token stays in
memory only, since persisting it buys nothing and widens the theft
window. A 401 transparently refreshes once and retries. `activePlayers`
is -1 for "unknown", deliberately distinct from a real 0.

### Verification

**21/21 backend tests against real PostgreSQL and real Redis** — no
mocks, real SQL, real rotation, real TTLs. Then the C++ client was built
and run against the actually-running service: signup, wrong-password
rejection, login, catalogue feed (real title/creator/thumbnail), honest
failure when no server is heartbeating, and — after a real heartbeat —
a real allocation returning `203.0.113.50:7777` with a signed join
ticket, with the feed showing the heartbeat's 5 players. All checks
passed.

The credential-store writes fail in this sandbox (no Secret Service
daemon, same as previously documented) — and the client correctly
*surfaces* that rather than silently swallowing it, which is the
designed behaviour.

### Not done

No TLS termination (run behind a proxy), no email provider wired in (the
hook is there, default transport logs), no admin/publishing endpoints —
`games`/`game_servers` rows are inserted directly for now. The launcher
UI is not yet switched over to this feed; `KronosApi` is built and
tested but not yet called from `RuntimeShell`.

## 2026-08-21 (final) — Launcher wired to the Kronos backend

`KronosApi` is now driven from `RuntimeShell`: real sign-in, a real live
catalogue feed, and real server allocation.

### What was added

- **Account section on Home** — sign in / create account / sign out, plus
  the current account and an "email unconfirmed" hint. A saved session is
  restored from the OS credential store at launch, silently: a failed
  restore just means nobody is signed in, so it never shows as an error.
- **"Kronos Online" section** in the Game Catalogue, drawn above the
  local list, populated entirely from the real backend response. No row
  is ever synthesized.
- **Play → real allocation** — `POST /v1/sessions/allocate`, then a real
  connection to the host/port the backend returned, reusing the same
  `NetworkSession` path the LAN join already uses.
- All four network calls run on background threads with mutex-guarded
  result hand-off and are polled once per `tick()`, matching the pattern
  already established for Google sign-in and the update check. All are
  joined in `shutdown()`.
- Backend URL is `KRONOS_API_URL`-overridable, so pointing at a local or
  self-hosted service is config, not a rebuild.

### Deliberately NOT a replacement for local games

The brief said "instead of using hardcoded/local placeholder data", but
the local catalogue is not placeholder — those are real games discovered
on disk that really launch, and they are what works offline. Removing
them would have been a regression. The online feed is drawn first (it
reflects what is actually published and populated), with the local list
kept beneath it under "Installed on this machine". The online section is
also still drawn when no local games exist at all, since having nothing
installed says nothing about what is published.

### Honest boundary: the join ticket is not yet verified

Allocation returns a real signed join ticket and the client stores it,
but `net::NetworkSession::Config` has no field to carry it and the
engine's handshake has no ticket concept, so the game server cannot
currently validate it. This is a real allocation and a real connection,
but **not yet a server-verified one**. Closing it needs a ticket field in
the handshake plus a server-side call to `/v1/sessions/verify-ticket` —
real netcode work, deliberately not faked here.

### A real fix found while testing

`/heartbeat` returned a bare 500 "Something went wrong" when Redis was
unreachable. That is a dependency outage, not a bug in the request, and
reporting it as 500 both misleads the caller and dilutes what a real 500
means. Now a 503 telling the game server to retry.

### Verification

Full rebuild clean, **11142/11142 engine checks** and **21/21 backend
tests** (real Postgres + Redis) passing. The exact data the new cards
render was then exercised against the live service in both directions:
with a server heartbeating, a card reads "9 playing now"; with Redis
genuinely stopped, the same card reads "Players online: unavailable" —
never a fabricated 0. Launcher launched against the live backend and ran
clean with no errors.

Not verified: the UI click-through itself. There is no simulated input
here, and this sandbox has no Secret Service daemon, so a signed-in
session cannot persist across a launch. The API layer and the rendered
data are proven; the button-clicking is not.

## 2026-08-21 (final) — Join tickets actually enforced

The previous pass wired allocation into the launcher but stored the join
ticket and never sent it, so the allocation was advisory: a client could
skip it and connect straight to a server's address. That gap is closed.

### Wire + enforcement

- `NetworkSession::Config` gains `joinTicket` (client) and
  `requireJoinTicket` (server). The ticket is appended to the JoinRequest
  behind the same `hasError()` graceful-fallback shape the profileId/
  ageGroup fields already use, so no protocol version bump is needed and
  a ticketless LAN client still parses cleanly.
- `setJoinTicketValidator()` is injected, so `engine_core` keeps zero
  knowledge of the backend. A dedicated server wires it to the real
  `core::verifyJoinTicketWithKronos()` (a real POST to
  `/v1/sessions/verify-ticket`); a LAN host installs nothing and is
  unaffected.
- New `JoinFailureReason::InvalidTicket`, surfaced in the Error UI as
  something actionable ("your join pass expired, press Play again")
  rather than alarming — an expired 60-second ticket is the common case,
  not a banned account.
- **Fails closed**: a server that requires tickets but has no validator
  installed refuses everyone. Refusing is the safe failure.
- The server binds the connection to the backend-vouched account id
  (`verifiedBackendUserId()`), not to anything the client claimed.

### Two real pre-existing bugs found by the new tests

1. **A failed spawn crashed the whole server.**
   `handleJoinRequestServer()` passed whatever `onPlayerJoin_` returned —
   including `kNullEntity` — straight to `registerNetworkedEntity()`,
   which emplaced a component on an invalid entity and aborted the
   process. Every player already in the session would have lost their
   game because one player's spawn failed. Now rejects that one join with
   a new, honest `ServerError` reason.
2. **Worse: the client did the same thing, and release builds hid it.**
   A client reaching `JoinAccepted` without a local player entity
   emplaced onto an invalid entity. In a debug build that asserts; under
   `NDEBUG` — which is exactly how the engine ships — the assertion is
   compiled out and it becomes undefined behaviour on live registry
   memory. Silent corruption is strictly worse than a crash. Found only
   because the throwaway end-to-end harness was built without `NDEBUG`,
   which is a good argument for running this suite in a debug
   configuration too.

### Verification

**11164/11164 engine checks** (+22), including five adversarial ticket
tests over real loopback ENet: no ticket rejected, forged ticket
rejected, valid ticket admitted *and* bound to the right account,
no-validator fails closed, and LAN play explicitly still works without
tickets. Plus a regression test for the spawn-failure crash.

Then the whole loop was run against the **real running backend**: a real
allocation produced a real signed ticket, a real ENet handshake carried
it, a real server verified it via the real `/verify-ticket` endpoint and
bound the connection to real backend user id 7 — and a forged ticket
presented to that same server was refused.

## 2026-08-21 (final) — Catalogue split: online feed vs Local/Dev

The main catalogue is now the Kronos feed and nothing else. Locally
discovered games moved to their own "Local / Dev" tab, with a line saying
plainly that they are not published and nobody else can see them.

`drawGameCataloguePanel()` is now just the tab bar; the entire local
scan/sort/Featured/genre/Hidden-Gems body moved verbatim into
`drawLocalGamesTab()`. No local behaviour changed -- those games are
still fully playable, and LAN sessions still surface on their cards.

Rationale: what a player sees in the catalogue should be what is
actually published to the platform, not whatever happens to be sitting
in this machine's `games/` folder. Keeping them in one list made
unpublished local content look like real catalogue entries.

Not done: the tab is always visible rather than gated behind a
Developer Mode toggle. Hiding it outright would make the launcher look
empty for anyone without a backend configured, which is currently
everyone. Gating it on a persisted setting is a small follow-up once
there is a real populated backend to fall back on.

## 2026-08-21 (final) — Release blocker: the published archive was broken

Asked whether we were ready to tag a final release, so the *published*
v0.2.0-alpha archive was downloaded and run in isolation rather than
assumed good. It was substantially broken.

The packaging step shipped only binaries and shaders. But the runtime
resolves `assets/` and `games/` relative to the executable, falling back
to a **compile-time absolute path** — which on a released build is the CI
machine's own `/home/runner/work/...`. On a real user's machine that path
does not exist, so the shipped launcher produced **13 "could not load"
failures**, including:

- `assets/textures/ui_font_atlas.png` → `UIRenderer::initialize failed --
  continuing without a real HUD`. **No HUD at all.**
- every avatar animation (idle/walk/run/jump) → a character that cannot
  animate
- the window icon
- no `games/` at all → nothing playable locally

Combined with the catalogue now being online-only against a backend that
is not deployed anywhere, a shipped v0.2.0-alpha would have started up
and then done essentially nothing.

### Fix

Both jobs now package `assets/` (1.8 MB) and `games/` (44 KB), and both
**hard-fail** if the font atlas, the idle animation, or `games/` are
missing — refusing to publish beats publishing something that starts but
cannot function. Same discipline as the Windows DLL check added earlier.

Verified by re-running the same extracted archive with the two
directories added: **13 load failures → 0**, font atlas loads, HUD
initializes, Jolt/Luau/terrain all come up clean.

## 2026-08-21 (final) — Kronos Client shell: dark theme, chrome, browser auth

First slice of the Client v0.2.0-alpha visual/architectural spec.

### Theme
Palette swapped from "Warm Ivory" to the spec's strict dark mode:
charcoal `#191B1D` background, slate `#232527` cards, sky blue `#4EA8DE`
accent, vibrant green `#00B259` primary actions, white/`#CCCCCC` text.
Exported as `core::kronos_palette` so the shell, buttons and cards draw
from one definition instead of re-typing hex values that drift.

Accent and primary-action colours are deliberately *different*: sky blue
means "where you are" (active tab, selection), green means "this does
the thing" (Play, Sign In). One colour for both would make a selected
tab look like a button.

### Chrome
`drawSidebar()` / `drawTopBar()` / `drawBrandPanel()` — three borderless
regions so the content area scrolls a long game grid without dragging
the sidebar with it. Sidebar: logo, search, Home / Discover / Avatar /
Create / Settings, version pinned bottom. Top bar: search, procedural
avatar-head profile card, notification bell, green Sign In / Sign Up.
Brand panel: concentric sky-blue rings around the **existing**
procedural hourglass (reused from the loading screen rather than adding
an art asset), plus version and a **real** status line driven by
observed backend reachability — it never claims a connection that was
never made.

### Authentication: no credential fields in the launcher
The email/password form added earlier is **deleted**. Per spec the only
entry point is the green header button, which opens the system browser
and waits on a real loopback callback (`LoopbackHttpServer`, reused from
the Google OAuth work) with real CSRF state checking that fails closed
on mismatch. A credential that never enters this process cannot be
captured from it.

Notably this needed **no new backend endpoint**: the browser hands back
a refresh token, which is exchanged through the same `/v1/auth/refresh`
an ordinary resume uses.

### Discover / Create
Tabs renamed to the spec's terms and driven by the same state the
sidebar sets, so selecting Create in either place is one piece of state.
Discover is now browsable **signed out** (the catalogue endpoint takes
optional auth) and prefetches on launch, so it has real content on first
paint. Create states plainly that local projects launch with no ticket
and no allocation, so they keep working with no network.

### Not done — needs things that do not exist yet
- **The web auth page.** `KRONOS_AUTH_URL` (default
  `<api>/auth/start`) is not served by anything. The C++ half is
  complete and correct; clicking Sign In today opens a URL that 404s.
- **Guest mode** (spec §4) — needs that page plus a guest-token backend
  endpoint.
- **Game card thumbnails** — the backend returns a `thumbnail_url` but
  the launcher has no remote-image download/GPU-upload path, so cards
  are text-only. That is a real subsystem, not a tweak.
- The reference mockups contain real Roblox artwork and logos. The
  layout and palette were implemented from them; none of that artwork
  was copied, and no placeholder pretends to be a real game.

## 2026-08-21 (final) — Social graph, guest mode, ban & username lifecycle (backend)

Backend for the Friends system, guest accounts, and the ban/appeal/
username-recycling lifecycle. **39/39 backend tests** against real
PostgreSQL and real Redis (18 new).

### Social graph
`friendships` stores **one row per relationship**, not two, with a unique
index on the *sorted* pair so (A,B) and (B,A) can never become two
contradictory rows, plus bidirectional indexes so "everything involving
me" hits an index from either side. Only the addressee may accept — a
requester accepting their own request would be a one-click way to friend
anybody, and there is a test for it. A blocked pair reads as "no such
user", never "you are blocked".

Search requires 3 characters and escapes LIKE metacharacters, so `%%%`
matches nobody rather than everybody.

### Presence
Redis-only with a 40s TTL against a 15s heartbeat: a client that dies
just stops appearing, no reaper and no stale "online" hours later.
Direct-join tickets are minted **only** for friends actually in a game
and only for the server they are actually on — issuing one for an offline
friend would be an entry pass to nothing. `presence_available` is
surfaced so the client can say "unavailable" instead of showing everyone
as offline when Redis is down.

### Guests
Real rows with no email and no password, so sessions/tickets/presence all
work unchanged, but flagged `is_guest` and refused the social graph **by
the server** — a client-side restriction is a suggestion. Guests are also
excluded from search, so nobody can friend them either.

### Ban & username lifecycle
Multi-layer identifiers (email/hwid/ip), all hashed. Tested that changing
only the email does **not** evade a ban while an unrelated device is
unaffected. 30-day username lock, idempotent recycling via a cron script,
and the appeal split: inside the window the handle comes back; after it
was recycled and taken the account returns with its data as
`requires_rename`, because we cannot take a handle off whoever holds it
now and a NULL username would look like data loss.

### Flagged, not silently accepted
- `hwid` fingerprints are personal data under GDPR and defeated by a
  reinstall or VM — a speed bump, not an identity system.
- IP bans should carry an `expires_at`; addresses get reassigned.
- The disposable-email list is small and will go stale; it is friction,
  deliberately not the only barrier.

### Not done
The launcher UI for all of this (friends carousel, search modal, guest
banner) is **not** built — it depends on the Home screen redesign, and
the guest flow depends on the web auth page that does not exist yet.

## 2026-08-21 (final) — Web auth page; two real client crashes fixed

The browser sign-in page the launcher hands off to, closing the "Sign In
opens a URL that 404s" gap. **45/45 backend tests** (6 new).

### The page
Served at `/auth/start`, Kronos dark palette, Sign In / Create Account
tabs and a prominent **Play as Guest**. It calls the *same* JSON
endpoints (`/v1/auth/login`, `/signup`, `/guest`) the client would, so
there is one authentication implementation rather than a second one
living in a web page, then hands the refresh token to the launcher's
loopback listener.

### The security-critical part
The page hands a **real refresh token** to whatever `redirect_uri` says,
so an unvalidated value there is an open redirect that mails working
credentials to an attacker — the classic way these flows break. Only
loopback is accepted, and the validator is tested against every escape
route: remote hosts, `http://127.0.0.1@evil.example` (which a naive
"contains 127.0.0.1" check waves through), lookalike hostnames,
`javascript:`/`data:`/`file:`, missing or privileged ports. A rejected
redirect renders a plain error page and **redirects nowhere** — sending
the user to an untrusted target is the exact failure being guarded
against. `no-store`, `X-Frame-Options: DENY` and a restrictive CSP are set.

### Two real client crashes found by the end-to-end harness
1. **Browser sign-in could not work without an OS keychain.**
   `completeBrowserSignIn()` persisted the token then read it *back*
   through `CredentialStore` — so on a headless box, a locked keyring or
   a container the read-back returned nothing and sign-in failed even
   though the token was perfectly good. Now the token it was handed is
   exchanged directly, with persistence as a separate best-effort step:
   failing to save costs a re-login next launch, which is far smaller
   than being unable to sign in at all.
2. **Guest sign-in crashed the launcher outright.** A guest has a `null`
   email, and nlohmann's `value()` *throws* on a present-but-null field
   rather than returning the default. Every field in every response is
   now read defensively — a client must never abort because the server
   sent a null it is entitled to send.

Both were found only because the harness ran the real chain in an
environment without a keychain, which is exactly the environment the
first bug needed.

### Verified end to end
Real backend → the page's own guest endpoint → real loopback listener →
real callback → real `KronosApi` session (`Guest af263f17`) → that
session working against the real API. No human clicked anything.

### Still not done
The backend is not deployed, so `KRONOS_API_URL`/`KRONOS_AUTH_URL` still
default to localhost. The friends carousel, search modal and guest banner
are still unbuilt UI. Card thumbnails still need a remote-image path.

## 2026-08-21 (final) — Cinematic Suite: IK engine + physical camera

Audited the cinematic spec against the codebase before writing anything.
Much of it already exists: `CaptureRig` already renders offline frame
sequences to disk, `RayTracingScene`/`scene_rt.frag` already implement a
real ray-query pass, `TrailerTimeline`/`TrailerDirector` already drive
scripted cinematics, `TimelineEditorPlugin` already does keyframes (one
track), and the full skeletal stack (Skeleton/SkinWeights/RiggedMesh/
AnimationPlayer) is there.

Two things were genuinely absent, and both are self-contained and
testable, so this pass built them properly rather than stubbing four
areas shallowly.

### IK engine (was completely absent -- zero matches repo-wide)

Two-bone analytic solver and FABRIK, both operating on model-space
positions so they carry no transform bookkeeping. Bone lengths are
preserved exactly in every path: an unreachable target produces a
straight chain pointing at it and `reached = false`, never a stretched
limb. Pole targets steer the bend for both solvers, which is what stops
an elbow flipping inside-out between frames. `buildIKChain()` **fails
loudly** if a chain runs off the top of the hierarchy rather than
silently truncating -- a short chain would still solve, and be subtly
wrong every frame.

**A real degenerate case found by the tests**: a perfectly straight chain
whose target lies on its own axis (exactly a fully-extended arm pulled
back toward the shoulder) collapses every direction vector in FABRIK's
passes, and it could not decide which way to bend. Fixed by seeding a
tiny perpendicular nudge that the length-preserving passes then erase --
it only breaks the tie.

### Physical camera

The renderer already had DoF as focus distance / range / CoC radius --
the right knobs for a renderer, the wrong ones for a cinematographer.
`PhysicalCamera` adds focal length (mm), f-stop, sensor size, ISO and
shutter, converting via the real thin-lens relations. Verified against
known lens facts rather than its own output: a 50mm on full-frame really
gives ~27 degrees vertical, a 24mm ~53. Focusing past hyperfocal reports
an **infinite** far limit rather than inventing a large finite number.

**A real limitation surfaced**: at 85mm f/1.4 the computed blur radius
saturates the DoF pass's 32px ceiling at both 1080p and 4K. That is a
genuine limit of the current pass, and it is asserted explicitly rather
than hidden by picking gentler test values.

### Verification
**11239/11239 checks** (+75). All assert real geometric and optical
invariants -- lengths preserved, targets reached, poles respected,
degenerate inputs finite -- not hardcoded coordinates.

### Not built (and why)
The multi-track sequencer, blend trees, camera rails, EXR/motion-blur
export and a GI path tracer are each substantial standalone features;
Unreal's Sequencer is a team-years product. Four shallow stubs would
have been worth less than two finished, tested subsystems.

## 2026-08-21 (final) — Discovery, directory, presence states, publishing

Audited the Studio/discovery spec first. Most of it already existed:
`/v1/games` was already keyset-paginated, the 15s heartbeat with Redis
TTL was already built, `core::SceneFile` already serialises scenes
(scripts included), ImGuizmo transform gizmos, the Explorer tree and the
Properties grid are all already in Studio, and Studio is a separate
binary with no network checks so it is already fully offline.

Four real gaps, all closed with tests (**55/55 backend tests**, 10 new):

- **Batch size** — `/v1/games` capped at 100; the spec wants 200. Raised,
  and over-limit requests clamp rather than being honoured, since an
  unbounded limit is how one request pulls the whole table.
- **`/v1/users` account directory** — only `/users/search` existed, which
  answers "find this person", not "show me the directory". Added,
  keyset-paginated on id so page boundaries stay stable while accounts
  are being created underneath, enriched with live presence, guests
  excluded.
- **`in_studio` presence** — a real distinct state: a creator with Studio
  open is present but *not joinable*, so the UI must not offer "Join
  Game" for them. Unrecognised states fall back to `online_launcher`
  rather than being stored verbatim, so clients never have to handle
  arbitrary strings other clients invented.
- **`/v1/games/publish`** — one-click publish. Re-publishing your own
  slug updates in place; somebody else's is refused by an ownership check
  in the WHERE clause rather than by hope. Guests refused server-side.
  `javascript:` thumbnails rejected, since every catalogue client renders
  that URL.

Plus `/v1/presence/summary` for the dashboard, counting only live Redis
keys -- a total including stale entries is worse than no total, because
people act on it.

### A deliberate storage decision
The `.kronos` scene body is **not** stored in Postgres. Publishing records
metadata and a content hash; the blob belongs in object storage. Putting
multi-megabyte scenes in a JSONB column is very hard to walk back once
there are real places in it.

### Not built
The UI halves -- infinite-scroll grids, the account dashboard panel, and
Studio's publish button -- are not built. The endpoints they need now
exist and are tested.
