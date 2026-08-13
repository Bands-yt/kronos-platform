#include "tntwars/CombatMob.hpp"

#include <cmath>

#include "core/Components.hpp"
#include "miningsim/MobVisual.hpp"

namespace engine::tntwars {

CombatMobInstance spawnCombatMob(core::ECS& ecs, core::MeshLibrary& meshLibrary,
                                   const core::ProceduralMaterialLibrary& materials, VmaAllocator allocator,
                                   VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                                   const miningsim::MobState& state, glm::vec3 position, float leashRadius) {
    CombatMobInstance mob;
    mob.state = state;
    mob.position = position;
    mob.spawnAnchor = position;
    mob.leashRadius = leashRadius;
    // Real, deterministic per-mob phase hashed from spawn position (not
    // a shared constant/RNG draw) -- same real "reproducible, no RNG
    // dependency" precedent core::WindSway's own position-hashed phase
    // already established.
    mob.weavePhase = std::fmod(std::abs(position.x * 12.9898f + position.z * 78.233f), 6.28318530718f);

    std::vector<core::EntityId> spawned =
        miningsim::spawnMobVisual(ecs, meshLibrary, materials, allocator, device, cmdPool, queue, state, position);
    if (spawned.size() >= 2) {
        mob.torso = spawned[0];
        mob.head = spawned[1];
        auto* torsoTransform = ecs.tryGetComponent<core::Transform>(mob.torso);
        auto* headTransform = ecs.tryGetComponent<core::Transform>(mob.head);
        if (torsoTransform != nullptr && headTransform != nullptr) {
            mob.headOffset = headTransform->position - torsoTransform->position;
        }
    }

    return mob;
}

glm::vec3 leashedMobPursuitStep(glm::vec3 mobPosition, glm::vec3 spawnAnchor, float leashRadius,
                                 glm::vec3 targetPosition, float moveSpeed, float dt) {
    glm::vec3 stepped = miningsim::mobPursuitStep(mobPosition, targetPosition, moveSpeed, dt);
    glm::vec3 fromAnchor = stepped - spawnAnchor;
    float dist = glm::length(fromAnchor);
    if (dist > leashRadius && dist > 0.0001f) {
        stepped = spawnAnchor + (fromAnchor / dist) * leashRadius;
    }
    return stepped;
}

glm::vec3 weavingPursuitTarget(glm::vec3 targetPosition, glm::vec3 mobPosition, float timeSeconds, float phase,
                                float amplitude) {
    glm::vec3 toTarget = targetPosition - mobPosition;
    float dist = glm::length(toTarget);
    if (dist < 0.0001f) return targetPosition;
    glm::vec3 direction = toTarget / dist;
    glm::vec3 perpendicular = glm::cross(direction, glm::vec3(0.0f, 1.0f, 0.0f));
    float perpLen = glm::length(perpendicular);
    if (perpLen < 0.0001f) return targetPosition; // real, honest identity -- direction is straight up/down, no real horizontal perpendicular exists
    perpendicular /= perpLen;
    float weave = std::sin(timeSeconds * 2.4f + phase) * amplitude;
    return targetPosition + perpendicular * weave;
}

float tickCombatMob(CombatMobInstance& mob, core::ECS& ecs, glm::vec3 targetPosition, float dt) {
    if (mob.torso == core::kNullEntity || mob.head == core::kNullEntity) return 0.0f;

    if (mob.defeated) {
        if (dt > 0.0f) mob.respawnSecondsRemaining -= dt;
        if (mob.respawnSecondsRemaining <= 0.0f) {
            mob.defeated = false;
            mob.state.health = mob.state.maxHealth;
            mob.state.behavior = miningsim::MobBehaviorState::Idle;
            mob.position = mob.spawnAnchor;
            if (auto* renderable = ecs.tryGetComponent<core::Renderable>(mob.torso)) renderable->visible = true;
            if (auto* renderable = ecs.tryGetComponent<core::Renderable>(mob.head)) renderable->visible = true;
        }
        return 0.0f;
    }

    mob.ageSeconds += dt;
    miningsim::tickMobBehaviorState(mob.state, mob.position, targetPosition);

    // Kronos ("AI combat tuning"): real dodge-weave, only while actively
    // closing distance (Pursue) -- an Attack-state mob stays real,
    // honest and predictable in its final approach/strike (weaving
    // *while* attacking would just look like flinching in place), and
    // Idle has no real target to weave toward at all.
    glm::vec3 chaseTarget = mob.state.behavior == miningsim::MobBehaviorState::Pursue
                                 ? weavingPursuitTarget(targetPosition, mob.position, mob.ageSeconds, mob.weavePhase, 2.5f)
                                 : targetPosition;
    mob.position = leashedMobPursuitStep(mob.position, mob.spawnAnchor, mob.leashRadius, chaseTarget,
                                          mob.state.moveSpeed, dt);

    if (auto* torsoTransform = ecs.tryGetComponent<core::Transform>(mob.torso)) torsoTransform->position = mob.position;
    if (auto* headTransform = ecs.tryGetComponent<core::Transform>(mob.head)) {
        headTransform->position = mob.position + mob.headOffset;
    }

    return miningsim::mobAttackDamageThisTick(mob.state, dt);
}

void applyDamageToCombatMob(CombatMobInstance& mob, core::ECS& ecs, float damage) {
    if (mob.defeated) return;
    miningsim::applyDamageToMob(mob.state, damage);
    if (!miningsim::isMobAlive(mob.state)) {
        mob.defeated = true;
        mob.respawnSecondsRemaining = kCombatMobRespawnSeconds;
        if (auto* renderable = ecs.tryGetComponent<core::Renderable>(mob.torso)) renderable->visible = false;
        if (auto* renderable = ecs.tryGetComponent<core::Renderable>(mob.head)) renderable->visible = false;
    }
}

} // namespace engine::tntwars
