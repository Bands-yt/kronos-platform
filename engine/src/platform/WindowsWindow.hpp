#pragma once

#include "core/Window.hpp"

namespace engine::platform {

// Mirrors LinuxWindow: SDL2 already covers window/Vulkan-surface creation
// portably. This adds the one Windows-native handle platform_adapters'
// WindowsAdapter (§8) needs -- the HWND -- for the handful of Win32 APIs
// SDL doesn't wrap (e.g. DualSense/Xbox controller vendor extensions,
// taskbar integration). Returned as void* rather than HWND so this header
// doesn't have to pull in <windows.h> on every platform that compiles it.
class WindowsWindow : public engine::core::Window {
public:
    [[nodiscard]] void* nativeWindowHandle() const; // HWND on Windows, nullptr elsewhere
};

} // namespace engine::platform
