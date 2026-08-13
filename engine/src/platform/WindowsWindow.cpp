#include "platform/WindowsWindow.hpp"

#if defined(_WIN32)
#include <SDL2/SDL_syswm.h>
#endif

namespace engine::platform {

#if defined(_WIN32)

void* WindowsWindow::nativeWindowHandle() const {
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!handle() || !SDL_GetWindowWMInfo(handle(), &info)) {
        return nullptr;
    }
    return static_cast<void*>(info.info.win.window);
}

#else // !_WIN32

// See LinuxWindow.cpp's matching #else -- always compiled, intentionally
// empty on non-Windows hosts.
void* WindowsWindow::nativeWindowHandle() const { return nullptr; }

#endif

} // namespace engine::platform
