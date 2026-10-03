#include "core/Physics.hpp"

#include <cstdarg>
#include <cstdio>
#include <atomic>
#include <thread>

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/AllowedDOFs.h>
#include <Jolt/Physics/Body/MotionProperties.h>
#include <Jolt/Physics/EActivation.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Geometry/Triangle.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/BodyManager.h>
#include <Jolt/Physics/Collision/GroupFilterTable.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>
#include <Jolt/Physics/Ragdoll/Ragdoll.h>
#include <Jolt/Skeleton/Skeleton.h>

#include <unordered_map>

#include <glm/gtc/quaternion.hpp>

namespace engine::core {

namespace {

// --- Layers --------------------------------------------------------------
// Object layers map 1:1 onto CollisionLayers.hpp's real, named
// CollisionLayer enum (Static/Default/Character/Debris/Trigger) -- five
// real object layers, not the original two (moving/non-moving) this
// scheme replaced (see git history/README's "Physics Core" section for
// the real collision-layers-and-masks feature this became). Broad-phase
// layers stay at two (NON_MOVING/MOVING) regardless -- that's a real,
// standard Jolt performance structure (a coarse moving-vs-non-moving
// tree split), independent of how many *object* layers exist for
// fine-grained pair filtering; the two concerns don't need to scale
// together.
namespace BroadPhaseLayers {
    constexpr JPH::BroadPhaseLayer NON_MOVING(0);
    constexpr JPH::BroadPhaseLayer MOVING(1);
    constexpr uint32_t NUM_LAYERS = 2;
}

JPH::ObjectLayer toObjectLayer(CollisionLayer layer) { return static_cast<JPH::ObjectLayer>(layer); }

class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface {
public:
    BPLayerInterfaceImpl() {
        for (size_t i = 0; i < kCollisionLayerCount; ++i) {
            objectToBroadPhase_[i] = (static_cast<CollisionLayer>(i) == CollisionLayer::Static)
                                          ? BroadPhaseLayers::NON_MOVING
                                          : BroadPhaseLayers::MOVING;
        }
    }
    JPH::uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::NUM_LAYERS; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return objectToBroadPhase_[layer];
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    // JPH::BroadPhaseLayerInterface::GetBroadPhaseLayerName() is only a
    // pure virtual under this same guard (BroadPhaseLayer.h) -- Jolt's
    // own profiler zones use it for debugging labels only, real gameplay
    // behavior never reads it. Real, honest names for this class's own
    // 2 broad-phase layers (BroadPhaseLayers::NUM_LAYERS above), not a
    // placeholder.
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
        switch (static_cast<JPH::BroadPhaseLayer::Type>(layer)) {
            case static_cast<JPH::BroadPhaseLayer::Type>(BroadPhaseLayers::NON_MOVING): return "NON_MOVING";
            case static_cast<JPH::BroadPhaseLayer::Type>(BroadPhaseLayers::MOVING): return "MOVING";
            default: return "INVALID";
        }
    }
#endif

private:
    JPH::BroadPhaseLayer objectToBroadPhase_[kCollisionLayerCount];
};

class ObjectVsBroadPhaseLayerFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer layer1, JPH::BroadPhaseLayer layer2) const override {
        // A Static-layer object never needs to test against the
        // NON_MOVING broad-phase tree (nothing static collides with
        // anything else static) -- everything else tests against both
        // trees. Same logic the original 2-layer scheme had, generalized
        // to "is this object layer Static or not" instead of hardcoding
        // just the two original layer values.
        if (static_cast<CollisionLayer>(layer1) == CollisionLayer::Static) {
            return layer2 == BroadPhaseLayers::MOVING;
        }
        return true;
    }
};

// Defers to the real, live, mutable CollisionMatrix Physics owns
// (collisionMatrix_) -- see CollisionLayers.hpp. `matrix_` is a
// reference, not a copy: Physics::setLayerCollision() mutates the same
// object this filter reads from, so a runtime reconfiguration takes
// effect on the very next broadphase pass without recreating anything.
class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter {
public:
    explicit ObjectLayerPairFilterImpl(const CollisionMatrix& matrix) : matrix_(matrix) {}

    bool ShouldCollide(JPH::ObjectLayer object1, JPH::ObjectLayer object2) const override {
        return matrix_.shouldCollide(static_cast<CollisionLayer>(object1), static_cast<CollisionLayer>(object2));
    }

private:
    const CollisionMatrix& matrix_;
};

glm::vec3 toGlm(JPH::RVec3 v) { return {static_cast<float>(v.GetX()), static_cast<float>(v.GetY()), static_cast<float>(v.GetZ())}; }
glm::quat toGlm(JPH::Quat q) { return {q.GetW(), q.GetX(), q.GetY(), q.GetZ()}; }
JPH::Vec3 toJolt(glm::vec3 v) { return JPH::Vec3(v.x, v.y, v.z); }
JPH::Quat toJolt(glm::quat q) { return JPH::Quat(q.x, q.y, q.z, q.w); }

// Reports only OnContactAdded (a body pair that just *started* touching --
// roughly Roblox's BasePart.Touched semantics), not OnContactPersisted
// (continues touching every step) or OnContactRemoved (Jolt's own docs
// warn the bodies may already be destroyed by the time that fires, see
// ContactListener.h) -- a deliberately smaller, real slice of what a full
// Touched/TouchEnded pair would need, matching this codebase's existing
// honesty pattern (e.g. AnimationTrack's by-Name-only targeting) rather
// than faking the removed-contact half.
//
// Jolt may call OnContactAdded from multiple physics worker threads
// concurrently during PhysicsSystem::Update() -- the mutex around
// `events`/`mutex` (references to Physics::pendingCollisionEvents_/
// collisionEventsMutex_, handed in at construction rather than requiring
// this class to be a Physics member/friend) is load-bearing, not
// defensive boilerplate.
class ContactListenerImpl final : public JPH::ContactListener {
public:
    ContactListenerImpl(std::mutex& mutex, std::vector<engine::core::Physics::CollisionEvent>& events,
                        std::mutex& impactMutex, std::vector<engine::core::Physics::ImpactEvent>& impactEvents,
                        const std::atomic<bool>& impactEnabled, const std::atomic<float>& minImpactSpeed)
        : mutex_(mutex),
          events_(events),
          impactMutex_(impactMutex),
          impactEvents_(impactEvents),
          impactEnabled_(impactEnabled),
          minImpactSpeed_(minImpactSpeed) {}

    void OnContactAdded(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold,
                         JPH::ContactSettings&) override {
        auto entity1 = static_cast<engine::core::EntityId>(static_cast<uint32_t>(body1.GetUserData()));
        auto entity2 = static_cast<engine::core::EntityId>(static_cast<uint32_t>(body2.GetUserData()));

        engine::core::Physics::CollisionEvent event;
        event.first = entity1;
        event.second = entity2;
        // Real contact data straight out of Jolt's manifold -- see
        // CollisionEvent's own doc comment (Physics.hpp) for why only the
        // first point of a (possibly up-to-4-point) manifold is kept.
        if (!manifold.mRelativeContactPointsOn1.empty()) {
            event.point = toGlm(manifold.GetWorldSpaceContactPointOn1(0));
        } else {
            event.point = toGlm(manifold.mBaseOffset);
        }
        event.normal = {manifold.mWorldSpaceNormal.GetX(), manifold.mWorldSpaceNormal.GetY(),
                         manifold.mWorldSpaceNormal.GetZ()};

        {
            std::lock_guard<std::mutex> lock(mutex_);
            events_.push_back(event);
        }

        if (!impactEnabled_.load(std::memory_order_relaxed) || body1.IsSensor() || body2.IsSensor()) return;
        JPH::RVec3 contactPoint = manifold.mRelativeContactPointsOn1.empty() ? manifold.mBaseOffset
                                                                              : manifold.GetWorldSpaceContactPointOn1(0);
        // mWorldSpaceNormal points from body1 towards body2, so a positive
        // value means the bodies are closing.
        float closingSpeed =
            (body1.GetPointVelocity(contactPoint) - body2.GetPointVelocity(contactPoint)).Dot(manifold.mWorldSpaceNormal);
        if (closingSpeed < minImpactSpeed_.load(std::memory_order_relaxed)) return;

        engine::core::Physics::ImpactEvent impact;
        impact.bodyA = body1.GetID().GetIndexAndSequenceNumber();
        impact.bodyB = body2.GetID().GetIndexAndSequenceNumber();
        impact.point = event.point;
        impact.normal = event.normal;
        impact.impactSpeed = closingSpeed;
        std::lock_guard<std::mutex> lock(impactMutex_);
        impactEvents_.push_back(impact);
    }

private:
    std::mutex& mutex_;
    std::vector<engine::core::Physics::CollisionEvent>& events_;
    std::mutex& impactMutex_;
    std::vector<engine::core::Physics::ImpactEvent>& impactEvents_;
    const std::atomic<bool>& impactEnabled_;
    const std::atomic<float>& minImpactSpeed_;
};

constexpr JPH::uint kMaxBodies = 65536;
constexpr JPH::uint kNumBodyMutexes = 0; // 0 = Jolt picks a default
constexpr JPH::uint kMaxBodyPairs = 65536;
constexpr JPH::uint kMaxContactConstraints = 16384;

// Shared by every dynamic-shape creation function: `explicitMass > 0`
// wins outright (an explicit override, matching every existing call
// site's prior behavior exactly); otherwise mass is computed for real
// from `material.density * volume` -- see PhysicsMaterial.hpp's
// closed-form volume functions. Falls back to 1kg only if both the
// override and the material are unhelpful (zero/negative density), so a
// body is never created with zero or negative mass, which Jolt itself
// would reject/misbehave on.
float resolveMass(float explicitMass, float volume, const PhysicsMaterial& material) {
    if (explicitMass > 0.0f) return explicitMass;
    float computed = material.density * volume;
    return computed > 0.0f ? computed : 1.0f;
}

} // namespace

struct Physics::RagdollStore {
    struct Entry {
        JPH::Ref<JPH::RagdollSettings> settings;
        JPH::Ref<JPH::Ragdoll> ragdoll;
    };
    std::unordered_map<RagdollHandle, Entry> entries;
    RagdollHandle nextHandle = 1;

    ~RagdollStore() {
        for (auto& [handle, entry] : entries) entry.ragdoll->RemoveFromPhysicsSystem();
    }

    [[nodiscard]] JPH::Ragdoll* find(RagdollHandle handle) const {
        auto it = entries.find(handle);
        return it == entries.end() ? nullptr : it->second.ragdoll.GetPtr();
    }
};

Physics::Physics() = default;
Physics::~Physics() { shutdown(); }

bool Physics::initialize() {
    JPH::RegisterDefaultAllocator();

    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();

    tempAllocator_ = std::make_unique<JPH::TempAllocatorImpl>(10 * 1024 * 1024);

    unsigned int hwThreads = std::thread::hardware_concurrency();
    int workerThreads = hwThreads > 1 ? static_cast<int>(hwThreads) - 1 : 1;
    jobSystem_ = std::make_unique<JPH::JobSystemThreadPool>(
        JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, workerThreads);

    broadPhaseLayerInterface_ = std::make_unique<BPLayerInterfaceImpl>();
    objectVsBroadPhaseLayerFilter_ = std::make_unique<ObjectVsBroadPhaseLayerFilterImpl>();
    objectLayerPairFilter_ = std::make_unique<ObjectLayerPairFilterImpl>(collisionMatrix_);

    physicsSystem_ = std::make_unique<JPH::PhysicsSystem>();
    physicsSystem_->Init(
        kMaxBodies, kNumBodyMutexes, kMaxBodyPairs, kMaxContactConstraints,
        *broadPhaseLayerInterface_, *objectVsBroadPhaseLayerFilter_, *objectLayerPairFilter_);

    contactListener_ = std::make_unique<ContactListenerImpl>(collisionEventsMutex_, pendingCollisionEvents_,
                                                             impactEventsMutex_, pendingImpactEvents_,
                                                             impactRecordingEnabled_, minImpactSpeed_);
    physicsSystem_->SetContactListener(contactListener_.get());

    ragdolls_ = std::make_unique<RagdollStore>();

    initialized_ = true;
    std::fprintf(stdout, "Physics: Jolt initialized (%d worker threads)\n", workerThreads);
    return true;
}

std::vector<Physics::CollisionEvent> Physics::drainCollisionEvents() {
    std::lock_guard<std::mutex> lock(collisionEventsMutex_);
    std::vector<CollisionEvent> drained;
    drained.swap(pendingCollisionEvents_);
    return drained;
}

void Physics::shutdown() {
    if (!initialized_) return;

    ragdolls_.reset(); // ragdoll destructors destroy their bodies through physicsSystem_

    contactListener_.reset(); // must outlive physicsSystem_'s use of it, so reset before it
    physicsSystem_.reset();
    jobSystem_.reset();
    tempAllocator_.reset();
    broadPhaseLayerInterface_.reset();
    objectVsBroadPhaseLayerFilter_.reset();
    objectLayerPairFilter_.reset();

    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;

    initialized_ = false;
}

void Physics::step(float dt, ECS& ecs) {
    if (!initialized_ || dt <= 0.0f) return;

    // One collision-detection sub-step per ~1/60s of frame time, matching
    // the fixed-tick default from docs/ARCHITECTURE.md §4.2 (60 Hz). Real
    // tuning belongs to the Adaptive Performance Controller (§4.1), not
    // hardcoded here -- this is the bring-up default.
    constexpr float kFixedSubStep = 1.0f / 60.0f;
    int collisionSteps = std::max(1, static_cast<int>(dt / kFixedSubStep + 0.5f));

    physicsSystem_->Update(dt, collisionSteps, tempAllocator_.get(), jobSystem_.get());
    syncTransforms(ecs);
}

void Physics::optimizeBroadPhase() {
    if (!initialized_) return;
    physicsSystem_->OptimizeBroadPhase();
}

uint32_t Physics::activeBodyCount() const {
    if (!initialized_) return 0;
    return physicsSystem_->GetNumActiveBodies(JPH::EBodyType::RigidBody);
}

uint32_t Physics::totalBodyCount() const {
    if (!initialized_) return 0;
    return physicsSystem_->GetNumBodies();
}

void Physics::syncTransforms(ECS& ecs) {
    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
    auto view = ecs.view<RigidBody, Transform>();
    for (auto entity : view) {
        auto& rb = view.get<RigidBody>(entity);
        if (rb.joltBodyId == RigidBody::kInvalidBodyId) continue;

        JPH::BodyID id(rb.joltBodyId);
        if (!bodyInterface.IsAdded(id)) continue;

        auto& transform = view.get<Transform>(entity);
        transform.position = toGlm(bodyInterface.GetPosition(id));
        transform.rotation = toGlm(bodyInterface.GetRotation(id));
    }
}

EntityId Physics::createGroundPlane(ECS& ecs, float halfExtentX, float halfExtentZ, PhysicsMaterial material) {
    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();

    JPH::BoxShapeSettings shapeSettings(JPH::Vec3(halfExtentX, 0.5f, halfExtentZ));
    auto shapeResult = shapeSettings.Create();
    JPH::BodyCreationSettings creationSettings(
        shapeResult.Get(), JPH::RVec3(0.0, -0.5, 0.0), JPH::Quat::sIdentity(),
        JPH::EMotionType::Static, toObjectLayer(CollisionLayer::Static));
    creationSettings.mFriction = material.friction;
    creationSettings.mRestitution = material.restitution;

    EntityId entity = ecs.createEntity("GroundPlane");
    // mUserData carries this body's EntityId into JPH::Body::GetUserData()
    // -- how ContactListenerImpl (see the anonymous namespace above) turns
    // a Jolt collision callback, which only ever sees JPH::Body, back into
    // the ECS entities drainCollisionEvents() reports.
    creationSettings.mUserData = static_cast<JPH::uint64>(static_cast<uint32_t>(entity));
    JPH::BodyID id = bodyInterface.CreateAndAddBody(creationSettings, JPH::EActivation::DontActivate);

    ecs.addComponent<RigidBody>(entity, RigidBody{id.GetIndexAndSequenceNumber(), RigidBodyMotionType::Static});
    ecs.addComponent<ColliderShape>(entity, ColliderShape{ColliderShapeKind::Box, {halfExtentX, 0.5f, halfExtentZ}, ""});
    ecs.addComponent<PhysicsMaterial>(entity, material);
    ecs.addComponent<Renderable>(entity);
    return entity;
}

EntityId Physics::createStaticBox(ECS& ecs, glm::vec3 position, glm::vec3 halfExtent, glm::quat rotation,
                                   PhysicsMaterial material, CollisionLayer layer, bool isSensor) {
    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();

    JPH::BoxShapeSettings shapeSettings(toJolt(halfExtent));
    auto shapeResult = shapeSettings.Create();
    JPH::BodyCreationSettings creationSettings(
        shapeResult.Get(), JPH::RVec3(position.x, position.y, position.z), toJolt(rotation),
        JPH::EMotionType::Static, toObjectLayer(layer));
    creationSettings.mFriction = material.friction;
    creationSettings.mRestitution = material.restitution;
    creationSettings.mIsSensor = isSensor;

    EntityId entity = ecs.createEntity("StaticBox");
    creationSettings.mUserData = static_cast<JPH::uint64>(static_cast<uint32_t>(entity));
    JPH::BodyID id = bodyInterface.CreateAndAddBody(creationSettings, JPH::EActivation::DontActivate);

    ecs.addComponent<RigidBody>(entity, RigidBody{id.GetIndexAndSequenceNumber(), RigidBodyMotionType::Static});
    ecs.addComponent<ColliderShape>(entity, ColliderShape{ColliderShapeKind::Box, halfExtent, ""});
    ecs.addComponent<PhysicsMaterial>(entity, material);
    ecs.addComponent<Renderable>(entity);
    return entity;
}

EntityId Physics::createDynamicBox(ECS& ecs, glm::vec3 position, glm::vec3 halfExtent, float mass,
                                    PhysicsMaterial material, CollisionLayer layer, bool isSensor) {
    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();

    JPH::BoxShapeSettings shapeSettings(toJolt(halfExtent));
    auto shapeResult = shapeSettings.Create();
    JPH::BodyCreationSettings creationSettings(
        shapeResult.Get(), JPH::RVec3(position.x, position.y, position.z), JPH::Quat::sIdentity(),
        JPH::EMotionType::Dynamic, toObjectLayer(layer));
    creationSettings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
    creationSettings.mMassPropertiesOverride.mMass = resolveMass(mass, boxVolume(halfExtent), material);
    creationSettings.mFriction = material.friction;
    creationSettings.mRestitution = material.restitution;
    creationSettings.mIsSensor = isSensor;

    EntityId entity = ecs.createEntity("DynamicBox");
    creationSettings.mUserData = static_cast<JPH::uint64>(static_cast<uint32_t>(entity));
    JPH::BodyID id = bodyInterface.CreateAndAddBody(creationSettings, JPH::EActivation::Activate);

    ecs.addComponent<RigidBody>(entity, RigidBody{id.GetIndexAndSequenceNumber(), RigidBodyMotionType::Dynamic});
    ecs.addComponent<ColliderShape>(entity, ColliderShape{ColliderShapeKind::Box, halfExtent, ""});
    ecs.addComponent<PhysicsMaterial>(entity, material);
    ecs.addComponent<Renderable>(entity);
    return entity;
}

EntityId Physics::createSphereBody(ECS& ecs, glm::vec3 position, float radius, float mass, PhysicsMaterial material,
                                    CollisionLayer layer, bool isSensor) {
    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();

    JPH::SphereShapeSettings shapeSettings(radius);
    auto shapeResult = shapeSettings.Create();
    JPH::BodyCreationSettings creationSettings(
        shapeResult.Get(), JPH::RVec3(position.x, position.y, position.z), JPH::Quat::sIdentity(),
        JPH::EMotionType::Dynamic, toObjectLayer(layer));
    creationSettings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
    creationSettings.mMassPropertiesOverride.mMass = resolveMass(mass, sphereVolume(radius), material);
    creationSettings.mFriction = material.friction;
    creationSettings.mRestitution = material.restitution;
    creationSettings.mIsSensor = isSensor;

    EntityId entity = ecs.createEntity("SphereBody");
    creationSettings.mUserData = static_cast<JPH::uint64>(static_cast<uint32_t>(entity));
    JPH::BodyID id = bodyInterface.CreateAndAddBody(creationSettings, JPH::EActivation::Activate);

    ecs.addComponent<RigidBody>(entity, RigidBody{id.GetIndexAndSequenceNumber(), RigidBodyMotionType::Dynamic});
    ecs.addComponent<ColliderShape>(entity, ColliderShape{ColliderShapeKind::Sphere, {radius, 0.0f, 0.0f}, ""});
    ecs.addComponent<PhysicsMaterial>(entity, material);
    ecs.addComponent<Renderable>(entity);
    return entity;
}

EntityId Physics::createMeshBody(ECS& ecs, glm::vec3 position, const std::vector<glm::vec3>& positions,
                                  const std::vector<uint32_t>& indices, PhysicsMaterial material) {
    JPH::TriangleList triangles;
    triangles.reserve(indices.size() / 3);
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        const glm::vec3& v0 = positions[indices[i]];
        const glm::vec3& v1 = positions[indices[i + 1]];
        const glm::vec3& v2 = positions[indices[i + 2]];
        triangles.push_back(JPH::Triangle(JPH::Float3(v0.x, v0.y, v0.z), JPH::Float3(v1.x, v1.y, v1.z),
                                           JPH::Float3(v2.x, v2.y, v2.z)));
    }

    JPH::MeshShapeSettings shapeSettings(triangles);
    auto shapeResult = shapeSettings.Create();
    if (shapeResult.HasError()) {
        std::fprintf(stderr, "Physics: createMeshBody failed to build a MeshShape: %s\n",
                     shapeResult.GetError().c_str());
        return kNullEntity;
    }

    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
    // Mesh shapes have no analytic inertia and can't be simulated as
    // Dynamic in Jolt (see createMeshBody()'s header comment) -- always
    // Static, always the Static collision layer.
    JPH::BodyCreationSettings creationSettings(
        shapeResult.Get(), JPH::RVec3(position.x, position.y, position.z), JPH::Quat::sIdentity(),
        JPH::EMotionType::Static, toObjectLayer(CollisionLayer::Static));
    creationSettings.mFriction = material.friction;
    creationSettings.mRestitution = material.restitution;

    EntityId entity = ecs.createEntity("MeshBody");
    creationSettings.mUserData = static_cast<JPH::uint64>(static_cast<uint32_t>(entity));
    JPH::BodyID id = bodyInterface.CreateAndAddBody(creationSettings, JPH::EActivation::DontActivate);

    ecs.addComponent<RigidBody>(entity, RigidBody{id.GetIndexAndSequenceNumber(), RigidBodyMotionType::Static});
    ecs.addComponent<ColliderShape>(entity, ColliderShape{ColliderShapeKind::Mesh, {0.0f, 0.0f, 0.0f}, ""});
    ecs.addComponent<PhysicsMaterial>(entity, material);
    ecs.addComponent<Renderable>(entity);
    return entity;
}

EntityId Physics::createKinematicBox(ECS& ecs, glm::vec3 position, glm::vec3 halfExtent, PhysicsMaterial material) {
    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();

    JPH::BoxShapeSettings shapeSettings(toJolt(halfExtent));
    auto shapeResult = shapeSettings.Create();
    JPH::BodyCreationSettings creationSettings(
        shapeResult.Get(), JPH::RVec3(position.x, position.y, position.z), JPH::Quat::sIdentity(),
        JPH::EMotionType::Kinematic, toObjectLayer(CollisionLayer::Default));
    creationSettings.mFriction = material.friction;
    creationSettings.mRestitution = material.restitution;

    EntityId entity = ecs.createEntity("KinematicPlatform");
    creationSettings.mUserData = static_cast<JPH::uint64>(static_cast<uint32_t>(entity));
    JPH::BodyID id = bodyInterface.CreateAndAddBody(creationSettings, JPH::EActivation::Activate);

    ecs.addComponent<RigidBody>(entity, RigidBody{id.GetIndexAndSequenceNumber(), RigidBodyMotionType::Kinematic});
    ecs.addComponent<ColliderShape>(entity, ColliderShape{ColliderShapeKind::Box, halfExtent, ""});
    ecs.addComponent<PhysicsMaterial>(entity, material);
    ecs.addComponent<Renderable>(entity);
    return entity;
}

bool Physics::attachBodyToEntity(EntityId entity, ECS& ecs, const ColliderShape& shape, PhysicsMaterial material,
                                  RigidBodyMotionType motionType, float mass, CollisionLayer layer, bool isSensor,
                                  const std::vector<glm::vec3>* meshPositions, const std::vector<uint32_t>* meshIndices) {
    auto* transform = ecs.tryGetComponent<Transform>(entity);
    if (!transform) {
        std::fprintf(stderr, "Physics: attachBodyToEntity failed -- entity has no Transform.\n");
        return false;
    }

    JPH::ShapeSettings::ShapeResult shapeResult;
    float volume = 0.0f;
    switch (shape.kind) {
        case ColliderShapeKind::Box: {
            JPH::BoxShapeSettings settings(toJolt(shape.params));
            shapeResult = settings.Create();
            volume = boxVolume(shape.params);
            break;
        }
        case ColliderShapeKind::Sphere: {
            JPH::SphereShapeSettings settings(shape.params.x);
            shapeResult = settings.Create();
            volume = sphereVolume(shape.params.x);
            break;
        }
        case ColliderShapeKind::Capsule: {
            JPH::CapsuleShapeSettings settings(shape.params.y, shape.params.x); // halfHeight, radius -- matches ColliderShape's own params convention
            shapeResult = settings.Create();
            volume = capsuleVolume(shape.params.x, shape.params.y);
            break;
        }
        case ColliderShapeKind::Mesh: {
            if (meshPositions == nullptr || meshIndices == nullptr || meshIndices->size() < 3) {
                std::fprintf(stderr, "Physics: attachBodyToEntity requires real mesh data for a Mesh collider shape.\n");
                return false;
            }
            JPH::TriangleList triangles;
            triangles.reserve(meshIndices->size() / 3);
            for (size_t i = 0; i + 2 < meshIndices->size(); i += 3) {
                const glm::vec3& v0 = (*meshPositions)[(*meshIndices)[i]];
                const glm::vec3& v1 = (*meshPositions)[(*meshIndices)[i + 1]];
                const glm::vec3& v2 = (*meshPositions)[(*meshIndices)[i + 2]];
                triangles.push_back(JPH::Triangle(JPH::Float3(v0.x, v0.y, v0.z), JPH::Float3(v1.x, v1.y, v1.z),
                                                   JPH::Float3(v2.x, v2.y, v2.z)));
            }
            JPH::MeshShapeSettings settings(triangles);
            shapeResult = settings.Create();
            // Same real constraint createMeshBody() documents -- Jolt
            // mesh shapes have no analytic inertia and can't be Dynamic.
            motionType = RigidBodyMotionType::Static;
            break;
        }
    }
    if (shapeResult.HasError()) {
        std::fprintf(stderr, "Physics: attachBodyToEntity failed to build a shape: %s\n", shapeResult.GetError().c_str());
        return false;
    }

    JPH::EMotionType joltMotionType = motionType == RigidBodyMotionType::Static     ? JPH::EMotionType::Static
                                       : motionType == RigidBodyMotionType::Kinematic ? JPH::EMotionType::Kinematic
                                                                                       : JPH::EMotionType::Dynamic;

    JPH::BodyCreationSettings creationSettings(
        shapeResult.Get(), JPH::RVec3(transform->position.x, transform->position.y, transform->position.z),
        toJolt(transform->rotation), joltMotionType, toObjectLayer(layer));
    creationSettings.mFriction = material.friction;
    creationSettings.mRestitution = material.restitution;
    creationSettings.mIsSensor = isSensor;
    if (motionType == RigidBodyMotionType::Dynamic) {
        creationSettings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        creationSettings.mMassPropertiesOverride.mMass = resolveMass(mass, volume, material);
    }
    creationSettings.mUserData = static_cast<JPH::uint64>(static_cast<uint32_t>(entity));

    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
    JPH::EActivation activation = motionType == RigidBodyMotionType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate;
    JPH::BodyID id = bodyInterface.CreateAndAddBody(creationSettings, activation);

    ecs.addComponent<RigidBody>(entity, RigidBody{id.GetIndexAndSequenceNumber(), motionType});
    ecs.addComponent<ColliderShape>(entity, shape);
    ecs.addComponent<PhysicsMaterial>(entity, material);
    return true;
}

void Physics::detachBody(EntityId entity, ECS& ecs) {
    auto* rb = ecs.tryGetComponent<RigidBody>(entity);
    if (!rb || rb->joltBodyId == RigidBody::kInvalidBodyId) return;

    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
    JPH::BodyID id(rb->joltBodyId);
    bodyInterface.RemoveBody(id);
    bodyInterface.DestroyBody(id);
    rb->joltBodyId = RigidBody::kInvalidBodyId;
}

void Physics::moveKinematic(EntityId entity, ECS& ecs, glm::vec3 targetPosition, glm::quat targetRotation, float dt) {
    auto* rb = ecs.tryGetComponent<RigidBody>(entity);
    if (!rb || rb->joltBodyId == RigidBody::kInvalidBodyId || dt <= 0.0f) return;

    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
    bodyInterface.MoveKinematic(JPH::BodyID(rb->joltBodyId), JPH::RVec3(targetPosition.x, targetPosition.y, targetPosition.z),
                                 toJolt(targetRotation), dt);
}

void Physics::applyImpulse(EntityId entity, ECS& ecs, glm::vec3 impulse) {
    auto* rb = ecs.tryGetComponent<RigidBody>(entity);
    if (!rb || rb->joltBodyId == RigidBody::kInvalidBodyId) return;

    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
    JPH::BodyID id(rb->joltBodyId);
    bodyInterface.AddImpulse(id, toJolt(impulse));
}

EntityId Physics::createCharacterCapsule(ECS& ecs, glm::vec3 position, float radius, float halfHeight, float mass,
                                          PhysicsMaterial material, CollisionLayer layer) {
    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();

    JPH::CapsuleShapeSettings shapeSettings(halfHeight, radius);
    auto shapeResult = shapeSettings.Create();
    JPH::BodyCreationSettings creationSettings(
        shapeResult.Get(), JPH::RVec3(position.x, position.y, position.z), JPH::Quat::sIdentity(),
        JPH::EMotionType::Dynamic, toObjectLayer(layer));
    creationSettings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
    creationSettings.mMassPropertiesOverride.mMass = resolveMass(mass, capsuleVolume(radius, halfHeight), material);
    // Pitch/roll locked (see header comment) -- yaw stays free so
    // setRotationY() drives facing through the real simulated body.
    creationSettings.mAllowedDOFs =
        JPH::EAllowedDOFs::TranslationX | JPH::EAllowedDOFs::TranslationY | JPH::EAllowedDOFs::TranslationZ |
        JPH::EAllowedDOFs::RotationY;
    creationSettings.mFriction = material.friction;
    creationSettings.mRestitution = material.restitution;
    // Locked-rotation dynamic bodies benefit from a lower linear damping
    // than Jolt's default so direct velocity sets (setHorizontalVelocity)
    // aren't fighting an artificial deceleration on top of player input.
    creationSettings.mLinearDamping = 0.0f;

    EntityId entity = ecs.createEntity("Character");
    creationSettings.mUserData = static_cast<JPH::uint64>(static_cast<uint32_t>(entity));
    JPH::BodyID id = bodyInterface.CreateAndAddBody(creationSettings, JPH::EActivation::Activate);

    ecs.addComponent<RigidBody>(entity, RigidBody{id.GetIndexAndSequenceNumber(), RigidBodyMotionType::Dynamic});
    ecs.addComponent<ColliderShape>(entity, ColliderShape{ColliderShapeKind::Capsule, {radius, halfHeight, 0.0f}, ""});
    ecs.addComponent<PhysicsMaterial>(entity, material);
    ecs.addComponent<Renderable>(entity);
    return entity;
}

void Physics::applyMaterial(EntityId entity, ECS& ecs, PhysicsMaterial material) {
    auto* rb = ecs.tryGetComponent<RigidBody>(entity);
    if (!rb || rb->joltBodyId == RigidBody::kInvalidBodyId) return;

    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
    JPH::BodyID id(rb->joltBodyId);
    bodyInterface.SetFriction(id, material.friction);
    bodyInterface.SetRestitution(id, material.restitution);

    // density is recorded on the component (below) but not retroactively
    // applied to this body's mass -- see this method's header comment.
    if (auto* existing = ecs.tryGetComponent<PhysicsMaterial>(entity)) {
        *existing = material;
    } else {
        ecs.addComponent<PhysicsMaterial>(entity, material);
    }
}

void Physics::setDamping(EntityId entity, ECS& ecs, float linearDamping, float angularDamping) {
    auto* rb = ecs.tryGetComponent<RigidBody>(entity);
    if (!rb || rb->joltBodyId == RigidBody::kInvalidBodyId) return;
    if (rb->motionType == RigidBodyMotionType::Static) return; // static bodies have no MotionProperties to set damping on

    JPH::BodyLockWrite lock(physicsSystem_->GetBodyLockInterface(), JPH::BodyID(rb->joltBodyId));
    if (!lock.Succeeded()) return;
    JPH::MotionProperties* motionProperties = lock.GetBody().GetMotionProperties();
    motionProperties->SetLinearDamping(std::max(0.0f, linearDamping));
    motionProperties->SetAngularDamping(std::max(0.0f, angularDamping));
}

void Physics::setGravity(glm::vec3 gravity) {
    if (!initialized_) return;
    physicsSystem_->SetGravity(toJolt(gravity));
}

glm::vec3 Physics::gravity() const {
    if (!initialized_) return glm::vec3(0.0f, -9.81f, 0.0f);
    JPH::Vec3 g = physicsSystem_->GetGravity();
    return {g.GetX(), g.GetY(), g.GetZ()};
}

void Physics::setLayerCollision(CollisionLayer a, CollisionLayer b, bool shouldCollide) {
    collisionMatrix_.setShouldCollide(a, b, shouldCollide);
}

bool Physics::layersShouldCollide(CollisionLayer a, CollisionLayer b) const {
    return collisionMatrix_.shouldCollide(a, b);
}

void Physics::setHorizontalVelocity(EntityId entity, ECS& ecs, glm::vec2 velocityXZ) {
    auto* rb = ecs.tryGetComponent<RigidBody>(entity);
    if (!rb || rb->joltBodyId == RigidBody::kInvalidBodyId) return;

    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
    JPH::BodyID id(rb->joltBodyId);

    JPH::Vec3 current = bodyInterface.GetLinearVelocity(id);
    bodyInterface.SetLinearVelocity(id, JPH::Vec3(velocityXZ.x, current.GetY(), velocityXZ.y));
}

void Physics::setVerticalVelocity(EntityId entity, ECS& ecs, float velocityY) {
    auto* rb = ecs.tryGetComponent<RigidBody>(entity);
    if (!rb || rb->joltBodyId == RigidBody::kInvalidBodyId) return;

    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
    JPH::BodyID id(rb->joltBodyId);

    JPH::Vec3 current = bodyInterface.GetLinearVelocity(id);
    bodyInterface.SetLinearVelocity(id, JPH::Vec3(current.GetX(), velocityY, current.GetZ()));
}

glm::vec3 Physics::getLinearVelocity(EntityId entity, ECS& ecs) const {
    auto* rb = ecs.tryGetComponent<RigidBody>(entity);
    if (!rb || rb->joltBodyId == RigidBody::kInvalidBodyId) return glm::vec3(0.0f);

    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
    JPH::Vec3 v = bodyInterface.GetLinearVelocity(JPH::BodyID(rb->joltBodyId));
    return {v.GetX(), v.GetY(), v.GetZ()};
}

void Physics::setPosition(EntityId entity, ECS& ecs, glm::vec3 position) {
    auto* rb = ecs.tryGetComponent<RigidBody>(entity);
    if (!rb || rb->joltBodyId == RigidBody::kInvalidBodyId) return;

    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
    bodyInterface.SetPosition(JPH::BodyID(rb->joltBodyId), JPH::RVec3(position.x, position.y, position.z),
                               JPH::EActivation::DontActivate);
}

void Physics::setRotationY(EntityId entity, ECS& ecs, float yawRadians) {
    auto* rb = ecs.tryGetComponent<RigidBody>(entity);
    if (!rb || rb->joltBodyId == RigidBody::kInvalidBodyId) return;

    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
    JPH::Quat yaw = JPH::Quat::sRotation(JPH::Vec3::sAxisY(), yawRadians);
    bodyInterface.SetRotation(JPH::BodyID(rb->joltBodyId), yaw, JPH::EActivation::DontActivate);
}

bool Physics::isGrounded(EntityId entity, ECS& ecs, float capsuleHalfHeight, float capsuleRadius, float skin,
                          float checkDistance) const {
    return checkGround(entity, ecs, capsuleHalfHeight, capsuleRadius, skin, checkDistance).grounded;
}

Physics::GroundInfo Physics::checkGround(EntityId entity, ECS& ecs, float capsuleHalfHeight, float capsuleRadius,
                                          float skin, float checkDistance) const {
    GroundInfo info;
    auto* rb = ecs.tryGetComponent<RigidBody>(entity);
    if (!rb || rb->joltBodyId == RigidBody::kInvalidBodyId) return info;

    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
    JPH::RVec3 center = bodyInterface.GetPosition(JPH::BodyID(rb->joltBodyId));

    // Origin starts just below the capsule's own bottom hemisphere (by
    // `skin`) so the ray begins outside the character's own collision
    // volume -- avoids a body filter entirely for what only needs to be a
    // short, cheap "is there ground right here" check.
    float bottomOffset = capsuleHalfHeight + capsuleRadius + skin;
    JPH::RVec3 origin = center - JPH::RVec3(0, bottomOffset, 0);
    JPH::RRayCast ray(origin, JPH::Vec3(0, -checkDistance, 0));

    JPH::RayCastResult hit;
    if (!physicsSystem_->GetNarrowPhaseQuery().CastRay(ray, hit)) return info;

    info.grounded = true;

    JPH::BodyLockRead lock(physicsSystem_->GetBodyLockInterface(), hit.mBodyID);
    if (lock.Succeeded()) {
        const JPH::Body& body = lock.GetBody();
        JPH::Vec3 normal = body.GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, ray.GetPointOnRay(hit.mFraction));
        info.normal = {normal.GetX(), normal.GetY(), normal.GetZ()};
    }
    return info;
}

Physics::RaycastHit Physics::raycast(glm::vec3 origin, glm::vec3 direction, float maxDistance) const {
    RaycastHit result;
    if (!initialized_) return result;

    float len = glm::length(direction);
    if (len < 1e-6f || maxDistance <= 0.0f) return result;
    glm::vec3 unitDir = direction / len;

    JPH::RRayCast ray(JPH::RVec3(origin.x, origin.y, origin.z), toJolt(unitDir * maxDistance));

    JPH::RayCastResult hit;
    if (!physicsSystem_->GetNarrowPhaseQuery().CastRay(ray, hit)) return result;

    result.hit = true;
    result.distance = hit.mFraction * maxDistance;
    result.point = origin + unitDir * result.distance;

    JPH::BodyLockRead lock(physicsSystem_->GetBodyLockInterface(), hit.mBodyID);
    if (lock.Succeeded()) {
        const JPH::Body& body = lock.GetBody();
        result.entity = static_cast<EntityId>(static_cast<uint32_t>(body.GetUserData()));
        result.normal = toGlm(body.GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, ray.GetPointOnRay(hit.mFraction)));
    }
    return result;
}

void Physics::setImpactRecording(bool enabled, float minImpactSpeed) {
    minImpactSpeed_.store(std::max(0.0f, minImpactSpeed), std::memory_order_relaxed);
    impactRecordingEnabled_.store(enabled, std::memory_order_relaxed);
    if (!enabled) {
        std::lock_guard<std::mutex> lock(impactEventsMutex_);
        pendingImpactEvents_.clear();
    }
}

std::vector<Physics::ImpactEvent> Physics::drainImpactEvents() {
    std::lock_guard<std::mutex> lock(impactEventsMutex_);
    std::vector<ImpactEvent> drained;
    drained.swap(pendingImpactEvents_);
    return drained;
}

namespace {

JPH::Mat44 toJoltMat(const glm::mat4& m) {
    glm::quat rotation = glm::normalize(glm::quat_cast(glm::mat3(m)));
    return JPH::Mat44::sRotationTranslation(toJolt(rotation), JPH::Vec3(m[3].x, m[3].y, m[3].z));
}

glm::mat4 toGlmMat(JPH::RVec3 position, JPH::Quat rotation) {
    glm::mat4 m = glm::mat4_cast(toGlm(rotation));
    m[3] = glm::vec4(toGlm(position), 1.0f);
    return m;
}

} // namespace

Physics::RagdollHandle Physics::createRagdoll(const RagdollDesc& desc, const glm::mat4& modelToWorld,
                                              const std::vector<glm::mat4>* initialPartWorld, glm::vec3 initialVelocity,
                                              CollisionLayer layer) {
    if (!initialized_) return kInvalidRagdoll;
    std::string error;
    if (!validateRagdollDesc(desc, error)) {
        std::fprintf(stderr, "Physics: createRagdoll rejected an invalid desc: %s\n", error.c_str());
        return kInvalidRagdoll;
    }
    if (initialPartWorld != nullptr && initialPartWorld->size() != desc.parts.size()) {
        std::fprintf(stderr, "Physics: createRagdoll got %zu initial transforms for %zu parts.\n",
                     initialPartWorld->size(), desc.parts.size());
        return kInvalidRagdoll;
    }

    const glm::mat3 worldRotation(modelToWorld);
    auto toWorldPoint = [&](glm::vec3 p) { return glm::vec3(modelToWorld * glm::vec4(p, 1.0f)); };
    auto toWorldAxis = [&](glm::vec3 v) { return toJolt(glm::normalize(worldRotation * v)); };

    JPH::Ref<JPH::Skeleton> skeleton = new JPH::Skeleton;
    JPH::Ref<JPH::RagdollSettings> settings = new JPH::RagdollSettings;
    settings->mSkeleton = skeleton;
    settings->mParts.resize(desc.parts.size());
    std::vector<JPH::Mat44> bindWorld(desc.parts.size());

    for (size_t i = 0; i < desc.parts.size(); ++i) {
        const RagdollPartDesc& part = desc.parts[i];
        skeleton->AddJoint(part.name, part.parent);

        float length = glm::length(part.to - part.from);
        JPH::ShapeSettings::ShapeResult shape;
        if (length < 1e-4f) {
            shape = JPH::SphereShapeSettings(part.radius).Create();
        } else {
            // Slightly longer than the bone so neighbouring capsules overlap at
            // the joint instead of leaving a visible gap.
            float halfHeight = std::max(0.5f * length - 0.5f * part.radius, 0.01f);
            shape = JPH::CapsuleShapeSettings(halfHeight, part.radius).Create();
        }
        if (shape.HasError()) {
            std::fprintf(stderr, "Physics: createRagdoll failed to build part \"%s\": %s\n", part.name.c_str(),
                         shape.GetError().c_str());
            return kInvalidRagdoll;
        }

        glm::mat4 world = modelToWorld * ragdollPartBindMatrix(part);
        bindWorld[i] = toJoltMat(world);

        JPH::RagdollSettings::Part& body = settings->mParts[i];
        body.SetShape(shape.Get());
        body.mPosition = JPH::RVec3(world[3].x, world[3].y, world[3].z);
        body.mRotation = bindWorld[i].GetQuaternion();
        body.mMotionType = JPH::EMotionType::Dynamic;
        body.mObjectLayer = toObjectLayer(layer);
        body.mMotionQuality = JPH::EMotionQuality::LinearCast; // falls reach tens of m/s
        body.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        body.mMassPropertiesOverride.mMass = std::max(part.mass, 0.1f);
        body.mFriction = 0.7f;
        body.mRestitution = 0.15f;
        body.mLinearDamping = 0.02f;
        body.mAngularDamping = 0.3f;
        body.mLinearVelocity = toJolt(initialVelocity);

        if (part.parent < 0) continue;
        JPH::RVec3 pivot(toWorldPoint(part.pivot).x, toWorldPoint(part.pivot).y, toWorldPoint(part.pivot).z);
        if (part.jointType == RagdollJointType::Hinge) {
            auto* hinge = new JPH::HingeConstraintSettings;
            hinge->mSpace = JPH::EConstraintSpace::WorldSpace;
            hinge->mPoint1 = hinge->mPoint2 = pivot;
            hinge->mHingeAxis1 = hinge->mHingeAxis2 = toWorldAxis(part.axis);
            hinge->mNormalAxis1 = hinge->mNormalAxis2 = toWorldAxis(part.normal);
            hinge->mLimitsMin = std::min(part.hingeMin, 0.0f);
            hinge->mLimitsMax = std::max(part.hingeMax, 0.0f);
            hinge->mMaxFrictionTorque = part.frictionTorque;
            body.mToParent = hinge;
        } else {
            auto* swingTwist = new JPH::SwingTwistConstraintSettings;
            swingTwist->mSpace = JPH::EConstraintSpace::WorldSpace;
            swingTwist->mPosition1 = swingTwist->mPosition2 = pivot;
            swingTwist->mTwistAxis1 = swingTwist->mTwistAxis2 = toWorldAxis(part.axis);
            swingTwist->mPlaneAxis1 = swingTwist->mPlaneAxis2 = toWorldAxis(part.normal);
            swingTwist->mNormalHalfConeAngle = part.normalHalfCone;
            swingTwist->mPlaneHalfConeAngle = part.planeHalfCone;
            swingTwist->mTwistMinAngle = part.twistMin;
            swingTwist->mTwistMaxAngle = part.twistMax;
            swingTwist->mMaxFrictionTorque = part.frictionTorque;
            body.mToParent = swingTwist;
        }
    }

    skeleton->CalculateParentJointIndices();
    if (!settings->Stabilize()) {
        std::fprintf(stderr, "Physics: createRagdoll -- RagdollSettings::Stabilize() failed.\n");
    }
    settings->DisableParentChildCollisions(bindWorld.data(), 0.02f);
    settings->CalculateBodyIndexToConstraintIndex();
    settings->CalculateConstraintIndexToBodyIdxPair();

    RagdollHandle handle = ragdolls_->nextHandle++;
    JPH::Ref<JPH::Ragdoll> ragdoll =
        settings->CreateRagdoll(handle, static_cast<JPH::uint64>(static_cast<uint32_t>(kNullEntity)), physicsSystem_.get());
    if (ragdoll == nullptr) {
        std::fprintf(stderr, "Physics: createRagdoll -- out of bodies.\n");
        return kInvalidRagdoll;
    }

    if (initialPartWorld != nullptr) {
        std::vector<JPH::Mat44> pose(desc.parts.size());
        for (size_t i = 0; i < pose.size(); ++i) pose[i] = toJoltMat((*initialPartWorld)[i]);
        ragdoll->SetPose(JPH::RVec3::sZero(), pose.data());
    }
    ragdoll->AddToPhysicsSystem(JPH::EActivation::Activate);

    ragdolls_->entries.emplace(handle, RagdollStore::Entry{settings, ragdoll});
    return handle;
}

void Physics::destroyRagdoll(RagdollHandle handle) {
    if (!ragdolls_) return;
    auto it = ragdolls_->entries.find(handle);
    if (it == ragdolls_->entries.end()) return;
    it->second.ragdoll->RemoveFromPhysicsSystem();
    ragdolls_->entries.erase(it);
}

bool Physics::ragdollExists(RagdollHandle handle) const { return ragdolls_ && ragdolls_->find(handle) != nullptr; }

bool Physics::getRagdollPartTransforms(RagdollHandle handle, std::vector<glm::mat4>& outPartWorld) const {
    JPH::Ragdoll* ragdoll = ragdolls_ ? ragdolls_->find(handle) : nullptr;
    if (ragdoll == nullptr) return false;
    const JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
    outPartWorld.resize(ragdoll->GetBodyCount());
    for (size_t i = 0; i < ragdoll->GetBodyCount(); ++i) {
        JPH::RVec3 position;
        JPH::Quat rotation;
        bodyInterface.GetPositionAndRotation(ragdoll->GetBodyID(static_cast<int>(i)), position, rotation);
        outPartWorld[i] = toGlmMat(position, rotation);
    }
    return true;
}

std::vector<uint32_t> Physics::ragdollBodyIds(RagdollHandle handle) const {
    std::vector<uint32_t> ids;
    JPH::Ragdoll* ragdoll = ragdolls_ ? ragdolls_->find(handle) : nullptr;
    if (ragdoll == nullptr) return ids;
    for (const JPH::BodyID& id : ragdoll->GetBodyIDs()) ids.push_back(id.GetIndexAndSequenceNumber());
    return ids;
}

int Physics::ragdollPartIndexForBody(RagdollHandle handle, uint32_t bodyId) const {
    JPH::Ragdoll* ragdoll = ragdolls_ ? ragdolls_->find(handle) : nullptr;
    if (ragdoll == nullptr) return -1;
    for (size_t i = 0; i < ragdoll->GetBodyCount(); ++i) {
        if (ragdoll->GetBodyID(static_cast<int>(i)).GetIndexAndSequenceNumber() == bodyId) return static_cast<int>(i);
    }
    return -1;
}

glm::vec3 Physics::ragdollPartVelocity(RagdollHandle handle, int partIndex) const {
    JPH::Ragdoll* ragdoll = ragdolls_ ? ragdolls_->find(handle) : nullptr;
    if (ragdoll == nullptr || partIndex < 0 || partIndex >= static_cast<int>(ragdoll->GetBodyCount())) {
        return glm::vec3(0.0f);
    }
    JPH::Vec3 v = physicsSystem_->GetBodyInterface().GetLinearVelocity(ragdoll->GetBodyID(partIndex));
    return {v.GetX(), v.GetY(), v.GetZ()};
}

void Physics::addRagdollImpulse(RagdollHandle handle, int partIndex, glm::vec3 impulse) {
    JPH::Ragdoll* ragdoll = ragdolls_ ? ragdolls_->find(handle) : nullptr;
    if (ragdoll == nullptr || partIndex < 0 || partIndex >= static_cast<int>(ragdoll->GetBodyCount())) return;
    physicsSystem_->GetBodyInterface().AddImpulse(ragdoll->GetBodyID(partIndex), toJolt(impulse));
}

void Physics::addRagdollVelocity(RagdollHandle handle, glm::vec3 deltaVelocity) {
    JPH::Ragdoll* ragdoll = ragdolls_ ? ragdolls_->find(handle) : nullptr;
    if (ragdoll == nullptr) return;
    ragdoll->AddLinearVelocity(toJolt(deltaVelocity));
    ragdoll->Activate();
}

void Physics::setRagdollJointLimp(RagdollHandle handle, int partIndex) {
    if (!ragdolls_) return;
    auto it = ragdolls_->entries.find(handle);
    if (it == ragdolls_->entries.end()) return;
    const JPH::RagdollSettings& settings = *it->second.settings;
    if (partIndex < 0 || partIndex >= static_cast<int>(settings.mParts.size())) return;
    int constraintIndex = settings.GetConstraintIndexForBodyIndex(partIndex);
    if (constraintIndex < 0) return;

    JPH::TwoBodyConstraint* constraint = it->second.ragdoll->GetConstraint(constraintIndex);
    constexpr float kLimp = 0.92f * JPH::JPH_PI;
    if (constraint->GetSubType() == JPH::EConstraintSubType::Hinge) {
        auto* hinge = static_cast<JPH::HingeConstraint*>(constraint);
        hinge->SetLimits(-kLimp, kLimp);
        hinge->SetMaxFrictionTorque(0.1f);
    } else if (constraint->GetSubType() == JPH::EConstraintSubType::SwingTwist) {
        auto* swingTwist = static_cast<JPH::SwingTwistConstraint*>(constraint);
        swingTwist->SetNormalHalfConeAngle(kLimp);
        swingTwist->SetPlaneHalfConeAngle(kLimp);
        swingTwist->SetTwistMinAngle(-kLimp);
        swingTwist->SetTwistMaxAngle(kLimp);
        swingTwist->SetMaxFrictionTorque(0.1f);
    }
    it->second.ragdoll->Activate();
}

} // namespace engine::core
