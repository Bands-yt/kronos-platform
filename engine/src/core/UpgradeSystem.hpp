#pragma once

#include <cstdint>

#include "core/Economy.hpp"
#include "core/Inventory.hpp"

namespace engine::core {

// Sprint 5 task category 5: pickaxe/backpack/boots tiers, thematically
// tied to the same ore ladder mining already uses (a Copper pickaxe
// isn't just "Tier 1", it's a real, recognizable name a shop UI can show
// as-is). Deliberately three separate, small tier tables rather than one
// generic "upgrade" abstraction with only three concrete users -- see
// core/Inventory.hpp's own reasoning for the same "don't build the
// generalization until something needs it" call.
enum class UpgradeCategory : uint8_t { Pickaxe, Backpack, Boots };

struct UpgradeTierInfo {
    const char* name;
    int64_t cost; // coins; tier 0 (the starting tier) is always cost=0/already owned
    // Only the field(s) relevant to this tier's own category are
    // meaningful -- Pickaxe tiers set miningPower, Backpack tiers set
    // slotCapacity/weightLimit, Boots tiers set speedMultiplier. Kept as
    // one flat struct (not three separate ones) since every tier table
    // is otherwise identical shape (name, cost) and every consumer wants
    // "the tier info for this category" as one uniform type.
    int miningPower;
    int slotCapacity;
    float weightLimit;
    float speedMultiplier;
};

constexpr int kPickaxeTierCount = 5;
constexpr int kBackpackTierCount = 5;
constexpr int kBootsTierCount = 5;

[[nodiscard]] const UpgradeTierInfo& pickaxeTier(int tierIndex);
[[nodiscard]] const UpgradeTierInfo& backpackTier(int tierIndex);
[[nodiscard]] const UpgradeTierInfo& bootsTier(int tierIndex);
[[nodiscard]] int tierCountFor(UpgradeCategory category);
[[nodiscard]] const UpgradeTierInfo& tierInfoFor(UpgradeCategory category, int tierIndex);

// Real per-player progression state -- tier 0 in every category is the
// free starting tier (a bare-handed Copper pickaxe, a starter Satchel,
// bare feet), never purchasable/never refunded, just the array index a
// brand-new character already starts at.
struct PlayerUpgrades {
    int pickaxeTier = 0;
    int backpackTier = 0;
    int bootsTier = 0;
};

// Pure -- what a caller actually reads live wherever it's needed
// (Application.cpp's mining dispatch calls `mineOreNode(node,
// miningPowerFor(upgrades))` fresh every swing; CharacterController's
// speed setup multiplies its base walk/run speed by
// `speedMultiplierFor(upgrades)`) -- there is no persistent "current
// mining power" field to keep in sync, it's always derived from
// `pickaxeTier` directly.
[[nodiscard]] int miningPowerFor(const PlayerUpgrades& upgrades);
[[nodiscard]] float speedMultiplierFor(const PlayerUpgrades& upgrades);

// Backpack is the one category whose effect needs to land in another
// system's live, mutable state (Inventory::slotCapacity/weightLimit)
// rather than being read fresh each use -- this one small, real helper
// centralizes "how a backpack tier maps to Inventory fields" in one
// place instead of every call site duplicating it.
void applyBackpackTier(Inventory& inventory, const PlayerUpgrades& upgrades);

struct UpgradePurchaseResult {
    bool success = false;
    int64_t coinsSpent = 0;
    const char* reason = ""; // real, human-readable reason when success=false -- what a shop UI shows
};

// The real purchase transaction: fails honestly (no coins deducted, no
// tier change) if already at the category's max tier, or if
// `wallet.coins` is short of the next tier's cost -- never partially
// applies a purchase. Deliberately does *not* touch Inventory/
// CharacterController itself (see `applyBackpackTier()` above and
// `speedMultiplierFor()`/`miningPowerFor()`) -- UpgradeSystem stays
// decoupled from exactly which live systems read its results, the same
// modularity this sprint's constraints ask for.
UpgradePurchaseResult purchaseUpgrade(PlayerUpgrades& upgrades, Wallet& wallet, UpgradeCategory category);

} // namespace engine::core
