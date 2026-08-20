#include "core/Window.hpp"

#include <SDL2/SDL_vulkan.h>
#include <stb_image.h>

#include <cstdio>

namespace engine::core {

namespace {
// Kronos ("UI/UX Revamp" -- "App Icon"): real, honest best-effort --
// SDL2 has no built-in PNG decoder (that's SDL2_image, not a dependency
// this codebase otherwise needs), so this reuses the same vendored
// stb_image core::Texture already loads real GPU textures with
// (Texture.cpp's own #include <stb_image.h>), then hands the raw RGBA8
// pixels to SDL via SDL_CreateRGBSurfaceFrom -- no new image-decoding
// dependency pulled in just for a window icon. A missing/corrupt file is
// a real, non-fatal no-op (logged, window opens with the OS/window-
// manager's own default icon), matching this codebase's "fail soft on a
// missing optional asset" convention throughout (e.g. UIRenderer's own
// font-atlas failure just above this call site in Application::initialize()).
void applyWindowIcon(SDL_Window* window, const std::string& iconPath) {
    if (iconPath.empty()) return;

    int width = 0, height = 0, channels = 0;
    stbi_uc* pixels = stbi_load(iconPath.c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (pixels == nullptr) {
        std::fprintf(stderr, "Window: could not load icon \"%s\" -- continuing with the default window icon.\n",
                     iconPath.c_str());
        return;
    }

    // Real, explicit RGBA8 byte order/masks -- SDL_CreateRGBSurfaceFrom
    // doesn't infer them from anything, and stbi_load's own STBI_rgb_alpha
    // request guarantees exactly 4 bytes/pixel in R,G,B,A order.
    constexpr int kBitsPerPixel = 32;
    constexpr int kBytesPerPixel = 4;
    Uint32 rMask = 0x000000ff, gMask = 0x0000ff00, bMask = 0x00ff0000, aMask = 0xff000000;
    SDL_Surface* surface = SDL_CreateRGBSurfaceFrom(pixels, width, height, kBitsPerPixel, width * kBytesPerPixel,
                                                     rMask, gMask, bMask, aMask);
    if (surface != nullptr) {
        SDL_SetWindowIcon(window, surface);
        SDL_FreeSurface(surface); // real, honest -- SDL_SetWindowIcon copies the surface, doesn't take ownership of it
    }
    stbi_image_free(pixels);
}
} // namespace

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

    applyWindowIcon(window_, info.iconPath);

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

void Window::setFullscreen(bool enabled) {
    if (window_ == nullptr) return;
    if (SDL_SetWindowFullscreen(window_, enabled ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) != 0) {
        std::fprintf(stderr, "Window: SDL_SetWindowFullscreen failed: %s\n", SDL_GetError());
    }
    // Real, immediate -- SDL_GetWindowSize() reflects the new real mode
    // synchronously (unlike the resize event, which pumpEvents() only
    // sees on a later poll), so this call's own caller can rely on
    // width()/height() being correct right away rather than waiting a
    // frame.
    int w = 0, h = 0;
    SDL_GetWindowSize(window_, &w, &h);
    width_ = static_cast<uint32_t>(w);
    height_ = static_cast<uint32_t>(h);
    resized_ = true;
}

bool Window::isFullscreen() const {
    if (window_ == nullptr) return false;
    return (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
}

void Window::setSize(uint32_t width, uint32_t height) {
    if (window_ == nullptr) return;
    SDL_SetWindowSize(window_, static_cast<int>(width), static_cast<int>(height));
    SDL_SetWindowPosition(window_, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    int w = 0, h = 0;
    SDL_GetWindowSize(window_, &w, &h);
    width_ = static_cast<uint32_t>(w);
    height_ = static_cast<uint32_t>(h);
    resized_ = true;
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
