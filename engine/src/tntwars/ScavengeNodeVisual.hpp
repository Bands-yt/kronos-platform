#pragma once

#include <vector>

#include <glm/glm.hpp>
#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/ECS.hpp"
#include "core/Mesh.hpp"
#include "core/ProceduralMaterials.hpp"
#include "tntwars/MapDefinition.hpp"
#include "tntwars/Scavenging.hpp"

namespace engine::tntwars {

// Kronos ("Gameplay Loop" world-building): the real ECS-touching half
// Scavenging.hpp's own header comment says was always missing -- turns
// each already-correct, already-tested `ScavengeNodeState` (pure
// position/material/quantity data, real territory-gated depletion/respawn
// logic) into a real, visible, walk-up-and-press-E world entity. Mirrors
// DestructibleGeometryVisual.hpp's own real "parallel-indexed state +
// visual" shape exactly, minus physics: a resource node is a walk-through
// pickup, not an obstacle, so it gets no collider at all.

// Per-node real runtime state, parallel-indexed with the
// `ScavengeNodeState` vector it was built from (nodes[i] <-> this[i]
// always refer to the same real node).
struct ScavengeNodeVisual {
    core::EntityId entity = core::kNullEntity;
    bool currentlyDepleted = false; // mirrors nodes[i].quantityRemaining <= 0, tracked to avoid redundant per-tick writes
};

// Real spawn -- one small emissive crystal-shard Box mesh per node
// (materials.crystal, tinted per `scavengeMaterialInfo(node.material).color`
// so Scrap/Powder/Wiring -- and their real per-map flavor names via
// mapMaterialDisplayName() -- stay visually distinguishable at a glance),
// each carrying a real `core::Interactable` (prompt = the node's own real
// biome-flavored display name, proximityEnabled=true). No physics body:
// see this file's own header comment. A node whose real Mesh/GPU upload
// fails gets `core::kNullEntity` in its own slot (a real, honest partial
// failure, matching spawnDestructibleWallVisual()'s own convention).
[[nodiscard]] std::vector<ScavengeNodeVisual> spawnScavengeNodeVisuals(
    core::ECS& ecs, core::MeshLibrary& meshLibrary, const core::ProceduralMaterialLibrary& materials,
    VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
    const std::vector<ScavengeNodeState>& nodes, MapId map);

// Real per-tick sync, called once per real gameplay tick after
// tickScavengeNodeRespawn()/scavengeNode() have already updated `nodes`
// this tick: a node that just became depleted (quantityRemaining <= 0,
// not yet marked in `visuals[i]`) gets its mesh hidden
// (`Renderable::visible = false`) and its `Interactable::proximityEnabled`
// cleared (so it drops out of findInteractablesInRange() -- a depleted
// node genuinely cannot be interacted with, not just visually hidden); a
// node that just respawned gets both restored. `nodes` and `visuals` must
// be the same real size (parallel-indexed) -- a caller passing mismatched
// vectors gets a real, honest no-op past the shorter one's length.
void tickScavengeNodeVisuals(const std::vector<ScavengeNodeState>& nodes, std::vector<ScavengeNodeVisual>& visuals,
                              core::ECS& ecs);

} // namespace engine::tntwars
