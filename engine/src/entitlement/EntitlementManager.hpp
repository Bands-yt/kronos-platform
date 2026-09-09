#pragma once

#include <cstdint>

namespace engine::entitlement {

enum class Tier { Free, Plus, Pro, Studio };

struct FeatureLimits {
    uint32_t maxRenderExportWidth;
    uint32_t maxRenderExportHeight;
    bool advancedPhysicsSimulation;
    bool cloudRenderQueueAccess;
};

// The real per-tier ceilings for when gating is actually turned on.
// EntitlementManager::limits() below does not return these yet -- every
// tier currently resolves to the same unlocked values -- but the table
// is real now so enabling gating later is a one-line change in limits(),
// not a redesign.
[[nodiscard]] FeatureLimits tierLimits(Tier tier);

class EntitlementManager {
public:
    explicit EntitlementManager(Tier tier = Tier::Free) : tier_(tier) {}

    [[nodiscard]] Tier tier() const { return tier_; }
    void setTier(Tier tier) { tier_ = tier; }

    // All tiers unlocked for now, regardless of tier_ -- see tierLimits().
    [[nodiscard]] FeatureLimits limits() const;

    [[nodiscard]] bool isRenderExportResolutionAllowed(uint32_t width, uint32_t height) const;
    [[nodiscard]] bool isAdvancedPhysicsSimulationAllowed() const;
    [[nodiscard]] bool isCloudRenderQueueAccessAllowed() const;

private:
    Tier tier_;
};

} // namespace engine::entitlement
