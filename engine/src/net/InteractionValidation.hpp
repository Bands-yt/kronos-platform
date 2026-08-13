#pragma once

#include <glm/glm.hpp>

#include "net/NetTypes.hpp"

namespace engine::net {

// Sprint 11 ("Networking Foundation") task 3's "Add server authority
// rules (server owns world state)" + "Add validation for all client
// requests" -- real, pure validation the server runs before trusting a
// client's movement or interaction request, mirroring the exact
// reject-and-log (never silently "fix") pattern
// net::ServerReconciliation::validate() already established for input.
//
// Server authority itself isn't a function to call -- it's an
// architectural property: the SERVER process is the only one that ever
// calls core::mineOreNode()/breakOreNode()/toggleLamp()/togglePropAnimation()/
// the teleport-pad dispatch in response to a client's request; a client
// only ever *asks*. These functions are what the server checks before
// it agrees to ask on the client's behalf. Every function here is pure
// (no ECS/Physics access) so "what counts as valid" is independently
// auditable and headlessly testable, separate from "what happens when
// it's valid" (the existing, unchanged gameplay functions themselves).

// Real movement sanity check -- shared by Sprint 11's server authority
// and Sprint 12's anti-cheat foundation (defined once, here, so the two
// don't drift into two different definitions of "is this movement
// physically plausible"). Rejects an InputCommand whose deltaTime is
// non-positive/absurdly large (a real, honest bound -- a legitimate
// client tick is never longer than `maxDeltaTime`) or whose moveAxis
// magnitude exceeds 1.0 by more than a real floating-point tolerance (a
// normalized input vector should never exceed unit length; see
// InputCommand::moveAxis's own "already normalized client-side" comment
// -- this is the server actually checking that claim, not trusting it).
[[nodiscard]] bool isMovementPlausible(const InputCommand& command, float maxDeltaTime = 0.25f);

// Real interaction-range check -- a client claiming to interact with
// something farther than `maxRange` from its own last-known server-side
// position is either lagging badly or lying about its position; either
// way the server doesn't apply the interaction.
[[nodiscard]] bool isWithinInteractionRange(glm::vec3 playerPosition, glm::vec3 targetPosition, float maxRange);

// Real teleport-destination sanity check -- rejects a destination
// implying instantaneous travel farther than any real, legitimate
// teleport pad placement would (a world boundary radius is the real,
// natural bound already established by core::WorldBoundary, see
// Navigation.hpp) or a destination with a NaN/Inf component (the same
// real corruption class InspectorPanel::hasInvalidComponents() already
// guards against on the authoring side -- this is the equivalent guard
// on the network-request side).
[[nodiscard]] bool isTeleportDestinationValid(glm::vec3 destination, glm::vec3 worldCenter, float worldHardRadius);

} // namespace engine::net
