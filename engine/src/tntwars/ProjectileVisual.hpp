#pragma once

#include <glm/glm.hpp>
#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/ECS.hpp"
#include "core/Mesh.hpp"
#include "core/ProceduralMaterials.hpp"
#include "tntwars/Projectile.hpp"

namespace engine::tntwars {

// Kronos bugfix (live-reported: "no visual models for the class
// attack"): fireWeapon() has always produced a real, correctly-simulated
// ProjectileState (position/velocity/damage, see Projectile.hpp), but
// nothing in Application.cpp ever spawned a mesh for one or stepped it
// forward client-side -- a shot was a real, accepted server decision with
// zero live representation, exactly the same shape of bug TntChargeVisual
// fixed for placed charges. This pairs a real ProjectileState with a
// real, small, emissive, elongated bolt entity oriented along the shot's
// own real velocity, so a fired weapon reads as a real, visible tracer
// crossing the world instead of a silent accept/reject log line.

struct ProjectileVisualState {
    ProjectileState projectile;
    core::EntityId entity = core::kNullEntity;
};

[[nodiscard]] core::EntityId spawnProjectileVisual(core::ECS& ecs, core::MeshLibrary& meshLibrary,
                                                     const core::ProceduralMaterialLibrary& materials,
                                                     VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool,
                                                     VkQueue queue, const ProjectileState& projectile,
                                                     const char* name);

// Real per-tick sync -- moves/re-orients the already-spawned entity to
// match the projectile's own just-stepped real position/velocity. Called
// every tick a projectile is still live, mirroring how DecalState/
// ExplosiveBarrelState keep their own visual entity in sync with pure
// backend state.
void updateProjectileVisualTransform(core::EntityId entity, core::ECS& ecs, const ProjectileState& projectile);

} // namespace engine::tntwars
