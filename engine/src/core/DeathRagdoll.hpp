#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "core/ECS.hpp"
#include "core/HumanoidRagdoll.hpp"
#include "core/Physics.hpp"
#include "core/Skeleton.hpp"

namespace engine::core {

// A dead character's avatar goes limp as a physics ragdoll until it respawns.
// The character's capsule is parked out of the way meanwhile so the two
// don't push each other.
class DeathRagdoll {
public:
    // Call each tick after the avatar is posed.
    void update(bool dead, ECS& ecs, Physics& physics, const Skeleton& skeleton, EntityId character,
                const std::vector<EntityId>& skinnedEntities);
    void release(Physics& physics);

    [[nodiscard]] bool active() const { return handle_ != Physics::kInvalidRagdoll; }
    // Where the body lies while active.
    [[nodiscard]] glm::vec3 pelvisPosition() const;

    static constexpr float kParkDepth = 200.0f;

private:
    bool start(ECS& ecs, Physics& physics, const Skeleton& skeleton, EntityId character,
               const std::vector<EntityId>& skinnedEntities);

    HumanoidRagdoll ragdoll_;
    size_t ragdollJoints_ = 0;
    Physics::RagdollHandle handle_ = Physics::kInvalidRagdoll;
    Physics* physics_ = nullptr;
    bool attempted_ = false;
    glm::vec3 parkPosition_{0.0f};
    std::vector<glm::mat4> partWorld_;
    std::vector<glm::mat4> skinning_;
};

} // namespace engine::core
