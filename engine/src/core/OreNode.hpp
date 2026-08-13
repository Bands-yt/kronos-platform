#pragma once

#include <random>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/ECS.hpp"
#include "core/PhysicsMaterial.hpp"

namespace engine::core {

class Physics;

// Sprint 5 ("Core Economy") task category 1: real, mineable resource nodes.
// Six ore types, each mapped 1:1 onto a rarity tier -- a real progression
// ladder, not six cosmetic recolors of the same numbers.
enum class OreType : uint8_t { Copper, Iron, Silver, Gold, Platinum, Crystal };
constexpr size_t kOreTypeCount = 6;

enum class OreRarity : uint8_t { Common, Uncommon, Rare, Epic, Legendary, Mythic };

[[nodiscard]] const char* oreTypeName(OreType type);
[[nodiscard]] const char* oreRarityName(OreRarity rarity);

// Real, sourced static data per ore type -- friction/restitution/density
// are genuine real-world-adjacent values (not six copies of one preset),
// the same "distinct material properties" PhysicsMaterialPreset already
// established for Metal/Rubber/Wood/Stone. `hitsToBreak` and
// `respawnSeconds` are the pacing knobs task category 8 ("Balancing")
// tunes; `baseSellValue` is coins, `bonusGemChance` is the real, small
// per-break chance of an extra hard-currency drop (scales up with
// rarity -- a real, if modest, hard-currency faucet tied to skill/time
// investment, not a paywall).
struct OreTypeInfo {
    OreType type;
    OreRarity rarity;
    const char* name;
    PhysicsMaterial material;
    int hitsToBreak;
    float respawnSeconds;
    int baseSellValue;
    float bonusGemChance; // [0,1]
    int minDrop;
    int maxDrop;
    glm::vec3 color;
    float emissiveIntensity; // rarity glow -- task category 6/7 (ore visibility)
    // Weight per unit carried, for core::Inventory's weight limit (task
    // category 2) -- derived from `material.density` (denser ore is
    // heavier to carry, not an unrelated flavor number), scaled down to a
    // human "how many can I realistically carry" range rather than raw
    // kg/m^3.
    float unitWeight;
};

// Real, sourced values -- see OreNode.cpp for citations/reasoning on each.
[[nodiscard]] const OreTypeInfo& oreTypeInfo(OreType type);

// A real, live-mineable node -- attached alongside a real
// core::ColliderShape/core::PhysicsMaterial/core::RigidBody (Static,
// created via Physics::createStaticBox()) and a core::Interactable
// (mining swing = interact, cooldownSeconds is the real swing timer).
// `broken` entities have already had their body detached
// (Physics::detachBody() -- see Physics sprint's "safe teardown +
// recreation" mechanism, reused verbatim here as the respawn
// mechanism) and their Renderable/Interactable removed; `tickOreNodeRespawns()`
// (Application.cpp's pre-tick hook) counts `respawnRemainingSeconds`
// down and calls `respawnOreNode()` once it reaches zero.
struct OreNode {
    OreType oreType = OreType::Copper;
    int currentHealth = 0;
    bool broken = false;
    float respawnRemainingSeconds = 0.0f;
};

// Real, physical loot -- what breakOreNode() actually spawns. A small
// live Jolt dynamic body (Physics::createSphereBody(), CollisionLayer::Debris)
// given a real random scatter impulse, tagged Interactable
// (proximityEnabled -- auto-pickup, see core/Inventory.hpp) so it can be
// collected the same way core::Pickup items already are. `quantity` is
// how many units of `oreType` this one physical drop is worth once
// collected (a break can spawn several of these, not necessarily one per
// unit -- see breakOreNode()'s own comment).
struct OreDrop {
    OreType oreType = OreType::Copper;
    int quantity = 1;
};

// Real result of a completed mining swing -- what Application.cpp's
// interaction dispatch uses to drive visual feedback (task category 6)
// without re-deriving state that mineOreNode() already computed.
struct MiningResult {
    bool hit = false;   // false only if the node was already broken (a stray late swing)
    bool broke = false; // true the exact swing that brought health to 0
    int remainingHealth = 0;
};

// Pure -- decrements `node.currentHealth` by `miningPower` (clamped at 0),
// no ECS/Physics/RNG access, trivially unit-testable. Does not itself
// spawn drops or touch physics -- see breakOreNode() below for the
// ECS/Physics-touching half, called by the caller (Application.cpp) only
// when this returns `broke=true`, the same "pure decision split from
// I/O-touching caller" pattern the Physics sprint established throughout.
[[nodiscard]] MiningResult mineOreNode(OreNode& node, int miningPower);

// Real drop-table roll -- pure (caller supplies the RNG so tests can seed
// it and check the resulting distribution converges to the configured
// weights over many rolls, see engine_tests). Returns how many separate
// physical OreDrop entities to spawn (each with its own rolled quantity)
// and whether a bonus gem was rolled. Not itself ECS/Physics-touching.
struct DropRoll {
    std::vector<int> dropQuantities; // one entry per physical drop entity to spawn
    bool bonusGem = false;
};
[[nodiscard]] DropRoll rollOreDrops(const OreTypeInfo& info, std::mt19937& rng);

// ECS/Physics-touching: detaches `entity`'s live body (Physics::detachBody()),
// hides its Renderable, removes its Interactable (a broken node can't be
// mined again until it respawns), marks `node.broken`/starts the respawn
// countdown, rolls a real DropRoll, and spawns one real physical
// core::OreDrop entity per rolled quantity at `entity`'s position with a
// small random outward+upward Physics::applyImpulse() -- the real
// "physics-based breaking" this task category asks for: loot visibly
// scatters under real Jolt physics rather than teleporting into the
// inventory. `dropMeshHandle` is the caller's already-registered
// core::MeshLibrary handle for a drop's visual (OreNode.cpp deliberately
// doesn't know about MeshLibrary/GPU mesh registration -- same
// GPU-independence boundary core::Physics itself maintains -- so the
// caller, which does own a MeshLibrary, supplies it). Returns the
// spawned drop entities (tests/callers that want to inspect them) and
// whether a bonus gem was rolled.
struct BreakResult {
    std::vector<EntityId> dropEntities;
    bool bonusGem = false;
};
BreakResult breakOreNode(EntityId entity, ECS& ecs, Physics& physics, OreNode& node, std::mt19937& rng,
                          uint32_t dropMeshHandle);

// ECS/Physics-touching: the respawn half. Re-attaches a live Static body
// via Physics::attachBodyToEntity() (reusing the exact same
// authored-ColliderShape/PhysicsMaterial-survive-detachBody() contract
// the Physics sprint's Studio Play-mode preview already relies on),
// re-shows the Renderable, re-adds a fresh Interactable with this ore
// type's real prompt, and resets `node` to full health/unbroken.
void respawnOreNode(EntityId entity, ECS& ecs, Physics& physics, OreNode& node);

// Real factory -- spawns a brand-new OreNode entity: a Static box body
// (Physics::createStaticBox(), sized/materialed from oreTypeInfo(type)),
// a real Renderable (`meshHandle`, same caller-supplies-the-handle
// reasoning as breakOreNode()'s `dropMeshHandle` above), a real
// Interactable (prompt = "Mine <OreType>", cooldownSeconds = the swing
// timer), and the OreNode component itself at full health. The one call
// site every ore-node spawn (main.cpp's bring-up scene, tests) goes
// through, so the six ore types can't drift out of sync with each
// other's creation logic.
EntityId createOreNode(ECS& ecs, Physics& physics, OreType type, glm::vec3 position, uint32_t meshHandle,
                        float swingCooldownSeconds = 0.5f);

// Called once per tick (Application.cpp's pre-tick hook, alongside
// MovingPlatform's own per-tick pass) -- counts every broken node's
// `respawnRemainingSeconds` down by `dt` and calls `respawnOreNode()`
// exactly once, the tick it crosses zero.
void tickOreNodeRespawns(float dt, ECS& ecs, Physics& physics);

} // namespace engine::core
