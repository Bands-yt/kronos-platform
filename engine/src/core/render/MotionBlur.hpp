#pragma once

#include <cstdint>

#include "core/render/GpuHelpers.hpp"

// Motion blur inputs for the cinematic pass: the scene velocity buffer plus
// a per-tile maximum of it (shaders/motion_tiles.comp). The gather in
// shaders/cinematic.frag samples along the largest velocity in the 3x3
// tile neighbourhood, so moving objects smear over static surroundings,
// and weights each sample by relative depth and blur extent.

namespace engine::core::render {

// Also the maximum blur radius in pixels; must match KRONOS_MOTION_TILE_SIZE.
inline constexpr uint32_t kMotionBlurTileSize = 32;

// Per-FrameSync resources.
struct MotionBlurBinding {
    GpuImage tiles;
    VkExtent2D extent{0, 0};
    VkDescriptorSet tileSet = VK_NULL_HANDLE;   // compute: velocity -> tiles
    VkDescriptorSet sampleSet = VK_NULL_HANDLE; // fragment: velocity + tiles
    VkImageView boundVelocity = VK_NULL_HANDLE;
    bool tilesReadable = false;
};

class MotionBlur {
public:
    [[nodiscard]] bool initialize(const GpuContext& ctx, uint32_t maxBindings);
    void shutdown(const GpuContext& ctx);

    // Set layout of MotionBlurBinding::sampleSet (binding 0 velocity, 1 tiles).
    [[nodiscard]] VkDescriptorSetLayout sampleSetLayout() const { return sampleSetLayout_; }

    // (Re)creates the tile image for `extent` and points both sets at `velocity`.
    [[nodiscard]] bool bind(const GpuContext& ctx, MotionBlurBinding& binding, VkExtent2D extent,
                            VkImageView velocity);
    void releaseBinding(const GpuContext& ctx, MotionBlurBinding& binding);

    // Expects the velocity in SHADER_READ_ONLY_OPTIMAL. Leaves the tiles
    // readable from fragment shaders; with `compute` false the tiles are
    // only made layout-valid, for passes that bind but skip the blur.
    void prepareTiles(VkCommandBuffer cmd, MotionBlurBinding& binding, bool compute) const;

private:
    VkDescriptorSetLayout tileSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout sampleSetLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout tileLayout_ = VK_NULL_HANDLE;
    VkPipeline tilePipeline_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkSampler pointSampler_ = VK_NULL_HANDLE;
};

} // namespace engine::core::render
