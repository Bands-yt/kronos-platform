#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <SDL2/SDL.h>
#include <volk.h>

namespace engine::core {

// Thin SDL2 wrapper around the platform window. It knows how to create
// itself, report the Vulkan instance extensions it needs, and turn a
// VkInstance into a VkSurfaceKHR -- it deliberately knows nothing about
// devices, swapchains, or rendering. Renderer owns that.
//
// The OS-specific glue (per docs/ARCHITECTURE.md §8 platform matrix) lives
// one layer down in src/platform/{Linux,Windows}Window.* -- this class is
// the platform-agnostic SDL2 path that already covers Windows/Linux/macOS,
// and is what LinuxWindow/WindowsWindow extend for the handful of things
// SDL2 doesn't abstract for us (e.g. native window handles for platform
// adapters that need them -- see src/platform_adapters/).
class Window {
public:
    struct CreateInfo {
        std::string title = "Engine Runtime";
        uint32_t width = 1280;
        uint32_t height = 720;
        bool resizable = true;
        // Kronos ("UI/UX Revamp" -- "App Icon"): a real, honest opt-in --
        // blank (the default) means no icon is set, matching SDL's own
        // "OS/window-manager default" behavior, not a fabricated fallback
        // asset. Resolved relative to the real assets directory by the
        // caller (core::Application/studio::StudioApp), same convention
        // every other real asset path in this codebase already follows
        // (e.g. UIRenderer's own font atlas path) -- this class doesn't
        // know about core::resolveResourceDir() itself.
        std::string iconPath;
    };

    Window() = default;
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    [[nodiscard]] bool initialize(const CreateInfo& info);
    void shutdown();

    // Kronos ("Fatal Init Diagnostics" -- Jay's Windows startup-crash
    // report): real, specific detail for a real initialize()==false --
    // the raw SDL_GetError() text plus a classified, actionable hint
    // (see Window.cpp's own classifySdlFailure()), not a fixed generic
    // string a caller would otherwise have to hardcode itself. Empty
    // when initialize() hasn't failed (or hasn't run yet).
    [[nodiscard]] const std::string& lastError() const { return lastError_; }

    // Pumps the SDL event queue. Returns false when a quit was requested.
    // Every raw SDL_Event is forwarded to rawEventCallback (if set) before
    // Window's own handling runs -- the same extension pattern
    // Renderer::setOverlayCallback uses, and for the same reason: Studio
    // (§5/§9) needs to feed ImGui_ImplSDL2_ProcessEvent() every event
    // without this class knowing ImGui exists, and without a second,
    // duplicate SDL_PollEvent loop competing for the same queue.
    using RawEventCallback = std::function<void(const SDL_Event&)>;
    void setRawEventCallback(RawEventCallback callback) { rawEventCallback_ = std::move(callback); }

    [[nodiscard]] bool pumpEvents();

    [[nodiscard]] std::vector<const char*> requiredInstanceExtensions() const;
    [[nodiscard]] VkSurfaceKHR createSurface(VkInstance instance) const;

    [[nodiscard]] SDL_Window* handle() const { return window_; }
    [[nodiscard]] uint32_t width() const { return width_; }
    [[nodiscard]] uint32_t height() const { return height_; }
    [[nodiscard]] bool wasResized() const { return resized_; }
    void clearResizedFlag() { resized_ = false; }

    // Kronos ("Settings Panel v2" -- "Window/Fullscreen scaling"): real
    // runtime mode switch, closing the gap RuntimeShell::drawSettingsPanel()'s
    // own "not yet supported" note previously stated plainly rather than
    // hiding. Desktop fullscreen (`SDL_WINDOW_FULLSCREEN_DESKTOP`), not
    // exclusive fullscreen -- borderless-at-desktop-resolution avoids a
    // real display mode switch (flicker, potential resolution mismatch
    // with a multi-monitor setup), the same safe default most modern
    // engines ship. Real, honest no-op if SDL has no window yet. Both
    // calls trigger the exact same real `SDL_WINDOWEVENT_RESIZED` path
    // pumpEvents() already handles -- Renderer::recreateSwapchain() picks
    // the new size up the same way any other live resize already does,
    // no separate Vulkan-side plumbing needed.
    void setFullscreen(bool enabled);
    [[nodiscard]] bool isFullscreen() const;
    // Real, windowed-mode-only resize -- SDL_SetWindowSize() is a no-op
    // while fullscreen (SDL's own documented behavior, not a bug here);
    // callers should exit fullscreen first if they want a specific
    // windowed resolution to actually apply.
    void setSize(uint32_t width, uint32_t height);

private:
    SDL_Window* window_ = nullptr;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    bool resized_ = false;
    RawEventCallback rawEventCallback_;
    std::string lastError_;
};

// Kronos ("Fatal Init Diagnostics" -- Jay's Windows startup-crash
// report): exposed (not file-local in Window.cpp) specifically so it's
// a real, directly testable pure function -- see
// testWindowClassifySdlFailure() in tests/test_main.cpp. `context` names
// what was being attempted ("SDL video/event subsystem initialization",
// "Window creation"); `rawSdlError` is SDL_GetError()'s own text,
// verbatim, always included in the result -- this never hides the real
// underlying error behind only a canned hint.
[[nodiscard]] std::string classifySdlFailure(const std::string& context, const std::string& rawSdlError);

} // namespace engine::core
