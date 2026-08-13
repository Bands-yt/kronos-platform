#include "platform/LinuxWindow.hpp"

#if defined(__linux__)
#include <SDL2/SDL_syswm.h>
#endif

namespace engine::platform {

#if defined(__linux__)

LinuxWindow::Subsystem LinuxWindow::nativeSubsystem() const {
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!handle() || !SDL_GetWindowWMInfo(handle(), &info)) {
        return Subsystem::Unknown;
    }
    switch (info.subsystem) {
        case SDL_SYSWM_X11: return Subsystem::X11;
        case SDL_SYSWM_WAYLAND: return Subsystem::Wayland;
        default: return Subsystem::Unknown;
    }
}

void* LinuxWindow::nativeDisplayHandle() const {
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!handle() || !SDL_GetWindowWMInfo(handle(), &info)) {
        return nullptr;
    }
#if defined(SDL_VIDEO_DRIVER_X11)
    if (info.subsystem == SDL_SYSWM_X11) return info.info.x11.display;
#endif
#if defined(SDL_VIDEO_DRIVER_WAYLAND)
    if (info.subsystem == SDL_SYSWM_WAYLAND) return info.info.wl.display;
#endif
    return nullptr;
}

unsigned long LinuxWindow::nativeWindowHandle() const {
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!handle() || !SDL_GetWindowWMInfo(handle(), &info)) {
        return 0;
    }
#if defined(SDL_VIDEO_DRIVER_X11)
    if (info.subsystem == SDL_SYSWM_X11) return static_cast<unsigned long>(info.info.x11.window);
#endif
    return 0; // Wayland has no integer window handle, see nativeDisplayHandle()
}

#else // !__linux__

// Compiled into every target regardless of host OS (see src/CMakeLists.txt)
// so a non-Linux build doesn't need conditional source lists. On any other
// platform this translation unit intentionally contributes nothing.
LinuxWindow::Subsystem LinuxWindow::nativeSubsystem() const { return Subsystem::Unknown; }
void* LinuxWindow::nativeDisplayHandle() const { return nullptr; }
unsigned long LinuxWindow::nativeWindowHandle() const { return 0; }

#endif

} // namespace engine::platform
