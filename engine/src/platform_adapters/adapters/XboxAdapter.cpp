#include "platform_adapters/adapters/XboxAdapter.hpp"

#include <cstdio>

namespace engine::platform_adapters {

PlatformCapabilities XboxAdapter::capabilities() const {
    PlatformCapabilities caps;
    caps.hasGamepad = true;
    caps.hasHaptics = true;
    caps.tvSafeZoneRequired = true;
    return caps;
}

bool XboxAdapter::initialize() {
    // Deliberately always fails -- see the class comment in
    // XboxAdapter.hpp and adapters/README.md. There is no GDK bootstrap
    // in this repository to call.
    std::fprintf(stderr,
                  "XboxAdapter: cannot initialize -- the Microsoft GDK is NDA-gated and is not present in "
                  "this repository (see src/platform_adapters/adapters/README.md).\n");
    return false;
}

void XboxAdapter::shutdown() {
    initialized_ = false;
}

} // namespace engine::platform_adapters
