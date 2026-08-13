#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/Texture.hpp"

namespace engine::core {

// Kronos ("User Interface" world-building): the real, first screen-space
// 2D UI/text rendering this engine has ever had -- closing the
// documented "engine_runtime renders no text at all" architecture
// boundary (see core::Interactable.hpp's own header comment on why
// every UI hint before this was a stdout stand-in). A real, self-
// contained subsystem (owns its own sampler/descriptor pool/pipeline,
// exactly like trailer::CaptureRig's own "small, self-contained render
// helper" shape) rather than new private state bolted onto the already-
// large Renderer class -- it is driven entirely through
// Renderer::setOverlayCallback() (the same real seam Studio's own ImGui
// pass already uses to composite on top of the exact same render graph
// the runtime uses, see that method's own header comment), so it needs
// no Renderer-internal access at all.
//
// Real, deliberately simple rendering model: one shared pipeline for
// both flat-color rects (panels/bars) and glyph quads (text), one
// dynamically-updated vertex buffer per frame-in-flight (matching
// FrameSync::particleInstanceBuffer's own real "host-visible, mapped,
// double-buffered" convention so a slow frame's GPU read never races a
// live CPU write), one real draw call per frame. Font rendering uses a
// real, offline-baked bitmap atlas (assets/textures/ui_font_atlas.png,
// rendered from the open-licensed Noto Sans Mono Bold font via a one-time
// Python/Pillow script -- not a licensed/scraped asset, not a hand-rolled
// low-res pixel font) covering printable ASCII 32..126 in a fixed
// monospace grid -- see kFirstChar/kAtlasColumns/kCellWidthPx/
// kCellHeightPx below, which must stay in sync with that script's own
// real layout constants.
class UIRenderer {
public:
    [[nodiscard]] bool initialize(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                                   VkFormat colorFormat, const std::string& fontAtlasPath);
    void shutdown();

    [[nodiscard]] bool isValid() const { return pipeline_ != VK_NULL_HANDLE; }

    // Real, must be called once per frame before any drawRect()/
    // drawText() calls -- clears the previous frame's own batch and
    // records `extent` for pixel->NDC conversion.
    void beginFrame(VkExtent2D extent);

    // Real, flat-color screen-space rectangle -- `topLeftPx` in real
    // pixel coordinates (origin top-left, +Y down, matching every other
    // 2D UI convention), `color.a` real-blended via the pipeline's own
    // real alpha blending (the first blend-enabled pipeline in this
    // renderer -- see ui.frag's own header comment).
    void drawRect(glm::vec2 topLeftPx, glm::vec2 sizePx, glm::vec4 color);

    // Real, monospace bitmap text -- unsupported characters (outside the
    // real baked 32..126 range) render as a real blank advance (an
    // honest skip, not a missing-glyph box glyph this atlas doesn't
    // have). `scale` 1.0 renders each glyph at the atlas's own real
    // baked-cell aspect, scaled to kGlyphAdvancePx x kGlyphHeightPx.
    void drawText(const std::string& text, glm::vec2 topLeftPx, float scale, glm::vec4 color);

    // Pure, real text extent -- monospace, so this is an exact real
    // computation, not a heuristic/estimate.
    [[nodiscard]] glm::vec2 measureText(const std::string& text, float scale) const;

    // Real, one real draw call -- uploads this frame's own batched
    // vertex data into the current frame-in-flight's own buffer and
    // records the real bind/draw commands. Must be called from inside
    // Renderer's own OverlayCallback (an active vkCmdBeginRendering
    // block already exists there -- see this class's own header
    // comment); this function does not begin/end rendering itself.
    void draw(VkCommandBuffer cmd, VkImageView targetView, VkExtent2D extent);

private:
    static constexpr int kFramesInFlight = 2;
    static constexpr size_t kMaxVertices = 65536;

    // Real font-atlas layout constants -- must exactly match
    // gen_font_atlas.py's own real FIRST_CHAR/LAST_CHAR/COLS/CELL_W/
    // CELL_H values.
    static constexpr int kFirstChar = 32;
    static constexpr int kLastChar = 126;
    static constexpr int kAtlasColumns = 16;
    static constexpr float kCellWidthPx = 32.0f;
    static constexpr float kCellHeightPx = 40.0f;

    // Real, on-screen glyph render size at scale=1.0 -- deliberately
    // narrower than the atlas cell's own real aspect (32:40) so
    // monospace text at real HUD sizes doesn't read as unusually wide.
    static constexpr float kGlyphAdvancePx = 15.0f;
    static constexpr float kGlyphHeightPx = 22.0f;

    void pushQuad(glm::vec2 topLeftPx, glm::vec2 sizePx, glm::vec2 uv0, glm::vec2 uv1, glm::vec4 color);

    VmaAllocator allocator_ = nullptr;
    VkDevice device_ = nullptr;

    Texture fontAtlas_;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;

    struct FrameBuffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = nullptr;
        void* mapped = nullptr;
    };
    FrameBuffer frameBuffers_[kFramesInFlight];
    int currentFrameIndex_ = 0;

    std::vector<float> vertexData_; // real, flat-packed UIVertex floats (8 floats/vertex): see .cpp's own pushQuad()
    VkExtent2D currentExtent_{0, 0};
};

} // namespace engine::core
