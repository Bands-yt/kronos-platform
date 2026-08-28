#pragma once

#include "core/Components.hpp"
#include "core/Physics.hpp"
#include "net/NetTypes.hpp"

namespace engine::net {

// Persistent per-entity vertical-motion state for applyNetworkedMovement()
// below -- real gravity/ground-relative state, not something that fits in
// the per-tick InputCommand or the position-only Transform. One of these
// per networked player entity (client's own predicted entity, and each
// player's own entity server-side).
struct NetworkedVerticalMotion {
    float velocityY = 0.0f;
    bool grounded = true;
};

// Kronos (beta-blocking fix -- "collides but i cant exactly move objects...
// glued with the object"): real, optional out-param filled in when the
// horizontal blocker probe below is actually blocked by something --
// `entity` is whichever body the most-restrictive ray hit, `direction` is
// the player's own intended move direction at the moment of contact, and
// `strength` is how much horizontal distance was blocked (a harder push
// against the object -> a bigger strength). Deliberately NOT applied
// inside applyNetworkedMovement() itself: this function stays ECS-free and
// Physics-const (see its own class comment on why prediction/
// reconciliation share it verbatim), but pushing a body is a real,
// mutating Physics call that also needs the ECS to check the body's own
// RigidBodyMotionType (only Dynamic bodies should ever move -- pushing a
// Static or Kinematic one is meaningless/unsafe) -- both of which only
// NetworkSession's own call sites actually have. Left at its default
// (entity == kNullEntity) whenever nothing was blocked this tick.
struct NetworkedMovementPush {
    core::EntityId entity = core::kNullEntity;
    glm::vec3 direction{0.0f};
    float strength = 0.0f;
};

// Sprint 11 ("Networking Foundation") task 2's real, deliberately simple
// kinematic movement model shared *identically* by client-side
// prediction (net::ClientPrediction's predictedApply) and server-side
// authoritative apply (net::ServerReconciliation's apply) -- using the
// exact same function on both sides is what makes prediction and
// reconciliation actually agree; two similar-but-different
// implementations would silently diverge and constantly "fight" every
// reconcile.
//
// Deliberately NOT a replay of the full Jolt-physics-capsule
// core::CharacterController: that class integrates against a live Jolt
// body via real physics steps, and Jolt has no cheap "replay N recorded
// steps instantly" primitive the way this simpler kinematic integration
// does -- full physics-based prediction replay is real, separate, harder
// work, a stated scope limit for this pass (see NetworkSession's own
// class comment), not an oversight. core::CharacterController itself is
// completely unchanged and remains the real, full-featured single-
// player/offline movement model; this is the parallel, simpler model
// networked play uses instead.
//
// `command.moveAxis` is local-space (x = right/left, z = forward/back,
// already normalized client-side, see InputCommand's own comment);
// `command.yaw` (degrees) rotates it into world space using the exact
// same yaw convention core::Camera::forward() already uses (yaw=0 faces
// +X), so a future unification has one consistent convention to build on
// rather than two to reconcile.
void applyNetworkedMovement(core::Transform& transform, NetworkedVerticalMotion& vertical, const core::Physics& physics,
                             const InputCommand& command, float moveSpeed, NetworkedMovementPush* outPush = nullptr);

} // namespace engine::net
