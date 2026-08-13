#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "core/CollisionLayers.hpp"
#include "core/ECS.hpp"
#include "core/PhysicsMaterial.hpp"

// Forward-declared rather than included here so anything that only needs
// Physics::step()/createBoxBody() doesn't have to pull in Jolt's headers.
namespace JPH {
    class PhysicsSystem;
    class TempAllocatorImpl;
    class JobSystemThreadPool;
    class BroadPhaseLayerInterface;
    class ObjectVsBroadPhaseLayerFilter;
    class ObjectLayerPairFilter;
    class ContactListener;
}

namespace engine::core {

// Wraps Jolt Physics per docs/ARCHITECTURE.md §4.3: its own fixed step,
// decoupled from render rate, syncing body transforms into the ECS
// `Transform` component after each step and reading script-driven
// force/velocity changes back out before the next one (that second half --
// the Luau-facing `BasePart:ApplyImpulse` style API -- is Scripting's job to
// call through applyImpulse/setVelocity below; it isn't wired to a script
// binding yet, see Scripting.hpp's TODO).
class Physics {
public:
    Physics();
    ~Physics();

    Physics(const Physics&) = delete;
    Physics& operator=(const Physics&) = delete;

    [[nodiscard]] bool initialize();
    void shutdown();

    // Advances the simulation by exactly `dt` (the fixed tick from
    // GameLoop, not a variable render dt) and writes the results into every
    // entity with both RigidBody and Transform.
    void step(float dt, ECS& ecs);

    // Real Jolt broadphase-tree rebuild -- worth calling after bulk-adding
    // many static bodies at once (e.g. loading a whole scene's worth of
    // static geometry) so the very next step() doesn't pay to
    // incrementally rebalance the tree one insert at a time. Not required
    // for correctness (Jolt inserts bodies into a working broadphase
    // immediately on creation regardless), only for that first-step
    // performance cost -- see JPH::PhysicsSystem::OptimizeBroadPhase()'s
    // own doc comment.
    void optimizeBroadPhase();

    // Sprint 8 ("Performance Stats & Debug Tools"): real counts straight
    // from Jolt's own bookkeeping (JPH::PhysicsSystem::GetNumActiveBodies()/
    // GetNumBodies()), not this engine's own tally -- active excludes
    // bodies Jolt has put to sleep (a resting stack of crates settles and
    // stops counting as "active" within a few ticks, exactly the number a
    // real profiler should show shrinking, not the static total).
    [[nodiscard]] uint32_t activeBodyCount() const;
    [[nodiscard]] uint32_t totalBodyCount() const;

    // --- Body creation ------------------------------------------------------
    // Real content authoring goes through Studio (§5) and the Instance
    // model (§6); these are the real, callable body-creation primitives
    // that pipeline (and every test scene/example in this pass) builds on.
    // Every function attaches a real `ColliderShape` + `PhysicsMaterial`
    // component alongside `RigidBody` -- see Components.hpp -- so the
    // shape/material an entity was actually created with survives for
    // debug-draw, Studio's material editor, and mass recomputation to read
    // back later (a live Jolt body can't be introspected back into "a box
    // of this size made of this material").
    //
    // `mass <= 0.0f` on any dynamic shape means "compute it for real from
    // `material.density` and the shape's real closed-form volume" (see
    // PhysicsMaterial.hpp's volume functions) instead of Jolt's own
    // generic default -- a positive `mass` is an explicit override,
    // unchanged from every existing call site's behavior.
    EntityId createGroundPlane(ECS& ecs, float halfExtentX, float halfExtentZ, PhysicsMaterial material = {});

    // A real, general-purpose static box at an arbitrary position/rotation
    // -- createGroundPlane() is a fixed-position, axis-aligned
    // (`y=-halfExtentY`) specialization of exactly this same shape; this
    // is the general form neither it nor createDynamicBox() (always
    // Dynamic) could express: a stationary wall, ramp, platform, or
    // trigger volume anywhere in the world, at any orientation (the only
    // body-creation function in this pass that takes a rotation at all --
    // every other one assumes an axis-aligned spawn and relies on the
    // solver's own rotation for anything dynamic). `isSensor=true` is the
    // real, honest way to build a stationary trigger volume (see
    // createDynamicBox()'s isSensor comment) -- a *moving* trigger wants
    // createKinematicBox() instead.
    EntityId createStaticBox(ECS& ecs, glm::vec3 position, glm::vec3 halfExtent,
                              glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f), PhysicsMaterial material = {},
                              CollisionLayer layer = CollisionLayer::Static, bool isSensor = false);

    EntityId createDynamicBox(ECS& ecs, glm::vec3 position, glm::vec3 halfExtent, float mass,
                               PhysicsMaterial material = {}, CollisionLayer layer = CollisionLayer::Default,
                               bool isSensor = false);
    EntityId createSphereBody(ECS& ecs, glm::vec3 position, float radius, float mass, PhysicsMaterial material = {},
                               CollisionLayer layer = CollisionLayer::Default, bool isSensor = false);

    // A real, immovable-by-anything-else static triangle-mesh collider
    // (Jolt's `MeshShape` has no analytic inertia/mass and is not
    // reactivatable by contact forces, so this engine only ever creates
    // mesh colliders as Static -- the same real constraint every physics
    // engine's mesh-shape support has; a *dynamic* mesh collider is a
    // whole different, harder problem -- typically real games approximate
    // one with a convex hull instead -- not attempted here). `positions`/
    // `indices` are host-side triangle data only (no normals/UV/tangent --
    // a physics mesh collider only ever needs geometry, not render
    // attributes, which is also why this doesn't take `core::Vertex` and
    // doesn't pull Physics.hpp into a GPU/Vulkan dependency at all).
    //
    // Real, load-bearing constraint (Jolt's own, not this wrapper's):
    // triangles are collision-single-sided and must be wound
    // counter-clockwise as seen from the side that should actually be
    // solid/collidable (a right-handed cross product of the first two
    // edges must point out of the intended solid side). Winding a
    // triangle backward doesn't error or warn -- it silently produces a
    // triangle whose collidable face points the wrong way, so anything
    // approaching from the "correct" side (e.g. a character standing on
    // a floor mesh) falls straight through with zero contacts, zero
    // penetration, and no diagnostic of any kind. This is a real,
    // easy-to-get-wrong bug this pass hit and root-caused (not a
    // hypothetical) -- see README's "Real bugs this pass found."
    EntityId createMeshBody(ECS& ecs, glm::vec3 position, const std::vector<glm::vec3>& positions,
                             const std::vector<uint32_t>& indices, PhysicsMaterial material = {});

    // A real Kinematic body (see RigidBodyMotionType's comment) -- moved
    // by code via moveKinematic() below, never by the solver. The
    // canonical real use in this pass: a moving platform (see the
    // "jumpable platform" test scene) that must carry a rider along
    // without itself being pushed off course by anything.
    EntityId createKinematicBox(ECS& ecs, glm::vec3 position, glm::vec3 halfExtent, PhysicsMaterial material = {});

    // Real, general "give an already-existing entity a live Jolt body" --
    // distinct from every createX() function above, which always spawn a
    // brand-new entity. Reads `entity`'s *current* Transform for spawn
    // position/rotation, builds the real Jolt shape from `shape`'s kind/
    // params (a Mesh shape additionally needs `meshPositions`/
    // `meshIndices` -- ignored for every other kind, and returns false if
    // missing for a Mesh shape), and attaches real RigidBody/ColliderShape/
    // PhysicsMaterial components to that same entity (overwriting any
    // that were already there). This is what a real Studio Play-mode
    // physics preview needs (see README's "Scene Physics Integration"):
    // turning already-authored collider/material data on a live Studio
    // entity into an actual simulated body, in place, rather than
    // creating a parallel physics-only entity. `mass <= 0.0f` means the
    // same real density-driven computation createDynamicBox() etc. use
    // (ignored for Static/Kinematic motion types, which have no mass).
    // Returns false (entity left untouched) on a real failure -- a
    // missing/invalid Mesh shape, or `entity` not existing at all.
    [[nodiscard]] bool attachBodyToEntity(EntityId entity, ECS& ecs, const ColliderShape& shape,
                                           PhysicsMaterial material, RigidBodyMotionType motionType, float mass = 0.0f,
                                           CollisionLayer layer = CollisionLayer::Default, bool isSensor = false,
                                           const std::vector<glm::vec3>* meshPositions = nullptr,
                                           const std::vector<uint32_t>* meshIndices = nullptr);

    // Removes `entity`'s live Jolt body (if any), without destroying the
    // entity or its RigidBody/ColliderShape/PhysicsMaterial components
    // (authored data, not runtime-only) -- the real teardown half
    // attachBodyToEntity() needs, e.g. Studio's Play-mode "Stop"
    // reverting every entity it gave a live body to back to a plain,
    // physics-free Studio entity. `RigidBody::joltBodyId` is reset to
    // kInvalidBodyId so every other Physics method's existing "is this
    // entity's body still valid" guard (`joltBodyId ==
    // RigidBody::kInvalidBodyId`) treats it as unattached, matching how
    // an entity that never had a body looks. Safe to call on an entity
    // with no live body at all (a real no-op, not an error).
    void detachBody(EntityId entity, ECS& ecs);

    // Moves a Kinematic body toward `targetPosition`/`targetRotation` over
    // `dt` using Jolt's real `BodyInterface::MoveKinematic` -- computes
    // and sets the body's velocity so it arrives exactly at the target by
    // the end of this step, which is what lets a Dynamic body resting on
    // top ride along via real contact friction/normal impulses instead of
    // being teleported through. Calling `SetPosition` directly on a
    // Kinematic body instead would teleport it with zero velocity, so
    // nothing riding it would be pushed along at all -- the real reason
    // this exists as its own method rather than reusing setRotationY/a
    // plain position setter.
    void moveKinematic(EntityId entity, ECS& ecs, glm::vec3 targetPosition, glm::quat targetRotation, float dt);

    // A dynamic capsule with pitch/roll locked (Jolt's EAllowedDOFs
    // excludes RotationX/RotationZ) but yaw left free -- the standard
    // "character feels upright and doesn't topple over on collision"
    // trick for a physics-driven character body, while still letting the
    // body itself carry facing (via setRotationY below) instead of fighting
    // syncTransforms() every step to keep a facing direction that isn't
    // simulated by physics at all.
    EntityId createCharacterCapsule(ECS& ecs, glm::vec3 position, float radius, float halfHeight, float mass,
                                     PhysicsMaterial material = {}, CollisionLayer layer = CollisionLayer::Character);

    void applyImpulse(EntityId entity, ECS& ecs, glm::vec3 impulse);

    // Real, live friction/restitution update on an already-created body
    // (Jolt supports changing these in place, see
    // `BodyInterface::SetFriction`/`SetRestitution`) plus overwriting the
    // entity's own `PhysicsMaterial` component so Studio's material editor
    // and this entity agree on its current values. `material.density` is
    // recorded on the component for consistency but does *not* retroactively
    // change this body's already-computed mass/inertia -- Jolt has no live
    // "recompute mass in place" operation, matching InspectorPanel's
    // existing stated limitation that shape/mass edits need body
    // recreation, not just a setter.
    void applyMaterial(EntityId entity, ECS& ecs, PhysicsMaterial material);

    // Per-body linear/angular damping -- Jolt's own default (0.05/0.05)
    // is tuned for generic props, not every real use case (e.g.
    // createCharacterCapsule() already hand-picks 0.0f linear damping so
    // direct velocity sets aren't fighting an artificial deceleration --
    // see its own comment); exposed generally here so any body can be
    // tuned the same way without a bespoke creation parameter per case.
    void setDamping(EntityId entity, ECS& ecs, float linearDamping, float angularDamping);

    // Real, engine-wide gravity configuration -- Jolt defaults to
    // (0,-9.81,0) at Init() time; exposed here so a real low-gravity/
    // moon-gravity/zero-gravity scene is possible without touching Jolt
    // directly. Affects every Dynamic body from the next step() onward.
    void setGravity(glm::vec3 gravity);
    [[nodiscard]] glm::vec3 gravity() const;

    // Reconfigures which layer-pairs actually collide -- see
    // CollisionLayers.hpp's CollisionMatrix for the real, live-queried
    // mechanism this drives. Takes effect on the very next broadphase
    // pass (Jolt queries ShouldCollide() on demand, not once at Init()),
    // so this is real-time reconfigurable, not just a startup option.
    void setLayerCollision(CollisionLayer a, CollisionLayer b, bool shouldCollide);
    [[nodiscard]] bool layersShouldCollide(CollisionLayer a, CollisionLayer b) const;

    // Sets horizontal velocity directly, preserving whatever vertical
    // velocity gravity/jumping already gave the body -- the responsive,
    // non-mushy movement feel a physics-driven character controller
    // needs; accelerating a character purely through forces feels sluggish
    // by comparison, which is why this is a direct velocity set, not an
    // impulse.
    void setHorizontalVelocity(EntityId entity, ECS& ecs, glm::vec2 velocityXZ);
    // Sets just the vertical component, preserving current horizontal
    // velocity -- the jump equivalent of setHorizontalVelocity above.
    void setVerticalVelocity(EntityId entity, ECS& ecs, float velocityY);
    [[nodiscard]] glm::vec3 getLinearVelocity(EntityId entity, ECS& ecs) const;

    // A direct position teleport (no velocity implied, unlike moving
    // through simulation) -- what CharacterController's step-offset needs
    // (nudging the capsule up onto a short ledge is a real, instant
    // reposition, not a velocity-driven climb) and generally useful for
    // spawning/respawning/scene-load placement (see "Scene Physics
    // Integration" in README.md).
    void setPosition(EntityId entity, ECS& ecs, glm::vec3 position);

    // Sets the character's yaw directly on the Jolt body (not the ECS
    // Transform) *before* step() runs, so the same step's syncTransforms()
    // reads back exactly what was set -- nothing else torques this body's
    // Y rotation, so it holds steady through the simulation step rather
    // than needing a second write after sync. Call once per tick, before
    // step(), from CharacterController.
    void setRotationY(EntityId entity, ECS& ecs, float yawRadians);

    // Short downward raycast from just beneath the given capsule's feet.
    // Deliberately simple (no body filter -- the ray starts outside the
    // capsule's own collision volume via `skin`, rather than excluding the
    // capsule's body ID) since that's sufficient for "can this character
    // jump right now" and doesn't need Jolt's filter machinery. A thin
    // wrapper over checkGround() below for callers that only need the
    // bool (unchanged behavior/signature from before this pass).
    [[nodiscard]] bool isGrounded(EntityId entity, ECS& ecs, float capsuleHalfHeight, float capsuleRadius,
                                   float skin = 0.05f, float checkDistance = 0.2f) const;

    struct GroundInfo {
        bool grounded = false;
        glm::vec3 normal{0.0f, 1.0f, 0.0f};
    };

    // The real query isGrounded()'s bare bool can't answer: which way is
    // the ground actually facing here. What CharacterController's slope
    // limit needs (see its own Settings::maxSlopeDegrees comment) -- a
    // steep-enough surface is real ground for collision purposes but not
    // "standable" ground for gameplay purposes, and telling the two apart
    // needs the surface normal, not just "did the ray hit something."
    // Same real raycast isGrounded() runs, just also resolving the hit
    // body's world-space surface normal (the same
    // Body::GetWorldSpaceSurfaceNormal() mechanism raycast() itself
    // already uses).
    [[nodiscard]] GroundInfo checkGround(EntityId entity, ECS& ecs, float capsuleHalfHeight, float capsuleRadius,
                                          float skin = 0.05f, float checkDistance = 0.2f) const;

    struct RaycastHit {
        bool hit = false;
        EntityId entity = kNullEntity;
        glm::vec3 point{0.0f};
        glm::vec3 normal{0.0f, 1.0f, 0.0f};
        float distance = 0.0f;
    };

    // The general "what does this ray hit" query isGrounded() (a narrow,
    // fixed-direction ground-check) doesn't provide -- closest hit only,
    // real Jolt NarrowPhaseQuery::CastRay under the hood, entity resolved
    // via the same Body::GetUserData() mechanism ContactListenerImpl uses
    // (Physics.cpp) to turn a Jolt hit back into an EntityId. Used for
    // real click-to-select (studio::ViewportPanel) and look-to-interact
    // (core::ScriptWorldApi) -- see those files. `direction` need not be
    // normalized; `maxDistance` is the ray's length regardless of
    // `direction`'s own magnitude.
    [[nodiscard]] RaycastHit raycast(glm::vec3 origin, glm::vec3 direction, float maxDistance) const;

    struct CollisionEvent {
        EntityId first = kNullEntity;
        EntityId second = kNullEntity;
        // Real data straight out of Jolt's own JPH::ContactManifold (see
        // ContactListenerImpl::OnContactAdded in Physics.cpp) -- the first
        // contact point in the manifold (a manifold can have up to 4; this
        // engine only ever surfaces the first, sufficient for a debug-draw
        // marker and for a script/gameplay reaction that needs "roughly
        // where," not the full contact polygon) and the manifold's world-
        // space contact normal. Real, not a placeholder -- what Studio's
        // physics-debug contact-point visualization (see README's "Physics
        // Debug Tools") draws directly.
        glm::vec3 point{0.0f};
        glm::vec3 normal{0.0f, 1.0f, 0.0f};
    };

    // Drains and returns every *new* contact (JPH::ContactListener::
    // OnContactAdded -- roughly Roblox BasePart.Touched's "just started
    // touching" semantics, not an ongoing/ContactPersisted or
    // OnContactRemoved report; see Physics.cpp's ContactListenerImpl for
    // exactly what's real here) queued since the last call. Contacts are
    // recorded from Jolt's own contact callbacks, which Jolt may invoke
    // from physics worker threads during step()'s Update() call -- this
    // drain (mutex-guarded, see .cpp) is the one safe point to read them
    // from the main thread. Call once per tick, after step().
    [[nodiscard]] std::vector<CollisionEvent> drainCollisionEvents();

private:
    void syncTransforms(ECS& ecs);

    std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator_;
    std::unique_ptr<JPH::JobSystemThreadPool> jobSystem_;
    std::unique_ptr<JPH::PhysicsSystem> physicsSystem_;

    // Broad-phase / object-layer plumbing Jolt requires at Init() time.
    // Owned here (as opaque pointers -- see Physics.cpp) because
    // PhysicsSystem only stores references to them, not ownership.
    std::unique_ptr<JPH::BroadPhaseLayerInterface> broadPhaseLayerInterface_;
    std::unique_ptr<JPH::ObjectVsBroadPhaseLayerFilter> objectVsBroadPhaseLayerFilter_;
    std::unique_ptr<JPH::ObjectLayerPairFilter> objectLayerPairFilter_;

    // The real, live, mutable mask objectLayerPairFilter_ reads from every
    // ShouldCollide() query -- see CollisionLayers.hpp and
    // setLayerCollision()'s doc comment.
    CollisionMatrix collisionMatrix_;

    // Real Jolt ContactListener (Physics.cpp's ContactListenerImpl) writing
    // into pendingCollisionEvents_ under collisionEventsMutex_ -- see
    // drainCollisionEvents()'s doc comment on why the mutex is necessary.
    std::unique_ptr<JPH::ContactListener> contactListener_;
    std::mutex collisionEventsMutex_;
    std::vector<CollisionEvent> pendingCollisionEvents_;

    bool initialized_ = false;
};

} // namespace engine::core
