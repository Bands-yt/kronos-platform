#pragma once

#include <vector>

#include <glm/glm.hpp>
#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/ECS.hpp"
#include "core/Mesh.hpp"
#include "core/ProceduralMaterials.hpp"
#include "miningsim/MiningTools.hpp"

namespace engine::miningsim {

// Kronos trailer production: the real, first ECS/render consumer of
// Milestone 15's own real MiningToolType roster -- a real Box (head) +
// Capsule (handle) composition (a real, honest pickaxe/hammer-style
// silhouette from primitives, the same technique this whole session's
// other creature/machinery visuals already use; no dedicated tool-mesh
// generator exists). Real emissive glow scaled by the tool's own real
// miningPower (Milestone 15) -- a visibly stronger tool glows brighter,
// tying the real forge-reveal shot directly back to that tool's own real
// stats rather than a cosmetic-only color choice.
[[nodiscard]] std::vector<core::EntityId> spawnForgedToolVisual(core::ECS& ecs, core::MeshLibrary& meshLibrary,
                                                                 const core::ProceduralMaterialLibrary& materials,
                                                                 VmaAllocator allocator, VkDevice device,
                                                                 VkCommandPool cmdPool, VkQueue queue,
                                                                 MiningToolType tool, glm::vec3 position);

} // namespace engine::miningsim
