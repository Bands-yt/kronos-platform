#pragma once

#include <cstdint>
#include <string>

#include "core/ECS.hpp"

// Roblox collision groups (PhysicsService): up to 32 named groups; "Default" is group 0 and
// always exists. Every pair collides until a script says otherwise.
namespace engine::core::collisiongroups {

constexpr int kMaxGroups = 32;

// Returns false (and sets `error`) for an empty name, a duplicate or a full table.
bool registerGroup(ECS& ecs, const std::string& name, std::string& error);
bool unregisterGroup(ECS& ecs, const std::string& name, std::string& error);
bool renameGroup(ECS& ecs, const std::string& from, const std::string& to, std::string& error);
// -1 when the name isn't registered.
[[nodiscard]] int indexOf(ECS& ecs, const std::string& name);
[[nodiscard]] std::string nameOf(ECS& ecs, int index);
bool setCollidable(ECS& ecs, const std::string& a, const std::string& b, bool collidable, std::string& error);
[[nodiscard]] bool collidable(ECS& ecs, int a, int b);

// Row i has bit j set when groups i and j collide. `version` changes on every edit.
[[nodiscard]] const uint32_t* rows(ECS& ecs);
[[nodiscard]] uint32_t version(ECS& ecs);
// Forgets every group (Play start and stop).
void reset(ECS& ecs);
// Registered groups in index order, for GetRegisteredCollisionGroups.
[[nodiscard]] int groupCount(ECS& ecs);

} // namespace engine::core::collisiongroups
