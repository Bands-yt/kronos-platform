#include "core/Inventory.hpp"

#include <algorithm>
#include <cmath>

namespace engine::core {

float inventoryWeight(const Inventory& inventory) {
    float total = 0.0f;
    for (const auto& stack : inventory.slots) {
        total += oreTypeInfo(stack.oreType).unitWeight * static_cast<float>(stack.quantity);
    }
    return total;
}

int remainingCapacityFor(const Inventory& inventory, OreType oreType) {
    int slotRoom = 0;
    for (const auto& stack : inventory.slots) {
        if (stack.oreType == oreType) slotRoom += kStackLimit - stack.quantity;
    }
    int freeSlots = inventory.slotCapacity - static_cast<int>(inventory.slots.size());
    if (freeSlots > 0) slotRoom += freeSlots * kStackLimit;
    slotRoom = std::max(0, slotRoom);

    float unitWeight = oreTypeInfo(oreType).unitWeight;
    if (unitWeight <= 0.0f) return slotRoom;

    float weightBudget = inventory.weightLimit - inventoryWeight(inventory);
    int weightRoom = static_cast<int>(std::floor(weightBudget / unitWeight));
    weightRoom = std::max(0, weightRoom);

    return std::min(slotRoom, weightRoom);
}

int addItem(Inventory& inventory, OreType oreType, int quantity) {
    if (quantity <= 0) return 0;

    int toAdd = std::min(quantity, remainingCapacityFor(inventory, oreType));
    int remaining = toAdd;

    for (auto& stack : inventory.slots) {
        if (remaining <= 0) break;
        if (stack.oreType != oreType) continue;
        int room = kStackLimit - stack.quantity;
        int add = std::min(room, remaining);
        stack.quantity += add;
        remaining -= add;
    }

    while (remaining > 0 && static_cast<int>(inventory.slots.size()) < inventory.slotCapacity) {
        int add = std::min(kStackLimit, remaining);
        inventory.slots.push_back(ItemStack{oreType, add});
        remaining -= add;
    }

    // Defensive, not decorative: `remainingCapacityFor()`'s weight-based
    // room uses a float-to-int floor, so this reflects the real amount
    // that actually landed in `slots` rather than assuming the two
    // computations can never disagree by a unit.
    return toAdd - remaining;
}

int removeItem(Inventory& inventory, OreType oreType, int quantity) {
    if (quantity <= 0) return 0;

    std::vector<size_t> indices;
    for (size_t i = 0; i < inventory.slots.size(); ++i) {
        if (inventory.slots[i].oreType == oreType) indices.push_back(i);
    }
    // Smallest-stack-first so a removal tends to fully empty (and erase)
    // small leftover stacks rather than nibbling evenly at every stack
    // and leaving the slot list just as fragmented as before.
    std::sort(indices.begin(), indices.end(),
              [&](size_t a, size_t b) { return inventory.slots[a].quantity < inventory.slots[b].quantity; });

    int remaining = quantity;
    for (size_t idx : indices) {
        if (remaining <= 0) break;
        ItemStack& stack = inventory.slots[idx];
        int take = std::min(stack.quantity, remaining);
        stack.quantity -= take;
        remaining -= take;
    }

    inventory.slots.erase(
        std::remove_if(inventory.slots.begin(), inventory.slots.end(), [](const ItemStack& s) { return s.quantity <= 0; }),
        inventory.slots.end());

    return quantity - remaining;
}

int totalQuantity(const Inventory& inventory, OreType oreType) {
    int total = 0;
    for (const auto& stack : inventory.slots) {
        if (stack.oreType == oreType) total += stack.quantity;
    }
    return total;
}

int collectOreDrop(OreDrop& drop, Inventory& inventory) {
    if (drop.quantity <= 0) return 0;
    int added = addItem(inventory, drop.oreType, drop.quantity);
    drop.quantity -= added;
    return added;
}

} // namespace engine::core
