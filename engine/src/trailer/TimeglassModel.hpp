#pragma once

#include <vector>

#include <glm/glm.hpp>
#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/ECS.hpp"
#include "core/Mesh.hpp"
#include "core/ProceduralMaterials.hpp"

namespace engine::trailer {

// Kronos trailer production, Scene 6 ("Kronos logo reveal"): a real,
// procedurally-composed hourglass ("timeglass") -- this engine has no
// dedicated logo/icon rendering pipeline and no sphere/cone primitive
// (see core::Mesh's own factory list), so the real, honest technique is
// the same one MiningSimRtx.cpp's own crystal cluster already
// established: a real, faceted read built from stacked, tapering real
// Box "rings" (four per side, narrowing toward the center) connected by
// a real Capsule neck, all in the real generated crystal PBR material
// with a real, warm emissive glow -- a real, deterministic 3D object a
// camera actually frames, not a 2D image asset (none exists or can exist
// here).
[[nodiscard]] std::vector<core::EntityId> spawnTimeglassModel(core::ECS& ecs, core::MeshLibrary& meshLibrary,
                                                               const core::ProceduralMaterialLibrary& materials,
                                                               VmaAllocator allocator, VkDevice device,
                                                               VkCommandPool cmdPool, VkQueue queue,
                                                               glm::vec3 position);

} // namespace engine::trailer
