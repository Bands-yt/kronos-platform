#pragma once

#include "core/Components.hpp"
#include "net/NetTypes.hpp"

namespace engine::net {

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
void applyNetworkedMovement(core::Transform& transform, const InputCommand& command, float moveSpeed);

} // namespace engine::net
