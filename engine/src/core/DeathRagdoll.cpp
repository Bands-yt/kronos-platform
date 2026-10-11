#include "core/DeathRagdoll.hpp"

#include <cstdio>
#include <string>

#include <glm/gtc/matrix_transform.hpp>

#include "core/Components.hpp"
#include "core/Hierarchy.hpp"

namespace engine::core {

void DeathRagdoll::update(bool dead, ECS& ecs, Physics& physics, const Skeleton& skeleton, EntityId character,
                          const std::vector<EntityId>& skinnedEntities) {
    if (!dead) {
        release(physics);
        attempted_ = false;
        return;
    }
    if (!active() && !attempted_) {
        attempted_ = true;
        start(ecs, physics, skeleton, character, skinnedEntities);
    }
    if (!active() || !physics.getRagdollPartTransforms(handle_, partWorld_) || partWorld_.empty()) return;

    physics.setPosition(character, ecs, parkPosition_);
    physics.setHorizontalVelocity(character, ecs, glm::vec2(0.0f));
    physics.setVerticalVelocity(character, ecs, 0.0f);

    // The mesh follows the pelvis so culling bounds stay near the body.
    const glm::mat4 meshWorld = glm::translate(glm::mat4(1.0f), glm::vec3(partWorld_[0][3]));
    computeRagdollSkinningMatrices(ragdoll_, partWorld_, meshWorld, skinning_);
    for (EntityId entity : skinnedEntities) {
        if (auto* transform = ecs.tryGetComponent<Transform>(entity)) {
            transform->position = glm::vec3(meshWorld[3]);
            transform->rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            transform->scale = glm::vec3(1.0f);
        }
        auto* skin = ecs.tryGetComponent<SkinnedRenderable>(entity);
        if (skin != nullptr && skin->skinningMatrices.size() == skinning_.size()) skin->skinningMatrices = skinning_;
    }
}

bool DeathRagdoll::start(ECS& ecs, Physics& physics, const Skeleton& skeleton, EntityId character,
                         const std::vector<EntityId>& skinnedEntities) {
    if (skinnedEntities.empty() || !ecs.raw().valid(character)) return false;
    const auto* skin = ecs.tryGetComponent<SkinnedRenderable>(skinnedEntities.front());
    if (skin == nullptr || skin->skinningMatrices.empty()) return false;
    if (ragdollJoints_ != skeleton.joints.size()) {
        std::string error;
        ragdoll_ = buildHumanoidRagdoll(skeleton, error);
        ragdollJoints_ = ragdoll_.jointOwner.empty() ? 0 : skeleton.joints.size();
        if (ragdollJoints_ == 0) {
            std::fprintf(stderr, "DeathRagdoll: no ragdoll for this skeleton: %s\n", error.c_str());
            return false;
        }
    }
    if (skin->skinningMatrices.size() != ragdoll_.jointOwner.size()) return false;

    const glm::mat4 meshWorld = hierarchy::computeWorldMatrix(ecs, skinnedEntities.front());
    const std::vector<glm::mat4> initialParts = ragdollPartsFromSkinning(ragdoll_, skin->skinningMatrices, meshWorld);
    const glm::vec3 velocity = physics.getLinearVelocity(character, ecs);
    handle_ = physics.createRagdoll(ragdoll_.desc, meshWorld, &initialParts, velocity, CollisionLayer::Debris);
    if (handle_ == Physics::kInvalidRagdoll) return false;
    physics_ = &physics;
    // A little push so the body tips over instead of folding straight down.
    const glm::vec3 forward = glm::normalize(glm::vec3(meshWorld[2]) + glm::vec3(0.0f, 0.0f, 1e-4f));
    physics.addRagdollImpulse(handle_, static_cast<int>(HumanoidRagdollPart::Chest), -forward * 6.0f);
    parkPosition_ = glm::vec3(meshWorld[3]) - glm::vec3(0.0f, kParkDepth, 0.0f);
    return true;
}

void DeathRagdoll::release(Physics& physics) {
    if (!active()) return;
    if (physics_ == &physics) physics.destroyRagdoll(handle_);
    handle_ = Physics::kInvalidRagdoll;
    physics_ = nullptr;
    partWorld_.clear();
}

glm::vec3 DeathRagdoll::pelvisPosition() const {
    return partWorld_.empty() ? glm::vec3(0.0f) : glm::vec3(partWorld_[0][3]);
}

} // namespace engine::core
