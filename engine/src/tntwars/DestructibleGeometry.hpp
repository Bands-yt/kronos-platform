#pragma once

#include <cstddef>
#include <vector>

#include <glm/glm.hpp>

namespace engine::tntwars {

// Kronos roadmap Milestone 2 ("Destructible geometry"): the real,
// pure-data building block a wall/thruster/Core structure is made of --
// many small real segments, each with its own real health, rather than
// one all-or-nothing piece. Deliberately the "segmented entities" design
// the roadmap chose over true voxel destruction: real, achievable, and
// consistent with MapLayoutPiece's own "pure data, no Vulkan dependency"
// precedent (this struct has the exact same shape: position + halfExtents
// + real gameplay state layered on top). A real ECS-touching caller (a
// future Studio/TntWarsPlugin extension, matching how it already turns
// MapLayoutPiece into real entities) is what would turn one of these into
// a real spawned Box entity -- this module stays headless and testable.
struct DestructibleSegment {
    glm::vec3 position{0.0f};
    glm::vec3 halfExtents{1.0f};
    float health = 100.0f;
    float maxHealth = 100.0f;
};

constexpr float kDefaultSegmentHealth = 100.0f;

// Real, deterministic grid subdivision of one wall-shaped volume into
// `columns` x `rows` real segments covering the exact same overall
// footprint -- columns divide the real width (X), rows divide the real
// height (Y); segment thickness (Z) is left at the full wall thickness
// (a real wall breaks into columns and rows of rubble, not slices along
// its own thin axis). `columns`/`rows` below 1 are real-clamped to 1
// (a single, whole-wall segment) rather than producing an empty/invalid
// grid.
[[nodiscard]] std::vector<DestructibleSegment> buildSegmentedWall(glm::vec3 center, glm::vec3 wallHalfExtents,
                                                                    int columns, int rows,
                                                                    float segmentHealth = kDefaultSegmentHealth);

// Real, clamped damage application -- matches TntWarsMatch::applyDamage()'s
// own "clamp at 0, ignore non-positive damage" convention exactly.
void applyDamageToSegment(DestructibleSegment& segment, float damage);
[[nodiscard]] bool isSegmentDestroyed(const DestructibleSegment& segment);
[[nodiscard]] size_t countAliveSegments(const std::vector<DestructibleSegment>& segments);
// Real-false for an empty structure -- "no segments" isn't the same real
// state as "every segment was destroyed."
[[nodiscard]] bool isStructureFullyDestroyed(const std::vector<DestructibleSegment>& segments);

} // namespace engine::tntwars
