#include "platform_adapters/adapters/AndroidAdapter.hpp"

#include <cstdio>

namespace engine::platform_adapters {

PlatformCapabilities AndroidAdapter::capabilities() const {
    PlatformCapabilities caps;
    caps.hasTouch = true;
    caps.hasGyro = true;   // present on most, not all, devices -- a real implementation queries at runtime
    caps.hasHaptics = true;
    caps.hasGamepad = true; // Bluetooth controllers, not guaranteed present
    return caps;
}

bool AndroidAdapter::initialize() {
    // TODO: ANativeActivity lifecycle callbacks, Vulkan surface via
    // ANativeWindow, Google Play Billing hook for the payment adapter
    // (§9/§14), device-fragmentation-aware capability probing (unlike
    // iOS's comparatively fixed hardware set, `hasGyro` above is a
    // reasonable default, not a guarantee, on Android).
    std::fprintf(stdout, "AndroidAdapter: initialized (stub -- see adapters/README.md)\n");
    initialized_ = true;
    return true;
}

void AndroidAdapter::shutdown() {
    initialized_ = false;
}

} // namespace engine::platform_adapters
