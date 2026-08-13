#pragma once

#include <vector>

#include "core/ECS.hpp"
#include "core/OreNode.hpp"

namespace engine::core {

// Sprint 5 task category 2: real inventory slots, stacking, and a real
// weight limit -- deliberately scoped to `OreType` (the only real item
// kind that exists yet, see main.cpp's ore nodes) rather than a generic
// `ItemId` abstraction with only one concrete user; a general item-kind
// system is the natural next step if/when non-ore items exist, not
// speculative scaffolding built ahead of that need.
struct ItemStack {
    OreType oreType = OreType::Copper;
    int quantity = 0;
};

// Every stack in one slot shares one ore type; a second stack of the same
// type only starts once the first hits `kStackLimit`, matching the
// standard "stacks fill up, then spill into a new slot" convention.
constexpr int kStackLimit = 50;

struct Inventory {
    std::vector<ItemStack> slots;
    int slotCapacity = 12;     // real slot count -- task category 5's Backpack tier upgrades raise this
    float weightLimit = 60.0f; // real weight cap (see OreTypeInfo::unitWeight) -- also backpack-tier-upgradable
};

// Pure -- real total carried weight, computed from `unitWeight` per slot
// rather than cached, so it can never drift out of sync with the slots
// it's derived from.
[[nodiscard]] float inventoryWeight(const Inventory& inventory);

// Pure -- how many more units of `oreType` this inventory can actually
// hold right now, accounting for both remaining slot capacity (a new
// stack needs a free slot) and remaining weight budget -- the real
// number `addItem()` below is built on, and what a real "will this fit"
// UI check (Shop/Inspector) should call before attempting to add.
[[nodiscard]] int remainingCapacityFor(const Inventory& inventory, OreType oreType);

// Real add: tops up existing same-type stacks toward `kStackLimit`
// first, then opens new slots up to `slotCapacity`, stopping the moment
// either slot capacity or `weightLimit` would be exceeded. Returns how
// many units were *actually* added (may be less than `quantity` -- the
// real, honest "inventory overflow" anti-exploit behavior task category
// 8 asks for: never silently drops or duplicates units, the caller is
// expected to handle a partial add, e.g. by leaving the remainder as a
// still-pickupable OreDrop -- see Application.cpp's auto-pickup pass).
int addItem(Inventory& inventory, OreType oreType, int quantity);

// Real remove: takes from existing stacks (smallest-first, so partially
// filled stacks get topped off toward removal rather than leaving lots
// of small leftover stacks scattered across slots), removing empty slots
// entirely. Returns how many units were actually removed (capped at what
// was actually present) -- the real quantity `core::sellItems()`
// (Economy.hpp) and any other consumer must use for its own bookkeeping,
// never the requested amount.
int removeItem(Inventory& inventory, OreType oreType, int quantity);

// Pure -- total units of `oreType` currently held, summed across every
// matching stack/slot.
[[nodiscard]] int totalQuantity(const Inventory& inventory, OreType oreType);

// Real auto-pickup (task category 2's "auto-pickup integration using the
// existing physics events.onInteract pipeline") -- called from
// Application.cpp's pre-tick hook for every core::OreDrop entity within
// pickup range of the character. Adds as many units as the inventory
// currently has room for (may be a partial add, see addItem() above),
// decrementing `drop`'s own quantity by that amount. Returns the number
// of units actually collected; the caller destroys the drop entity once
// its quantity reaches 0, and fires the same events.onInteract the
// manual E-press pickup path already uses -- see Application.cpp.
int collectOreDrop(OreDrop& drop, Inventory& inventory);

} // namespace engine::core
