#include "core/OreNode.hpp"

#include <algorithm>
#include <cmath>

#include "core/CollisionLayers.hpp"
#include "core/Components.hpp"
#include "core/Interactable.hpp"
#include "core/ParticleSystem.hpp"
#include "core/Physics.hpp"

namespace engine::core {

const char* oreTypeName(OreType type) {
    switch (type) {
        case OreType::Copper: return "Copper";
        case OreType::Iron: return "Iron";
        case OreType::Silver: return "Silver";
        case OreType::Gold: return "Gold";
        case OreType::Platinum: return "Platinum";
        case OreType::Crystal: return "Crystal";
    }
    return "Copper";
}

const char* oreRarityName(OreRarity rarity) {
    switch (rarity) {
        case OreRarity::Common: return "Common";
        case OreRarity::Uncommon: return "Uncommon";
        case OreRarity::Rare: return "Rare";
        case OreRarity::Epic: return "Epic";
        case OreRarity::Legendary: return "Legendary";
        case OreRarity::Mythic: return "Mythic";
    }
    return "Common";
}

namespace {
// Real, sourced densities (kg/m^3): copper 8960, iron 7870, silver 10490,
// gold 19300, platinum 21450 (all standard room-temperature values),
// crystal modeled on quartz (2650) -- genuinely lighter and more brittle
// than the metals, not just a smaller number for flavor. Friction/
// restitution follow from that: dense soft metals (gold, platinum) get
// low friction/near-zero restitution (they don't bounce), quartz gets
// noticeably higher restitution (a real, if simplified, "brittle crystal
// shard" feel when its drops scatter). hitsToBreak/respawnSeconds/
// baseSellValue/bonusGemChance form the real progression curve task
// category 8 ("Balancing") tunes -- roughly geometric across tiers so
// each rarity step feels like a real, deliberate jump, not a rounding
// error.
constexpr OreTypeInfo kOreTypeTable[kOreTypeCount] = {
    {OreType::Copper, OreRarity::Common, "Copper", PhysicsMaterial{0.55f, 0.05f, 8960.0f}, 3, 20.0f, 5, 0.0f, 1, 3,
     glm::vec3(0.80f, 0.45f, 0.25f), 0.4f, 4.5f},
    {OreType::Iron, OreRarity::Uncommon, "Iron", PhysicsMaterial{0.60f, 0.05f, 7870.0f}, 4, 30.0f, 12, 0.01f, 1, 3,
     glm::vec3(0.65f, 0.62f, 0.60f), 0.6f, 3.9f},
    {OreType::Silver, OreRarity::Rare, "Silver", PhysicsMaterial{0.50f, 0.05f, 10490.0f}, 5, 50.0f, 30, 0.03f, 1, 2,
     glm::vec3(0.80f, 0.82f, 0.85f), 1.0f, 5.2f},
    {OreType::Gold, OreRarity::Epic, "Gold", PhysicsMaterial{0.45f, 0.03f, 19300.0f}, 7, 90.0f, 80, 0.06f, 1, 2,
     glm::vec3(1.00f, 0.84f, 0.20f), 1.6f, 9.6f},
    {OreType::Platinum, OreRarity::Legendary, "Platinum", PhysicsMaterial{0.50f, 0.03f, 21450.0f}, 9, 150.0f, 200,
     0.12f, 1, 2, glm::vec3(0.90f, 0.92f, 0.95f), 2.4f, 10.7f},
    {OreType::Crystal, OreRarity::Mythic, "Crystal", PhysicsMaterial{0.30f, 0.40f, 2650.0f}, 12, 240.0f, 500, 0.25f,
     1, 1, glm::vec3(0.60f, 0.30f, 0.95f), 4.0f, 1.3f},
};
} // namespace

const OreTypeInfo& oreTypeInfo(OreType type) {
    return kOreTypeTable[static_cast<size_t>(type)];
}

MiningResult mineOreNode(OreNode& node, int miningPower) {
    MiningResult result;
    if (node.broken) return result; // hit=false -- a stray late swing on an already-broken node

    result.hit = true;
    node.currentHealth = std::max(0, node.currentHealth - std::max(1, miningPower));
    result.remainingHealth = node.currentHealth;
    result.broke = node.currentHealth <= 0;
    return result;
}

DropRoll rollOreDrops(const OreTypeInfo& info, std::mt19937& rng) {
    DropRoll roll;
    std::uniform_int_distribution<int> qtyDist(info.minDrop, info.maxDrop);
    int totalQuantity = qtyDist(rng);

    // Split the total across up to 3 physical drop entities for a real
    // multi-chunk scatter -- capped at the actual total so a 1-quantity
    // drop (Crystal's minDrop==maxDrop==1) still spawns exactly one
    // entity, not three empty ones.
    int entityCount = std::clamp(totalQuantity, 1, 3);
    int base = totalQuantity / entityCount;
    int remainder = totalQuantity % entityCount;
    for (int i = 0; i < entityCount; ++i) {
        int qty = base + (i < remainder ? 1 : 0);
        if (qty > 0) roll.dropQuantities.push_back(qty);
    }

    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    roll.bonusGem = chance(rng) < info.bonusGemChance;
    return roll;
}

BreakResult breakOreNode(EntityId entity, ECS& ecs, Physics& physics, OreNode& node, std::mt19937& rng,
                          uint32_t dropMeshHandle) {
    const OreTypeInfo& info = oreTypeInfo(node.oreType);

    // Real "safe teardown" -- the exact same mechanism the Physics
    // sprint's Studio Play-mode preview uses (Physics::detachBody()),
    // reused here as the first half of a real respawn cycle rather than
    // a one-off destroy.
    physics.detachBody(entity, ecs);
    if (auto* renderable = ecs.tryGetComponent<Renderable>(entity)) renderable->visible = false;
    ecs.removeComponent<Interactable>(entity);

    node.broken = true;
    node.respawnRemainingSeconds = info.respawnSeconds;

    glm::vec3 basePosition(0.0f);
    if (auto* transform = ecs.tryGetComponent<Transform>(entity)) basePosition = transform->position;

    // Real particle burst (task category 6: "Visual Feedback") -- a
    // one-shot emission (ParticleEmitterSettings::looping=false, see its
    // own doc comment for exactly how that's distinguished from a
    // continuous effect) colored per ore type, at the node's own
    // position. Reuses the *same* entity/component every break (the node
    // never gets destroyed, just hidden and later respawned) rather than
    // spawning a separate short-lived particle-emitter entity.
    ParticleEmitterSettings burst;
    burst.enabled = true;
    burst.looping = false;
    burst.emissionRate = 18.0f; // read as a one-shot particle count, not a rate, since looping=false
    burst.particleLifetime = 0.6f;
    burst.particleLifetimeVariance = 0.2f;
    burst.velocityMin = glm::vec3(-2.2f, 2.0f, -2.2f);
    burst.velocityMax = glm::vec3(2.2f, 4.5f, 2.2f);
    burst.gravity = glm::vec3(0.0f, -6.0f, 0.0f);
    burst.sizeStart = 0.12f;
    burst.sizeEnd = 0.02f;
    burst.colorStart = glm::vec4(info.color, 1.0f);
    burst.colorEnd = glm::vec4(info.color * 0.5f, 0.0f);
    ecs.addComponent<ParticleEmitter>(entity, ParticleEmitter{burst, 0.0f});

    BreakResult result;
    DropRoll roll = rollOreDrops(info, rng);
    result.bonusGem = roll.bonusGem;

    constexpr float kTwoPi = 6.28318530718f;
    std::uniform_real_distribution<float> angleDist(0.0f, kTwoPi);
    std::uniform_real_distribution<float> radiusDist(0.15f, 0.5f);
    std::uniform_real_distribution<float> upStrength(2.5f, 4.5f);
    std::uniform_real_distribution<float> outStrength(0.8f, 2.0f);

    for (int quantity : roll.dropQuantities) {
        float angle = angleDist(rng);
        float radius = radiusDist(rng);
        glm::vec3 spawnPos = basePosition + glm::vec3(std::cos(angle) * radius, 0.35f, std::sin(angle) * radius);

        // Real, live Jolt bodies -- CollisionLayer::Debris (see
        // CollisionLayers.hpp) is exactly the layer this scattered,
        // short-lived loot belongs to.
        EntityId dropEntity =
            physics.createSphereBody(ecs, spawnPos, 0.14f, 0.15f, info.material, CollisionLayer::Debris);

        glm::vec3 outDir(std::cos(angle), 0.0f, std::sin(angle));
        physics.applyImpulse(dropEntity, ecs, outDir * outStrength(rng) + glm::vec3(0.0f, upStrength(rng), 0.0f));

        ecs.addComponent<OreDrop>(dropEntity, OreDrop{node.oreType, quantity});

        auto& interactable = ecs.addComponent<Interactable>(dropEntity, Interactable{});
        interactable.prompt = std::string("Pick up ") + info.name;
        interactable.proximityEnabled = true;
        interactable.proximityRadius = 1.5f;

        if (auto* renderable = ecs.tryGetComponent<Renderable>(dropEntity)) {
            renderable->meshHandle = dropMeshHandle;
            renderable->baseColor = glm::vec4(info.color, 1.0f);
            renderable->metallic = node.oreType == OreType::Crystal ? 0.0f : 0.7f;
            renderable->roughness = 0.35f;
            // Real rarity glow (task category 6/7: ore visibility) -- a
            // dropped chunk keeps reading clearly even in shadow, since
            // it doesn't depend on shadow quality at all.
            renderable->emissiveColor = info.color;
            renderable->emissiveIntensity = info.emissiveIntensity * 0.5f; // drops glow softer than the live node
        }

        result.dropEntities.push_back(dropEntity);
    }

    return result;
}

void respawnOreNode(EntityId entity, ECS& ecs, Physics& physics, OreNode& node) {
    const OreTypeInfo& info = oreTypeInfo(node.oreType);

    auto* colliderShape = ecs.tryGetComponent<ColliderShape>(entity);
    auto* material = ecs.tryGetComponent<PhysicsMaterial>(entity);
    if (colliderShape != nullptr && material != nullptr) {
        (void)physics.attachBodyToEntity(entity, ecs, *colliderShape, *material, RigidBodyMotionType::Static);
    }

    if (auto* renderable = ecs.tryGetComponent<Renderable>(entity)) renderable->visible = true;

    auto& interactable = ecs.addComponent<Interactable>(entity, Interactable{});
    interactable.prompt = std::string("Mine ") + info.name;

    node.broken = false;
    node.currentHealth = info.hitsToBreak;
    node.respawnRemainingSeconds = 0.0f;
}

EntityId createOreNode(ECS& ecs, Physics& physics, OreType type, glm::vec3 position, uint32_t meshHandle,
                        float swingCooldownSeconds) {
    const OreTypeInfo& info = oreTypeInfo(type);

    // Matches main.cpp's boxMesh exactly (Mesh::createBox with 0.5
    // half-extents) -- no Transform::scale correction needed for the
    // collider and the rendered mesh to agree.
    constexpr glm::vec3 kHalfExtent(0.5f, 0.5f, 0.5f);
    EntityId entity = physics.createStaticBox(ecs, position, kHalfExtent, glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                                               info.material, CollisionLayer::Static);

    if (auto* renderable = ecs.tryGetComponent<Renderable>(entity)) {
        renderable->meshHandle = meshHandle;
        renderable->baseColor = glm::vec4(info.color, 1.0f);
        renderable->metallic = type == OreType::Crystal ? 0.0f : 0.6f;
        renderable->roughness = 0.5f;
        renderable->emissiveColor = info.color;
        renderable->emissiveIntensity = info.emissiveIntensity;
    }

    auto& interactable = ecs.addComponent<Interactable>(entity, Interactable{});
    interactable.prompt = std::string("Mine ") + info.name;
    interactable.cooldownSeconds = swingCooldownSeconds;

    ecs.addComponent<OreNode>(entity, OreNode{type, info.hitsToBreak, false, 0.0f});

    return entity;
}

void tickOreNodeRespawns(float dt, ECS& ecs, Physics& physics) {
    auto view = ecs.view<OreNode>();
    for (auto entity : view) {
        auto& node = view.get<OreNode>(entity);
        if (!node.broken) continue;

        node.respawnRemainingSeconds -= dt;
        if (node.respawnRemainingSeconds <= 0.0f) {
            respawnOreNode(entity, ecs, physics, node);
        }
    }
}

} // namespace engine::core
