#include "platform_adapters/adapters/IOSAdapter.hpp"

#include <cstdio>

namespace engine::platform_adapters {

PlatformCapabilities IOSAdapter::capabilities() const {
    PlatformCapabilities caps;
    caps.hasTouch = true;
    caps.hasGyro = true;
    caps.hasHaptics = true;  // Taptic Engine
    caps.hasGamepad = true;  // MFi/Bluetooth controllers are commonly paired, not guaranteed present
    return caps;
}

bool IOSAdapter::initialize() {
    // TODO: UIApplicationDelegate lifecycle hooks (foreground/background/
    // termination), MoltenVK surface setup via CAMetalLayer, App Store
    // receipt validation hook for the StoreKit payment adapter (§9/§14).
    std::fprintf(stdout, "IOSAdapter: initialized (stub -- see adapters/README.md)\n");
    initialized_ = true;
    return true;
}

void IOSAdapter::shutdown() {
    initialized_ = false;
}

} // namespace engine::platform_adapters
