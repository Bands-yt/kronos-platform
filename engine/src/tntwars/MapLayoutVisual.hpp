#pragma once

#include <vector>

#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/ECS.hpp"
#include "core/Mesh.hpp"
#include "core/Physics.hpp"
#include "core/ProceduralMaterials.hpp"
#include "tntwars/MapLayout.hpp"

namespace engine::tntwars {

// Kronos ("TNT Wars Foundational Playability" Phase 2): the real, live-
// game counterpart to studio::plugins::TntWarsPlugin::buildMapGeometry()
// -- same real per-piece mesh/material spawn (see that function's own
// comment), plus the one real thing it deliberately never adds: a live
// physics collider per piece. That omission is real and correct for
// Studio (core::SceneFile.hpp: Studio never owns a live core::Physics at
// all), but a live TNT Wars match needs players/TNT/projectiles to
// actually collide with the ground, cover, and bases -- this is that
// real runtime path. Kept as its own module (not a Studio-plugin
// extension) so Studio's own authoring-only scope stays exactly what its
// own header comments already document.

// Real spawn: one Box/Plane mesh entity + one real static Jolt collider
// per `tntwars::MapLayoutPiece` in `buildMapLayout(map)`. Returns the
// spawned entities (for a caller wanting to clear/rebuild the map later,
// matching TntWarsPlugin::clearMapGeometry()'s own precedent).
[[nodiscard]] std::vector<core::EntityId> spawnMapLayoutVisual(core::ECS& ecs, core::Physics& physics,
                                                                 core::MeshLibrary& meshLibrary,
                                                                 const core::ProceduralMaterialLibrary& materials,
                                                                 VmaAllocator allocator, VkDevice device,
                                                                 VkCommandPool cmdPool, VkQueue queue, MapId map);

} // namespace engine::tntwars
