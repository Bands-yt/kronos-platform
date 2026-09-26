#pragma once

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "core/render/GpuHelpers.hpp"

// Temporal anti-aliasing: sub-pixel projection jitter (Halton 2,3) plus a
// compute resolve that reprojects the previous frame's output through
// per-pixel motion vectors. See shaders/taa_resolve.comp for the
// rejection/filtering details.
//
// Extension point: the resolve already takes depth + velocity, so a
// temporal upscaler (render at lower res, resolve to full) only needs a
// different output extent and a jitter sequence scaled to the ratio.

namespace engine::core::render {

// Everything that has to persist between frames for one camera view.
// Frames in flight for the same view share one ViewHistory.
struct ViewHistory {
    std::array<GpuImage, 2> history{};
    VkExtent2D extent{0, 0};
    uint32_t generation = 0;  // bumped whenever the history images are recreated
    uint32_t readIndex = 0;
    bool historyValid = false;
    bool layoutsInitialised = false;

    uint64_t frameIndex = 0;
    glm::vec2 previousJitter{0.0f};
    glm::mat4 previousViewProjNoJitter{1.0f};
    bool hasPreviousViewProj = false;
    // Previous-frame world matrices and skinning palettes, keyed by entity
    // id, for object and deformation motion.
    std::unordered_map<uint32_t, glm::mat4> previousModels;
    std::unordered_map<uint32_t, glm::mat4> currentModels;
    std::unordered_map<uint32_t, std::vector<glm::mat4>> previousBones;
    std::unordered_map<uint32_t, std::vector<glm::mat4>> currentBones;
    // ParticleSystem::simulationTime() at the previous frame.
    double previousParticleTime = 0.0;
    bool hasParticleTime = false;

    // Call once per rendered frame after all draws have looked up the
    // previous-frame state.
    void advanceObjects() {
        previousModels.swap(currentModels);
        currentModels.clear();
        previousBones.swap(currentBones);
        currentBones.clear();
    }
};

// Descriptor sets bound to one FrameSync's inputs (one per ping-pong
// direction). Rewritten only when an input view or the history changes.
struct TaaBinding {
    std::array<VkDescriptorSet, 2> sets{};
    VkImageView boundCurrent = VK_NULL_HANDLE;
    VkImageView boundVelocity = VK_NULL_HANDLE;
    VkImageView boundDepth = VK_NULL_HANDLE;
    const ViewHistory* boundHistory = nullptr;
    uint32_t boundGeneration = UINT32_MAX;
};

class TemporalAA {
public:
    static constexpr uint32_t kJitterPhases = 8;

    [[nodiscard]] bool initialize(const GpuContext& ctx, uint32_t maxBindings);
    void shutdown(const GpuContext& ctx);

    // Jitter in NDC units for the given frame (0 when disabled).
    [[nodiscard]] static glm::vec2 jitterNdc(uint64_t frameIndex, VkExtent2D extent);

    // (Re)creates the history images when the extent changes. Waits for the
    // device when replacing existing images.
    [[nodiscard]] bool ensureHistory(const GpuContext& ctx, ViewHistory& view, VkExtent2D extent) const;
    static void destroyHistory(const GpuContext& ctx, ViewHistory& view);

    [[nodiscard]] bool bind(const GpuContext& ctx, TaaBinding& binding, const ViewHistory& view,
                            VkImageView currentColor, VkImageView velocity, VkImageView depth);
    void releaseBinding(const GpuContext& ctx, TaaBinding& binding);

    // Resolves into the history image and copies the result over
    // `targetImage` (the current HDR colour). Expects the colour, velocity
    // and depth inputs in SHADER_READ_ONLY_OPTIMAL, readable from compute;
    // leaves `targetImage` in SHADER_READ_ONLY_OPTIMAL. `debugVelocity`
    // replaces the output with a motion-vector visualisation.
    void resolve(VkCommandBuffer cmd, const TaaBinding& binding, ViewHistory& view, VkImage targetImage,
                 float feedback, bool debugVelocity = false) const;

private:
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkSampler pointSampler_ = VK_NULL_HANDLE;
    VkSampler linearSampler_ = VK_NULL_HANDLE;
};

} // namespace engine::core::render
