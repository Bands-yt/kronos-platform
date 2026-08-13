#include "platform_adapters/adapters/MacOSAdapter.hpp"

#include <cstdio>

namespace engine::platform_adapters {

PlatformCapabilities MacOSAdapter::capabilities() const {
    PlatformCapabilities caps;
    caps.hasKeyboardMouse = true;
    caps.hasGamepad = true;
    caps.hasHaptics = true;
    return caps;
}

bool MacOSAdapter::initialize() {
    std::fprintf(stdout, "MacOSAdapter: initialized (stub -- see adapters/README.md)\n");
    initialized_ = true;
    return true;
}

void MacOSAdapter::shutdown() {
    initialized_ = false;
}

} // namespace engine::platform_adapters
