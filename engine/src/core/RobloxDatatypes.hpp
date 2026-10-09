#pragma once

struct lua_State;

namespace engine::core {

// Installs Roblox's value types as globals: Vector3, Vector2, CFrame, Color3,
// BrickColor, UDim, UDim2, Enum, TweenInfo, NumberRange, NumberSequence(Keypoint),
// ColorSequence(Keypoint), Ray, RaycastParams, Random, and a typeof that names them.
// Returns false (and installs nothing) if the embedded Luau failed to load.
bool registerRobloxDatatypes(lua_State* L);

} // namespace engine::core
