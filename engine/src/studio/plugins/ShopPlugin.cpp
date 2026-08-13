#include "studio/plugins/ShopPlugin.hpp"

#include <cstdio>

#include <imgui.h>

#include "core/OreNode.hpp"
#include "core/Shop.hpp"
#include "studio/PluginChrome.hpp"

namespace engine::studio::plugins {

void ShopPlugin::update(float dt, core::ECS& /*ecs*/, core::EntityId /*selected*/,
                         const std::vector<core::EntityId>& /*selectedEntities*/) {
    core::tickEarnThrottle(throttle_, dt);
}

void ShopPlugin::drawPanel(core::ECS& /*ecs*/, core::EntityId /*selected*/,
                            const std::vector<core::EntityId>& /*selectedEntities*/) {
    ImGui::Begin("Shop");
    drawPluginHeader("Shop");

    ImGui::TextWrapped(
        "A real economy-tuning sandbox -- its own Wallet/Inventory/PlayerUpgrades, not tied to any live ECS "
        "entity (Studio runs no gameplay session by default). Calls the exact same core::Economy/core::Inventory/"
        "core::UpgradeSystem logic engine_runtime's real mining loop uses.");
    ImGui::Separator();

    ImGui::Text("Wallet: %lld coins, %lld gems", static_cast<long long>(wallet_.coins),
                static_cast<long long>(wallet_.gems));
    ImGui::Text("Earn this window: %lld / %lld coins (window resets in %.0fs)",
                 static_cast<long long>(throttle_.coinsEarnedInWindow), static_cast<long long>(core::kEarnCapPerWindow),
                 core::kEarnWindowSeconds - throttle_.windowElapsedSeconds);

    if (wallet_.gems > 0 && ImGui::Button("Convert 1 Gem -> Coins")) {
        core::convertGemsToCoins(wallet_, 1);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset Economy")) {
        wallet_ = core::Wallet{200, 5};
        inventory_ = core::Inventory{};
        upgrades_ = core::PlayerUpgrades{};
        throttle_ = core::EarnThrottle{};
        core::applyBackpackTier(inventory_, upgrades_);
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Inventory");
    ImGui::Text("Weight: %.1f / %.1f  |  Slots: %d / %d", core::inventoryWeight(inventory_), inventory_.weightLimit,
                static_cast<int>(inventory_.slots.size()), inventory_.slotCapacity);

    if (ImGui::BeginTable("ore_inventory", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Ore");
        ImGui::TableSetupColumn("Held");
        ImGui::TableSetupColumn("Simulate Mining");
        ImGui::TableSetupColumn("Sell All");
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < core::kOreTypeCount; ++i) {
            core::OreType type = static_cast<core::OreType>(i);
            const core::OreTypeInfo& info = core::oreTypeInfo(type);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(ImVec4(info.color.x, info.color.y, info.color.z, 1.0f), "%s (%s)", info.name,
                                core::oreRarityName(info.rarity));

            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%d", core::totalQuantity(inventory_, type));

            ImGui::TableSetColumnIndex(2);
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Button("Mine")) {
                // A real drop-table roll (core::rollOreDrops()), the same
                // function breakOreNode() calls in the live runtime --
                // exercised here without needing a live Physics world.
                core::DropRoll roll = core::rollOreDrops(info, rng_);
                for (int quantity : roll.dropQuantities) core::addItem(inventory_, type, quantity);
                if (roll.bonusGem) wallet_.gems += 1;
            }
            ImGui::SameLine();
            if (ImGui::Button("Mine x10")) {
                for (int n = 0; n < 10; ++n) {
                    core::DropRoll roll = core::rollOreDrops(info, rng_);
                    for (int quantity : roll.dropQuantities) core::addItem(inventory_, type, quantity);
                    if (roll.bonusGem) wallet_.gems += 1;
                }
            }

            ImGui::TableSetColumnIndex(3);
            int held = core::totalQuantity(inventory_, type);
            ImGui::BeginDisabled(held <= 0);
            if (ImGui::Button("Sell")) {
                core::sellOre(inventory_, wallet_, throttle_, type, held);
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (!inventory_.slots.empty() && ImGui::Button("Sell Everything")) {
        core::sellAllInventory(inventory_, wallet_, throttle_);
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Upgrades");
    const struct {
        const char* label;
        core::UpgradeCategory category;
        int* currentTier;
    } categories[] = {
        {"Pickaxe", core::UpgradeCategory::Pickaxe, &upgrades_.pickaxeTier},
        {"Backpack", core::UpgradeCategory::Backpack, &upgrades_.backpackTier},
        {"Boots", core::UpgradeCategory::Boots, &upgrades_.bootsTier},
    };
    for (const auto& entry : categories) {
        const core::UpgradeTierInfo& current = core::tierInfoFor(entry.category, *entry.currentTier);
        int maxTier = core::tierCountFor(entry.category) - 1;
        ImGui::PushID(entry.label);
        if (*entry.currentTier >= maxTier) {
            ImGui::Text("%s: %s (max tier)", entry.label, current.name);
        } else {
            const core::UpgradeTierInfo& next = core::tierInfoFor(entry.category, *entry.currentTier + 1);
            ImGui::Text("%s: %s -> %s (%lld coins)", entry.label, current.name, next.name,
                        static_cast<long long>(next.cost));
            ImGui::SameLine();
            ImGui::BeginDisabled(wallet_.coins < next.cost);
            if (ImGui::Button("Buy")) {
                core::UpgradePurchaseResult purchase = core::purchaseUpgrade(upgrades_, wallet_, entry.category);
                if (purchase.success && entry.category == core::UpgradeCategory::Backpack) {
                    core::applyBackpackTier(inventory_, upgrades_);
                }
            }
            ImGui::EndDisabled();
        }
        ImGui::PopID();
    }

    drawPluginFooter("Sandboxed state -- resets when Studio restarts.");
    ImGui::End();
}

} // namespace engine::studio::plugins
