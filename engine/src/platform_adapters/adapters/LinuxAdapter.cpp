#include "platform_adapters/adapters/LinuxAdapter.hpp"

#include <cstdio>

namespace engine::platform_adapters {

PlatformCapabilities LinuxAdapter::capabilities() const {
    PlatformCapabilities caps;
    caps.hasKeyboardMouse = true;
    caps.hasGamepad = true;
    caps.hasHaptics = true;
    return caps;
}

bool LinuxAdapter::initialize() {
    // Steam Deck (also in docs/ARCHITECTURE.md §8.1's matrix) is this same
    // adapter, not a separate one -- it's native Linux + Vulkan + a
    // gamepad-shaped input device, which UnifiedInput's existing
    // keyboard/mouse/gamepad handling already covers. Its one
    // differentiator (a touchscreen alongside the gamepad) is a
    // capabilities flag, not a reason for a distinct adapter class.
    std::fprintf(stdout, "LinuxAdapter: initialized (stub -- see adapters/README.md)\n");
    initialized_ = true;
    return true;
}

void LinuxAdapter::shutdown() {
    initialized_ = false;
}

} // namespace engine::platform_adapters
