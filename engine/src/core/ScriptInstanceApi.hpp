#pragma once

#include <cstdint>

struct lua_State;

namespace engine::core {

class ECS;

// Installs Roblox's Instance tree into a script VM: game, workspace,
// Instance.new and the Instance members (core/InstanceTree.hpp). Needs the
// Roblox datatypes (core/RobloxDatatypes.hpp) registered first.
void registerInstanceApi(lua_State* L, ECS& ecs);

// Pushes the Instance for `entity` (the `script` global). Returns false,
// pushing nothing, when the VM has no Instance API or the entity is gone.
bool pushScriptInstance(lua_State* L, uint32_t entity);

} // namespace engine::core
