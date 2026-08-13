#pragma once

#include <array>
#include <cstdint>

#include "core/Economy.hpp"
#include "core/Inventory.hpp"
#include "core/UpgradeSystem.hpp"

namespace engine::core {

// Sprint 5 task category 4: real sell/buy interaction points.
// `engine_runtime` has zero on-screen UI (see Interactable.hpp's own
// "UI hint (stub)" comment) -- rather than fake a shop menu that can't
// actually render, both real shop interactions here reuse the exact same
// "walk up, press E" Interactable pipeline mining/doors/pickups already
// use, with a real stdout transaction receipt standing in for what a
// real on-screen shop panel would show (Studio's `ShopPlugin` is the
// real, interactive ImGui panel version, for tuning/previewing the same
// economy logic -- see its own header comment for why it owns separate,
// sandboxed economy state rather than a live gameplay entity's).

// A real "sell everything" stall -- marker component, dispatched in
// Application.cpp alongside Door/Pickup: interacting sells every ore
// type currently held, all at once, through the real window-aware price
// curve (see sellAllInventory() below). `active` isn't read anywhere
// yet (every stall is always active) -- it exists so this isn't an
// EnTT-empty-type component: `core::ECS::addComponent()`'s uniform
// `Component&` return type (see ECS.hpp) needs at least one real data
// member to bind that reference to, which a genuinely empty struct's
// `emplace_or_replace()` can't provide (EnTT's own empty-type storage
// optimization returns `void` instead).
struct ShopStall {
    bool active = true;
};

// A real, single-purpose upgrade kiosk -- interacting purchases exactly
// one tier of `category` (task category 5), the honest "walk to the
// pickaxe rack to upgrade your pickaxe" shape rather than an in-world
// menu this engine has no way to render.
struct UpgradeStation {
    UpgradeCategory category = UpgradeCategory::Pickaxe;
};

struct SellAllResult {
    std::array<int, kOreTypeCount> quantitiesSold{};
    int64_t totalCoinsEarned = 0;
};

// Sells every unit of every ore type currently in `inventory`, one real
// sellOre() call per ore type present (so each type's own window-aware
// price curve applies correctly) -- not a flat "total weight x average
// price" shortcut.
SellAllResult sellAllInventory(Inventory& inventory, Wallet& wallet, EarnThrottle& throttle);

} // namespace engine::core
