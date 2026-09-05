#pragma once

#include <vector>

#include <volk.h>

namespace engine::core {

// Pure swapchain-selection logic, extracted out of Renderer.cpp so a
// second real swapchain (studio::SecondaryViewport, the windowed 3D
// Mesh & CSG Editor plugin's own OS window) picks a present mode/extent
// the exact same way the main window's Renderer::createSwapchain()
// always has, from one real source instead of two copies that could
// silently drift apart. Also what makes this logic headlessly testable
// (see test_main.cpp's testChoose* tests) -- every parameter here is a
// plain struct/vector, no live VkInstance/VkDevice/VkSurfaceKHR needed.

// Prefers real sRGB BGRA8 (the common desktop default) when the surface
// offers it; falls back to whatever the surface's own first-listed
// format is otherwise, rather than asserting or picking arbitrarily.
[[nodiscard]] VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats);

// See choosePresentMode's own comment in the .cpp for the real fan-
// spike/coil-whine finding behind FIFO being the default: MAILBOX is
// only chosen when the caller has explicitly opted out of vsync AND the
// surface actually supports it (never assumed -- the Vulkan spec never
// guarantees MAILBOX).
[[nodiscard]] VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& modes, bool vsyncEnabled);

// Uses the surface's own currentExtent when the platform reports one
// (the common case); otherwise clamps the caller's requested
// width/height into [minImageExtent, maxImageExtent] rather than
// handing Vulkan a size it will reject.
[[nodiscard]] VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& caps, uint32_t width, uint32_t height);

} // namespace engine::core
