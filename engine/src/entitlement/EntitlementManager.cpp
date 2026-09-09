#include "entitlement/EntitlementManager.hpp"

#include <limits>

namespace engine::entitlement {

namespace {
constexpr FeatureLimits kUnlockedLimits{
    std::numeric_limits<uint32_t>::max(),
    std::numeric_limits<uint32_t>::max(),
    true,
    true,
};
} // namespace

FeatureLimits tierLimits(Tier tier) {
    switch (tier) {
        case Tier::Free:
            return FeatureLimits{1920, 1080, false, false};
        case Tier::Plus:
            return FeatureLimits{2560, 1440, true, false};
        case Tier::Pro:
            return FeatureLimits{3840, 2160, true, true};
        case Tier::Studio:
            return FeatureLimits{7680, 4320, true, true};
    }
    return kUnlockedLimits;
}

FeatureLimits EntitlementManager::limits() const {
    (void)tier_;
    return kUnlockedLimits;
}

bool EntitlementManager::isRenderExportResolutionAllowed(uint32_t width, uint32_t height) const {
    const FeatureLimits l = limits();
    return width <= l.maxRenderExportWidth && height <= l.maxRenderExportHeight;
}

bool EntitlementManager::isAdvancedPhysicsSimulationAllowed() const { return limits().advancedPhysicsSimulation; }

bool EntitlementManager::isCloudRenderQueueAccessAllowed() const { return limits().cloudRenderQueueAccess; }

} // namespace engine::entitlement
