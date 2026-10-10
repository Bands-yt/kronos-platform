#include "core/PartBodies.hpp"

#include <unordered_map>
#include <vector>

#include "core/Components.hpp"
#include "core/InstanceTree.hpp"
#include "core/Physics.hpp"
#include "core/PhysicsMaterial.hpp"
#include "core/RobloxPlayers.hpp"

namespace engine::core::partbodies {
namespace {

struct PartBodyState {
    ColliderShape shape;
    RigidBodyMotionType motion = RigidBodyMotionType::Static;
    bool sensor = false;
};

// Bodies sync() looks after, so a destroyed part's body can be removed too.
struct TrackedBodies {
    std::unordered_map<EntityId, uint32_t> bodies;
};

TrackedBodies& tracked(ECS& ecs) { return ecs.raw().ctx().emplace<TrackedBodies>(); }

void dropDeadBodies(ECS& ecs, Physics& physics) {
    auto& bodies = tracked(ecs).bodies;
    for (auto it = bodies.begin(); it != bodies.end();) {
        if (ecs.raw().valid(it->first)) {
            ++it;
            continue;
        }
        physics.destroyBodyById(it->second);
        it = bodies.erase(it);
    }
}

bool inCharacter(ECS& ecs, InstanceRef ref) {
    for (int depth = 0; depth < 16 && ref != kNoInstance && ref != kWorkspaceInstance; ++depth) {
        if (players::playerFromCharacter(ecs, ref) != kNoInstance) return true;
        ref = instances::parent(ecs, ref);
    }
    return false;
}

bool anchored(ECS& ecs, InstanceRef ref) {
    const PropertyDef* def = instances::findProperty(instances::className(ecs, ref), "Anchored");
    InstanceValue value;
    return def != nullptr && instances::getProperty(ecs, ref, *def, value) && value.boolean;
}

bool sameShape(const ColliderShape& a, const ColliderShape& b) { return a.kind == b.kind && a.params == b.params; }

} // namespace

void sync(ECS& ecs, Physics& physics, bool serverMoved) {
    dropDeadBodies(ecs, physics);
    auto& bodies = tracked(ecs).bodies;
    std::vector<EntityId> parts;
    for (EntityId e : ecs.raw().view<RigidBody, ColliderShape, InstanceInfo>()) parts.push_back(e);
    for (EntityId e : parts) {
        const auto& shape = ecs.raw().get<ColliderShape>(e);
        if (shape.kind == ColliderShapeKind::Mesh || ecs.tryGetComponent<PlayerAvatarPart>(e) != nullptr) continue;
        const InstanceRef ref = instances::refOf(ecs, e);
        if (!instances::classIsA(instances::className(ecs, ref), "BasePart") || inCharacter(ecs, ref)) continue;
        auto& body = ecs.raw().get<RigidBody>(e);
        const bool hasBody = body.joltBodyId != RigidBody::kInvalidBodyId;
        if (!instances::isInWorld(ecs, e)) {
            if (hasBody) physics.detachBody(e, ecs);
            ecs.raw().remove<PartBodyState>(e);
            bodies.erase(e);
            continue;
        }
        PartBodyState wanted;
        wanted.shape = shape;
        wanted.sensor = !instances::canCollide(ecs, e);
        if (anchored(ecs, ref)) wanted.motion = RigidBodyMotionType::Static;
        else wanted.motion = serverMoved ? RigidBodyMotionType::Kinematic : RigidBodyMotionType::Dynamic;
        if (hasBody) {
            // A body from scene loading or Play: assume it was right when made.
            const auto* state = ecs.tryGetComponent<PartBodyState>(e);
            const PartBodyState have = state != nullptr ? *state : PartBodyState{shape, body.motionType, wanted.sensor};
            if (sameShape(have.shape, wanted.shape) && have.motion == wanted.motion && have.sensor == wanted.sensor) {
                if (state == nullptr) ecs.raw().emplace<PartBodyState>(e, have);
                bodies[e] = body.joltBodyId;
                continue;
            }
            physics.detachBody(e, ecs);
        }
        const PhysicsMaterial* material = ecs.tryGetComponent<PhysicsMaterial>(e);
        if (physics.attachBodyToEntity(e, ecs, wanted.shape, material != nullptr ? *material : PhysicsMaterial{},
                                       wanted.motion, 0.0f, CollisionLayer::Default, wanted.sensor)) {
            ecs.raw().get<RigidBody>(e).motionType = wanted.motion;
            ecs.raw().emplace_or_replace<PartBodyState>(e, wanted);
            bodies[e] = ecs.raw().get<RigidBody>(e).joltBodyId;
        } else {
            // Don't retry every frame.
            ecs.raw().remove<RigidBody>(e);
            bodies.erase(e);
        }
    }
}

void detachAll(ECS& ecs, Physics& physics) {
    dropDeadBodies(ecs, physics);
    tracked(ecs).bodies.clear();
    std::vector<EntityId> list;
    for (EntityId e : ecs.raw().view<PartBodyState>()) list.push_back(e);
    for (EntityId e : list) {
        physics.detachBody(e, ecs);
        ecs.raw().remove<PartBodyState>(e);
    }
}

} // namespace engine::core::partbodies
