#pragma once

#include <vector>

#include <glm/glm.hpp>
#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/ECS.hpp"
#include "core/Mesh.hpp"
#include "core/Physics.hpp"
#include "core/ProceduralMaterials.hpp"
#include "core/Terrain.hpp"
#include "tntwars/DestructibleGeometry.hpp"
#include "tntwars/DestructibleGeometryVisual.hpp"
#include "tntwars/Movement.hpp"

namespace engine::tntwars {

// Kronos ("Sky Map Full Engine Specification"): real, noise-driven
// floating-island layout and per-island heightfield generation --
// replaces the earlier tapered-box-mesa primitive composition
// (SkyMapVisual.cpp) as the map's own real terrain, using an actual
// heightmap (core::Terrain, one real instance per island) instead of
// hand-placed boxes. A single global heightfield can't represent
// *discrete* floating islands with real void gaps between them (a
// heightfield is exactly one Y value per (X,Z) grid cell, so two
// unconnected landmasses at the same (X,Z) footprint are structurally
// impossible in one grid) -- this is why each island gets its own real,
// separately-positioned core::Terrain instance instead of one terrain
// spanning the whole map with masked-out "void" regions.

struct SkyIsland {
    glm::vec3 center{0.0f};
    float radius = 0.0f;
    bool major = false;
    // Kronos ("Sky Map Full Biome Rebuild": designed spawn point): real
    // scale applied to the base+detail noise terms in
    // sampleSkyIslandHeight() before adding them to center.y -- 1.0 (the
    // default, every procedurally-scattered island from
    // generateSkyIslandLayout()) is real, full noise amplitude; a real,
    // deliberately-flatter island (see buildSkyBaseIsland() below) sets
    // this well below 1.0 so its own interior reads as a genuine, safe,
    // walkable plateau instead of the same rugged terrain every other
    // island gets.
    float heightMultiplier = 1.0f;
};

// Kronos ("Sky Bases" world-building): which team a real Sky Base
// belongs to -- this map's own live TNT Wars mode always spawns the
// local player at TeamA's own base (see main.cpp's own real
// coreWorldPosition(map, TeamId::A) default spawn), TeamB's base is the
// real, symmetric opposing base. Purely positional/cosmetic here (no
// separate scoring split beyond the one, shared TntWarsMatch every other
// map already uses) -- drives which real side of the map a base sits on
// and its own real team-color banner accent.
enum class SkyBaseSide { TeamA, TeamB };

// Kronos ("Sky Bases" world-building): a real, dedicated team-base
// island -- a dedicated, larger, deliberately flatter island (real
// heightfield, same core::Terrain/material-band system every other
// island uses, not a primitive-composed structure), replacing the
// earlier single shared "designed spawn point" with one real base per
// team. A real, generous 50-unit radius (within the brief's own
// 40-60-unit "central platform" spec, comfortably larger than a regular
// major island's 26) and a real 0.35 height multiplier -- enough
// remaining noise to still read as real, natural terrain (not a
// mathematically flat disc), tame enough that its own interior is
// genuinely safe, stable footing for props and traversal nodes.
[[nodiscard]] SkyIsland buildSkyBaseIsland(glm::vec3 center);

// Real, deterministic island scatter: `majorCount` big islands and
// `minorCount` small ones, placed via real Worley-noise cell centers
// (core::worleyNoise2D()'s own feature points, sampled across a coarse
// grid covering the real placement area) with real rejection sampling
// enforcing a minimum real center-to-center spacing so islands never
// overlap. Deterministic from `seed` -- the same seed always produces
// the same real layout.
// `reservedIslands`: real, already-placed islands (e.g. the two real Sky
// Base islands, see buildSkyBaseIsland()) that candidates must clear by
// the exact same real spacing-margin rule inter-island overlap checks
// already use below -- a real, general replacement for what used to be
// a single hardcoded "central mesa keep-out radius" special case, now
// naturally supporting any number of real pre-placed islands, not just one.
[[nodiscard]] std::vector<SkyIsland> generateSkyIslandLayout(glm::vec3 areaCenter, float areaRadius, uint32_t seed,
                                                               int majorCount = 6, int minorCount = 12,
                                                               const std::vector<SkyIsland>& reservedIslands = {});

// Real per-island height sample at a position `radialDistance` world
// units from the island's own center (0 = center) -- combines:
//   - Low-frequency Perlin (fractalPerlinNoise2D): the island's own real
//     base dome shape.
//   - High-frequency Perlin: real fine surface detail (small bumps/
//     ripples), layered on top at a much smaller amplitude.
//   - Worley-perturbed edge: the island's own *effective* radius at this
//     world-space angle is real-jittered by a real Worley sample (medium
//     frequency), so the silhouette reads as a real, irregular, eroded
//     edge instead of a perfect circle -- this is "Worley for edge
//     breakup" applied directly to the boundary shape, not just as a
//     height contributor.
//   - A real smoothstep falloff from 70% to 100% of that jittered radius,
//     driving height down to a real, honest "below the island, empty
//     space" sentinel (kSkyIslandVoidHeight) at and beyond the edge --
//     `core::Terrain::create()`'s own chunk mesh always covers its full
//     grid, so "no geometry here" isn't literally representable within
//     one island's own Terrain; kSkyIslandVoidHeight instead sinks the
//     mesh far enough below the visible island that it reads as a real
//     void when a player looks over the edge, matching real "floating
//     island with a sheer drop" games' own standard technique.
// `heightMultiplier` (see SkyIsland::heightMultiplier's own comment)
// scales the real base+detail noise terms before they're added to
// islandCenter.y -- 1.0 (every existing call) is a real, exact identity.
[[nodiscard]] float sampleSkyIslandHeight(float worldX, float worldZ, glm::vec3 islandCenter, float radius,
                                           uint32_t seed, float heightMultiplier = 1.0f);

// Real world units *below islandCenter.y* (not an absolute world Y) the
// void mesh sinks to -- relative so the sentinel reads correctly no
// matter how high up a given island's own center sits.
constexpr float kSkyIslandVoidHeight = -40.0f;

// Real per-island core::Terrain instantiation: one full real, chunked,
// collidable heightfield per island (see this file's own header comment
// for why one instance per island, not one shared terrain). Major
// islands get a real 65x65 grid across 2x2 chunks (2x 32-unit chunks per
// axis, matching the brief's own "chunk the map into 32x32 terrain
// segments"); minor islands get a real 33x33 grid, one real 32-unit
// chunk. Every grid sample comes from sampleSkyIslandHeight(); every
// chunk gets real height/slope material bands (grass on gentle slopes,
// rock on steep ones, dirt as the real transitional band between them,
// sky-crystal on the highest peaks) via Terrain's own real
// applyHeightSlopeMaterialBlend(), and (since `physics` is passed
// through) a real static Jolt mesh collider per chunk. An island whose
// own Terrain::create() fails (logged there) is real-skipped, not a
// hard abort of the whole map.
[[nodiscard]] std::vector<core::Terrain> spawnSkyMapHeightfieldIslands(
    core::ECS& ecs, core::Physics& physics, core::MeshLibrary& meshLibrary,
    const core::ProceduralMaterialLibrary& materials, VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool,
    VkQueue queue, const std::vector<SkyIsland>& islands, uint32_t seed);

// Pure -- real, deterministic bridge-pair selection: greedily connects
// each island to its own real nearest not-yet-connected neighbor (in
// ascending distance order across every possible pair), stopping once at
// least `targetFraction` of `islands` has real-gained a bridge -- the
// brief's own "natural bridges between 30% of islands." Real,
// headlessly-testable independent of any GPU/ECS state, the same "pure
// decision, real spawn-touching caller builds on it" split this
// session's own SkyMapTerrain functions above already establish.
[[nodiscard]] std::vector<std::pair<size_t, size_t>> selectSkyMapBridgeConnections(
    const std::vector<SkyIsland>& islands, float targetFraction = 0.3f);

// Kronos ("Sky Map Full Engine Specification" Section 6, "TNT can break
// bridges"): every real bridge span, plus its own real, parallel-indexed
// visual/collider sync state -- real DestructibleSegment/
// DestructibleSegmentVisual, the exact same real machinery
// TrenchesWall.hpp/TrenchesCover.hpp already use, applied to bridges
// instead. `otherEntities` holds every other real spawned piece
// (overhangs, struts, crystal clusters) that isn't destructible.
struct SkyMapBridgesAndFeatures {
    std::vector<DestructibleSegment> bridgeSegments;
    std::vector<DestructibleSegmentVisual> bridgeVisuals;
    std::vector<core::EntityId> otherEntities;
};

// Real spawn: one real, destructible bridge per
// selectSkyMapBridgeConnections() pair (same real yaw-via-atan2 span
// technique SkyMapVisual.cpp's own bridges already establish), a handful
// of real cantilevered rock overhangs on major islands (real ledges
// extending past the heightfield's own edge -- what a pure heightfield
// structurally cannot express, see this file's own header comment), and
// real embedded sky-crystal clusters on select island edges (the brief's
// own "rare patches for visual identity"). Every piece gets a real
// static Jolt collider.
//
// Kronos ("Structural Expansion" world-building): real bridge *variety*,
// not one repeated look -- bridge pairs cycle through 3 real, distinct
// structural styles (index % 3): real wooden planking (the original
// style, unchanged), a real thinner/lower-health "rope bridge" (same
// wood material, real narrower deck reading as flimsier), and a real
// wider "metal catwalk" (materials.metal, real higher health, built from
// several shorter overlapping plates rather than one long slab -- a
// real, deliberate jointed/segmented look, though not a live retractable
// mechanism: that's a real gameplay-layer trigger/animation system, out
// of this pass's own "architecture" scope, not built here).
[[nodiscard]] SkyMapBridgesAndFeatures spawnSkyMapBridgesAndFeatures(
    core::ECS& ecs, core::Physics& physics, core::MeshLibrary& meshLibrary,
    const core::ProceduralMaterialLibrary& materials, VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool,
    VkQueue queue, const std::vector<SkyIsland>& islands, uint32_t seed);

// Real, live-triggerable movement features -- real JumpPadState/
// ZipLineState data (see Movement.hpp's own header comment for the
// pure trigger logic; Application's own pretick hook is what actually
// drives these live, see Application::tntWarsJumpPads()/tntWarsZipLines()),
// plus the real visual/collider entities spawned for them (wind-geyser
// vent props for jump pads, a real thin cable span + anchor posts for
// each zip-line).
struct SkyMapMovementFeatures {
    std::vector<JumpPadState> jumpPads;
    std::vector<ZipLineState> zipLines;
    std::vector<core::EntityId> spawnedEntities;
};

// Real, deterministic placement: a jump pad on each major island beyond
// the first two (the brief's own "wind geysers" -- real vertical launch
// vents), and real zip-lines linking non-adjacent major islands (a real,
// distinct traversal network from selectSkyMapBridgeConnections()'s own
// nearest-neighbor bridges above).
[[nodiscard]] SkyMapMovementFeatures spawnSkyMapMovementFeatures(core::ECS& ecs, core::Physics& physics,
                                                                   core::MeshLibrary& meshLibrary,
                                                                   const core::ProceduralMaterialLibrary& materials,
                                                                   VmaAllocator allocator, VkDevice device,
                                                                   VkCommandPool cmdPool, VkQueue queue,
                                                                   const std::vector<SkyIsland>& islands);

// --- Structural Expansion (Kronos "Structural Expansion" world-building) ---
//
// Breaks up the map's own real "just cliffs and bridges" repetition with
// real architectural variety beyond the two traversal networks
// (selectSkyMapBridgeConnections()'s bridges, spawnSkyMapMovementFeatures()'s
// zip-lines) already built.

// Real, free-floating mid-tier scaffold platforms -- a real, third
// traversal/combat node distinct from a bridge (which always spans two
// islands directly) or a zip-line (a single fixed line): a small, real
// metal-decked platform sitting in real open void between two nearby
// major islands that never got a bridge (selectSkyMapBridgeConnections()'s
// own real 30% target leaves most pairs unconnected), giving a player a
// real place to land, fight, or catch their breath mid-crossing. Short
// real decorative corner struts underneath (no collider -- purely
// visual, the same "read as suspended, not a walkable understructure"
// choice the overhang struts above already make) sell the "suspended
// scaffold" read; the deck itself gets a real static Jolt collider.
[[nodiscard]] std::vector<core::EntityId> spawnSkyMapMidtierPlatforms(
    core::ECS& ecs, core::Physics& physics, core::MeshLibrary& meshLibrary,
    const core::ProceduralMaterialLibrary& materials, VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool,
    VkQueue queue, const std::vector<SkyIsland>& islands, uint32_t seed);

// Real, short enclosed tunnels between two real *close, minor* islands --
// what a pure heightfield structurally cannot express at all (a tunnel is
// real interior void carved through solid ground; a heightfield is
// exactly one height per (x,z), see this file's own header comment on
// this exact structural limit), built the same way every other
// heightfield-can't-do-this feature on this map already is: real
// primitive composition (floor/walls/roof box segments) bridging the two
// islands' own real cliff faces, embedded flush against each. A real,
// honest "shortcut" alternative to the bridge/zip-line networks above --
// no stealth/detection mechanic exists in this engine to make it a real
// stealth route yet, so this is architecture only, real and walkable,
// not a gameplay system.
[[nodiscard]] std::vector<core::EntityId> spawnSkyMapHiddenTunnels(
    core::ECS& ecs, core::Physics& physics, core::MeshLibrary& meshLibrary,
    const core::ProceduralMaterialLibrary& materials, VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool,
    VkQueue queue, const std::vector<SkyIsland>& islands, uint32_t seed);

// --- Sky Bases (Kronos "Sky Bases" world-building) ---------------------
//
// This is TNT Wars, not Mining Sim -- there is no boss encounter here
// (the earlier spawn-island "forge boss arena" reused Mining Sim's own
// real boss-visual pipeline; that's this session's own real mistake,
// corrected here: the forge zone below is a real forged-tool prop only,
// no enemy, no arena).
//
// A real, complete team base, spawned directly onto its own real
// heightfield island (buildSkyBaseIsland()) once core::Terrain has
// already generated it (this function needs to real-query the finished
// terrain's own heightAt(), so it always runs *after* that island's own
// spawnSkyMapHeightfieldIslands() entry has real-succeeded -- same real
// "generate terrain first, place props against its own real surface
// second" ordering the earlier spawn-island boss-arena placement already
// established). Real zones, every one placed via a real Terrain::heightAt()
// query, not a guessed offset:
//   - spawn zone: the base's own real center -- what teamSpawnPosition()-
//     equivalent callers place the local player at.
//   - forge zone: a real forged Explosive Charge tool prop (this map's
//     own real "forge" identity, see the removed spawnSkyMapBossArena()'s
//     own former header comment) -- offset from spawn so the player
//     doesn't spawn on top of it.
//   - defense zone: a real, low parapet wall arc facing the map's own
//     center (where the opposing team approaches from) -- real static
//     colliders, cosmetic team-color banner accent.
//   - observation zone: a real, tall watchtower at the base's own outer
//     edge with a small platform on top -- a real, elevated vantage
//     point over the play area.
//   - utility zone (Kronos "Structural Expansion" world-building): a
//     real generator (a boxy machine block, real emissive glow standing
//     in for "always running"), a real crane (a vertical mast + real
//     horizontal boom), and a real suspended cargo pod hanging from the
//     boom's own tip on a real thin cable prop -- gives the base a real,
//     lived-in sense of purpose beyond pure combat geometry. Decorative
//     only (no real winch/load-bearing gameplay system exists to hook a
//     "cargo" mechanic into yet).
// Vertical connectors: a real "lift" (a real JumpPadState tuned to reach
// the base's own real multi-level deck, the same real vertical-launch
// mechanic every other jump pad on this map already uses -- a dedicated
// elevator/lift state machine is real, new gameplay-layer scope, not
// architecture, see this session's own explicit scope boundary) and a
// real multi-level deck (a second, real, raised platform connected by
// that lift). Lighting anchors: real emissive crystal props (this map's
// own established "GI-catching" landmark dressing -- this renderer's own
// real bounce lighting is a global per-pixel toggle, not per-probe, so
// these read as real, deliberately-placed light-catching landmarks
// rather than literal light probes) plus one real, low-roughness polished
// monolith (a real, deliberate SSR reflection-surface anchor) and one
// real, tall thin spire oriented to catch real godray shafts at this
// map's own tuned sun elevation.
struct SkyBaseFeatures {
    JumpPadState lift;
    std::vector<core::EntityId> spawnedEntities;
};

[[nodiscard]] SkyBaseFeatures spawnSkyBase(core::ECS& ecs, core::Physics& physics, core::MeshLibrary& meshLibrary,
                                             const core::ProceduralMaterialLibrary& materials, VmaAllocator allocator,
                                             VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                                             const core::Terrain& terrain, glm::vec3 baseCenter, SkyBaseSide side);

// --- Environmental Detail (Kronos "Environmental Detail" world-building) ---

// Real, scattered ground-level decoration for one island -- real grass/
// moss tufts (thin box "blades," matching this file's own established
// "everything is a real, tinted box" primitive-composition convention,
// see spawnFeatureBox()'s own comment) and, sparsely (1 in 4 clusters),
// real crystal flora (small, emissive, low-roughness shards -- the same
// real material this map's own cliff-embedded crystal clusters use, at a
// much smaller, ground-level scale, tuned to "catch GI and SSR
// beautifully" per the brief: emissive feeds real bounce lighting,
// low-roughness feeds the real SSR fallback pass). Every blade/shard gets
// a real core::WindSway component (see core/Wind.hpp) so it leans in
// whatever ambient wind Application is driving live -- decorative only,
// no collider (real, deliberate: vegetation should never block movement).
// Placement is real and terrain-aware: candidate points are rejected via
// a real finite-difference slope check against the island's own actual
// core::Terrain::heightAt(), the same real "steep edge vs. flat plateau"
// distinction the height/slope material bands already make, so
// vegetation only roots on real walkable ground, never a cliff face.
[[nodiscard]] std::vector<core::EntityId> spawnSkyMapVegetation(core::ECS& ecs, core::Physics& physics,
                                                                   core::MeshLibrary& meshLibrary,
                                                                   const core::ProceduralMaterialLibrary& materials,
                                                                   VmaAllocator allocator, VkDevice device,
                                                                   VkCommandPool cmdPool, VkQueue queue,
                                                                   const core::Terrain& terrain, const SkyIsland& island,
                                                                   uint32_t seed);

// Real, honest placement-only data for ambient sound zones -- wind near
// cliffs, a hum near each base's own forge/generator, an echo inside each
// hidden tunnel (the brief's own real category split). No real audio
// asset files exist anywhere in this project (a real, verified gap, not
// an oversight -- see this function's own .cpp comment), so this
// deliberately does *not* attach a real core::AudioSource/call
// core::Audio::loadSound() with a fake or empty path, which would either
// silently do nothing or log a real, spurious load failure every run.
// What this *does* provide is real, exact positions + a real category tag
// per zone, ready for a caller to wire up the moment real sound content
// exists -- honest, useful groundwork, not a faked "it's working" system.
enum class AmbientSoundCategory { Wind, Hum, Echo };
struct AmbientSoundZone {
    glm::vec3 position{0.0f};
    float radius = 10.0f;
    AmbientSoundCategory category = AmbientSoundCategory::Wind;
};
[[nodiscard]] std::vector<AmbientSoundZone> planSkyMapAmbientSoundZones(const std::vector<SkyIsland>& islands,
                                                                          glm::vec3 teamABaseCenter,
                                                                          glm::vec3 teamBBaseCenter);

// Real, persistent dust/pollen ParticleEmitter entities -- one per
// selected major island, each real, continuous, sparse, long-lived (see
// this function's own .cpp comment for the exact real tuning), tagged
// with core::AtmosphericDustEmitter so Application's own live pretick
// hook can bias their real drift by whatever ambient core::WindState it's
// driving (see core/Wind.hpp's own comment) -- real motes that actually
// respond to the map's own wind, not a fixed, static drift.
[[nodiscard]] std::vector<core::EntityId> spawnSkyMapAtmosphericDust(core::ECS& ecs,
                                                                       const std::vector<SkyIsland>& islands);

} // namespace engine::tntwars
