#pragma once

#include <array>
#include <vector>

#include <glm/glm.hpp>

#include "core/render/GpuHelpers.hpp"

// Image-based lighting from the live procedural sky.
//
// Owns the split-sum resources: a DFG LUT (scale/bias for the GGX
// specular term plus the Charlie sheen albedo), a GGX-prefiltered specular
// cube and SH9 irradiance. The sky is re-captured on the GPU only when its
// (quantised) inputs change, so steady-state cost is zero.
//
// Extension point: this holds a single global probe. Local reflection
// probes would become an array of prefiltered cubes (cube-array view)
// indexed per object/cluster, reusing the same capture/prefilter passes.

namespace engine::core::render {

struct IblInputs {
    glm::vec3 zenith{0.0f};
    glm::vec3 horizon{0.0f};
    glm::vec3 ground{0.0f};
    glm::vec3 towardSun{0.0f, 1.0f, 0.0f};
    glm::vec3 origin{0.0f};
    glm::vec4 atmosphere{0.0f};
    glm::vec4 clouds{0.0f};
};

class ImageBasedLighting {
public:
    static constexpr uint32_t kEnvironmentSize = 256;
    static constexpr uint32_t kPrefilteredSize = 128;
    static constexpr uint32_t kPrefilteredMips = 6;
    static constexpr uint32_t kDfgSize = 128;

    [[nodiscard]] bool initialize(const GpuContext& ctx);
    void shutdown(const GpuContext& ctx);

    // Records whatever work is needed before this frame's shading reads the
    // IBL resources. Must be called outside any rendering scope. Returns
    // true if the environment was re-captured.
    bool update(VkCommandBuffer cmd, const IblInputs& inputs);

    // Forces a re-capture on the next update().
    void invalidate() { lastKey_.clear(); }

    [[nodiscard]] bool valid() const { return captured_; }
    [[nodiscard]] VkImageView prefilteredView() const { return prefiltered_.view; }
    [[nodiscard]] VkImageView dfgView() const { return dfg_.view; }
    [[nodiscard]] VkSampler sampler() const { return sampler_; }
    [[nodiscard]] VkBuffer shBuffer() const { return sh_.buffer; }
    [[nodiscard]] VkDeviceSize shBufferSize() const { return sh_.size; }
    [[nodiscard]] float prefilteredMaxMip() const { return static_cast<float>(kPrefilteredMips - 1); }

private:
    struct ComputePass {
        VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
    };

    bool createPass(const GpuContext& ctx, ComputePass& pass, const char* shader,
                    std::initializer_list<VkDescriptorType> bindings, uint32_t pushBytes);
    void destroyPass(const GpuContext& ctx, ComputePass& pass);
    [[nodiscard]] std::vector<float> quantisedKey(const IblInputs& inputs) const;

    void recordDfg(VkCommandBuffer cmd);
    void recordCapture(VkCommandBuffer cmd, const IblInputs& inputs);
    void recordMipChain(VkCommandBuffer cmd);
    void recordPrefilter(VkCommandBuffer cmd);
    void recordSh(VkCommandBuffer cmd);

    GpuImage dfg_;
    GpuImage environment_;
    GpuImage prefiltered_;
    GpuBuffer sh_;
    VkImageView environmentMip0Storage_ = VK_NULL_HANDLE;
    std::array<VkImageView, kPrefilteredMips> prefilteredMipStorage_{};
    VkSampler sampler_ = VK_NULL_HANDLE;
    uint32_t environmentMips_ = 1;

    ComputePass dfgPass_;
    ComputePass capturePass_;
    ComputePass prefilterPass_;
    ComputePass shPass_;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkDescriptorSet dfgSet_ = VK_NULL_HANDLE;
    VkDescriptorSet captureSet_ = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, kPrefilteredMips> prefilterSets_{};
    VkDescriptorSet shSet_ = VK_NULL_HANDLE;

    bool dfgReady_ = false;
    bool captured_ = false;
    std::vector<float> lastKey_;
};

} // namespace engine::core::render
