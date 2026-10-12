#pragma once

#include "core/ECS.hpp"

struct ImDrawList;

namespace engine::core {

// Roblox's player list with leaderstats columns, drawn in the top-right
// corner whose right edge is `right` and top is `top`. Draws nothing when
// no player has a "leaderstats" folder (core/RobloxPlayers.hpp).
void drawLeaderboard(ECS& ecs, ImDrawList* drawList, float right, float top);

// Roblox's tool hotbar along the bottom centre of a view (core/RobloxTools.hpp).
// Draws nothing when the local player has no tools.
void drawToolHotbar(ECS& ecs, ImDrawList* drawList, float centerX, float bottom);

} // namespace engine::core
