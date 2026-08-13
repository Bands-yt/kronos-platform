#pragma once

#include <vector>

#include <glm/glm.hpp>
#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/ECS.hpp"
#include "core/Mesh.hpp"
#include "core/ProceduralMaterials.hpp"
#include "miningsim/Mob.hpp"

namespace engine::miningsim {

// Kronos trailer production: the real, first ECS/render consumer of
// Milestone 19/20's own real MobState/spawnBossOfRarity() -- a real Box
// (torso) + Capsule (head) composition, the same real primitive-
// composition technique MiningSimRtx.cpp's own drill already established
// (no dedicated creature-mesh generator exists in this engine, see that
// file's own header comment for the precedent this follows). Real,
// scaled by the mob's own maxHealth relative to the real baseline
// (spawnMobOfRarity(Common)'s own 50.0f) -- a boss (4x stats, Milestone
// 20) reads as a real, visibly larger creature, not just a bigger number
// on an unseen stat sheet. Real emissive glow, tinted by
// rarityTierColor(mob.rarity) and scaled by rarity rank -- a higher-
// rarity creature is a real, visibly brighter threat.
constexpr float kMobVisualBaselineHealth = 50.0f;

// Returns every real spawned entity (torso + head), so a caller can
// real-clean them all up between beats -- matches
// miningsim::spawnDungeonVisual()'s own real "return the full spawned
// list" convention.
[[nodiscard]] std::vector<core::EntityId> spawnMobVisual(core::ECS& ecs, core::MeshLibrary& meshLibrary,
                                                          const core::ProceduralMaterialLibrary& materials,
                                                          VmaAllocator allocator, VkDevice device,
                                                          VkCommandPool cmdPool, VkQueue queue, const MobState& mob,
                                                          glm::vec3 position);

} // namespace engine::miningsim
