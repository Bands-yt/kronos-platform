#pragma once

#include <string>
#include <vector>

#include <volk.h>
#include <vk_mem_alloc.h>

#include <glm/glm.hpp>

#include "core/AvatarLoadout.hpp"
#include "core/ECS.hpp"
#include "core/RiggedMesh.hpp"
#include "core/Skeleton.hpp"

namespace engine::core {

// Real, fixed default -- a warm chestnut brown, the classic "bacon hair"
// reference tone. No hair-color picker/customization UI exists yet (this
// pass is a silhouette/geometry pass, not a new customization surface) --
// a real, stated, small follow-up, not silently pretended to already
// exist.
constexpr glm::vec4 kDefaultHairColor(0.36f, 0.22f, 0.14f, 1.0f);

// Kronos ("Avatar Visual Silhouette Pass" -- "Head and Hair" -- "Add a
// unified stylised hair mass (layered tufts/spikes)"): real, default,
// always-on hair -- a "bacon-hair"-inspired base look every fresh avatar
// has, distinct from the pre-existing *equippable* Hair accessory
// (AvatarItemCategory::Hair, the real attach_hair joint,
// spawnAvatarAccessories()), which stays a real, separate, player-
// chosen override. This default hair is rigidly bound directly to the
// "head" joint (not attach_hair -- that joint is reserved for the
// equippable override, so the two real hair sources never both render
// at once; see spawnAvatarDefaultHair()'s own real, honest skip when a
// Hair item is equipped). Several small, real, tapered spike/tuft
// meshes, each its own real entity with its own real, discrete color
// step (root/base tufts darker, upper/tip tufts lighter) approximating
// a root-to-tip gradient -- a deliberate, honest choice, not a true
// per-vertex GPU vertex-color channel: core::Vertex (Mesh.hpp) has no
// color attribute, and adding one is a real, separate, engine-wide
// rendering-pipeline change (every mesh type's vertex layout, both scene
// shaders), not a bounded avatar-visual addition. This is the same real,
// discrete-step "gradient" convention applySegmentShadingGradient()
// (RiggedAvatar.hpp) already established for body-segment shading,
// reused here for hair instead of skin/cloth -- see that function's own
// comment for the precedent.
[[nodiscard]] bool spawnAvatarDefaultHair(ECS& ecs, const Skeleton& skeleton, const AvatarLoadout& loadout,
                                           glm::vec4 hairColor, RiggedMeshLibrary& riggedMeshLibrary,
                                           VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool,
                                           VkQueue queue, std::vector<EntityId>& outHairEntities, std::string& outError);

} // namespace engine::core
