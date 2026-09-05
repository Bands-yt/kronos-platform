#pragma once

#include <vector>

#include <SDL2/SDL.h>
#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/Camera.hpp"
#include "core/ECS.hpp"
#include "core/ParticleSystem.hpp"
#include "core/Renderer.hpp"
#include "core/Window.hpp"

namespace engine::studio {

// Kronos ("3D Mesh & CSG Editor" -- Beta Roadmap Phase 2, "windowed
// plugin module"): a real, independent second OS window with its own
// Vulkan surface and swapchain, sharing every other piece of GPU state
// (instance/physical device/device/allocator/queues/pipelines) with the
// main core::Renderer that owns them -- see this class's own initialize()
// for why that sharing needs no extra plumbing at all.
//
// Deliberately NOT a second core::Renderer: Renderer.hpp is a ~1400-line
// class every one of this codebase's real, passing tests already
// exercises transitively, and duplicating its swapchain/pipeline/
// resource lifecycle for a second window would mean two copies of that
// machinery to keep in sync. This class instead reuses Renderer's own
// AuxiliarySceneHandle mechanism (core::Renderer::createAuxiliaryScene(),
// the same real "independent UBO/shadow-cascade/HDR resource slot,
// shares the main device/pipelines" seam studio::ThumbnailCameraRig and
// studio::PreviewScene already use) for the *rendering* half, and only
// adds the parts Renderer genuinely has none of: a second VkSurfaceKHR,
// a second VkSwapchainKHR, and this window's own real depth image.
//
// Real, honest simplicity over max throughput: unlike Renderer's own
// double/triple-buffered renderFrame(), this issues one real, self-
// contained command buffer per renderFrame() call and vkQueueWaitIdle()s
// before presenting -- the exact same real, already-tested pattern
// trailer::CaptureRig::captureFrame() uses for its own one-shot renders
// (see that function's own comment on why). A tool window showing
// grey-boxed editor geometry doesn't need to hit main-viewport frame
// rates; a real fence/semaphore ring for double-buffered presentation is
// separate, deliberately unattempted scope here. The one real cost:
// this stalls renderer.graphicsQueue() (shared with the main window)
// for the duration of this window's own draw -- acceptable for an
// editor tool panel, not for anything latency-sensitive.
//
// Formats are NOT independently chosen: drawSceneInto()'s pipelines are
// created once, at Renderer::initialize() time, with a fixed
// VkPipelineRenderingCreateInfo baked to renderer.swapchainFormat()/
// depthFormat() -- handing them a differently-formatted image would be
// a real, silent pipeline/attachment mismatch (no validation layer is
// installed in this environment to catch it, the same class of bug
// CaptureRig::initialize()'s own header comments describe). initialize()
// below verifies the main window's own swapchainFormat() is actually
// supported on this window's surface (real, honest failure if not,
// rather than silently requesting an incompatible one) instead of
// running its own independent core::chooseSurfaceFormat() selection.
class SecondaryViewport {
public:
    SecondaryViewport() = default;
    ~SecondaryViewport();
    SecondaryViewport(const SecondaryViewport&) = delete;
    SecondaryViewport& operator=(const SecondaryViewport&) = delete;

    [[nodiscard]] bool initialize(core::Renderer& renderer, const core::Window::CreateInfo& windowInfo);
    void shutdown(core::Renderer& renderer);

    [[nodiscard]] bool isValid() const { return swapchain_ != VK_NULL_HANDLE; }

    // Real windowId()-filtered handling -- see core::Window::pumpEvents()'s
    // own comment on why only one SDL_PollEvent loop may run per process;
    // this is the dispatch target for every event belonging to *this*
    // window specifically (resize, close, and real mouse/wheel input for
    // the plugin's own orbit camera).
    void handleEvent(const SDL_Event& event);

    [[nodiscard]] bool closeRequested() const { return closeRequested_; }

    // Real render: acquires an image from this window's own swapchain,
    // draws the live `ecs` through it via renderer.drawSceneInto(), and
    // presents. A real, honest no-op while minimized (zero extent) --
    // matches Renderer::renderFrame()'s own minimized-window guard.
    void renderFrame(core::Renderer& renderer, core::ECS& ecs, core::MeshLibrary& meshLibrary,
                      core::TextureLibrary& textureLibrary, const core::Camera& camera);

    [[nodiscard]] core::Window& window() { return window_; }
    [[nodiscard]] uint32_t windowId() const { return window_.windowId(); }

private:
    bool createSurfaceAndSwapchain(core::Renderer& renderer);
    void destroySwapchainResources(core::Renderer& renderer);
    bool recreateSwapchain(core::Renderer& renderer);

    core::Window window_;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat colorFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_{0, 0};
    std::vector<VkImage> images_;
    std::vector<VkImageView> imageViews_;

    VkImage depthImage_ = VK_NULL_HANDLE;
    VmaAllocation depthAllocation_ = VK_NULL_HANDLE;
    VkImageView depthImageView_ = VK_NULL_HANDLE;

    VkSemaphore imageAvailable_ = VK_NULL_HANDLE;

    core::Renderer::AuxiliarySceneHandle auxiliaryScene_ = core::Renderer::kInvalidAuxiliaryScene;
    core::ParticleSystem particleSystem_; // real, never populated -- required by drawSceneInto()'s signature only, same as ThumbnailCameraRig/PreviewScene

    bool resizePending_ = false;
    bool closeRequested_ = false;
};

} // namespace engine::studio
