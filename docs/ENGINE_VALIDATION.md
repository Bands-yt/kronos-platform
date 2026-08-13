# Kronos Alpha — Section 1: Engine Validation Pass

Status of the [Alpha Completion Checklist](ALPHA_COMPLETION_CHECKLIST.md)'s
Section 1 ("Engine Validation Pass"). All 9 roadmap phases were already
done (see [ALPHA_ROADMAP.md](ALPHA_ROADMAP.md)); this pass is different in
kind — not "build the missing feature," but "audit every real subsystem's
test coverage and close whatever nobody ever actually verified," including
code that predates this whole effort.

Verified with a full rebuild and the full test suite green after every
change (9518/9518 checks passing, up from 9459 — 59 new tests).

## Method

For each of the six checklist items, cross-referenced the real, shipped
surface (every component type, every scene operation, every plugin
lifecycle event, every wire message type, every asset kind, every Lua
binding) against `grep`-verified test coverage. A miss meant either a
real gap (write the test) or, in a few cases, discovering something that
was already comprehensively covered and just confirming it.

## 1. Component types

Two real, complete gaps found: `movingPlatformTarget()` (a real, pure,
already-documented sine-wave function — zero test coverage) and
`core::AudioSource`/`core::Audio` (the component and the system that
reads it — zero test coverage anywhere, despite `Audio::mix()` being real,
shipped code). Both closed:

- `testMovingPlatformTargetOscillatesAroundBasePosition` — checks the
  real quarter/half/three-quarter-period extremes and the real
  zero-period division-by-zero guard.
- `testAudioSourceComponentAttachesAndReadsBack` — real component
  attach/read-back, matching every other component's own test shape.
- `testAudioInitializeLoadPlayMixReal` — a real `core::Audio::initialize()`
  attempt; if the sandbox has no real audio device, the honest failure
  path itself is the check (matches this codebase's own "headless
  environment may lack a real GPU/device" convention already applied to
  Vulkan). If it succeeds, `mix()` is exercised against a real
  `AudioSource` with an unloaded sound handle to confirm the real
  early-continue guard doesn't dereference a null sound.

## 2. Scene operations

`SceneManager::newScene()` had zero coverage (a real, user-facing "File >
New Scene" operation). `loadScene()`'s "Renderable with no saved
MeshSource" honest-degradation branch (a real, deliberate fallback) had
never been exercised either — every prior test either had no Renderable
or would have needed a real Vulkan mesh-build. That specific branch never
calls the Vulkan-touching `buildMeshFromSource()`, so it's real and safe
to test headlessly with `VK_NULL_HANDLE`.

- `testSceneManagerNewSceneClearsEcsAndState`
- `testSceneManagerLoadSceneHandlesRenderableWithNoMeshSourceHonestly`

## 3. Plugin lifecycle events

`events.onUpdate`/`onCollision`/`onInteract` — the core scheduler/gameplay
event bus every gameplay script and scripted plugin relies on — had
**zero direct test coverage anywhere**, despite predating this session
entirely. Only `events.onUnload` (built this alpha) had tests.

- `testScriptingOnUpdateFiresEveryTickWithRealDt` — confirms registering
  a handler doesn't fire it prematurely, a real `tick(dt)` passes the
  real `dt` through, and two ticks fire it exactly twice.
- `testScriptingFireCollisionReachesRegisteredHandler`
- `testScriptingFireInteractReachesRegisteredHandler`

## 4. Networking events

Confirmed already comprehensive — every real `WireMessageType`
(`Handshake`/`Snapshot`/`Input`, `TeleportRequest`, `ChatMessage`/
`ChatBroadcast`, `ReportPlayer`, `SelectClass`/`FireWeapon`/
`ProjectileSpawned`/`TriggerUltimate`/`UltimateTriggered`,
`RemoteEventFire`/`RemoteEventBroadcast`) already has real client/server
test coverage over real loopback ENet. No new work needed.

## 5. Asset imports

`AssetKind::Audio` — the real miniaudio-backed duration/sample-rate/
channel-count probe — had zero coverage; only Mesh and Texture were ever
exercised. Added a real, minimal, valid 16-bit PCM WAV builder
(`buildWav()`, the same "real, standard, parseable format" discipline
`buildPng()`/`buildJpeg()` already established) and extended
`testAssetMetadataExtraction` to probe a real WAV file, verifying real
sample rate, channel count, and duration all come back correctly from a
real `ma_decoder`.

## 6. Lua bindings

The big one. `core::ScriptWorldApi`'s `world` table has 15 real
functions; only 5 (`createEntity`/`setParent`/`unparent`/`setPosition`/
`findByName`) had ever been exercised through an actual Luau script. The
other ten — `destroy`, `getPosition`, `getRotation`, `setRotation`,
`setScale`, `setColor`, `setMaterial`, `setEmissive`, `applyImpulse`,
`setVelocity`, `playAnimation`, `stopAnimation` — had never been called
from Lua in any test.

`testScriptWorldApiRemainingBindingsFullCoverage` closes all ten in one
real, cohesive pass: position/rotation get/set round-trip (verified via
real Luau `assert()`s inside the script itself), scale, destroy, color/
material/emissive against a real `Renderable`, and — the most
substantial piece — `applyImpulse`/`setVelocity` against a real Jolt
dynamic body (`Physics::createDynamicBox()`), verified by actually
stepping physics once and checking the body really moved.

**A real bug caught while writing this test**: the first draft checked
`scriptId != kInvalidScript` to mean "the assert()s all passed." That's
wrong — a failed Lua `assert()` is a *runtime* error, and `loadAndRun()`
only returns `kInvalidScript` for a *compile* error; a runtime error still
registers a real script and just reports the error through the output
callback. Fixed by capturing output and checking for the real, honest
absence of a `"runtime error"`-prefixed line since the last script ran —
the only way to actually prove every assertion passed. Caught before it
shipped as a silently-broken test, the same discipline that caught the
Phase 5 destructor-order bug.

Also closed: `network.fireAllClients()`/`network.onClientEvent()` (the
broadcast direction of `core::ScriptNetworkApi`'s `network` table — only
the fire-to-server direction had Luau-level coverage before), and
`core::ScriptUiApi`'s `ui` table (real registration + real call-through
with optional-argument defaults, verified error-free — the actual render
flush needs a live `core::UIRenderer`, which needs real Vulkan handles
this headless suite deliberately never touches, the same honest ceiling
already flagged for the glass/water shader and the renderer's point-light
UBO fill).

## Summary

| Item | Status |
|---|---|
| Component types | 2 real gaps found and closed (`movingPlatformTarget`, `AudioSource`/`Audio`) |
| Scene operations | 2 real gaps found and closed (`newScene()`, meshless-Renderable load) |
| Plugin lifecycle events | 3 real gaps found and closed (`onUpdate`/`onCollision`/`onInteract`) |
| Networking events | Confirmed already comprehensive — no gaps |
| Asset imports | 1 real gap found and closed (Audio metadata probe) |
| Lua bindings | 12 real gaps found and closed (10 `world.*` functions, `network` broadcast direction, `ui.*`) — plus a real test-correctness bug caught and fixed along the way |

59 new tests, zero regressions, full suite green throughout.
