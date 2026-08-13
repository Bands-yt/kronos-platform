#pragma once

#include <vector>

#include <glm/glm.hpp>
#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/ECS.hpp"
#include "core/Mesh.hpp"
#include "core/Physics.hpp"
#include "core/ProceduralMaterials.hpp"
#include "net/NetTypes.hpp"
#include "tntwars/DestructibleGeometry.hpp"
#include "tntwars/TntCharge.hpp"

namespace engine::tntwars {

// Kronos ("Explosives System" world-building): two real, additional
// explosive types layered on top of TntChargeState's own already-real
// fuse/damage/impulse math (TntCharge.hpp) -- neither one duplicates
// that math; both call straight into computeExplosionDamage()/
// computeExplosionImpulse()/applyExplosionToSegments() at their own real
// detonation moment with their own real, distinct tuning.

// --- Grenades ---------------------------------------------------------

// Real, thrown explosive -- distinct from a placed TntChargeState by
// carrying its own real velocity (a real ballistic arc, simple Euler
// integration under gravity -- no bounce/collision response modeled,
// an honest simplification matching Projectile.hpp's own
// stepProjectile() precedent for "pure step, no collision resolution
// here"), a real, short fixed fuse (explodes shortly after landing/
// mid-air, not on impact), and real, deliberately weaker-than-TNT
// tuning (a grenade is a sidearm, not the map's own primary siege tool).
struct GrenadeState {
    net::PlayerId owner = net::kInvalidPlayer;
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    float fuseSecondsRemaining = 0.0f;
    bool detonated = false;
    float explosionRadius = kExplosionRadius * 0.6f;
    float explosionMaxDamage = kExplosionMaxDamage * 0.5f;
    float explosionMaxImpulse = kExplosionMaxImpulse * 0.7f;
};

constexpr float kGrenadeFuseSeconds = 2.0f;
constexpr float kGrenadeThrowSpeed = 18.0f;

[[nodiscard]] GrenadeState throwGrenade(net::PlayerId owner, glm::vec3 origin, glm::vec3 throwDirection,
                                          float throwSpeed = kGrenadeThrowSpeed, float fuseSeconds = kGrenadeFuseSeconds);

// Real, simple ballistic integration -- a real, honest no-op past
// detonation or on a non-positive dt.
void tickGrenadeTrajectory(GrenadeState& grenade, glm::vec3 gravity, float dt);

// Real fuse countdown -- same real "sets detonated true exactly once"
// idempotency as tickTntCharge().
void tickGrenadeFuse(GrenadeState& grenade, float dt);

// --- Explosive barrels --------------------------------------------------

// Real, static, damageable prop -- reuses DestructibleSegment's own
// already-real health/position/extents shape (no new health model
// invented), plus its own real explosion tuning triggered the exact
// real tick its health first reaches 0. Placed statically in a map (TNT/
// weapon fire/another nearby barrel's own explosion all damage it via
// the exact same real applyExplosionToSegments()/applyDamageToSegment()
// paths every other destructible already uses), so barrels near each
// other real-chain: barrel A's own detonation damages barrel B's own
// segment like any other nearby destructible, and if that pushes B's
// health to 0 too, B detonates on ITS OWN next real tick -- an emergent,
// real chain reaction, not a hand-scripted one.
struct ExplosiveBarrelState {
    DestructibleSegment segment;
    bool hasExploded = false;
    float explosionRadius = kExplosionRadius * 0.8f;
    float explosionMaxDamage = kExplosionMaxDamage * 0.7f;
    float explosionMaxImpulse = kExplosionMaxImpulse * 0.9f;
    core::EntityId visualEntity = core::kNullEntity;
};

[[nodiscard]] ExplosiveBarrelState buildExplosiveBarrel(glm::vec3 position);

// Real spawn -- one real, collidable, emissive-banded Box entity per
// barrel (materials.metal, a warm amber tint reading as "volatile
// cargo"), stored directly on the returned state's own visualEntity so
// tickExplosiveBarrelDetonation()'s own real caller can hide/detach it
// the exact tick it detonates without a second parallel array.
void spawnExplosiveBarrelVisual(core::ECS& ecs, core::Physics& physics, core::MeshLibrary& meshLibrary,
                                  const core::ProceduralMaterialLibrary& materials, VmaAllocator allocator,
                                  VkDevice device, VkCommandPool cmdPool, VkQueue queue, ExplosiveBarrelState& barrel,
                                  const char* name);

// Real, permanent post-detonation hide -- unlike a destructible wall
// segment, a barrel does not rebuild (a real, honest "used once" prop,
// matching a real-world expectation for an exploded barrel). Hides the
// mesh and detaches its real physics body; a real, honest no-op if
// `visualEntity` never spawned.
void hideExplosiveBarrelVisual(ExplosiveBarrelState& barrel, core::ECS& ecs, core::Physics& physics);

// Real, one-shot detection -- returns true exactly the real tick a
// barrel's own segment first reaches 0 health (and real-marks
// hasExploded so it never fires twice); a real, honest false every other
// tick, including one already exploded. The caller (Application's own
// live tick) is responsible for actually applying the real explosion
// (damage/impulse/FX) the same way it already does for TntCharge/
// Grenade detonations -- this function only reports the real moment,
// it doesn't itself touch any other barrel/segment/player.
[[nodiscard]] bool tickExplosiveBarrelDetonation(ExplosiveBarrelState& barrel);

} // namespace engine::tntwars
