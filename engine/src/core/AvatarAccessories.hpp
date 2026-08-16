#pragma once

#include <string>
#include <vector>

#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/AvatarLoadout.hpp"
#include "core/CatalogueIndex.hpp"
#include "core/ECS.hpp"
#include "core/RiggedMesh.hpp"
#include "core/Skeleton.hpp"

namespace engine::core {

// Kronos ("Avatar 2.0" -- "Accessory Rigging"): real, small, procedural
// placeholder geometry for the five real equip categories that (unlike
// Torso/Legs, see AvatarFace.hpp's sibling AvatarClothing work) have no
// custom-shaped mesh authored for them at all yet -- a real, honest
// simple primitive per real attachment joint (buildHumanoidSkeleton()'s
// own attach_hat/attach_hair/attach_face_accessory/attach_back, plus the
// already-existing hand_R for a handheld), tinted with the real equipped
// item's own color. Only spawns a real entity for a category that
// genuinely has something equipped -- unlike clothing, there's no real
// "everyone wears a hat by default" honest baseline to fall back to, so
// an empty slot spawns nothing (not a placeholder shape with no real
// item behind it).
[[nodiscard]] bool spawnAvatarAccessories(ECS& ecs, const Skeleton& skeleton, const AvatarLoadout& loadout,
                                           const CatalogueIndex& index, RiggedMeshLibrary& riggedMeshLibrary,
                                           VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                                           std::vector<EntityId>& outAccessoryEntities, std::string& outError);

// Kronos ("Avatar 2.0" -- "Accessory Rigging" -- "dynamic offsets, e.g.
// backpack sway"): real, pure -- the one real accessory this pass gives
// genuine per-frame procedural motion to (a back item swaying as the
// character moves is the concrete example the spec itself names).
// `phase` is real, caller-owned (same "accumulate radians, wrap at 2*pi"
// convention AvatarController::secondaryMotionPhase_ already
// establishes) -- this function doesn't advance it itself.
[[nodiscard]] float computeBackAccessorySwayDegrees(float phase);

// Real -- right-multiplies computeBackAccessorySwayDegrees() onto the
// real attach_back joint (pivoting around its own bind-pose position,
// the exact same real correctness construction AvatarController::tick()'s
// own head-bob and AvatarFace's own facial transforms already use -- see
// either one's own comment for why a naive right-multiply alone would be
// wrong). A real, honest no-op if `skeleton` has no attach_back joint.
void applyAccessoryDynamicsToSkinningMatrices(std::vector<glm::mat4>& skinningMatrices, const Skeleton& skeleton,
                                               float phase);

} // namespace engine::core
