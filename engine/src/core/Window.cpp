#include "core/Window.hpp"

#include <SDL2/SDL_vulkan.h>
#include <stb_image.h>

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "core/Logger.hpp"

namespace engine::core {

// Kronos ("Fatal Init Diagnostics" -- Jay's Windows startup-crash
// report): SDL_GetError() is real and always included verbatim in the
// result, but its raw text ("No available video device", a wrapped
// Win32 error, ...) means little to someone who isn't already familiar
// with SDL internals -- Jay's own report was exactly this: a real
// failure with a real cause, but a message that gave no next step. This
// inspects the raw text for the real, documented SDL failure shapes
// worth calling out by name and appends one concrete, actionable next
// step; anything unrecognized still gets a real, honest generic
// fallback covering this bug class's actual known causes (out-of-date
// driver, missing Vulkan runtime, no display attached) -- never silence,
// never a guess dressed up as a diagnosis.
std::string classifySdlFailure(const std::string& context, const std::string& rawSdlError) {
    std::string lower = rawSdlError;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    std::string hint;
    if (lower.find("no available video device") != std::string::npos ||
        lower.find("no video device") != std::string::npos ||
        lower.find("not found a working video") != std::string::npos ||
        lower.find("no video mode") != std::string::npos) {
        hint =
            "No display/video device could be found. If you're connecting over Remote Desktop (RDP) or a "
            "headless/virtual session, Windows may not expose a real display to Kronos -- try a local session, "
            "or a remote tool with GPU passthrough (e.g. Parsec, or RDP with RemoteFX/GPU redirection enabled) "
            "instead.";
    } else if (lower.find("vulkan") != std::string::npos || lower.find("icd") != std::string::npos) {
        hint =
            "This looks like a missing or broken Vulkan runtime/driver (ICD). Install or update your GPU driver "
            "(it bundles the Vulkan runtime), or install the standalone Vulkan Runtime from vulkan.lunarg.com.";
    } else if (lower.find("driver") != std::string::npos) {
        hint = "This looks like a graphics driver problem. Update your GPU driver to the latest version from "
               "your GPU vendor (NVIDIA/AMD/Intel) and try again.";
    } else {
        hint =
            "Common causes: an out-of-date or missing graphics driver, no Vulkan-capable GPU, or running in an "
            "environment with no real display attached (a remote/headless session, or a sandboxed/virtual "
            "machine without GPU passthrough).";
    }

    return context + " failed: " + rawSdlError + ". " + hint;
}

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
    lastError_.clear(); // real, honest reset -- a retried initialize() after a fixed environment shouldn't report a stale error

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        // Kronos ("Fatal Init Diagnostics" -- Jay's Windows startup-crash
        // report): real, specific diagnosis (see classifySdlFailure()'s
        // own comment) instead of the raw SDL error alone -- logError()
        // both mirrors to stderr (same real visibility this always had)
        // and, once a caller has opted into Logger::enableFileLogging()
        // (see main.cpp/StudioMain.cpp's own call sites), persists it to
        // a real on-disk log file that survives the process dying
        // moments later.
        lastError_ = classifySdlFailure("SDL video/event subsystem initialization", SDL_GetError());
        logError("Window", "%s", lastError_.c_str());
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
        lastError_ = classifySdlFailure("Window creation", SDL_GetError());
        logError("Window", "%s", lastError_.c_str());
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
