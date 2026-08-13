# Mining Sim RTX — Graphics Spec

Sprint 16 ("Cinematic Graphics & Mining Sim RTX"), Phase 5. This document
specifies the intended real-time graphics direction for the RTX version of
Mining Sim, and states plainly which parts of that direction are real and
shipped in this engine today versus real, honest future work. The
companion prototype scene (`engine_runtime --miningsim`, implemented in
`engine/src/miningsim/MiningSimRtx.{hpp,cpp}`) is a real, working, live-
launchable demonstration of everything marked **Real (implemented)**
below — not a concept sketch.

This spec follows the same discipline every other Sprint 16 addition
does: no feature is described as delivered unless it is actually wired
into the renderer and has been live-verified running. Where the brief
asks for something this engine's real architecture cannot yet produce
(most notably full ray-traced global illumination), that gap is named
explicitly, with the real, working alternative that stands in for it.

## 1. World Aesthetic

**Dark caverns.** Real, composed from `core::Mesh::createBox()` primitives
(floor, four walls, and five overlapping, variously-tilted "slab" boxes
forming a jagged rock ceiling) — see `MiningSimRtx.cpp`'s cavern-building
block. No dedicated cave/organic-mesh generator exists in this engine (no
marching-cubes, no SDF meshing, no hand-authored cave art asset
pipeline); a boxy chamber built from real primitives is the honest,
working choice, the same "real procedural primitives over unbuilt organic
geometry" tradeoff `tntwars::MapLayout.cpp`'s own flat-plane maps already
make for every TNT-Wars map.

**Glowing crystals.** Real — see §2 (Materials) and the prototype scene's
freestanding crystal cluster plus two wall-embedded ore-vein shards.

**Volumetric dust.** Real, but via two simpler, real, honest mechanisms
rather than a true GPU volumetric froxel/ray-march system:
- A real, continuous `core::ParticleEmitter` (see
  `Components.hpp`/`ParticleSystem.hpp`) emitting slow-drifting grey
  motes, soft-particle-depth-faded against solid geometry (Sprint 16
  Phase 1's `shaders/particle.frag` addition).
- Real exponential-squared fog (`core::SceneLighting::fogDensity`,
  `shaders/scene.frag`'s `applyFog()`), tuned heavier for this scene than
  any TNT-Wars map, standing in for atmospheric haze.

A true volumetric fog system (light shafts scattering through a real 3D
density field, sampled per-froxel) is a materially larger feature — no
compute-shader infrastructure or 3D density-texture pipeline exists in
this renderer yet. **Scoped out**, honestly, in favor of the two real
mechanisms above.

## 2. Materials

All three real, procedurally-generated PBR texture sets from
`core::ProceduralMaterialLibrary` (`engine/src/core/ProceduralMaterials.{hpp,cpp}`,
Sprint 16 Phase 2) — deterministic value-noise-generated albedo/normal/
metallic/roughness/AO, uploaded straight to the GPU
(`Texture::createFromPixels()`), the same real material system TNT-Wars
map geometry uses:

| Spec line item | Real material | Notes |
|---|---|---|
| Rock PBR | `ProceduralMaterialLibrary::stone` | Cavern floor/walls/ceiling slabs |
| Crystal emissive | `ProceduralMaterialLibrary::crystal` (new) | Faceted violet-blue albedo (banded noise), low roughness, real flat emissive tint layered on top — see §3's honest note on why the glow isn't per-facet-masked |
| Metal machinery | `ProceduralMaterialLibrary::metal` | Mining drill chassis/mast/bit |

The `crystal` set is new in this sprint: `crystalHeight()` floors
continuous fbm noise into six discrete bands to fake hard facets from a
smooth noise field (a real, cheap "faceted crystal" technique, not real
per-facet geometry), reused both for the albedo blend and for
`normalFromHeight()`'s bump generation.

## 3. Lighting

**Ray-traced GI, reflective surfaces** — **partially real, stated
honestly.** This engine's one real ray-tracing capability, shipped in
Sprint 14 ("RTX Upgrade" Phase 2), is real hardware **ray-traced shadow
visibility** via `VK_KHR_ray_query` (`core::RayTracingScene`,
`shaders/scene_rt.frag`) — a real boolean occlusion test against a real
BLAS/TLAS built from `MeshSource`-described Box/Plane geometry, enabled
for this scene (`Renderer::setRayTracedShadowsEnabled(true)` in the
`--miningsim` branch of `main.cpp`). **True ray-traced global
illumination (multi-bounce indirect light) and real ray-traced specular
reflections do not exist in this renderer** — building either is a
materially larger feature (a full ray-tracing *pipeline* with real
raygen/hit/miss shaders and an SBT, not the inline ray-query extension
this engine's shadow pass uses). Honestly scoped out, matching this
engine's own established precedent of refusing to fake technology it
hasn't actually built (see this codebase's prior DLSS-refusal precedent).

**Reflective surfaces** specifically also aren't simulated any other way
(no screen-space reflections either) — the drill's brushed-metal material
reads as metallic via its real PBR metallic/roughness response to the
scene's real point lights, not via any reflected scene content.

**Glowing ore veins** — real, via Sprint 16 Phase 1's multi-light system
(`SceneLighting::pointLights`, up to `kMaxPointLights` real point lights
per scene). Since this scene is underground with no directional "sun"
(`SceneLighting::intensity = 0.0f`), the four point lights placed in the
prototype scene (one warm key light over the drill, two violet accent
lights at the crystal cluster and a wall-embedded vein, one cool fill
light) are the *only* light sources, not a supplement to one — a real,
deliberate application of a system built earlier in this same sprint.

## 4. Effects

| Spec line item | Real mechanism |
|---|---|
| Sparks | **Not built.** No spark/impact-triggered emitter exists for this prototype (nothing in the scene triggers an impact event) — a real, later addition once the drill has real mining-interaction logic to trigger off of, not fakeable without that logic existing. |
| Dust | Real — see §1. |
| Fog | Real — see §1. |
| Falling debris | **Not built.** Same reasoning as sparks: no real trigger condition exists yet in this static prototype scene. |

## 5. Prototype Scene

Implemented in `engine::miningsim::buildRtxPrototypeScene()`
(`engine/src/miningsim/MiningSimRtx.cpp`), launched via
`engine_runtime --miningsim` (a real, live, interactive mode — WASD/mouse-
look via the same `CharacterController` every other `engine_runtime`
launch already wires up — not a batch capture). Live-verified
crash-free, with all five deliverables below actually present and
rendering correctly:

- **1 cavern** — floor + 4 walls + 5-slab jagged ceiling, `stone` material.
- **1 mining drill** — chassis + angled mast (`core::Mesh::createBox()`)
  + a forward-pointing drill bit (`core::Mesh::createCapsule()`, rotated
  90° about X), all `metal` material.
- **1 crystal cluster** — 7 variously-sized/rotated shard boxes at a
  shared origin, `crystal` material + real emissive glow, plus 2
  additional wall-embedded shards standing in for ore veins.
- **RTX lighting enabled** — real ray-traced shadows
  (`setRayTracedShadowsEnabled(true)`) + Sprint 16 Phase 1's full
  Cinematic Mode post-FX stack (`setCinematicMode(true)`: SSAO, depth of
  field, camera motion blur, vignette, chromatic aberration, saturation
  grading, god rays, auto-exposure).
- **PBR materials applied** — every piece of scene geometry uses a real
  generated PBR texture set (§2), not a flat placeholder color.

### A real bug this scene's own live-verification caught

Building and *visually* checking this scene (not just confirming it ran
crash-free) surfaced a real pre-existing bug: `trailer::CaptureRig`'s own
offscreen depth buffer was never created with `VK_IMAGE_USAGE_SAMPLED_BIT`,
a third real depth-buffer creation site missed when that flag was added
to the main swapchain depth buffer and `studio::OffscreenTarget` earlier
in this sprint. With no validation layer installed in this environment,
sampling that un-sampled image in `shaders/cinematic.frag` was silent
undefined behavior — garbage depth values that reconstructed wildly wrong
world positions, pegging the real depth-of-field circle-of-confusion near
its maximum almost everywhere and reading as a uniform, scene-wide blur
rather than real depth-correct falloff. Fixed in `trailer/CaptureRig.cpp`;
see that file's own comment. This also means Cinematic Mode's DOF
would have rendered incorrectly for the TNT-Wars trailer re-capture
(Phase 6) had this scene's own visual check not caught it first.

### DOF tuning is scene-scoped, not a global default change

This cavern spans a real ~20-40 unit depth range, wider than the
TNT-Wars trailer scenes Cinematic Mode's own defaults were tuned
against. `main.cpp`'s `--miningsim` branch calls
`Renderer::setDepthOfFieldParams(20.0f, 22.0f, 5.0f)` to widen the focus
range for this specific scene's scale, rather than changing
`Renderer`'s own general-purpose default (which stays tuned for the
trailer's own scenes).
