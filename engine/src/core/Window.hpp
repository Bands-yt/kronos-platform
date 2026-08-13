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
    };

    Window() = default;
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    [[nodiscard]] bool initialize(const CreateInfo& info);
    void shutdown();

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

private:
    SDL_Window* window_ = nullptr;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    bool resized_ = false;
    RawEventCallback rawEventCallback_;
};

} // namespace engine::core
