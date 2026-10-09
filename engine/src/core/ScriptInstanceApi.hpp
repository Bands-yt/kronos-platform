#pragma once

#include <cstdint>

struct lua_State;

namespace engine::core {

class ECS;
struct SignalArg;

// Installs Roblox's Instance tree into a script VM: game, workspace,
// Instance.new and the Instance members (core/InstanceTree.hpp). Needs the
// Roblox datatypes (core/RobloxDatatypes.hpp) registered first.
void registerInstanceApi(lua_State* L, ECS& ecs);

// Pushes the Instance for `entity` (the `script` global). Returns false,
// pushing nothing, when the VM has no Instance API or the entity is gone.
bool pushScriptInstance(lua_State* L, uint32_t entity);

// Signal handler arguments (core/InstanceSignals.hpp).
void pushSignalArg(lua_State* L, const SignalArg& arg);
SignalArg toSignalArg(lua_State* L, int index);

} // namespace engine::core
