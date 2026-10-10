#pragma once

#include "core/ECS.hpp"

// Keeps Roblox parts' physics bodies in step with the Instance tree while a
// game runs: a part made or moved into the workspace gets a body, one that
// leaves loses it, and a new Size, Shape, Anchored or CanCollide rebuilds it.
namespace engine::core {
class Physics;

namespace partbodies {

// serverMoved: a client copy whose parts the server moves; unanchored parts
// are kinematic there instead of simulated twice.
void sync(ECS& ecs, Physics& physics, bool serverMoved);
// Removes every body sync() made or adopted.
void detachAll(ECS& ecs, Physics& physics);

} // namespace partbodies
} // namespace engine::core
