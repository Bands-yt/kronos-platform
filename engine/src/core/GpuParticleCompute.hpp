#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <volk.h>
#include <vk_mem_alloc.h>

namespace engine::core {

// Kronos ("Real-Time GPU Particle Compute" -- v0.4.0 Creator Suite): the
// real core-side consumer of compiled compute-shader SPIR-V -- takes
// raw SPIR-V words (from studio::generateComputeParticleShaderGlsl() +
// studio::RuntimeShaderCompiler::compile(), or an equivalent build-time-
// compiled .comp, see engine/src/shaders/particle_compute.comp) and
// runs a real Vulkan compute dispatch against a real GPU-resident
// particle buffer. Deliberately knows NOTHING about
// studio::ParticleComputeGraph/ShaderGraph -- core cannot depend on
// studio (see engine/src/CMakeLists.txt's own engine_core/studio target
// split: studio's sources compile directly into the `studio` executable,
// never into the engine_core library core lives in). This class's only
// real contract is "hand me valid SPIR-V matching GpuParticle's exact
// layout" -- the same "generate text/bytes in one layer, consume opaque
// bytes in the lower one" split studio::RuntimeShaderCompiler's own
// header comment already documents for its GLSL->SPIR-V half.
//
// stepParticles() is a real, synchronous, one-shot GPU round trip
// (host-write -> dispatch -> vkQueueWaitIdle -> host-read) -- the same
// documented tradeoff core::Texture::loadFromFile()'s own staging
// upload and core::Mesh::uploadFromHost() already make for one-off GPU
// work ("one-shot, synchronous", see Texture.cpp's own comment).
// Deliberately NOT wired into core::Renderer's live per-frame draw
// loop: Renderer::drawParticles() runs *inside* an active
// vkCmdBeginRendering scope, where a compute dispatch is illegal, and
// calling this synchronous, GPU-stalling method once per frame would be
// a real, serious performance regression, not a reference-quality
// integration -- a real async/double-buffered every-frame pipeline is
// separate, larger follow-on work this pass does not attempt. Real,
// honest scope here: an on-demand GPU integration step a caller (a
// Studio preview action, or a test) invokes explicitly, its actual
// correctness verified by a real, live, headless Vulkan compute test
// (see tests/test_main.cpp) rather than just "it compiled."
struct GpuParticle {
    glm::vec3 position{0.0f};
    float age = 0.0f;
    glm::vec3 velocity{0.0f};
    float lifetime = 1.0f;
};

class GpuParticleCompute {
public:
    GpuParticleCompute() = default;
    ~GpuParticleCompute();

    GpuParticleCompute(const GpuParticleCompute&) = delete;
    GpuParticleCompute& operator=(const GpuParticleCompute&) = delete;

    // Real pipeline/buffer creation from raw SPIR-V words -- `spirv`
    // must be a real compute shader matching GpuParticle's exact
    // std430 layout (vec3 position, float age, vec3 velocity, float
    // lifetime) bound at descriptor set 0, binding 0, with a
    // push-constant block of {float deltaTime; uint particleCount;} --
    // see engine/src/shaders/particle_compute.comp for the real,
    // canonical shape studio::generateComputeParticleShaderGlsl() also
    // produces. Allocates a real, host-visible, mapped SSBO sized for
    // `maxParticles` real GpuParticle entries.
    [[nodiscard]] bool initialize(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                                   const std::vector<uint32_t>& spirv, uint32_t maxParticles, std::string& outError);
    void destroy();

    [[nodiscard]] bool isValid() const { return pipeline_ != VK_NULL_HANDLE; }

    // Real, one-shot dispatch: writes `particles` into the real GPU
    // SSBO, dispatches ceil(particles.size()/256) real workgroups with
    // the given real deltaTime, waits for completion, and reads the
    // real results back into `particles` in place. Returns false
    // (leaving `particles` unmodified) if particles.size() exceeds the
    // real maxParticles this instance was initialize()'d with, or on
    // any real Vulkan failure -- `outError` names which.
    [[nodiscard]] bool stepParticles(std::vector<GpuParticle>& particles, float deltaTime, std::string& outError);

private:
    // Must match the real `local_size_x` this SPIR-V was compiled with
    // -- both studio::generateComputeParticleShaderGlsl()'s own default
    // and engine/src/shaders/particle_compute.comp use 256.
    static constexpr uint32_t kLocalSizeX = 256;

    VmaAllocator allocator_ = nullptr;
    VkDevice device_ = VK_NULL_HANDLE;
    VkCommandPool cmdPool_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;

    VkBuffer particleBuffer_ = VK_NULL_HANDLE;
    VmaAllocation particleBufferAllocation_ = nullptr;
    void* particleBufferMapped_ = nullptr;
    uint32_t maxParticles_ = 0;

    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkShaderModule shaderModule_ = VK_NULL_HANDLE;
};

} // namespace engine::core
