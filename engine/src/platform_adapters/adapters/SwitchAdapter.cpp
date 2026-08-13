#include "platform_adapters/adapters/SwitchAdapter.hpp"

#include <cstdio>

namespace engine::platform_adapters {

PlatformCapabilities SwitchAdapter::capabilities() const {
    PlatformCapabilities caps;
    caps.hasGamepad = true;
    caps.hasTouch = true;   // handheld mode
    caps.hasGyro = true;    // Joy-Con
    caps.hasHaptics = true; // HD Rumble
    caps.tvSafeZoneRequired = true; // docked mode
    return caps;
}

bool SwitchAdapter::initialize() {
    // Deliberately always fails -- see the class comment in
    // SwitchAdapter.hpp and adapters/README.md. There is no NX SDK
    // bootstrap in this repository to call.
    std::fprintf(stderr,
                  "SwitchAdapter: cannot initialize -- the Nintendo NX SDK is NDA-gated and is not present "
                  "in this repository (see src/platform_adapters/adapters/README.md).\n");
    return false;
}

void SwitchAdapter::shutdown() {
    initialized_ = false;
}

} // namespace engine::platform_adapters
