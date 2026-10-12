#pragma once

#include <vector>

#include "core/ECS.hpp"
#include "core/InstanceTree.hpp"

// Roblox Tools: a Tool is equipped while its Parent is a character. Equipping
// welds its Handle to the character ("RightGrip") and fires Equipped.
namespace engine::core::tools {

// From instances::setParent when a Tool moves.
void parentChanged(ECS& ecs, InstanceRef tool, InstanceRef oldParent, InstanceRef newParent);

// Tool:Activate()/Deactivate(): Activated/Deactivated, if the tool is Enabled and equipped.
void activate(ECS& ecs, InstanceRef tool);
void deactivate(ECS& ecs, InstanceRef tool);

// Humanoid:EquipTool(tool) / UnequipTools(): other tools go back to the Backpack.
void equip(ECS& ecs, EntityId humanoid, InstanceRef tool);
void unequipAll(ECS& ecs, EntityId humanoid);

// The Tool in this character, or kNoInstance.
[[nodiscard]] InstanceRef equippedTool(ECS& ecs, InstanceRef character);
// True when `model` holds a Humanoid.
[[nodiscard]] bool isCharacter(ECS& ecs, InstanceRef model);

// A Tool's Handle lying in the world is picked up (equipped) by the first
// living character that touches it. From signals::touch.
void touched(ECS& ecs, EntityId a, EntityId b);

// The local player's hotbar: Backpack tools and the equipped one, in the
// order they arrived (slot 1 first).
[[nodiscard]] std::vector<InstanceRef> hotbar(ECS& ecs);
struct HotbarInput {
    int slotPressed = 0;        // 1-9, 0 for none
    bool clickPressed = false;  // in the 3D view, not on GUI
    bool clickReleased = false;
};
// A slot key equips that tool (or puts it away when it's already held);
// a click activates the held tool and the release deactivates it.
void updateHotbar(ECS& ecs, const HotbarInput& input);

} // namespace engine::core::tools
