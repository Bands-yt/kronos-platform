// Stand-ins for the SDL calls that shared engine code still makes. kronos_server never opens a
// window or reads input, so none of these do real work. Only the clock and the quit signal matter.
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>

namespace {

std::atomic<bool> g_quitRequested{false};

void onTerminate(int) { g_quitRequested.store(true); }

void installQuitHandlers() {
    static bool installed = false;
    if (installed) return;
    installed = true;
    std::signal(SIGINT, onTerminate);
    std::signal(SIGTERM, onTerminate);
}

} // namespace

extern "C" {

int SDL_Init(Uint32) {
    installQuitHandlers();
    return 0;
}
int SDL_InitSubSystem(Uint32) {
    installQuitHandlers();
    return 0;
}
void SDL_Quit(void) {}
void SDL_QuitSubSystem(Uint32) {}
const char* SDL_GetError(void) { return ""; }
SDL_bool SDL_SetHint(const char*, const char*) { return SDL_FALSE; }

Uint64 SDL_GetTicks64(void) {
    using namespace std::chrono;
    static const auto start = steady_clock::now();
    return static_cast<Uint64>(duration_cast<milliseconds>(steady_clock::now() - start).count());
}

// SDL_QuitRequested() peeks for SDL_QUIT; report one after SIGINT/SIGTERM.
void SDL_PumpEvents(void) {}
int SDL_PollEvent(SDL_Event*) { return 0; }
int SDL_PeepEvents(SDL_Event*, int, SDL_eventaction, Uint32 minType, Uint32 maxType) {
    return (g_quitRequested.load() && minType <= SDL_QUIT && SDL_QUIT <= maxType) ? 1 : 0;
}

SDL_Window* SDL_CreateWindow(const char*, int, int, int, int, Uint32) { return nullptr; }
void SDL_DestroyWindow(SDL_Window*) {}
void SDL_ShowWindow(SDL_Window*) {}
Uint32 SDL_GetWindowFlags(SDL_Window*) { return 0; }
Uint32 SDL_GetWindowID(SDL_Window*) { return 0; }
void SDL_GetWindowSize(SDL_Window*, int* w, int* h) {
    if (w != nullptr) *w = 0;
    if (h != nullptr) *h = 0;
}
const char* SDL_GetWindowTitle(SDL_Window*) { return ""; }
void SDL_SetWindowTitle(SDL_Window*, const char*) {}
void SDL_SetWindowSize(SDL_Window*, int, int) {}
void SDL_SetWindowPosition(SDL_Window*, int, int) {}
int SDL_SetWindowFullscreen(SDL_Window*, Uint32) { return -1; }
void SDL_SetWindowIcon(SDL_Window*, SDL_Surface*) {}
SDL_Surface* SDL_CreateRGBSurfaceFrom(void*, int, int, int, int, Uint32, Uint32, Uint32, Uint32) { return nullptr; }
void SDL_FreeSurface(SDL_Surface*) {}

SDL_bool SDL_Vulkan_GetInstanceExtensions(SDL_Window*, unsigned int* count, const char**) {
    if (count != nullptr) *count = 0;
    return SDL_FALSE;
}
SDL_bool SDL_Vulkan_CreateSurface(SDL_Window*, VkInstance, VkSurfaceKHR*) { return SDL_FALSE; }
void SDL_Vulkan_GetDrawableSize(SDL_Window*, int* w, int* h) {
    if (w != nullptr) *w = 0;
    if (h != nullptr) *h = 0;
}

const Uint8* SDL_GetKeyboardState(int* numkeys) {
    static const Uint8 keys[SDL_NUM_SCANCODES] = {};
    if (numkeys != nullptr) *numkeys = SDL_NUM_SCANCODES;
    return keys;
}
SDL_Window* SDL_GetMouseFocus(void) { return nullptr; }
Uint32 SDL_GetMouseState(int* x, int* y) {
    if (x != nullptr) *x = 0;
    if (y != nullptr) *y = 0;
    return 0;
}
Uint32 SDL_GetRelativeMouseState(int* x, int* y) {
    if (x != nullptr) *x = 0;
    if (y != nullptr) *y = 0;
    return 0;
}
int SDL_SetRelativeMouseMode(SDL_bool) { return -1; }
void SDL_WarpMouseInWindow(SDL_Window*, int, int) {}

int SDL_NumJoysticks(void) { return 0; }
SDL_bool SDL_IsGameController(int) { return SDL_FALSE; }
SDL_GameController* SDL_GameControllerOpen(int) { return nullptr; }
void SDL_GameControllerClose(SDL_GameController*) {}
const char* SDL_GameControllerName(SDL_GameController*) { return ""; }
Sint16 SDL_GameControllerGetAxis(SDL_GameController*, SDL_GameControllerAxis) { return 0; }
Uint8 SDL_GameControllerGetButton(SDL_GameController*, SDL_GameControllerButton) { return 0; }
int SDL_GameControllerRumble(SDL_GameController*, Uint16, Uint16, Uint32) { return -1; }

} // extern "C"
