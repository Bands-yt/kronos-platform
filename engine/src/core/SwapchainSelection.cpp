#include "core/SwapchainSelection.hpp"

#include <algorithm>
#include <limits>

namespace engine::core {

VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) {
    for (const auto& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return f;
        }
    }
    return formats.front();
}

VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& modes, bool vsyncEnabled) {
    // Kronos ("UI/UX Revamp" -- "Performance & Stability", "whistling
    // sound when Studio opens"): real, identified root cause -- MAILBOX
    // presents as fast as the GPU can produce frames, uncapped by the
    // display's own refresh rate. On an idle/near-static scene (the Home
    // Screen, an empty Studio viewport) that means sustained
    // near-100%-duty-cycle GPU work for zero visible benefit -- a
    // textbook cause of both fan ramp-up and coil whine. FIFO is real
    // vsync: guaranteed present-mode support (unlike MAILBOX, which
    // isn't universally available -- see the fallback below), capped to
    // the display's own refresh rate, and the real, standard default
    // every editor/engine uses for exactly this "idle quietly" reason.
    //
    // Kronos ("Settings Panel v2 + Input Remapping + Accessibility
    // Layer" -- "Graphics: VSync"): MAILBOX's real, legitimate use case
    // (lower input latency for a fast-paced game, at the real cost of
    // burning full GPU power even when idle) is now the real, explicit,
    // user-facing graphics setting this comment always said it should
    // be -- see Renderer::setVsyncEnabled(). Real, honest fallback if
    // the physical device doesn't actually support MAILBOX (the Vulkan
    // spec never guarantees it): stay on FIFO rather than silently
    // requesting an unsupported mode.
    if (!vsyncEnabled) {
        for (VkPresentModeKHR mode : modes) {
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR) return mode;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& caps, uint32_t width, uint32_t height) {
    if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return caps.currentExtent;
    }
    VkExtent2D extent{width, height};
    extent.width = std::clamp(extent.width, caps.minImageExtent.width, caps.maxImageExtent.width);
    extent.height = std::clamp(extent.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    return extent;
}

} // namespace engine::core
