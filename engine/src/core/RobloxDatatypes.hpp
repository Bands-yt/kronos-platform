#pragma once

#include <string>
#include <utility>
#include <vector>

struct lua_State;

namespace engine::core {

// Installs Roblox's value types as globals: Vector3, Vector2, CFrame, Color3,
// BrickColor, UDim, UDim2, Enum, TweenInfo, NumberRange, NumberSequence(Keypoint),
// ColorSequence(Keypoint), Ray, RaycastParams, Random, and a typeof that names them.
// Returns false (and installs nothing) if the embedded Luau failed to load.
bool registerRobloxDatatypes(lua_State* L);

// Items of a Roblox enum Kronos knows (name, value), in declaration order; empty if unknown.
[[nodiscard]] std::vector<std::pair<std::string, int>> robloxEnumItems(const std::string& enumType);

} // namespace engine::core
