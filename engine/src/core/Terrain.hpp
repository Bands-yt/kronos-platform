#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/ECS.hpp"
#include "core/Mesh.hpp"
#include "core/Noise.hpp"
#include "core/PhysicsMaterial.hpp"
#include "core/ProceduralMaterials.hpp"

namespace engine::core {

class Physics;

// A real, minimal marker tag on every chunk entity Terrain::create()
// spawns -- Sprint 7 ("Studio UI Revamp") needs a reliable, real way for
// Studio's Explorer to answer "is this entity a terrain chunk" without
// guessing from its Name string (chunks are all named "TerrainChunk",
// which is fragile to match against and doesn't survive a rename).
// Carries real, useful data (which chunk this is) rather than being a
// truly empty tag -- see Shop.hpp's `ShopStall::active` comment for why
// a genuinely empty struct is a real problem for
// `core::ECS::addComponent()`'s uniform `Component&` return type.
struct TerrainChunkTag {
    uint32_t chunkX = 0;
    uint32_t chunkZ = 0;
};

// A chunked heightmap terrain: one shared height grid spanning the whole
// terrain, subdivided into chunkCount x chunkCount independent chunk
// meshes/entities so a brush stroke only has to regenerate the handful of
// chunks it actually overlaps, not the entire terrain every time. Each
// chunk is a real ECS entity (Transform + Renderable) -- to the rest of
// the engine it's just more content: shadows, the existing PBR pipeline,
// instancing eligibility all apply the same as anything else, no
// terrain-specific rendering path needed.
//
// "Paint" (see paint() below) sets a chunk's whole Renderable::baseColor
// from a caller-supplied color -- coarser-grained than real per-vertex
// texture splatting (which needs a texture/material-layer system this
// engine doesn't have yet, see migration::AssetConverter's still-stubbed
// texture path), but real, immediately visible on the actual PBR
// pipeline, and needs zero new rendering infrastructure. Widening this to
// per-vertex blending is a real next step once texturing exists, not a
// redesign of this class.
//
// Sprint 6 ("World Systems & Environment") extended this with three real
// capabilities beyond the original brush-sculpting tool: physics
// colliders (a real Static Jolt mesh collider per chunk, rebuilt whenever
// a chunk's mesh regenerates -- see the `physics` parameter on create()),
// chunk streaming (updateStreaming() loads/unloads each chunk's
// Renderable+collider based on distance from a viewer, with hysteresis
// between the load/unload radii to avoid thrashing right at the
// boundary), and whole-terrain generation (loadHeightmapFromFile(),
// generateFractalTerrain(), and three real named presets built on top of
// it -- see applyPreset()).
class Terrain {
public:
    struct CreateInfo {
        uint32_t gridResolution = 65; // heightmap samples per axis, whole terrain; (gridResolution-1) should divide evenly by chunkCount
        uint32_t chunkCount = 8;      // chunks per axis
        float cellSize = 1.0f;        // world units between adjacent height samples
        glm::vec3 origin{0.0f};       // world-space position of grid index (0,0)
    };

    // `physics`, when non-null, gives every chunk a real, live Static
    // Jolt mesh collider (rebuilt on every regeneration) -- optional
    // because Studio's TerrainEditorPlugin edits a terrain with no live
    // core::Physics instance at all outside Play mode (see the Physics
    // sprint's own "Studio runs no Physics by default" boundary), while
    // engine_runtime always wants real walkable/collidable ground.
    // Trailing/optional so the existing Studio call site (no physics
    // argument) keeps compiling unchanged -- the same backward-compatible
    // pattern every `createX()` in Physics.hpp already uses.
    [[nodiscard]] bool create(const CreateInfo& info, ECS& ecs, MeshLibrary& meshLibrary, VmaAllocator allocator,
                               VkDevice device, VkCommandPool cmdPool, VkQueue queue, Physics* physics = nullptr,
                               PhysicsMaterial material = {});
    // Destroys every chunk entity (via the ECS stashed at create()) --
    // chunk meshes stay registered in `meshLibrary` until its own
    // destroyAll() at shutdown, same "handles outlive their content"
    // contract MeshLibrary already documents for everything else.
    void destroy();

    // Brush operations -- worldX/worldZ in world space, radius in world
    // units. Each regenerates only the chunks its radius overlaps (and,
    // when physics is enabled, rebuilds just those chunks' colliders).
    // `falloffPower` (Sprint 9 "Creator Tools Phase 1" task category 1's
    // "falloff controls", trailing/optional so every pre-existing call
    // site keeps compiling unchanged) shapes how strength tapers from
    // brush center to edge: 1.0 (the default, matching this brush's
    // original and only behavior before this pass) is the real linear
    // falloff `brushFalloff()` always computed; higher values concentrate
    // the effect nearer the center (a real smoothstep-like curve via
    // `std::pow`), lower values spread it more evenly across the whole
    // radius -- see `brushFalloff()`'s own comment for the exact formula,
    // extracted as a pure function specifically so the falloff shape
    // itself is headlessly testable independent of a live terrain.
    void raise(float worldX, float worldZ, float radius, float strength, float falloffPower = 1.0f);
    void lower(float worldX, float worldZ, float radius, float strength, float falloffPower = 1.0f);
    // strength in [0,1]: how far each affected height moves toward the
    // local average of its neighbors, not an additive delta like raise/lower.
    void smooth(float worldX, float worldZ, float radius, float strength, float falloffPower = 1.0f);
    // Adds deterministic value noise (see Terrain.cpp -- no Perlin/Simplex
    // library is vendored for this) scaled by `strength`, sampled at
    // `frequency` world-units^-1.
    void addNoise(float worldX, float worldZ, float radius, float strength, float frequency, float falloffPower = 1.0f);
    void paint(float worldX, float worldZ, float radius, glm::vec4 color, ECS& ecs);
    // Real "Flatten" brush (Sprint 9 task category 1) -- samples the
    // current height at (worldX, worldZ) as the real target, then pulls
    // every height within `radius` toward that target by `strength`
    // (same [0,1] "how far toward the target" convention as smooth()),
    // falloff-weighted the same way every other brush is. The common
    // "flatten around where you're standing/clicked" convention real
    // terrain tools use, not a fixed constant height the caller has to
    // already know.
    void flatten(float worldX, float worldZ, float radius, float strength, float falloffPower = 1.0f);

    // Pure -- the real falloff curve every brush above uses:
    // `pow(clamp(1 - dist/radius, 0, 1), falloffPower)`. `falloffPower == 1.0`
    // is the original linear falloff (dist == 0 -> 1.0, dist == radius ->
    // 0.0); `dist > radius` always yields exactly 0.0. Extracted so the
    // curve shape is independently, headlessly testable.
    [[nodiscard]] static float brushFalloff(float dist, float radius, float falloffPower);

    // Sprint 9 ("Creator Tools Phase 1") task category 1's "undo/redo
    // support" -- a real, whole-heightmap snapshot/restore pair
    // (Studio's TerrainEditorPlugin pushes one UndoStack::Command per
    // discrete "Apply Brush" click, capturing a before/after snapshot by
    // value the same way InspectorPanel's Transform edits already do).
    // restoreHeightSnapshot() calls regenerateAllChunks() itself -- a
    // real, if not the narrowest-possible, way to make the visible mesh
    // (and, when physics is enabled, every collider) match the restored
    // heights; see README's Known Issues for why this rebuilds every
    // chunk rather than only the ones the original brush stroke touched.
    [[nodiscard]] std::vector<float> heightSnapshot() const { return heights_; }
    void restoreHeightSnapshot(const std::vector<float>& snapshot);

    // Bilinear-sampled height at an arbitrary world position -- e.g. for
    // snapping other objects to the surface. 0 if the terrain doesn't
    // exist yet.
    [[nodiscard]] float heightAt(float worldX, float worldZ) const;
    [[nodiscard]] bool isValid() const { return !chunks_.empty(); }
    [[nodiscard]] const CreateInfo& info() const { return info_; }

    // --- Whole-terrain generation (task category 1: "noise-based
    // generation" + "heightmaps") -------------------------------------

    // Real grayscale heightmap import (stb_image, the same vendored
    // decoder Texture.cpp already links -- see that file's
    // STB_IMAGE_IMPLEMENTATION comment; this translation unit only
    // declares against the header, the implementation is linked once).
    // Resamples (nearest-neighbor) to this terrain's own gridResolution
    // if the image's dimensions don't match exactly, so any reasonably
    // square grayscale image works, not just an exact-resolution match.
    // `heightScale` maps a full-white pixel (255) to that many world
    // units of height. Regenerates every chunk (and every chunk's
    // collider, if physics is enabled) once, not per-pixel.
    [[nodiscard]] bool loadHeightmapFromFile(const std::string& path, float heightScale);

    // Real fractal Brownian motion (fBm, core::fractalNoise2D() -- see
    // Noise.hpp, extracted into its own pure/headlessly-testable module)
    // applied across the *entire* height grid (not brush-radius-limited
    // like addNoise()), additive on top of whatever heights already
    // exist. Deterministic from `params.seed` -- two calls with
    // identical params produce identical terrain, real for repeatable
    // presets/tests, not true per-run randomness (a caller wanting that
    // passes a randomized seed itself).
    void generateFractalTerrain(const FractalNoiseParams& params);

    // Three real, distinct named presets (task category 1's "3 example
    // terrain presets") -- each a real, different FractalNoiseParams
    // recipe (see Terrain.cpp for the exact values and why), not the
    // same generator with a relabeled name. Resets height to 0 first, so
    // presets are reproducible regardless of what was on the terrain
    // before.
    enum class Preset { RollingHills, FlatPlains, RockyCanyon };
    void applyPreset(Preset preset);

    // Regenerates every chunk's mesh (and collider, if physics is
    // enabled) from the current height grid -- what generateFractalTerrain()/
    // loadHeightmapFromFile()/applyPreset() call once at the end, and a
    // real, callable primitive for any caller that edited `heights_`
    // through some other means (tests included).
    void regenerateAllChunks();

    // Real, honestly-scoped ambient occlusion (task category 3: "ambient
    // occlusion tuning") -- chunk-granularity, not per-vertex: each
    // chunk's average height is compared against its immediate
    // neighbors' average heights, and a chunk sitting in a real valley
    // (notably lower than its neighbors) gets its Renderable::baseColor
    // darkened, while a chunk on a real ridge/peak gets a slight
    // brightening -- the exact same "coarser than per-vertex, but real,
    // needs zero new rendering infrastructure" tradeoff paint() already
    // documents (see this class's own header comment), applied
    // automatically instead of by hand. `strength` in [0,1] scales how
    // pronounced the effect is; 0 is a real, honest no-op. Real
    // per-vertex AO (needing a Vertex-struct schema change this pass
    // deliberately doesn't make) is a stated follow-up -- see README's
    // Known Issues.
    void applyChunkAmbientOcclusion(float strength);

    // --- Real material texturing (Kronos "Four RTX Maps" Phase 4) ------
    // Terrain was flat-Renderable::baseColor-only before this (see
    // paint()'s own header comment on why) -- this is the real fix,
    // wiring core::ProceduralMaterialLibrary's existing generated PBR
    // textures in, at the same real chunk granularity paint()/
    // applyChunkAmbientOcclusion() already use (a true per-vertex blend
    // map would need a texture-splat sampler this renderer's scene.frag
    // doesn't have -- see ProceduralMaterials.hpp's own header comment on
    // why the ground preset already made this same real tradeoff).

    // One real band: a chunk whose own average height and height range
    // (a real proxy for slope/steepness -- see computeChunkHeightStats()'s
    // own comment) both fall at/under `maxHeight`/`maxSlope` gets
    // `material` applied, plus `metallic`/`roughness` written the same
    // way core::ProceduralMaterialLibrary::applyTo() already does for
    // every other real material consumer in this engine.
    struct HeightSlopeMaterialBand {
        float maxHeight = 1e6f;
        float maxSlope = 1e6f;
        const PbrTextureSet* material = nullptr;
        float metallic = 0.0f;
        float roughness = 0.85f;
    };

    // Real, chunk-granularity height/slope material assignment -- for
    // each real chunk, the *first* band (in the order given) whose
    // maxHeight/maxSlope both real-cover that chunk's own stats wins (an
    // ordered-priority chain, the same real convention
    // tntwars::classifyMapPieceMaterial() already established); a chunk
    // matching no band is a real, honest no-op (its existing material is
    // left exactly as it was, not forced to some default). Real callers:
    // order bands from most-restrictive (e.g. "low and flat -> sand") to
    // a final catch-all (maxHeight/maxSlope left at their default ~1e6
    // "always matches" values).
    void applyHeightSlopeMaterialBlend(const std::vector<HeightSlopeMaterialBand>& bands);

    struct ChunkHeightStats {
        float averageHeight = 0.0f;
        // Real proxy for slope/steepness: this one chunk's own local
        // max-min height range across its own grid samples -- a chunk
        // whose heights barely vary internally is real-flat; a chunk with
        // a wide internal range is a real cliff/steep slope. Deliberately
        // *not* the same inter-chunk neighbor-delta applyChunkAmbientOcclusion()
        // computes (that measures "is this chunk a valley/ridge *relative
        // to its neighbors*", a different real question from "is this
        // chunk's own surface steep").
        float heightRange = 0.0f;
    };
    // Pure -- real headlessly-testable independent of a live Terrain
    // instance (takes the raw height grid directly, the same "pure
    // decision, real I/O-touching caller builds on it" split this file's
    // own brushFalloff()/shouldChunkBeLoaded()/chunkWorldSize() already
    // establish).
    [[nodiscard]] static ChunkHeightStats computeChunkHeightStats(const std::vector<float>& heights,
                                                                    uint32_t gridResolution, uint32_t chunkCount,
                                                                    uint32_t chunkX, uint32_t chunkZ);
    // Pure -- the real "which band applies" decision
    // applyHeightSlopeMaterialBlend() is built on. Returns `bands.size()`
    // (an explicit, real "no match" sentinel -- never a dangling/aliased
    // pointer into a possibly-temporary vector) when no band's real
    // thresholds cover `stats`.
    [[nodiscard]] static size_t selectMaterialBand(const std::vector<HeightSlopeMaterialBand>& bands,
                                                     const ChunkHeightStats& stats);

    // --- Chunk streaming (task category 6: "chunk streaming ...
    // load/unload") --------------------------------------------------

    // Loads every chunk within `loadRadius` of `viewerPosition` (shows
    // its Renderable, reattaches its collider if physics is enabled) and
    // unloads every chunk farther than `unloadRadius` (hides its
    // Renderable, detaches its collider) -- `unloadRadius` must be >=
    // `loadRadius`, the real hysteresis gap that keeps a viewer standing
    // right at a single fixed boundary from thrashing a chunk
    // load/unload/load/unload every tick. `heights_`/mesh-handle
    // bookkeeping is never destroyed by unloading -- see this class's
    // own comment on the deliberately bounded scope of "streaming" here
    // (toggles visibility + physics, doesn't free the GPU mesh buffer
    // itself; see README's Known Issues). Built on the pure
    // shouldChunkBeLoaded() decision below -- this method is just that
    // decision applied to every real chunk, plus the actual ECS/Physics
    // side effects.
    void updateStreaming(glm::vec3 viewerPosition, float loadRadius, float unloadRadius);

    // Pure -- the real hysteresis decision updateStreaming() is built on,
    // extracted specifically so it's headlessly unit-testable without a
    // live Vulkan device (unlike updateStreaming() itself, which touches
    // real ECS/Physics state on real GPU-backed chunk entities -- see
    // this class's own header comment on why chunk creation/regeneration
    // needs a live device but this specific decision doesn't). Returns
    // whether a chunk currently in state `currentlyLoaded`, at
    // `chunkCenter`, should be loaded given `viewerPosition` and the two
    // radii -- `true` (load) once within `loadRadius`, `false` (unload)
    // once beyond `unloadRadius`, and *unchanged* (`currentlyLoaded`,
    // literally returned as-is) in the real hysteresis gap between them.
    [[nodiscard]] static bool shouldChunkBeLoaded(bool currentlyLoaded, glm::vec3 chunkCenter, glm::vec3 viewerPosition,
                                                   float loadRadius, float unloadRadius);
    [[nodiscard]] bool isChunkLoaded(uint32_t chunkX, uint32_t chunkZ) const;
    [[nodiscard]] size_t loadedChunkCount() const;
    [[nodiscard]] size_t chunkCount() const { return chunks_.size(); }

    // Kronos ("Sky Map Full Engine Specification"): a real, direct,
    // distance-independent chunk hide/show -- the same real
    // Renderable::visible + detachChunkPhysics()/attachChunkPhysics()
    // toggle updateStreaming() already does per-chunk, callable for one
    // specific chunk regardless of viewer distance. What a real caller
    // uses to permanently hide a chunk it knows is real, honest "empty
    // space" content (e.g. a floating-island terrain's own void margin,
    // entirely below the void sentinel height) that would otherwise
    // always render as a real, visible flat floor no viewer distance
    // would ever stream away. A real, honest no-op for an out-of-range
    // (chunkX, chunkZ).
    void setChunkVisible(uint32_t chunkX, uint32_t chunkZ, bool visible);

    // --- Biome tagging (task category 5: "Add biome tags to terrain
    // chunks") -- deliberately just an opaque int here, not a dependency
    // on core/Biome.hpp: Terrain has no need to know what a biome *means*
    // (lighting presets, spawn tables, ...), only that each chunk can
    // carry one. core::BiomeId (Biome.hpp) is what gives these values
    // real meaning; the cast at the call site is real, not a code smell
    // -- the same "this class doesn't need to know" boundary
    // core::Physics keeps from core::MeshLibrary. -1 = untagged.
    void setChunkBiome(uint32_t chunkX, uint32_t chunkZ, int32_t biomeId);
    [[nodiscard]] int32_t chunkBiome(uint32_t chunkX, uint32_t chunkZ) const;

    // Sprint 8 ("Performance Stats & Debug Tools"): a real, minimal per-
    // chunk snapshot for the viewport's terrain-streaming debug overlay --
    // world-space center + half-extent (XZ; a chunk has no meaningful Y
    // extent of its own beyond the heightmap it samples) + current loaded
    // state, one entry per chunk. Pure read of already-built chunk state
    // (no GPU/physics work of its own), but only callable on an already-
    // `create()`d terrain, same GPU-instance-dependent category as every
    // other non-`static` method here.
    struct ChunkDebugInfo {
        glm::vec3 center{0.0f};
        float halfExtentX = 0.0f;
        float halfExtentZ = 0.0f;
        bool loaded = true;
    };
    [[nodiscard]] std::vector<ChunkDebugInfo> chunkDebugInfo() const;

    // Pure -- the real chunk-world-size formula chunkCenterWorld() and
    // chunkDebugInfo() both build on, extracted specifically so it has
    // real headless test coverage independent of a live terrain instance
    // (the same "pure decision split from the I/O-touching caller"
    // pattern this project has used since the Physics sprint).
    [[nodiscard]] static float chunkWorldSize(uint32_t gridResolution, uint32_t chunkCount, float cellSize);

private:
    struct Chunk {
        EntityId entity = kNullEntity;
        uint32_t meshHandle = Renderable::kInvalidHandle;
        uint32_t chunkX = 0;
        uint32_t chunkZ = 0;
        bool loaded = true;
        int32_t biomeId = -1;
    };

    [[nodiscard]] size_t heightIndex(uint32_t x, uint32_t z) const {
        return static_cast<size_t>(z) * info_.gridResolution + x;
    }
    [[nodiscard]] glm::vec2 worldToGrid(float worldX, float worldZ) const;
    [[nodiscard]] Mesh buildChunkMesh(uint32_t chunkX, uint32_t chunkZ) const;
    // Same vertex grid buildChunkMesh() walks, but returning plain
    // positions/indices -- what Physics::attachBodyToEntity()'s Mesh
    // path needs (host-side glm::vec3 positions, not full Vertex structs
    // with normal/uv Physics has no use for). A small, deliberate
    // duplication of buildChunkMesh()'s loop rather than restructuring
    // that function's return type -- lower risk to the already-working
    // render-mesh path.
    void collectChunkPositionsAndIndices(uint32_t chunkX, uint32_t chunkZ, std::vector<glm::vec3>& outPositions,
                                          std::vector<uint32_t>& outIndices) const;
    void regenerateChunk(Chunk& chunk);
    void regenerateChunksInRadius(float worldX, float worldZ, float radius);
    void attachChunkPhysics(Chunk& chunk);
    void detachChunkPhysics(Chunk& chunk);
    [[nodiscard]] glm::vec3 chunkCenterWorld(const Chunk& chunk) const;

    CreateInfo info_{};
    std::vector<float> heights_; // gridResolution * gridResolution, row-major (z-major)
    std::vector<Chunk> chunks_;
    PhysicsMaterial material_{};

    // Stashed at create() so brush calls don't need every Vulkan handle
    // threaded through each one -- same "own the resources, take plain
    // world-space params" shape core::CharacterController's Settings uses.
    ECS* ecs_ = nullptr;
    MeshLibrary* meshLibrary_ = nullptr;
    VmaAllocator allocator_ = nullptr;
    VkDevice device_ = nullptr;
    VkCommandPool cmdPool_ = nullptr;
    VkQueue queue_ = nullptr;
    Physics* physics_ = nullptr;
};

} // namespace engine::core
