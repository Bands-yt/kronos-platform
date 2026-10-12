#include "core/RobloxTools.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include <glm/gtc/quaternion.hpp>

#include "core/InstanceSignals.hpp"
#include "core/RobloxPlayers.hpp"

namespace engine::core::tools {
namespace {

constexpr const char* kGripName = "RightGrip";

void fire(ECS& ecs, InstanceRef ref, const char* event, std::vector<SignalArg> args = {}) {
    if (SignalHub* hub = signals::findHub(ecs)) hub->fire(ref, event, std::move(args));
}

InstanceValue get(ECS& ecs, InstanceRef ref, const char* property) {
    InstanceValue value;
    const PropertyDef* def = instances::findProperty(instances::className(ecs, ref), property);
    if (def != nullptr) (void)instances::getProperty(ecs, ref, *def, value);
    return value;
}

void set(ECS& ecs, InstanceRef ref, const char* property, const InstanceValue& value) {
    const PropertyDef* def = instances::findProperty(instances::className(ecs, ref), property);
    if (def != nullptr) instances::setProperty(ecs, ref, *def, value);
}

InstanceRef childOfClass(ECS& ecs, InstanceRef parent, const std::string& cls) {
    for (InstanceRef child : instances::children(ecs, parent)) {
        if (instances::classIsA(instances::className(ecs, child), cls)) return child;
    }
    return kNoInstance;
}

InstanceRef handleOf(ECS& ecs, InstanceRef tool) {
    for (InstanceRef child : instances::children(ecs, tool)) {
        if (instances::name(ecs, child) == "Handle" && instances::classIsA(instances::className(ecs, child), "BasePart")) {
            return child;
        }
    }
    return kNoInstance;
}

InstanceRef backpackFor(ECS& ecs, InstanceRef character) {
    const InstanceRef player = players::playerFromCharacter(ecs, character);
    return player == kNoInstance ? kNoInstance : childOfClass(ecs, player, "Backpack");
}

void removeGrips(ECS& ecs, InstanceRef handle) {
    std::vector<InstanceRef> grips;
    for (auto [entity, info] : ecs.raw().view<InstanceInfo>().each()) {
        if (info.className != "Weld") continue;
        const InstanceRef weld = instances::refOf(ecs, entity);
        if (instances::name(ecs, weld) == kGripName && get(ecs, weld, "Part1").ref == handle) grips.push_back(weld);
    }
    for (InstanceRef weld : grips) instances::destroy(ecs, weld);
}

void addGrip(ECS& ecs, InstanceRef tool, InstanceRef character) {
    const InstanceRef handle = handleOf(ecs, tool);
    const InstanceRef humanoid = childOfClass(ecs, character, "Humanoid");
    const InstanceRef root = humanoid == kNoInstance ? kNoInstance : get(ecs, humanoid, "RootPart").ref;
    if (handle == kNoInstance || root == kNoInstance || !instances::isAlive(ecs, root)) return;
    removeGrips(ecs, handle);
    std::string error;
    const InstanceRef weld = instances::createUnchecked(ecs, "Weld");
    instances::setName(ecs, weld, kGripName);
    // The right hand, holding the handle pointing forward.
    const glm::quat forward = glm::angleAxis(-glm::half_pi<float>(), glm::vec3(1.0f, 0.0f, 0.0f));
    set(ecs, weld, "C0", InstanceValue::ofCFrame(glm::vec3(1.2f, 0.0f, -0.8f), forward));
    const InstanceValue grip = get(ecs, tool, "Grip");
    set(ecs, weld, "C1", grip.type == InstanceValue::Type::CFrame ? grip : InstanceValue::ofCFrame(glm::vec3(0.0f), glm::quat(1, 0, 0, 0)));
    set(ecs, weld, "Part0", InstanceValue::ofInstance(root));
    set(ecs, weld, "Part1", InstanceValue::ofInstance(handle));
    (void)instances::setParent(ecs, weld, root, error);
}

} // namespace

bool isCharacter(ECS& ecs, InstanceRef model) {
    return model != kNoInstance && instances::isAlive(ecs, model) && childOfClass(ecs, model, "Humanoid") != kNoInstance;
}

InstanceRef equippedTool(ECS& ecs, InstanceRef character) {
    return isCharacter(ecs, character) ? childOfClass(ecs, character, "Tool") : kNoInstance;
}

void parentChanged(ECS& ecs, InstanceRef tool, InstanceRef oldParent, InstanceRef newParent) {
    const bool was = isCharacter(ecs, oldParent);
    const bool now = isCharacter(ecs, newParent);
    if (was) {
        if (const InstanceRef handle = handleOf(ecs, tool); handle != kNoInstance) removeGrips(ecs, handle);
        fire(ecs, tool, "Unequipped");
    }
    if (now) {
        addGrip(ecs, tool, newParent);
        fire(ecs, tool, "Equipped", {SignalArg::of(InstanceValue{})});
    }
}

void activate(ECS& ecs, InstanceRef tool) {
    if (!get(ecs, tool, "Enabled").boolean || !isCharacter(ecs, instances::parent(ecs, tool))) return;
    fire(ecs, tool, "Activated");
}

void deactivate(ECS& ecs, InstanceRef tool) {
    if (!isCharacter(ecs, instances::parent(ecs, tool))) return;
    fire(ecs, tool, "Deactivated");
}

void unequipAll(ECS& ecs, EntityId humanoid) {
    const InstanceRef character = instances::parent(ecs, instances::refOf(ecs, humanoid));
    if (!isCharacter(ecs, character)) return;
    const InstanceRef backpack = backpackFor(ecs, character);
    std::string error;
    for (InstanceRef child : instances::children(ecs, character)) {
        if (!instances::classIsA(instances::className(ecs, child), "Tool")) continue;
        if (backpack != kNoInstance) {
            (void)instances::setParent(ecs, child, backpack, error);
        } else {
            (void)instances::setParent(ecs, child, kWorkspaceInstance, error);
        }
    }
}

void equip(ECS& ecs, EntityId humanoid, InstanceRef tool) {
    const InstanceRef character = instances::parent(ecs, instances::refOf(ecs, humanoid));
    if (!isCharacter(ecs, character) || !instances::isAlive(ecs, tool) || instances::parent(ecs, tool) == character) return;
    unequipAll(ecs, humanoid);
    std::string error;
    (void)instances::setParent(ecs, tool, character, error);
}

namespace {

struct HotbarState {
    std::vector<InstanceRef> slots;
    InstanceRef activated = kNoInstance;
};

HotbarState& hotbarState(ECS& ecs) {
    auto& ctx = ecs.raw().ctx();
    if (!ctx.contains<HotbarState>()) ctx.emplace<HotbarState>();
    return ctx.get<HotbarState>();
}

} // namespace

void touched(ECS& ecs, EntityId a, EntityId b) {
    for (int pass = 0; pass < 2; ++pass) {
        const InstanceRef handle = instances::refOf(ecs, pass == 0 ? a : b);
        const InstanceRef other = instances::refOf(ecs, pass == 0 ? b : a);
        if (handle == kNoInstance || other == kNoInstance || instances::name(ecs, handle) != "Handle") continue;
        const InstanceRef tool = instances::parent(ecs, handle);
        if (tool == kNoInstance || !instances::classIsA(instances::className(ecs, tool), "Tool")) continue;
        if (!instances::isDescendantOf(ecs, tool, kWorkspaceInstance) && instances::parent(ecs, tool) != kWorkspaceInstance) continue;
        if (isCharacter(ecs, instances::parent(ecs, tool))) continue;
        const InstanceRef character = instances::parent(ecs, other);
        if (!isCharacter(ecs, character) || players::playerFromCharacter(ecs, character) == kNoInstance) continue;
        const InstanceRef humanoid = childOfClass(ecs, character, "Humanoid");
        if (get(ecs, humanoid, "Health").number <= 0.0) continue;
        equip(ecs, instances::entityOf(ecs, humanoid), tool);
        return;
    }
}

std::vector<InstanceRef> hotbar(ECS& ecs) {
    HotbarState& state = hotbarState(ecs);
    const InstanceRef player = players::localPlayer(ecs);
    std::vector<InstanceRef> owned;
    if (player != kNoInstance) {
        const InstanceRef backpack = childOfClass(ecs, player, "Backpack");
        if (backpack != kNoInstance) {
            for (InstanceRef child : instances::children(ecs, backpack)) {
                if (instances::classIsA(instances::className(ecs, child), "Tool")) owned.push_back(child);
            }
        }
        const InstanceRef held = equippedTool(ecs, get(ecs, player, "Character").ref);
        if (held != kNoInstance) owned.push_back(held);
    }
    std::vector<InstanceRef> kept;
    for (InstanceRef slot : state.slots) {
        if (std::find(owned.begin(), owned.end(), slot) != owned.end()) kept.push_back(slot);
    }
    for (InstanceRef tool : owned) {
        if (std::find(kept.begin(), kept.end(), tool) == kept.end()) kept.push_back(tool);
    }
    state.slots = kept;
    return kept;
}

void updateHotbar(ECS& ecs, const HotbarInput& input) {
    HotbarState& state = hotbarState(ecs);
    const std::vector<InstanceRef> slots = hotbar(ecs);
    const InstanceRef player = players::localPlayer(ecs);
    if (player == kNoInstance) return;
    const InstanceRef character = get(ecs, player, "Character").ref;
    const InstanceRef humanoidRef = isCharacter(ecs, character) ? childOfClass(ecs, character, "Humanoid") : kNoInstance;
    if (humanoidRef == kNoInstance || get(ecs, humanoidRef, "Health").number <= 0.0) return;
    const EntityId humanoid = instances::entityOf(ecs, humanoidRef);

    if (input.slotPressed >= 1 && input.slotPressed <= static_cast<int>(slots.size())) {
        const InstanceRef tool = slots[static_cast<size_t>(input.slotPressed - 1)];
        if (instances::parent(ecs, tool) == character) {
            unequipAll(ecs, humanoid);
        } else {
            equip(ecs, humanoid, tool);
        }
    }
    const InstanceRef held = equippedTool(ecs, character);
    if (input.clickPressed && held != kNoInstance && !get(ecs, held, "ManualActivationOnly").boolean) {
        activate(ecs, held);
        state.activated = held;
    }
    if (input.clickReleased && state.activated != kNoInstance) {
        if (instances::isAlive(ecs, state.activated)) deactivate(ecs, state.activated);
        state.activated = kNoInstance;
    }
}

} // namespace engine::core::tools
