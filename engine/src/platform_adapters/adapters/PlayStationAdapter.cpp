#include "platform_adapters/adapters/PlayStationAdapter.hpp"

#include <cstdio>

namespace engine::platform_adapters {

PlatformCapabilities PlayStationAdapter::capabilities() const {
    PlatformCapabilities caps;
    caps.hasGamepad = true;
    caps.hasHaptics = true;
    caps.hasAdaptiveTriggers = true; // DualSense
    caps.tvSafeZoneRequired = true;
    return caps;
}

bool PlayStationAdapter::initialize() {
    // Deliberately always fails -- see the class comment in
    // PlayStationAdapter.hpp and adapters/README.md. There is no PS5 SDK
    // bootstrap in this repository to call.
    std::fprintf(stderr,
                  "PlayStationAdapter: cannot initialize -- the Sony PS5 SDK is NDA-gated and is not present "
                  "in this repository (see src/platform_adapters/adapters/README.md).\n");
    return false;
}

void PlayStationAdapter::shutdown() {
    initialized_ = false;
}

} // namespace engine::platform_adapters
