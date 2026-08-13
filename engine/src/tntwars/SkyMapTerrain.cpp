#include "tntwars/SkyMapTerrain.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "core/Components.hpp"
#include "core/Noise.hpp"
#include "core/Wind.hpp"
#include "miningsim/ForgeVisual.hpp"

namespace engine::tntwars {

namespace {
struct Candidate {
    glm::vec3 pos{0.0f};
    float distFromCenter = 0.0f;
};
} // namespace

std::vector<SkyIsland> generateSkyIslandLayout(glm::vec3 areaCenter, float areaRadius, uint32_t seed, int majorCount,
                                                int minorCount, const std::vector<SkyIsland>& reservedIslands) {
    constexpr float kMajorRadius = 26.0f;
    constexpr float kMinorRadius = 9.0f;
    // Kronos ("Sky Map Full Engine Specification" Section 3, "make the
    // map more open"): real, wider gap left between two islands' own
    // edges -- widened from an earlier 6.0 (live-inspected and judged
    // too tight/cluttered) to real, genuinely open void gaps a player
    // can feel between islands, not just a narrow crack.
    constexpr float kSpacingMargin = 18.0f;

    // Real candidate scatter: one Worley feature point per real grid cell
    // covering the placement area -- Worley's own defining property (one
    // point per cell, no two points arbitrarily close) makes it a real,
    // natural island-scattering source, not just an edge-texture input.
    // Widened alongside kSpacingMargin above so candidates are actually
    // spaced far enough apart to honor the wider real gap requirement.
    constexpr float kCellSize = 42.0f;
    int cellRange = static_cast<int>(areaRadius / kCellSize) + 2;
    std::vector<Candidate> candidates;
    for (int cz = -cellRange; cz <= cellRange; ++cz) {
        for (int cx = -cellRange; cx <= cellRange; ++cx) {
            glm::vec2 local = core::worleyFeaturePoint2D(cx, cz, seed) * kCellSize;
            glm::vec3 worldPos = areaCenter + glm::vec3(local.x, 0.0f, local.y);
            float dist = glm::length(glm::vec2(worldPos.x - areaCenter.x, worldPos.z - areaCenter.z));
            if (dist <= areaRadius) candidates.push_back({worldPos, dist});
        }
    }
    // Real, deterministic order (nearest-to-center first) -- keeps major
    // islands (placed first, below) real-clustered nearer the map's own
    // center, minors filling the outer ring, a real, sensible "hub and
    // satellites" layout rather than a fully random scatter.
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) { return a.distFromCenter < b.distFromCenter; });

    std::vector<SkyIsland> islands;
    auto tryPlace = [&](float radius, bool major) -> bool {
        for (const Candidate& candidate : candidates) {
            bool overlapsExisting = false;
            for (const SkyIsland& existing : islands) {
                float minSpacing = existing.radius + radius + kSpacingMargin;
                if (glm::length(candidate.pos - existing.center) < minSpacing) {
                    overlapsExisting = true;
                    break;
                }
            }
            // Real, general reserved-island clearance -- see this
            // function's own `reservedIslands` parameter comment.
            if (!overlapsExisting) {
                for (const SkyIsland& reserved : reservedIslands) {
                    float minSpacing = reserved.radius + radius + kSpacingMargin;
                    if (glm::length(candidate.pos - reserved.center) < minSpacing) {
                        overlapsExisting = true;
                        break;
                    }
                }
            }
            if (overlapsExisting) continue;
            // Real, honest "already used" check -- a candidate accepted
            // for an earlier island can't be reused for a later one.
            bool alreadyUsed = false;
            for (const SkyIsland& existing : islands) {
                if (glm::length(candidate.pos - existing.center) < 0.01f) {
                    alreadyUsed = true;
                    break;
                }
            }
            if (alreadyUsed) continue;
            islands.push_back(SkyIsland{candidate.pos, radius, major});
            return true;
        }
        return false; // real, honest failure -- ran out of non-overlapping candidates
    };

    for (int i = 0; i < majorCount; ++i) tryPlace(kMajorRadius, true);
    for (int i = 0; i < minorCount; ++i) tryPlace(kMinorRadius, false);

    return islands;
}

SkyIsland buildSkyBaseIsland(glm::vec3 center) {
    // Real, designed team-base island -- see SkyIsland::heightMultiplier's
    // own header comment. 50 units (within the brief's own 40-60-unit
    // spec, and vs 26 for a regular major) so a team's own base reads as
    // a genuine, generous destination in its own right, not a
    // slightly-bigger copy of every other island; 0.35 height multiplier
    // keeps real Perlin variation (so it doesn't read as a mathematically
    // flat disc) while taming it enough that the whole interior is
    // genuinely safe, stable footing for props and traversal nodes.
    return SkyIsland{center, 50.0f, /*major=*/true, /*heightMultiplier=*/0.35f};
}

float sampleSkyIslandHeight(float worldX, float worldZ, glm::vec3 islandCenter, float radius, uint32_t seed,
                             float heightMultiplier) {
    float dx = worldX - islandCenter.x;
    float dz = worldZ - islandCenter.z;
    float dist = std::sqrt(dx * dx + dz * dz);
    float angle = std::atan2(dz, dx);

    // Real Worley-jittered effective radius -- sampled around a real,
    // fixed-radius circle in noise-space parameterized purely by angle
    // (plus the island's own center, so two islands don't share the
    // exact same ragged silhouette), giving a real, smoothly-varying
    // (not per-vertex-random) irregular edge as angle sweeps 0..2*PI.
    float edgeNoiseX = islandCenter.x * 0.01f + std::cos(angle) * 3.0f;
    float edgeNoiseZ = islandCenter.z * 0.01f + std::sin(angle) * 3.0f;
    float edgeJitter = core::worleyNoise2D(edgeNoiseX, edgeNoiseZ, seed + 500u);
    float effectiveRadius = radius * (0.82f + 0.18f * glm::clamp(edgeJitter, 0.0f, 1.0f));

    // Real, per-island void level -- kSkyIslandVoidHeight below *this*
    // island's own real altitude (islandCenter.y), not a fixed absolute
    // world Y, so the void sentinel reads correctly regardless of how
    // high up this particular island sits.
    float voidHeight = islandCenter.y + kSkyIslandVoidHeight;
    if (dist >= effectiveRadius) return voidHeight;

    // Real low-frequency base dome + real high-frequency surface detail,
    // both real Perlin fBm (see fractalPerlinNoise2D()'s own header
    // comment for why gradient noise, not value noise, is used here),
    // added on top of the island's own real center altitude.
    core::FractalNoiseParams baseParams{0.015f, 6.0f, 3, 2.0f, 0.5f, seed};
    float base = core::fractalPerlinNoise2D(worldX, worldZ, baseParams);
    core::FractalNoiseParams detailParams{0.15f, 0.6f, 2, 2.0f, 0.5f, seed + 111u};
    float detail = core::fractalPerlinNoise2D(worldX, worldZ, detailParams);
    float height = islandCenter.y + (base + detail) * heightMultiplier;

    // Real cliff -- a smoothstep falloff from 70% to 100% of the
    // island's own real jittered radius, driving height down to the real
    // void sentinel right at the edge.
    float edgeT = glm::smoothstep(effectiveRadius * 0.7f, effectiveRadius, dist);
    height = glm::mix(height, voidHeight, edgeT);

    return height;
}

std::vector<core::Terrain> spawnSkyMapHeightfieldIslands(core::ECS& ecs, core::Physics& physics,
                                                           core::MeshLibrary& meshLibrary,
                                                           const core::ProceduralMaterialLibrary& materials,
                                                           VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool,
                                                           VkQueue queue, const std::vector<SkyIsland>& islands,
                                                           uint32_t seed) {
    std::vector<core::Terrain> terrains;
    terrains.reserve(islands.size());

    for (const SkyIsland& island : islands) {
        core::Terrain terrain;
        core::Terrain::CreateInfo info;
        info.cellSize = 1.0f;
        // Real chunk sizing: chunks must be meaningfully *smaller* than
        // the island's own real plateau (the flat interior before the
        // cliff falloff band starts at 70% of the effective radius) or
        // every real chunk ends up straddling plateau-to-void within
        // itself -- a real, live-captured bug this exact sizing fixes
        // (an earlier 32-unit-chunk version gave every single chunk a
        // real height range of ~40, always losing to the rock catch-all
        // band; see this function's own git history / session notes).
        // 10-unit chunks for a major island (real plateau diameter ~36)
        // and 6-unit chunks for a minor one (real plateau diameter ~12.6)
        // both real-guarantee at least a few chunks sitting fully inside
        // the flat top, distinct from the real edge chunks.
        if (island.major) {
            info.gridResolution = 61; // (61-1)/6 = 10 real world units per chunk
            info.chunkCount = 6;
        } else {
            info.gridResolution = 25; // (25-1)/4 = 6 real world units per chunk
            info.chunkCount = 4;
        }
        float halfExtent = static_cast<float>(info.gridResolution - 1) * info.cellSize * 0.5f;
        info.origin = glm::vec3(island.center.x - halfExtent, island.center.y, island.center.z - halfExtent);

        if (!terrain.create(info, ecs, meshLibrary, allocator, device, cmdPool, queue, &physics)) continue;

        std::vector<float> heights(static_cast<size_t>(info.gridResolution) * info.gridResolution);
        for (uint32_t z = 0; z < info.gridResolution; ++z) {
            for (uint32_t x = 0; x < info.gridResolution; ++x) {
                float worldX = info.origin.x + static_cast<float>(x) * info.cellSize;
                float worldZ = info.origin.z + static_cast<float>(z) * info.cellSize;
                heights[static_cast<size_t>(z) * info.gridResolution + x] =
                    sampleSkyIslandHeight(worldX, worldZ, island.center, island.radius, seed, island.heightMultiplier);
            }
        }
        // restoreHeightSnapshot() itself real-regenerates every chunk's
        // mesh + (since physics was passed to create() above) real
        // collider from these heights -- see that method's own comment.
        terrain.restoreHeightSnapshot(heights);

        // Real height/slope material bands -- ordered most-restrictive
        // first, matching applyHeightSlopeMaterialBlend()'s own real
        // priority-chain convention. Gated on real *slope* alone (maxHeight
        // left wide open at 1e6 -- a real, exact match to the brief's own
        // "grass on gentle slopes, rock on steep slopes" rule, which is
        // about steepness, not altitude): the base dome's own real low-
        // frequency amplitude (6.0 world units) means a flat-topped
        // chunk can legitimately sit well above the island's own average
        // height, so gating grass/dirt on an absolute height ceiling
        // real-excluded most legitimate flat peaks in an earlier version
        // of this function -- caught via a real live capture showing
        // every island uniformly rock-gray, fixed here. Rock (the
        // catch-all) still naturally covers every real edge chunk (a
        // chunk straddling the island's own real cliff has a real, huge
        // height range within it, from full island height down toward
        // the void sentinel).
        std::vector<core::Terrain::HeightSlopeMaterialBand> bands = {
            {1e6f, 2.0f, &materials.grass, 0.0f, 0.82f},
            {1e6f, 6.0f, &materials.dirt, 0.0f, 0.88f},
            {1e6f, 1e6f, &materials.stone, 0.05f, 0.85f},
        };
        terrain.applyHeightSlopeMaterialBlend(bands);

        // Kronos ("Sky Map Full Engine Specification"): real, permanent
        // hide for every chunk that's real, pure void -- entirely below
        // the island's own real effective radius, its own real
        // heightRange near 0 and averageHeight real-pinned to the void
        // level (a *partial* edge/cliff chunk, mixing real terrain and
        // void within itself, keeps its own real, visible rock face --
        // only chunks that are *nothing but* void get hidden). Without
        // this, every island's own real void margin always rendered as a
        // real, visible flat floor no viewer distance would ever stream
        // away (updateStreaming() only toggles by *distance*, not
        // content) -- a real, live-flagged visual defect this fixes.
        float voidHeight = island.center.y + kSkyIslandVoidHeight;
        for (uint32_t cz = 0; cz < info.chunkCount; ++cz) {
            for (uint32_t cx = 0; cx < info.chunkCount; ++cx) {
                core::Terrain::ChunkHeightStats stats =
                    core::Terrain::computeChunkHeightStats(heights, info.gridResolution, info.chunkCount, cx, cz);
                if (stats.heightRange < 1.0f && stats.averageHeight <= voidHeight + 1.0f) {
                    terrain.setChunkVisible(cx, cz, false);
                }
            }
        }

        terrains.push_back(std::move(terrain));
    }

    return terrains;
}

std::vector<std::pair<size_t, size_t>> selectSkyMapBridgeConnections(const std::vector<SkyIsland>& islands,
                                                                       float targetFraction) {
    struct PairDist {
        size_t a = 0;
        size_t b = 0;
        float dist = 0.0f;
    };
    std::vector<PairDist> pairs;
    for (size_t i = 0; i < islands.size(); ++i) {
        for (size_t j = i + 1; j < islands.size(); ++j) {
            pairs.push_back({i, j, glm::length(islands[i].center - islands[j].center)});
        }
    }
    // Real, deterministic order -- nearest pair first, the real "natural
    // bridge" read (islands close enough to plausibly span).
    std::sort(pairs.begin(), pairs.end(), [](const PairDist& x, const PairDist& y) { return x.dist < y.dist; });

    // Real, honest bridge-length cap -- a span connecting two islands on
    // opposite sides of the map wouldn't real-read as a walkable bridge.
    // Raised alongside generateSkyIslandLayout()'s own real, wider
    // kSpacingMargin (islands sit real-farther apart now) so a
    // meaningful real fraction of them can still real-bridge.
    constexpr float kMaxBridgeSpan = 140.0f;

    std::vector<bool> connected(islands.size(), false);
    std::vector<std::pair<size_t, size_t>> result;
    size_t targetCount = static_cast<size_t>(std::ceil(static_cast<float>(islands.size()) * targetFraction));
    size_t connectedCount = 0;
    for (const PairDist& p : pairs) {
        if (connectedCount >= targetCount) break;
        if (p.dist > kMaxBridgeSpan) continue;
        if (connected[p.a] && connected[p.b]) continue; // real, avoid a redundant span between two already-bridged islands
        result.push_back({p.a, p.b});
        if (!connected[p.a]) {
            connected[p.a] = true;
            ++connectedCount;
        }
        if (!connected[p.b]) {
            connected[p.b] = true;
            ++connectedCount;
        }
    }
    return result;
}

namespace {

core::EntityId spawnFeatureBox(core::ECS& ecs, core::Physics& physics, core::MeshLibrary& meshLibrary,
                                const core::PbrTextureSet& material, float metallic, float roughness,
                                glm::vec3 position, glm::vec3 halfExtents, glm::quat rotation, VmaAllocator allocator,
                                VkDevice device, VkCommandPool cmdPool, VkQueue queue, const char* name,
                                glm::vec3 emissiveColor = glm::vec3(0.0f), float emissiveIntensity = 0.0f,
                                bool collidable = true, glm::vec3 tintColor = glm::vec3(1.0f)) {
    core::Mesh mesh = core::Mesh::createBox(allocator, device, cmdPool, queue, halfExtents);
    if (mesh.vertexBuffer() == VK_NULL_HANDLE) return core::kNullEntity;
    uint32_t meshHandle = meshLibrary.registerMesh(std::move(mesh));

    core::EntityId entity = ecs.createEntity(name);
    if (auto* transform = ecs.tryGetComponent<core::Transform>(entity)) {
        transform->position = position;
        transform->rotation = rotation;
    }

    auto& renderable = ecs.addComponent<core::Renderable>(entity);
    renderable.meshHandle = meshHandle;
    // Real, deliberate multiplicative tint on top of the material's own
    // real albedo texture -- (1,1,1) (every existing caller) is a real,
    // exact no-op; Sky Bases' own real team-color banners are the first
    // real caller to pass anything else (see spawnSkyBase()'s own comment).
    renderable.baseColor = glm::vec4(tintColor, 1.0f);
    renderable.metallic = metallic;
    renderable.roughness = roughness;
    renderable.albedoTexture = material.albedo;
    renderable.normalTexture = material.normal;
    renderable.metallicTexture = material.metallic;
    renderable.roughnessTexture = material.roughness;
    renderable.aoTexture = material.ao;
    renderable.emissiveColor = emissiveColor;
    renderable.emissiveIntensity = emissiveIntensity;

    auto& meshSource = ecs.addComponent<core::MeshSource>(entity);
    meshSource.kind = core::MeshSourceKind::Box;
    meshSource.params = halfExtents;

    if (collidable) {
        core::ColliderShape shape;
        shape.kind = core::ColliderShapeKind::Box;
        shape.params = halfExtents;
        if (!physics.attachBodyToEntity(entity, ecs, shape, core::PhysicsMaterial{}, core::RigidBodyMotionType::Static)) {
            std::fprintf(stderr, "spawnSkyMapBridgesAndFeatures: attachBodyToEntity failed for \"%s\".\n", name);
        }
    }
    return entity;
}

} // namespace

SkyMapBridgesAndFeatures spawnSkyMapBridgesAndFeatures(core::ECS& ecs, core::Physics& physics,
                                                         core::MeshLibrary& meshLibrary,
                                                         const core::ProceduralMaterialLibrary& materials,
                                                         VmaAllocator allocator, VkDevice device,
                                                         VkCommandPool cmdPool, VkQueue queue,
                                                         const std::vector<SkyIsland>& islands, uint32_t seed) {
    SkyMapBridgesAndFeatures result;
    std::vector<core::EntityId>& spawned = result.otherEntities;

    // Kronos ("Sky Map Full Engine Specification" Section 6, "TNT can
    // break bridges"): one real DestructibleSegment per
    // selectSkyMapBridgeConnections() pair, anchored at each island's own
    // real edge point facing the other (island.center.y is a real, close
    // approximation of the plateau's own height there). Spawned via
    // spawnDestructibleWallVisual() -- the exact real machinery
    // TrenchesWall.hpp's own segments already use -- so a real, tested
    // detach/hide-then-rebuild cycle comes for free. Real, honest
    // limitation inherited from DestructibleSegment itself (position +
    // half-extents only, no rotation -- the same real constraint every
    // existing Trenches Wall_*/Cover_* segment already has, not a new gap
    // this function introduces): a diagonal bridge's own real collider/
    // mesh renders axis-aligned rather than yawed to the exact island-to-
    // island direction. `halfExtents.z` still real-matches the bridge's
    // own true span length.
    // Kronos ("Structural Expansion" world-building): real bridge variety
    // -- each bridge pair cycles through 3 real, distinct structural
    // styles by its own index (see this function's own header comment).
    // Segments are bucketed by style since spawnDestructibleWallVisual()
    // takes one real material per call ("a caller wanting mixed
    // materials calls this once per material group," see that function's
    // own header comment) -- built as 3 real, separate batches, then
    // concatenated back into one real, parallel-indexed result.
    std::vector<DestructibleSegment> woodSegments;
    std::vector<DestructibleSegment> ropeSegments;
    std::vector<DestructibleSegment> metalSegments;
    auto bridges = selectSkyMapBridgeConnections(islands, 0.3f);
    int bridgeIndex = 0;
    for (const auto& [ia, ib] : bridges) {
        glm::vec3 a = islands[ia].center;
        glm::vec3 b = islands[ib].center;
        glm::vec3 dirAB = glm::normalize(glm::vec3(b.x - a.x, 0.0f, b.z - a.z));
        glm::vec3 endA = a + dirAB * (islands[ia].radius * 0.9f);
        glm::vec3 endB = b - dirAB * (islands[ib].radius * 0.9f);
        glm::vec3 mid = (endA + endB) * 0.5f;
        float length = glm::length(glm::vec3(endB.x - endA.x, 0.0f, endB.z - endA.z)) * 0.5f;
        if (length < 1.0f) {
            ++bridgeIndex;
            continue; // real, honest skip -- islands close enough that no real span is needed
        }

        int style = bridgeIndex % 3; // 0 = wood plank, 1 = rope, 2 = metal catwalk
        if (style == 0) {
            DestructibleSegment segment;
            segment.position = mid;
            segment.halfExtents = glm::vec3(2.0f, 0.3f, length);
            segment.health = segment.maxHealth = 60.0f; // real, tuned lower than a wall segment -- a bridge should fall fast under TNT
            woodSegments.push_back(segment);
        } else if (style == 1) {
            // Real "rope bridge" -- real narrower deck, real lower
            // health than the wood-plank style above, reading as
            // genuinely flimsier (still the same real wood material --
            // no dedicated rope texture exists, the real geometry
            // difference is what carries the read).
            DestructibleSegment segment;
            segment.position = mid;
            segment.halfExtents = glm::vec3(1.1f, 0.15f, length);
            segment.health = segment.maxHealth = 35.0f;
            ropeSegments.push_back(segment);
        } else {
            // Real "metal catwalk" -- wider, tougher, and (the real,
            // deliberate jointed/segmented look, see this function's own
            // header comment) built from kPlateCount real, independently
            // destructible overlapping plates rather than one long slab.
            constexpr int kPlateCount = 3;
            float plateHalfLength = length / static_cast<float>(kPlateCount) * 1.15f; // real, slight overlap between neighboring plates
            for (int p = 0; p < kPlateCount; ++p) {
                float centerT = (static_cast<float>(p) + 0.5f) / static_cast<float>(kPlateCount);
                glm::vec3 platePos = endA + (endB - endA) * centerT;
                DestructibleSegment segment;
                segment.position = platePos;
                segment.halfExtents = glm::vec3(2.6f, 0.25f, plateHalfLength);
                segment.health = segment.maxHealth = 90.0f;
                metalSegments.push_back(segment);
            }
        }
        ++bridgeIndex;
    }

    std::vector<DestructibleSegmentVisual> woodVisuals = spawnDestructibleWallVisual(
        ecs, meshLibrary, physics, materials.wood, allocator, device, cmdPool, queue, woodSegments);
    std::vector<DestructibleSegmentVisual> ropeVisuals = spawnDestructibleWallVisual(
        ecs, meshLibrary, physics, materials.wood, allocator, device, cmdPool, queue, ropeSegments);
    std::vector<DestructibleSegmentVisual> metalVisuals = spawnDestructibleWallVisual(
        ecs, meshLibrary, physics, materials.metal, allocator, device, cmdPool, queue, metalSegments);

    result.bridgeSegments.insert(result.bridgeSegments.end(), woodSegments.begin(), woodSegments.end());
    result.bridgeSegments.insert(result.bridgeSegments.end(), ropeSegments.begin(), ropeSegments.end());
    result.bridgeSegments.insert(result.bridgeSegments.end(), metalSegments.begin(), metalSegments.end());
    result.bridgeVisuals.insert(result.bridgeVisuals.end(), woodVisuals.begin(), woodVisuals.end());
    result.bridgeVisuals.insert(result.bridgeVisuals.end(), ropeVisuals.begin(), ropeVisuals.end());
    result.bridgeVisuals.insert(result.bridgeVisuals.end(), metalVisuals.begin(), metalVisuals.end());

    // Real cantilevered rock overhangs -- every 3rd major island gets one
    // real ledge extending past its own heightfield edge (structurally
    // impossible for the heightfield itself to express, see this file's
    // own header comment), a real angled support strut underneath so it
    // doesn't read as floating.
    int majorIndex = 0;
    for (const SkyIsland& island : islands) {
        if (!island.major) continue;
        if (majorIndex % 3 == 0) {
            float angle = static_cast<float>(majorIndex) * 2.4f; // real, deterministic per-island direction
            glm::vec3 dir(std::cos(angle), 0.0f, std::sin(angle));
            glm::vec3 ledgeCenter = island.center + dir * (island.radius * 1.15f);
            float yaw = std::atan2(dir.x, dir.z);
            core::EntityId ledge = spawnFeatureBox(
                ecs, physics, meshLibrary, materials.stone, 0.05f, 0.85f, ledgeCenter, glm::vec3(6.0f, 0.6f, 5.0f),
                glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f)), allocator, device, cmdPool, queue,
                ("SkyMap_Overhang_" + std::to_string(majorIndex)).c_str());
            if (ledge != core::kNullEntity) spawned.push_back(ledge);
            // Real angled strut, visually bracing the ledge back to the
            // island's own real cliff face -- decorative-only (no
            // collider) so it doesn't create an awkward diagonal walking
            // surface underneath the real, flat, walkable ledge above.
            glm::vec3 strutCenter = island.center + dir * (island.radius * 0.85f) + glm::vec3(0.0f, -2.0f, 0.0f);
            core::EntityId strut = spawnFeatureBox(
                ecs, physics, meshLibrary, materials.stone, 0.05f, 0.85f, strutCenter, glm::vec3(0.6f, 2.5f, 0.6f),
                glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f)), allocator, device, cmdPool, queue,
                ("SkyMap_OverhangStrut_" + std::to_string(majorIndex)).c_str(), glm::vec3(0.0f), 0.0f, false);
            if (strut != core::kNullEntity) spawned.push_back(strut);
        }
        ++majorIndex;
    }

    // Real embedded sky-crystal clusters -- every 4th island (major or
    // minor) gets a real, small emissive crystal cluster near one of its
    // own edges, the brief's own "rare patches for visual identity," same
    // real decorative-cluster technique SkyMapVisual.cpp's own crystal
    // vein already establishes (no collider -- purely decorative). Real,
    // tighter/shorter/sky-blue-tinted cluster -- an earlier magenta-leaning
    // (0.5,0.3,1.0) tint + widely-spaced 0.7-unit-apart shards read as
    // stray "debug marker" boxes when live-inspected, not a real
    // embedded crystal formation; a cyan-blue tint (matching this map's
    // own real sky palette) and shards clustered close together fixes
    // that read.
    for (size_t i = 0; i < islands.size(); ++i) {
        if (i % 4 != 0) continue;
        const SkyIsland& island = islands[i];
        float angle = core::worleyNoise2D(island.center.x * 0.05f, island.center.z * 0.05f, seed + 900u) * 6.28318530718f;
        glm::vec3 dir(std::cos(angle), 0.0f, std::sin(angle));
        glm::vec3 clusterBase = island.center + dir * (island.radius * 0.75f);
        for (int c = 0; c < 3; ++c) {
            glm::vec3 halfExtents(0.25f, 0.55f + static_cast<float>(c % 2) * 0.25f, 0.25f);
            glm::vec3 pos = clusterBase + glm::vec3(static_cast<float>(c) * 0.35f, static_cast<float>(c % 2) * 0.2f, 0.0f);
            core::EntityId crystal = spawnFeatureBox(ecs, physics, meshLibrary, materials.crystal, 0.0f, 0.15f, pos,
                                                       halfExtents, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), allocator, device,
                                                       cmdPool, queue,
                                                       ("SkyMap_CrystalCluster_" + std::to_string(i) + "_" + std::to_string(c)).c_str(),
                                                       glm::vec3(0.25f, 0.65f, 0.95f), 0.2f, /*collidable=*/false);
            if (crystal != core::kNullEntity) spawned.push_back(crystal);
        }
    }

    return result;
}

SkyMapMovementFeatures spawnSkyMapMovementFeatures(core::ECS& ecs, core::Physics& physics,
                                                     core::MeshLibrary& meshLibrary,
                                                     const core::ProceduralMaterialLibrary& materials,
                                                     VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool,
                                                     VkQueue queue, const std::vector<SkyIsland>& islands) {
    SkyMapMovementFeatures features;

    // Real jump pads -- one per major island from index 2 onward
    // (islands[0]/[1] stay clear for the zip-line anchors below), a real
    // short, wide, emissive-cyan "wind geyser" vent prop sitting right on
    // the island's own real center (island.center.y is a real, close
    // approximation of that island's own plateau height, same reasoning
    // spawnSkyMapBridgesAndFeatures()'s own bridge anchors already use).
    for (size_t i = 2; i < islands.size(); ++i) {
        if (!islands[i].major) continue;
        tntwars::JumpPadState pad;
        pad.position = islands[i].center + glm::vec3(0.0f, 0.5f, 0.0f);
        pad.triggerRadius = 4.0f;
        pad.launchStrength = 16.0f;
        features.jumpPads.push_back(pad);

        core::EntityId vent = spawnFeatureBox(ecs, physics, meshLibrary, materials.crystal, 0.1f, 0.3f, pad.position,
                                               glm::vec3(2.5f, 0.5f, 2.5f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                                               allocator, device, cmdPool, queue,
                                               ("SkyMap_JumpPad_" + std::to_string(i)).c_str(), glm::vec3(0.3f, 0.9f, 1.0f),
                                               0.4f);
        if (vent != core::kNullEntity) features.spawnedEntities.push_back(vent);
    }

    // Kronos ("Sky Map Full Engine Specification" Section 5): real,
    // bidirectional, curved zip-lines -- linking islands[0]<->islands[1]
    // and (if a third major exists) a second real span, a real, distinct
    // traversal network from the nearest-neighbor bridge web above. The
    // real curve's own controlPoint is raised well above the straight
    // chord (scaled with span length) so it arcs *over* whatever real
    // terrain/rock sits between the two islands instead of clipping
    // through it -- the real, direct fix for the earlier straight-line
    // version's own live-flagged "cable pathing intersects rock" defect.
    // A real, multi-segment wooden cable (each segment oriented along
    // its own real local tangent, not one long straight box) traces that
    // same curve; a real anchor post at *both* ends (bidirectional riding
    // means either end is a real, valid launch point), each embedded
    // flush against that island's own real surface height rather than
    // floating.
    auto spawnZipLine = [&](size_t ia, size_t ib, const char* name) {
        if (ia >= islands.size() || ib >= islands.size()) return;
        tntwars::ZipLineState zip;
        zip.start = islands[ia].center + glm::vec3(0.0f, 2.0f, 0.0f);
        zip.end = islands[ib].center + glm::vec3(0.0f, 2.0f, 0.0f);
        float span = glm::length(zip.end - zip.start);
        if (span < 1.0f) return;
        glm::vec3 straightMid = (zip.start + zip.end) * 0.5f;
        // Real arc height -- a real fraction of the span, clamped to a
        // real sensible minimum/maximum so a short hop still clears real
        // rock and a long span doesn't arc absurdly high.
        float arcHeight = glm::clamp(span * 0.25f, 6.0f, 20.0f);
        zip.controlPoint = straightMid + glm::vec3(0.0f, arcHeight, 0.0f);
        zip.triggerRadius = 3.5f;
        zip.travelSpeed = 24.0f;
        features.zipLines.push_back(zip);

        // Real, multi-segment cable tracing the real curve.
        constexpr int kCableSegments = 8;
        for (int s = 0; s < kCableSegments; ++s) {
            float t0 = static_cast<float>(s) / static_cast<float>(kCableSegments);
            float t1 = static_cast<float>(s + 1) / static_cast<float>(kCableSegments);
            glm::vec3 p0 = tntwars::sampleZipLineCurve(zip, t0);
            glm::vec3 p1 = tntwars::sampleZipLineCurve(zip, t1);
            glm::vec3 segMid = (p0 + p1) * 0.5f;
            float segLength = glm::length(p1 - p0) * 0.5f;
            if (segLength < 0.05f) continue;
            glm::vec3 dir = (p1 - p0) / (segLength * 2.0f);
            float yaw = std::atan2(dir.x, dir.z);
            float pitch = std::asin(glm::clamp(dir.y, -1.0f, 1.0f));
            glm::quat rotation =
                glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f)) * glm::angleAxis(-pitch, glm::vec3(1.0f, 0.0f, 0.0f));
            core::EntityId cableSeg = spawnFeatureBox(ecs, physics, meshLibrary, materials.wood, 0.1f, 0.4f, segMid,
                                                        glm::vec3(0.12f, 0.12f, segLength), rotation, allocator, device,
                                                        cmdPool, queue, (std::string(name) + "_Seg" + std::to_string(s)).c_str(),
                                                        glm::vec3(0.0f), 0.0f, false);
            if (cableSeg != core::kNullEntity) features.spawnedEntities.push_back(cableSeg);
        }

        // Real anchor posts, embedded into each island's own real
        // surface (island.center.y, not the raised zip.start/end used
        // for the curve itself) so they read as attached to real rock,
        // not floating in open air.
        glm::vec3 anchorAPos(islands[ia].center.x, islands[ia].center.y + 0.75f, islands[ia].center.z);
        core::EntityId anchorA = spawnFeatureBox(ecs, physics, meshLibrary, materials.wood, 0.0f, 0.6f, anchorAPos,
                                                  glm::vec3(0.5f, 1.5f, 0.5f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                                                  allocator, device, cmdPool, queue, (std::string(name) + "_AnchorA").c_str());
        if (anchorA != core::kNullEntity) features.spawnedEntities.push_back(anchorA);

        glm::vec3 anchorBPos(islands[ib].center.x, islands[ib].center.y + 0.75f, islands[ib].center.z);
        core::EntityId anchorB = spawnFeatureBox(ecs, physics, meshLibrary, materials.wood, 0.0f, 0.6f, anchorBPos,
                                                  glm::vec3(0.5f, 1.5f, 0.5f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                                                  allocator, device, cmdPool, queue, (std::string(name) + "_AnchorB").c_str());
        if (anchorB != core::kNullEntity) features.spawnedEntities.push_back(anchorB);
    };
    spawnZipLine(0, 1, "SkyMap_ZipLine_0_1");
    if (islands.size() > 5) spawnZipLine(2, 5, "SkyMap_ZipLine_2_5");

    return features;
}

std::vector<core::EntityId> spawnSkyMapMidtierPlatforms(core::ECS& ecs, core::Physics& physics,
                                                          core::MeshLibrary& meshLibrary,
                                                          const core::ProceduralMaterialLibrary& materials,
                                                          VmaAllocator allocator, VkDevice device,
                                                          VkCommandPool cmdPool, VkQueue queue,
                                                          const std::vector<SkyIsland>& islands, uint32_t seed) {
    std::vector<core::EntityId> spawned;

    // Real, honest "already bridged" set -- the exact same real,
    // deterministic pair selection spawnSkyMapBridgesAndFeatures() itself
    // calls, so mid-tier platforms never duplicate a span that already
    // has a real bridge on it.
    auto bridgedPairs = selectSkyMapBridgeConnections(islands, 0.3f);
    auto isBridged = [&](size_t a, size_t b) {
        for (const auto& [pa, pb] : bridgedPairs) {
            if ((pa == a && pb == b) || (pa == b && pb == a)) return true;
        }
        return false;
    };

    // Real nearest-pair-first scan over every major-island pair, same
    // real O(n^2) distance sort selectSkyMapBridgeConnections() itself
    // uses -- picks the closest *not already bridged* pairs, a real,
    // deterministic "fill the next-best gaps" selection.
    struct PairDist {
        size_t a = 0;
        size_t b = 0;
        float dist = 0.0f;
    };
    std::vector<PairDist> candidates;
    for (size_t i = 0; i < islands.size(); ++i) {
        if (!islands[i].major) continue;
        for (size_t j = i + 1; j < islands.size(); ++j) {
            if (!islands[j].major) continue;
            if (isBridged(i, j)) continue;
            float dist = glm::length(islands[i].center - islands[j].center);
            // Real span cap -- a platform between two islands on
            // opposite sides of the map wouldn't real-read as a
            // meaningful mid-crossing rest stop, same real reasoning
            // selectSkyMapBridgeConnections()'s own kMaxBridgeSpan uses.
            if (dist > 160.0f) continue;
            candidates.push_back({i, j, dist});
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const PairDist& x, const PairDist& y) { return x.dist < y.dist; });

    constexpr int kMaxPlatforms = 3;
    int placed = 0;
    for (const PairDist& pair : candidates) {
        if (placed >= kMaxPlatforms) break;
        glm::vec3 a = islands[pair.a].center;
        glm::vec3 b = islands[pair.b].center;
        // Real, deterministic offset off the exact midpoint (a Worley
        // sample keyed by the pair itself) -- avoids every platform
        // reading as mechanically "exactly halfway," a real, small
        // organic variation.
        float jitter = core::worleyNoise2D(a.x * 0.02f + b.x * 0.02f, a.z * 0.02f + b.z * 0.02f, seed + 1500u);
        glm::vec3 mid = (a + b) * 0.5f + glm::vec3(0.0f, (jitter - 0.5f) * 6.0f, 0.0f);

        core::EntityId deck = spawnFeatureBox(ecs, physics, meshLibrary, materials.metal, 0.5f, 0.35f, mid,
                                               glm::vec3(6.0f, 0.4f, 6.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                                               allocator, device, cmdPool, queue,
                                               ("SkyMap_MidtierPlatform_" + std::to_string(placed)).c_str());
        if (deck != core::kNullEntity) spawned.push_back(deck);

        // Real, short decorative corner struts -- no collider (see this
        // function's own header comment on why), purely a real
        // "suspended scaffold" silhouette underneath the deck.
        const glm::vec3 corners[4] = {{4.5f, -1.2f, 4.5f}, {-4.5f, -1.2f, 4.5f}, {4.5f, -1.2f, -4.5f}, {-4.5f, -1.2f, -4.5f}};
        for (int c = 0; c < 4; ++c) {
            core::EntityId strut = spawnFeatureBox(
                ecs, physics, meshLibrary, materials.metal, 0.5f, 0.35f, mid + corners[c], glm::vec3(0.2f, 1.2f, 0.2f),
                glm::quat(1.0f, 0.0f, 0.0f, 0.0f), allocator, device, cmdPool, queue,
                ("SkyMap_MidtierStrut_" + std::to_string(placed) + "_" + std::to_string(c)).c_str(), glm::vec3(0.0f),
                0.0f, false);
            if (strut != core::kNullEntity) spawned.push_back(strut);
        }
        ++placed;
    }

    return spawned;
}

namespace {
struct TunnelPairDist {
    size_t a = 0;
    size_t b = 0;
    float dist = 0.0f;
};

// Real, pure tunnel-pair selection -- extracted so both
// spawnSkyMapHiddenTunnels() (the real spawn-touching caller) and
// planSkyMapAmbientSoundZones() (real echo-zone placement) agree on
// exactly which islands get tunnels, without duplicating the real
// selection logic in two places. Same "pure decision, real caller builds
// on it" split selectSkyMapBridgeConnections() already establishes.
std::vector<TunnelPairDist> selectSkyMapTunnelConnections(const std::vector<SkyIsland>& islands) {
    // Real, close-minor-island pair scan -- a tunnel only reads as a real
    // "shortcut" when the two islands are already near each other (a
    // long tunnel spanning open void would just be a floating tube, not
    // a real embedded cave route).
    std::vector<TunnelPairDist> candidates;
    for (size_t i = 0; i < islands.size(); ++i) {
        if (islands[i].major) continue;
        for (size_t j = i + 1; j < islands.size(); ++j) {
            if (islands[j].major) continue;
            float dist = glm::length(islands[i].center - islands[j].center);
            float combinedRadius = islands[i].radius + islands[j].radius;
            // Real, tight window -- close enough that a short tunnel
            // real-spans the real gap, far enough that the two islands'
            // own real edges aren't already touching.
            if (dist < combinedRadius + 6.0f || dist > combinedRadius + 30.0f) continue;
            candidates.push_back({i, j, dist});
        }
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const TunnelPairDist& x, const TunnelPairDist& y) { return x.dist < y.dist; });
    constexpr size_t kMaxTunnels = 2;
    if (candidates.size() > kMaxTunnels) candidates.resize(kMaxTunnels);
    return candidates;
}
} // namespace

std::vector<core::EntityId> spawnSkyMapHiddenTunnels(core::ECS& ecs, core::Physics& physics,
                                                       core::MeshLibrary& meshLibrary,
                                                       const core::ProceduralMaterialLibrary& materials,
                                                       VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool,
                                                       VkQueue queue, const std::vector<SkyIsland>& islands,
                                                       uint32_t seed) {
    std::vector<core::EntityId> spawned;
    (void)seed; // real, reserved for a future real per-tunnel variation seed -- deterministic geometry today doesn't need it

    int placed = 0;
    for (const TunnelPairDist& pair : selectSkyMapTunnelConnections(islands)) {
        glm::vec3 a = islands[pair.a].center;
        glm::vec3 b = islands[pair.b].center;
        glm::vec3 dirAB = glm::normalize(glm::vec3(b.x - a.x, 0.0f, b.z - a.z));
        float yaw = std::atan2(dirAB.x, dirAB.z);
        glm::quat rotation = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));

        glm::vec3 endA = a + dirAB * (islands[pair.a].radius * 0.85f);
        glm::vec3 endB = b - dirAB * (islands[pair.b].radius * 0.85f);
        glm::vec3 mid = (endA + endB) * 0.5f;
        float halfLength = glm::length(endB - endA) * 0.5f;
        if (halfLength < 1.0f) continue;

        std::string prefixStr = "SkyMap_Tunnel_" + std::to_string(placed);
        const char* prefix = prefixStr.c_str();
        // Real floor/roof/two-wall enclosed corridor -- real primitive
        // composition (see this function's own header comment on why a
        // heightfield can't express this at all), embedded flush at each
        // island's own real plateau height.
        core::EntityId floor = spawnFeatureBox(ecs, physics, meshLibrary, materials.stone, 0.05f, 0.85f,
                                                mid + glm::vec3(0.0f, -1.4f, 0.0f), glm::vec3(2.2f, 0.3f, halfLength),
                                                rotation, allocator, device, cmdPool, queue,
                                                (std::string(prefix) + "_Floor").c_str());
        if (floor != core::kNullEntity) spawned.push_back(floor);
        core::EntityId roof = spawnFeatureBox(ecs, physics, meshLibrary, materials.stone, 0.05f, 0.85f,
                                               mid + glm::vec3(0.0f, 1.6f, 0.0f), glm::vec3(2.2f, 0.3f, halfLength),
                                               rotation, allocator, device, cmdPool, queue,
                                               (std::string(prefix) + "_Roof").c_str());
        if (roof != core::kNullEntity) spawned.push_back(roof);
        glm::vec3 sideOffset = glm::vec3(-dirAB.z, 0.0f, dirAB.x) * 2.0f;
        core::EntityId wallLeft = spawnFeatureBox(ecs, physics, meshLibrary, materials.stone, 0.05f, 0.85f,
                                                   mid + sideOffset + glm::vec3(0.0f, 0.1f, 0.0f),
                                                   glm::vec3(0.3f, 1.5f, halfLength), rotation, allocator, device,
                                                   cmdPool, queue, (std::string(prefix) + "_WallLeft").c_str());
        if (wallLeft != core::kNullEntity) spawned.push_back(wallLeft);
        core::EntityId wallRight = spawnFeatureBox(ecs, physics, meshLibrary, materials.stone, 0.05f, 0.85f,
                                                    mid - sideOffset + glm::vec3(0.0f, 0.1f, 0.0f),
                                                    glm::vec3(0.3f, 1.5f, halfLength), rotation, allocator, device,
                                                    cmdPool, queue, (std::string(prefix) + "_WallRight").c_str());
        if (wallRight != core::kNullEntity) spawned.push_back(wallRight);

        ++placed;
    }

    return spawned;
}

SkyBaseFeatures spawnSkyBase(core::ECS& ecs, core::Physics& physics, core::MeshLibrary& meshLibrary,
                              const core::ProceduralMaterialLibrary& materials, VmaAllocator allocator,
                              VkDevice device, VkCommandPool cmdPool, VkQueue queue, const core::Terrain& terrain,
                              glm::vec3 baseCenter, SkyBaseSide side) {
    SkyBaseFeatures features;
    const char* prefix = (side == SkyBaseSide::TeamA) ? "SkyBase_TeamA" : "SkyBase_TeamB";
    // Real team-color banner accent -- matches MapLayout.cpp's own real
    // kTeamAColor/kTeamBColor exactly (not re-derived), so a Sky Base's
    // own real color identity agrees with every other map's own team
    // dressing.
    glm::vec3 teamColor = (side == SkyBaseSide::TeamA) ? glm::vec3(0.75f, 0.25f, 0.20f) : glm::vec3(0.20f, 0.35f, 0.80f);
    // Real facing direction toward the opposing team -- this map's own
    // real base placement (see main.cpp's own real call site) always
    // sits the two bases symmetric across the map's own center along
    // world X, so TeamA (negative X) faces +X and TeamB (positive X)
    // faces -X. A real, honest, documented assumption tied to that one
    // real layout, not a general-purpose bearing computation.
    float facingSign = (side == SkyBaseSide::TeamA) ? 1.0f : -1.0f;
    glm::vec3 facing(facingSign, 0.0f, 0.0f);
    glm::vec3 sideways(0.0f, 0.0f, 1.0f);
    auto groundAt = [&](float x, float z) { return terrain.heightAt(x, z); };

    // --- Forge zone: a real forged Explosive Charge tool prop on a real
    // anvil block -- no boss, no arena (this is TNT Wars, not Mining
    // Sim, see this file's own header comment). Offset from the base's
    // own real spawn point (baseCenter itself) so a spawning player
    // doesn't land on top of it. ---
    glm::vec3 forgePos = baseCenter + facing * -14.0f + sideways * 14.0f;
    forgePos.y = groundAt(forgePos.x, forgePos.z);
    core::EntityId anvil = spawnFeatureBox(ecs, physics, meshLibrary, materials.metal, 0.7f, 0.4f,
                                            forgePos + glm::vec3(0.0f, 0.4f, 0.0f), glm::vec3(1.2f, 0.4f, 0.8f),
                                            glm::quat(1.0f, 0.0f, 0.0f, 0.0f), allocator, device, cmdPool, queue,
                                            (std::string(prefix) + "_ForgeAnvil").c_str());
    if (anvil != core::kNullEntity) features.spawnedEntities.push_back(anvil);
    std::vector<core::EntityId> forgeToolEntities = miningsim::spawnForgedToolVisual(
        ecs, meshLibrary, materials, allocator, device, cmdPool, queue, miningsim::MiningToolType::ExplosiveCharge,
        forgePos + glm::vec3(0.0f, 0.85f, 0.0f));
    features.spawnedEntities.insert(features.spawnedEntities.end(), forgeToolEntities.begin(), forgeToolEntities.end());

    // --- Defense zone: a real, low parapet wall arc facing the
    // opposing team's own real approach direction -- each segment's own
    // real yaw (same "align local Z with the world direction, long axis
    // falls tangential automatically" technique the zip-line cable
    // segments above already use) faces it outward. ---
    constexpr int kParapetSegments = 5;
    constexpr float kParapetRadius = 32.0f;
    constexpr float kParapetArcRadians = 1.4f; // real, ~80 degrees of arc
    for (int i = 0; i < kParapetSegments; ++i) {
        float t = (kParapetSegments > 1)
                      ? (static_cast<float>(i) / static_cast<float>(kParapetSegments - 1) - 0.5f)
                      : 0.0f;
        float angle = t * kParapetArcRadians;
        glm::vec3 dir = glm::normalize(facing * std::cos(angle) + sideways * std::sin(angle));
        glm::vec3 segCenter = baseCenter + dir * kParapetRadius;
        segCenter.y = groundAt(segCenter.x, segCenter.z);
        glm::quat rotation = glm::angleAxis(std::atan2(dir.x, dir.z), glm::vec3(0.0f, 1.0f, 0.0f));
        core::EntityId wall = spawnFeatureBox(
            ecs, physics, meshLibrary, materials.stone, 0.05f, 0.85f, segCenter + glm::vec3(0.0f, 1.5f, 0.0f),
            glm::vec3(4.0f, 1.5f, 0.6f), rotation, allocator, device, cmdPool, queue,
            (std::string(prefix) + "_Parapet" + std::to_string(i)).c_str(), glm::vec3(0.0f), 0.0f, true,
            teamColor * 0.4f + glm::vec3(0.6f));
        if (wall != core::kNullEntity) features.spawnedEntities.push_back(wall);
    }

    // --- Observation zone: a real, tall watchtower with a small
    // platform on top, at the base's own outer edge -- same real facing
    // direction as the defense zone above (a real vantage point over
    // wherever the opposing team approaches from). ---
    glm::vec3 towerBase = baseCenter + facing * 40.0f;
    towerBase.y = groundAt(towerBase.x, towerBase.z);
    constexpr float kTowerHeight = 14.0f;
    core::EntityId towerShaft = spawnFeatureBox(
        ecs, physics, meshLibrary, materials.stone, 0.05f, 0.85f, towerBase + glm::vec3(0.0f, kTowerHeight * 0.5f, 0.0f),
        glm::vec3(1.8f, kTowerHeight * 0.5f, 1.8f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), allocator, device, cmdPool,
        queue, (std::string(prefix) + "_TowerShaft").c_str());
    if (towerShaft != core::kNullEntity) features.spawnedEntities.push_back(towerShaft);
    core::EntityId towerDeck = spawnFeatureBox(
        ecs, physics, meshLibrary, materials.wood, 0.1f, 0.6f, towerBase + glm::vec3(0.0f, kTowerHeight + 0.3f, 0.0f),
        glm::vec3(3.0f, 0.3f, 3.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), allocator, device, cmdPool, queue,
        (std::string(prefix) + "_TowerDeck").c_str());
    if (towerDeck != core::kNullEntity) features.spawnedEntities.push_back(towerDeck);

    // --- Utility zone (Kronos "Structural Expansion" world-building):
    // a real generator, a real crane, and a real suspended cargo pod --
    // gives the base a real, lived-in sense of purpose beyond pure
    // combat geometry (see this file's own header comment). Mirrored to
    // the opposite `sideways` side from the forge zone above so the two
    // don't crowd each other. ---
    glm::vec3 utilityBase = baseCenter + facing * -14.0f + sideways * -14.0f;
    utilityBase.y = groundAt(utilityBase.x, utilityBase.z);
    // Real generator -- a boxy machine block, a real emissive glow
    // standing in for "always running" (no real hum/audio system exists
    // in this engine yet to pair with it).
    core::EntityId generator = spawnFeatureBox(
        ecs, physics, meshLibrary, materials.metal, 0.6f, 0.5f, utilityBase + glm::vec3(0.0f, 0.9f, 0.0f),
        glm::vec3(1.4f, 0.9f, 1.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), allocator, device, cmdPool, queue,
        (std::string(prefix) + "_Generator").c_str(), glm::vec3(0.9f, 0.5f, 0.15f), 0.35f);
    if (generator != core::kNullEntity) features.spawnedEntities.push_back(generator);

    // Real crane -- a real vertical mast + a real horizontal boom.
    glm::vec3 craneBase = utilityBase + sideways * -6.0f;
    constexpr float kCraneHeight = 10.0f;
    core::EntityId craneMast = spawnFeatureBox(
        ecs, physics, meshLibrary, materials.metal, 0.6f, 0.4f, craneBase + glm::vec3(0.0f, kCraneHeight * 0.5f, 0.0f),
        glm::vec3(0.5f, kCraneHeight * 0.5f, 0.5f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), allocator, device, cmdPool,
        queue, (std::string(prefix) + "_CraneMast").c_str());
    if (craneMast != core::kNullEntity) features.spawnedEntities.push_back(craneMast);
    constexpr float kBoomLength = 7.0f;
    glm::vec3 boomCenter = craneBase + facing * (kBoomLength * 0.5f) + glm::vec3(0.0f, kCraneHeight, 0.0f);
    glm::quat boomRotation = glm::angleAxis(std::atan2(facing.x, facing.z), glm::vec3(0.0f, 1.0f, 0.0f));
    core::EntityId craneBoom =
        spawnFeatureBox(ecs, physics, meshLibrary, materials.metal, 0.6f, 0.4f, boomCenter, glm::vec3(0.35f, 0.35f, kBoomLength * 0.5f),
                         boomRotation, allocator, device, cmdPool, queue, (std::string(prefix) + "_CraneBoom").c_str(),
                         glm::vec3(0.0f), 0.0f, false);
    if (craneBoom != core::kNullEntity) features.spawnedEntities.push_back(craneBoom);

    // Real suspended cargo pod, hanging from the boom's own real tip on
    // a real thin cable prop -- decorative only (no real winch/load-
    // bearing gameplay system exists to hook a "cargo" mechanic into yet,
    // see this file's own header comment).
    glm::vec3 boomTip = craneBase + facing * kBoomLength + glm::vec3(0.0f, kCraneHeight, 0.0f);
    constexpr float kCableLength = 3.5f;
    core::EntityId cable = spawnFeatureBox(ecs, physics, meshLibrary, materials.metal, 0.6f, 0.4f,
                                            boomTip + glm::vec3(0.0f, -kCableLength * 0.5f, 0.0f),
                                            glm::vec3(0.08f, kCableLength * 0.5f, 0.08f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                                            allocator, device, cmdPool, queue, (std::string(prefix) + "_CraneCable").c_str(),
                                            glm::vec3(0.0f), 0.0f, false);
    if (cable != core::kNullEntity) features.spawnedEntities.push_back(cable);
    core::EntityId cargoPod = spawnFeatureBox(
        ecs, physics, meshLibrary, materials.wood, 0.1f, 0.6f, boomTip + glm::vec3(0.0f, -kCableLength - 0.9f, 0.0f),
        glm::vec3(1.1f, 0.9f, 1.1f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), allocator, device, cmdPool, queue,
        (std::string(prefix) + "_CargoPod").c_str());
    if (cargoPod != core::kNullEntity) features.spawnedEntities.push_back(cargoPod);

    // --- Vertical connector: a real multi-level deck + real lift
    // reaching it. A dedicated elevator/lift state machine is real
    // gameplay-layer scope (not architecture, see this file's own header
    // comment) -- this real "lift" instead reuses the exact same
    // JumpPadState vertical-launch mechanic every other jump pad on this
    // map already uses, just tuned to real-clear the deck's own height. ---
    glm::vec3 deckBase = baseCenter + facing * -22.0f;
    deckBase.y = groundAt(deckBase.x, deckBase.z);
    constexpr float kDeckHeight = 12.0f;
    core::EntityId deckSupport = spawnFeatureBox(
        ecs, physics, meshLibrary, materials.stone, 0.05f, 0.85f, deckBase + glm::vec3(0.0f, kDeckHeight * 0.5f, 0.0f),
        glm::vec3(2.5f, kDeckHeight * 0.5f, 2.5f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), allocator, device, cmdPool,
        queue, (std::string(prefix) + "_DeckSupport").c_str());
    if (deckSupport != core::kNullEntity) features.spawnedEntities.push_back(deckSupport);
    core::EntityId deckPlatform = spawnFeatureBox(
        ecs, physics, meshLibrary, materials.wood, 0.1f, 0.6f, deckBase + glm::vec3(0.0f, kDeckHeight + 0.3f, 0.0f),
        glm::vec3(9.0f, 0.3f, 9.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), allocator, device, cmdPool, queue,
        (std::string(prefix) + "_DeckPlatform").c_str());
    if (deckPlatform != core::kNullEntity) features.spawnedEntities.push_back(deckPlatform);

    JumpPadState lift;
    lift.position = deckBase + glm::vec3(0.0f, 0.5f, 0.0f);
    lift.triggerRadius = 3.5f;
    // Real, tuned launch strength -- enough real vertical speed to clear
    // kDeckHeight above the lift's own real ground level (same real
    // ballistic-launch convention every other jump pad on this map
    // already uses).
    lift.launchStrength = 22.0f;
    features.lift = lift;
    core::EntityId liftPad = spawnFeatureBox(ecs, physics, meshLibrary, materials.crystal, 0.1f, 0.3f, lift.position,
                                              glm::vec3(2.0f, 0.4f, 2.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), allocator,
                                              device, cmdPool, queue, (std::string(prefix) + "_Lift").c_str(),
                                              glm::vec3(0.3f, 0.9f, 1.0f), 0.4f);
    if (liftPad != core::kNullEntity) features.spawnedEntities.push_back(liftPad);

    // --- Lighting anchors ---
    // Real emissive crystal "GI-catching" landmarks near spawn -- this
    // renderer's own real bounce lighting (Renderer::setRTGIEnabled()) is
    // a global per-pixel toggle, not per-probe, so these read as real,
    // deliberately-placed light-catching landmarks, not literal probes.
    for (int i = 0; i < 3; ++i) {
        float angle = glm::radians(120.0f * static_cast<float>(i));
        glm::vec3 pos = baseCenter + glm::vec3(std::cos(angle), 0.0f, std::sin(angle)) * 8.0f;
        pos.y = groundAt(pos.x, pos.z);
        core::EntityId crystal = spawnFeatureBox(
            ecs, physics, meshLibrary, materials.crystal, 0.0f, 0.15f, pos + glm::vec3(0.0f, 1.0f, 0.0f),
            glm::vec3(0.5f, 1.0f, 0.5f), glm::angleAxis(angle, glm::vec3(0.0f, 1.0f, 0.0f)), allocator, device,
            cmdPool, queue, (std::string(prefix) + "_LightAnchor" + std::to_string(i)).c_str(),
            glm::vec3(0.25f, 0.65f, 0.95f), 0.5f, false);
        if (crystal != core::kNullEntity) features.spawnedEntities.push_back(crystal);
    }

    // Real, low-roughness polished monolith -- a real, deliberate SSR
    // reflection-surface anchor (see Renderer::setSSREnabled()'s own
    // comment: that pass reads real scene depth/color directly, not a
    // material buffer, so *any* sufficiently smooth real surface
    // benefits -- this prop just puts one somewhere a player will
    // actually walk past and see it catch a real reflection).
    glm::vec3 monolithPos = baseCenter + facing * 6.0f;
    monolithPos.y = groundAt(monolithPos.x, monolithPos.z);
    core::EntityId monolith = spawnFeatureBox(ecs, physics, meshLibrary, materials.metal, 0.9f, 0.08f,
                                               monolithPos + glm::vec3(0.0f, 3.0f, 0.0f), glm::vec3(1.0f, 3.0f, 1.0f),
                                               glm::quat(1.0f, 0.0f, 0.0f, 0.0f), allocator, device, cmdPool, queue,
                                               (std::string(prefix) + "_Monolith").c_str());
    if (monolith != core::kNullEntity) features.spawnedEntities.push_back(monolith);

    // Real, tall thin spire -- real height/thinness alone (no
    // shader-side special-casing needed) is what catches real godray
    // shafts at this map's own tuned sun elevation, see
    // Renderer::setAtmosphereScatteringEnabled()'s own real light-shaft
    // comment.
    glm::vec3 spirePos = baseCenter + sideways * -30.0f + facing * 10.0f;
    spirePos.y = groundAt(spirePos.x, spirePos.z);
    core::EntityId spire = spawnFeatureBox(ecs, physics, meshLibrary, materials.stone, 0.05f, 0.7f,
                                            spirePos + glm::vec3(0.0f, 9.0f, 0.0f), glm::vec3(0.6f, 9.0f, 0.6f),
                                            glm::quat(1.0f, 0.0f, 0.0f, 0.0f), allocator, device, cmdPool, queue,
                                            (std::string(prefix) + "_Spire").c_str(), teamColor, 0.15f);
    if (spire != core::kNullEntity) features.spawnedEntities.push_back(spire);

    return features;
}

std::vector<core::EntityId> spawnSkyMapVegetation(core::ECS& ecs, core::Physics& physics, core::MeshLibrary& meshLibrary,
                                                    const core::ProceduralMaterialLibrary& materials,
                                                    VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool,
                                                    VkQueue queue, const core::Terrain& terrain, const SkyIsland& island,
                                                    uint32_t seed) {
    std::vector<core::EntityId> spawned;
    int clusterCount = island.major ? 7 : 3;

    for (int i = 0; i < clusterCount; ++i) {
        float angleSeedX = island.center.x * 0.1f + static_cast<float>(i) * 3.7f;
        float angleSeedZ = island.center.z * 0.1f + static_cast<float>(i) * 3.7f;
        float angle = core::worleyNoise2D(angleSeedX, angleSeedZ, seed + 2000u) * 6.28318530718f;
        float radiusFrac = 0.15f + 0.55f * core::worleyNoise2D(angleSeedX * 1.7f, angleSeedZ * 1.7f, seed + 2001u);
        glm::vec3 pos = island.center + glm::vec3(std::cos(angle), 0.0f, std::sin(angle)) * (island.radius * radiusFrac);

        // Real finite-difference slope check -- rejects a candidate too
        // close to the island's own real cliff edge (see this function's
        // own header comment).
        float h0 = terrain.heightAt(pos.x, pos.z);
        float hx = terrain.heightAt(pos.x + 1.0f, pos.z);
        float hz = terrain.heightAt(pos.x, pos.z + 1.0f);
        if (std::abs(hx - h0) + std::abs(hz - h0) > 1.2f) continue;
        pos.y = h0;

        // Real, sparse crystal flora -- 1 in 4 clusters, matching the
        // brief's own "rare patches" identity every other crystal
        // placement on this map already establishes.
        bool isCrystalFlora = (i % 4 == 3);
        constexpr int kBladeCount = 3;
        for (int b = 0; b < kBladeCount; ++b) {
            float bladeAngle = static_cast<float>(b) / static_cast<float>(kBladeCount) * 6.28318530718f;
            glm::vec3 bladePos = pos + glm::vec3(std::cos(bladeAngle), 0.0f, std::sin(bladeAngle)) * 0.15f;
            glm::quat bladeRotation = glm::angleAxis(bladeAngle, glm::vec3(0.0f, 1.0f, 0.0f));
            std::string name = "SkyMap_Vegetation_" + std::to_string(static_cast<int>(pos.x * 10.0f)) + "_" +
                                std::to_string(static_cast<int>(pos.z * 10.0f)) + "_" + std::to_string(b);

            core::EntityId blade = core::kNullEntity;
            if (isCrystalFlora) {
                glm::vec3 halfExtents(0.05f, 0.22f + static_cast<float>(b % 2) * 0.12f, 0.05f);
                blade = spawnFeatureBox(ecs, physics, meshLibrary, materials.crystal, 0.0f, 0.15f,
                                         bladePos + glm::vec3(0.0f, halfExtents.y, 0.0f), halfExtents, bladeRotation,
                                         allocator, device, cmdPool, queue, name.c_str(), glm::vec3(0.25f, 0.65f, 0.95f),
                                         0.35f, false);
            } else {
                glm::vec3 halfExtents(0.04f, 0.16f + static_cast<float>(b % 2) * 0.08f, 0.04f);
                blade = spawnFeatureBox(ecs, physics, meshLibrary, materials.grass, 0.0f, 0.8f,
                                         bladePos + glm::vec3(0.0f, halfExtents.y, 0.0f), halfExtents, bladeRotation,
                                         allocator, device, cmdPool, queue, name.c_str(), glm::vec3(0.0f), 0.0f, false);
            }
            if (blade == core::kNullEntity) continue;
            spawned.push_back(blade);

            // Real wind sway -- see core::WindSway's own comment. `phase`
            // hashed from world position so a cluster's own blades don't
            // all swing in obviously-synchronized unison.
            core::WindSway sway;
            sway.baseRotation = bladeRotation;
            sway.amplitudeDegrees = isCrystalFlora ? 2.0f : 6.0f; // real -- rigid crystal sways far less than soft grass
            sway.speed = 1.2f + core::worleyNoise2D(bladePos.x, bladePos.z, seed + 2002u) * 0.8f;
            sway.phase = core::worleyNoise2D(bladePos.x * 2.3f, bladePos.z * 2.3f, seed + 2003u) * 6.28318530718f;
            ecs.addComponent<core::WindSway>(blade, sway);
        }
    }

    return spawned;
}

std::vector<AmbientSoundZone> planSkyMapAmbientSoundZones(const std::vector<SkyIsland>& islands,
                                                            glm::vec3 teamABaseCenter, glm::vec3 teamBBaseCenter) {
    // Kronos ("Environmental Detail" world-building): real, honest
    // placement-only data -- see this function's own header comment in
    // SkyMapTerrain.hpp for why no real core::AudioSource gets attached
    // here. Verified before writing this: no .wav/.ogg/.mp3 asset exists
    // anywhere in this project (core::Audio::loadSound() needs a real
    // file path, and this map has none to give it) -- wiring a fake path
    // in would either silently no-op or spam a real load-failure log
    // every single run, neither of which is honest "it works" behavior.
    std::vector<AmbientSoundZone> zones;

    // Wind near cliffs -- one real zone per major island's own real edge,
    // where the terrain's own real cliff falloff (see
    // sampleSkyIslandHeight()'s own header comment) is steepest.
    for (const SkyIsland& island : islands) {
        if (!island.major) continue;
        AmbientSoundZone zone;
        zone.position = island.center;
        zone.radius = island.radius * 1.3f;
        zone.category = AmbientSoundCategory::Wind;
        zones.push_back(zone);
    }

    // Hum near each base's own real forge/generator (see spawnSkyBase()'s
    // own real forge-zone/utility-zone offsets -- `facing` there is +X
    // for Team A, -X for Team B, `sideways` a real, constant +Z for both;
    // mirrored here directly rather than re-deriving a SkyBaseSide this
    // function was never given).
    AmbientSoundZone humA;
    humA.position = teamABaseCenter + glm::vec3(-14.0f, 0.0f, -14.0f);
    humA.radius = 18.0f;
    humA.category = AmbientSoundCategory::Hum;
    zones.push_back(humA);
    AmbientSoundZone humB;
    humB.position = teamBBaseCenter + glm::vec3(14.0f, 0.0f, -14.0f);
    humB.radius = 18.0f;
    humB.category = AmbientSoundCategory::Hum;
    zones.push_back(humB);

    // Echo inside each real hidden tunnel -- the exact same real pair
    // selection spawnSkyMapHiddenTunnels() itself uses (see
    // selectSkyMapTunnelConnections()'s own comment), so an echo zone
    // only ever exists where a real tunnel actually does.
    for (const TunnelPairDist& pair : selectSkyMapTunnelConnections(islands)) {
        AmbientSoundZone zone;
        zone.position = (islands[pair.a].center + islands[pair.b].center) * 0.5f;
        zone.radius = 12.0f;
        zone.category = AmbientSoundCategory::Echo;
        zones.push_back(zone);
    }

    return zones;
}

std::vector<core::EntityId> spawnSkyMapAtmosphericDust(core::ECS& ecs, const std::vector<SkyIsland>& islands) {
    std::vector<core::EntityId> spawned;

    int majorIndex = 0;
    for (const SkyIsland& island : islands) {
        if (!island.major) continue;
        // Real, sparse selection -- every other major island, not all of
        // them (a dust mote in front of every single island would read as
        // uniform haze, not a real, deliberate atmospheric accent).
        bool place = (majorIndex % 2 == 0);
        ++majorIndex;
        if (!place) continue;

        core::EntityId entity = ecs.createEntity(("SkyMap_AtmosphericDust_" + std::to_string(majorIndex)).c_str());
        if (auto* transform = ecs.tryGetComponent<core::Transform>(entity)) {
            transform->position = island.center + glm::vec3(0.0f, 4.0f, 0.0f);
        }

        auto& emitter = ecs.addComponent<core::ParticleEmitter>(entity);
        emitter.settings.looping = true;
        emitter.settings.emissionRate = 4.0f; // real, sparse -- individually visible motes, not a thick haze
        emitter.settings.particleLifetime = 12.0f;
        emitter.settings.particleLifetimeVariance = 4.0f;
        emitter.settings.gravity = glm::vec3(0.0f, -0.01f, 0.0f); // real, near-zero -- dust hangs/floats, it doesn't fall
        emitter.settings.sizeStart = 0.06f;
        emitter.settings.sizeEnd = 0.03f;
        // Real, warm, pale gold -- sunlit dust motes, real low alpha so
        // they read as a subtle accent, never competing with real scene
        // geometry for attention.
        emitter.settings.colorStart = glm::vec4(1.0f, 0.92f, 0.75f, 0.35f);
        emitter.settings.colorEnd = glm::vec4(1.0f, 0.90f, 0.70f, 0.0f);

        core::AtmosphericDustEmitter dust;
        emitter.settings.velocityMin = dust.baseVelocityMin;
        emitter.settings.velocityMax = dust.baseVelocityMax;
        ecs.addComponent<core::AtmosphericDustEmitter>(entity, dust);

        spawned.push_back(entity);
    }

    return spawned;
}

} // namespace engine::tntwars
