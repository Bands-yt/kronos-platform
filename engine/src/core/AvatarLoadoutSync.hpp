#pragma once

#include <glm/glm.hpp>
#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/AssetCache.hpp"
#include "core/AvatarItem.hpp"
#include "core/AvatarLoadout.hpp"
#include "core/ECS.hpp"
#include "core/Mesh.hpp"
#include "core/Texture.hpp"

namespace engine::core {

class CatalogueIndex;

// A fixed, honestly-simplified local-space offset for where each
// category attaches on the stand-in mannequin rig this engine uses
// instead of a real skeleton (see AvatarAttachment.hpp's class comment
// on why -- no bone/skinning data exists anywhere in this engine).
// Emote's offset is never used (see applyLoadoutToAvatar()'s comment).
// Not meant to look production-quality -- meant to prove the real
// mechanism (equip -> real ECS entities -> real attachment tracking ->
// real rendering) end to end.
[[nodiscard]] glm::vec3 defaultAttachmentOffset(AvatarItemCategory category);

// Rebuilds `avatarRoot`'s equipped-item child entities from `loadout`:
// destroys every existing entity with AttachedTo{.parent == avatarRoot}
// first (a full regenerate, not an incremental diff -- simple and
// correct for how often a loadout actually changes: on equip/unequip,
// not every frame), then for each equipped item that resolves in
// `index`, loads its real mesh (core::loadObj + a real GPU upload) and
// real texture (core::Texture::loadFromFile), through
// `meshCache`/`textureCache` so re-applying the same loadout -- or two
// avatars wearing the same item -- doesn't redundantly re-upload the
// same GPU resources, and spawns a real child entity (Transform +
// Renderable + MeshSource + AttachedTo). A per-item load failure
// (missing file, malformed OBJ) is logged to stderr and that one item is
// skipped, not fatal to the rest of the loadout -- same fail-soft
// precedent as SceneManager::loadScene(). AvatarItemCategory::Emote
// spawns no entity at all -- there is nothing to attach visually (see
// AvatarItem.hpp's category comment).
//
// Calls core::updateAttachments() itself before returning, so the very
// first frame after applying a loadout already shows equipped items at
// the avatar's current pose, not one tick late.
void applyLoadoutToAvatar(const AvatarLoadout& loadout, EntityId avatarRoot, ECS& ecs, const CatalogueIndex& index,
                          MeshLibrary& meshLibrary, TextureLibrary& textureLibrary, AssetCache<uint32_t>& meshCache,
                          AssetCache<uint32_t>& textureCache, VmaAllocator allocator, VkDevice device,
                          VkCommandPool cmdPool, VkQueue queue);

} // namespace engine::core
