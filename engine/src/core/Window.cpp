#include "core/Window.hpp"

#include <SDL2/SDL_vulkan.h>

#include <cstdio>

namespace engine::core {

Window::~Window() {
    shutdown();
}

bool Window::initialize(const CreateInfo& info) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        std::fprintf(stderr, "Window: SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    Uint32 flags = SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN;
    if (info.resizable) {
        flags |= SDL_WINDOW_RESIZABLE;
    }

    window_ = SDL_CreateWindow(
        info.title.c_str(),
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        static_cast<int>(info.width), static_cast<int>(info.height),
        flags
    );

    if (!window_) {
        std::fprintf(stderr, "Window: SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    width_ = info.width;
    height_ = info.height;
    return true;
}

void Window::shutdown() {
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
        SDL_Quit();
    }
}

bool Window::pumpEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (rawEventCallback_) {
            rawEventCallback_(event);
        }
        switch (event.type) {
            case SDL_QUIT:
                return false;
            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_RESIZED ||
                    event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    width_ = static_cast<uint32_t>(event.window.data1);
                    height_ = static_cast<uint32_t>(event.window.data2);
                    resized_ = true;
                }
                break;
            default:
                break;
        }
        // TODO(net/input): forward raw SDL input events into
        // platform_adapters::UnifiedInput so InputAction bindings
        // (docs/ARCHITECTURE.md §8.2) see them, instead of handling
        // window-management events only, as done here.
    }
    return true;
}

std::vector<const char*> Window::requiredInstanceExtensions() const {
    unsigned int count = 0;
    if (!SDL_Vulkan_GetInstanceExtensions(window_, &count, nullptr)) {
        std::fprintf(stderr, "Window: SDL_Vulkan_GetInstanceExtensions failed: %s\n", SDL_GetError());
        return {};
    }

    std::vector<const char*> extensions(count);
    if (!SDL_Vulkan_GetInstanceExtensions(window_, &count, extensions.data())) {
        std::fprintf(stderr, "Window: SDL_Vulkan_GetInstanceExtensions (fill) failed: %s\n", SDL_GetError());
        return {};
    }
    return extensions;
}

VkSurfaceKHR Window::createSurface(VkInstance instance) const {
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(window_, instance, &surface)) {
        std::fprintf(stderr, "Window: SDL_Vulkan_CreateSurface failed: %s\n", SDL_GetError());
        return VK_NULL_HANDLE;
    }
    return surface;
}

} // namespace engine::core
