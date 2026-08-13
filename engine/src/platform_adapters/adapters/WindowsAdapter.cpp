#include "platform_adapters/adapters/WindowsAdapter.hpp"

#include <cstdio>

namespace engine::platform_adapters {

PlatformCapabilities WindowsAdapter::capabilities() const {
    PlatformCapabilities caps;
    caps.hasKeyboardMouse = true;
    caps.hasGamepad = true;
    caps.hasHaptics = true; // Xbox controller rumble, via UnifiedInput's SDL2 backend
    return caps;
}

bool WindowsAdapter::initialize() {
    // TODO: no Win32-specific bootstrap yet -- core::Window/SDL2 already
    // handles window + Vulkan surface creation portably (see
    // platform/WindowsWindow.hpp for the one native-handle extra this
    // platform needs beyond that).
    std::fprintf(stdout, "WindowsAdapter: initialized (stub -- see adapters/README.md)\n");
    initialized_ = true;
    return true;
}

void WindowsAdapter::shutdown() {
    initialized_ = false;
}

} // namespace engine::platform_adapters
