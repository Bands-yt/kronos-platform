#include "core/PartBodies.hpp"

#include <algorithm>
#include <unordered_map>
#include <vector>

#include <glm/gtc/epsilon.hpp>

#include "core/Components.hpp"
#include "core/InstanceTree.hpp"
#include "core/Physics.hpp"
#include "core/PhysicsMaterial.hpp"
#include "core/RobloxPlayers.hpp"
#include "core/UnionGeometry.hpp"

namespace engine::core::partbodies {
namespace {

struct PartBodyState {
    ColliderShape shape;
    RigidBodyMotionType motion = RigidBodyMotionType::Static;
    bool sensor = false;
};

// Bodies sync() looks after, so a destroyed part's body can be removed too.
struct WeldJoint {
    uint32_t joint = 0; // 0 when neither part can move
    uint32_t bodyA = 0;
    uint32_t bodyB = 0;
};

// A union piece's body, rebuilt when the piece moves or changes size.
struct PieceBodyState {
    ColliderShape shape;
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    bool sensor = false;
};

struct TrackedBodies {
    std::unordered_map<EntityId, uint32_t> bodies;
    std::unordered_map<EntityId, WeldJoint> welds;
    Physics* physics = nullptr;
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

uint32_t bodyOf(ECS& ecs, EntityId part) {
    const auto* body = ecs.tryGetComponent<RigidBody>(part);
    return body != nullptr ? body->joltBodyId : RigidBody::kInvalidBodyId;
}

void setActive(ECS& ecs, EntityId weld, bool active) {
    auto& value = ecs.raw().get<InstanceInfo>(weld).properties["Active"];
    value = InstanceValue::ofBool(active);
}

void syncWelds(ECS& ecs, Physics& physics) {
    auto& welds = tracked(ecs).welds;
    std::vector<EntityId> moved;
    if (auto* changed = ecs.raw().ctx().find<instances::WeldOffsetsChanged>()) moved.swap(changed->parts);
    auto wasMoved = [&](EntityId part) { return std::find(moved.begin(), moved.end(), part) != moved.end(); };

    std::vector<EntityId> live;
    for (auto [weld, info] : ecs.raw().view<InstanceInfo>().each()) {
        if (instances::isWeldClass(info.className)) live.push_back(weld);
    }
    for (EntityId weld : live) {
        EntityId a = kNullEntity, b = kNullEntity;
        const bool joined = instances::weldParts(ecs, weld, a, b);
        setActive(ecs, weld, joined);
        const auto found = welds.find(weld);
        const uint32_t bodyA = joined ? bodyOf(ecs, a) : RigidBody::kInvalidBodyId;
        const uint32_t bodyB = joined ? bodyOf(ecs, b) : RigidBody::kInvalidBodyId;
        if (found != welds.end()) {
            if (found->second.bodyA == bodyA && found->second.bodyB == bodyB && !wasMoved(a) && !wasMoved(b)) continue;
            physics.removeFixedJoint(found->second.joint);
            welds.erase(found);
        }
        if (!joined || bodyA == RigidBody::kInvalidBodyId || bodyB == RigidBody::kInvalidBodyId) continue;
        // Weld/Motor6D: Part1 goes where C0/C1 put it before the joint holds it there.
        if (instances::snapJoint(ecs, weld, 0.01f)) continue;
        // A part moved alone reaches its new place in the next physics step; join after that.
        if (ecs.tryGetComponent<instances::PhysicsPoseWrite>(a) != nullptr ||
            ecs.tryGetComponent<instances::PhysicsPoseWrite>(b) != nullptr) {
            continue;
        }
        welds[weld] = WeldJoint{physics.addFixedJoint(bodyA, bodyB), bodyA, bodyB};
    }
    for (auto it = welds.begin(); it != welds.end();) {
        if (std::find(live.begin(), live.end(), it->first) != live.end()) {
            ++it;
            continue;
        }
        physics.removeFixedJoint(it->second.joint);
        it = welds.erase(it);
    }
}

void syncPieces(ECS& ecs, Physics& physics) {
    auto& bodies = tracked(ecs).bodies;
    std::vector<EntityId> pieces;
    for (EntityId e : ecs.raw().view<unions::UnionPiece>()) pieces.push_back(e);
    for (EntityId e : pieces) {
        const EntityId owner = ecs.raw().get<unions::UnionPiece>(e).owner;
        const bool hasBody = bodyOf(ecs, e) != RigidBody::kInvalidBodyId;
        const InstanceRef ownerRef = instances::refOf(ecs, owner);
        if (!ecs.raw().valid(owner) || !instances::isInWorld(ecs, owner) || !anchored(ecs, ownerRef)) {
            if (hasBody) physics.detachBody(e, ecs);
            ecs.raw().remove<PieceBodyState>(e);
            bodies.erase(e);
            continue;
        }
        const instances::Pose pose = instances::worldPose(ecs, e);
        PieceBodyState wanted;
        const glm::vec3 half = glm::max(glm::abs(pose.scale) * 0.5f, glm::vec3(0.01f));
        wanted.shape = ecs.raw().get<unions::UnionPiece>(e).shape == 0
                           ? ColliderShape{ColliderShapeKind::Sphere, glm::vec3(std::min({half.x, half.y, half.z})), ""}
                           : ColliderShape{ColliderShapeKind::Box, half, ""};
        wanted.position = pose.position;
        wanted.rotation = pose.rotation;
        wanted.sensor = !instances::canCollide(ecs, owner);
        if (hasBody) {
            const auto* have = ecs.tryGetComponent<PieceBodyState>(e);
            if (have != nullptr && sameShape(have->shape, wanted.shape) && have->sensor == wanted.sensor &&
                glm::all(glm::epsilonEqual(have->position, wanted.position, 1e-4f)) &&
                std::abs(glm::dot(have->rotation, wanted.rotation)) > 0.99999f) {
                continue;
            }
            physics.detachBody(e, ecs);
        }
        const PhysicsMaterial* material = ecs.tryGetComponent<PhysicsMaterial>(owner);
        if (physics.attachBodyToEntity(e, ecs, wanted.shape, material != nullptr ? *material : PhysicsMaterial{},
                                       RigidBodyMotionType::Static, 0.0f, CollisionLayer::Default, wanted.sensor)) {
            ecs.raw().emplace_or_replace<PieceBodyState>(e, wanted);
            bodies[e] = bodyOf(ecs, e);
        }
    }
}

} // namespace

void sync(ECS& ecs, Physics& physics, bool serverMoved) {
    tracked(ecs).physics = &physics;
    unions::sync(ecs);
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
        if (unions::hasPieces(ecs, e) && anchored(ecs, ref)) {
            // Its pieces collide instead.
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
    syncPieces(ecs, physics);
    syncWelds(ecs, physics);
}

void detachAll(ECS& ecs, Physics& physics) {
    dropDeadBodies(ecs, physics);
    tracked(ecs).bodies.clear();
    for (auto& [weld, joint] : tracked(ecs).welds) physics.removeFixedJoint(joint.joint);
    tracked(ecs).welds.clear();
    tracked(ecs).physics = nullptr;
    std::vector<EntityId> list;
    for (EntityId e : ecs.raw().view<PartBodyState>()) list.push_back(e);
    for (EntityId e : ecs.raw().view<PieceBodyState>()) list.push_back(e);
    for (EntityId e : list) {
        physics.detachBody(e, ecs);
        ecs.raw().remove<PartBodyState>(e);
        ecs.raw().remove<PieceBodyState>(e);
    }
}

void setPhysics(ECS& ecs, Physics* physics) { tracked(ecs).physics = physics; }

Physics* physicsOf(ECS& ecs) {
    const auto* state = ecs.raw().ctx().find<TrackedBodies>();
    return state != nullptr ? state->physics : nullptr;
}

} // namespace engine::core::partbodies
