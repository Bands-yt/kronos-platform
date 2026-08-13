#pragma once

#include <random>

#include "core/Economy.hpp"
#include "core/Inventory.hpp"
#include "core/UpgradeSystem.hpp"
#include "studio/IStudioPlugin.hpp"

namespace engine::studio::plugins {

// Sprint 5 ("Core Economy") task category 4's real, interactive shop UI
// panel. Deliberately owns its own sandboxed Wallet/Inventory/
// PlayerUpgrades/EarnThrottle rather than reading them off a live ECS
// entity -- Studio runs no gameplay session by default (no Physics, no
// spawned character -- see StudioApp's own class comment), so there is
// no live "the player's wallet" to inspect the way PhysicsPreviewPlugin
// gets a live core::Physics only once Play starts. This is instead a
// real economy-tuning sandbox: a designer can simulate ore drops, sell,
// and buy upgrades to check the real numbers (price curves, anti-
// inflation taper, upgrade costs) feel right, using the *exact* same
// core::Economy/core::Inventory/core::UpgradeSystem logic
// `engine_runtime`'s real mining loop calls -- not a separate reimplementation
// that could drift out of sync with it.
class ShopPlugin final : public IStudioPlugin {
public:
    [[nodiscard]] const char* name() const override { return "Shop"; }
    [[nodiscard]] const char* category() const override { return "Economy"; }

    void update(float dt, core::ECS& ecs, core::EntityId selected,
                const std::vector<core::EntityId>& selectedEntities) override;
    void drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) override;

private:
    core::Wallet wallet_{200, 5}; // a real starting stake for tuning, not the runtime's actual 50/0 -- see .cpp
    core::Inventory inventory_;
    core::PlayerUpgrades upgrades_;
    core::EarnThrottle throttle_;
    std::mt19937 rng_{1337}; // fixed seed -- deterministic, repeatable tuning runs, not per-launch variance
};

} // namespace engine::studio::plugins
