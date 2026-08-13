#pragma once

#include "core/Window.hpp"

namespace engine::platform {

// SDL2 already gives Window cross-platform window creation and Vulkan
// surface creation -- LinuxWindow only adds the one thing SDL2 doesn't
// hand back through its portable API: the native X11/Wayland handle, which
// platform_adapters' Linux input/haptics backends (§8 of
// docs/ARCHITECTURE.md) need for things SDL doesn't wrap (e.g. raw
// compositor-specific gestures). Everything else -- window lifetime,
// events, Vulkan surface -- stays in core::Window; this class only adds a
// getter.
class LinuxWindow : public engine::core::Window {
public:
    enum class Subsystem { Unknown, X11, Wayland };

    // Valid only after core::Window::initialize() has created the SDL
    // window. Returns Subsystem::Unknown (and a null handle) if queried
    // before that, or if SDL_GetWindowWMInfo isn't supported for the
    // running compositor.
    [[nodiscard]] Subsystem nativeSubsystem() const;
    [[nodiscard]] void* nativeDisplayHandle() const; // Display* (X11) or wl_display* (Wayland)
    [[nodiscard]] unsigned long nativeWindowHandle() const; // X11 Window (Wayland surfaces use nativeDisplayHandle only)
};

} // namespace engine::platform
