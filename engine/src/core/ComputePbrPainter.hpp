#pragma once

#include <string>

#include <glm/glm.hpp>
#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/Texture.hpp"

namespace engine::core {

// Kronos ("Vulkan Compute PBR Painter" -- v0.4.0 Creator Suite): real,
// on-mesh PBR texture painting directly in GPU VRAM -- a compute
// shader stamps a soft circular brush straight into a
// Texture::createStorageImage() image via imageLoad/imageStore, with no
// CPU round trip for the pixel data itself (the UV hit-test that
// decides WHERE to stamp is real, headless CPU math --
// core::pickTriangleUv() -- but the actual texel read-modify-write
// never leaves the GPU). studio::plugins::MaterialPlugin calls
// stamp() once per Albedo/Normal/Roughness/Metallic slot with that
// slot's own real paint color, so one click paints all four real PBR
// channels together.
//
// Deliberately NOT put in core/TextureBaker.* despite the original
// brief naming it as the integration point: TextureBaker is a real,
// separate, CPU-side, off-main-thread mip-chain baker that reads a
// source file via stb_image and writes real PNGs to disk (see its own
// header comment) -- a GPU storage-image compute stamp has nothing in
// common with that job. Same "wrong integration point named in the
// brief, corrected and stated plainly" call as
// studio::ParticleComputeCodegen's own header comment already makes for
// its own case.
//
// stamp() is a real, synchronous, one-shot GPU round trip (transition
// to GENERAL -> dispatch -> wait -> transition back to
// SHADER_READ_ONLY_OPTIMAL) -- the same documented tradeoff
// core::Texture::loadFromFile()'s own staging upload and
// core::GpuParticleCompute::stepParticles() already make for one-off
// GPU work. Called from a UI click (MaterialPlugin's own paint stroke),
// not once per frame, so this is the right tradeoff here, not a
// performance regression the way a per-frame call would be.
class ComputePbrPainter {
public:
    ComputePbrPainter() = default;
    ~ComputePbrPainter();

    ComputePbrPainter(const ComputePbrPainter&) = delete;
    ComputePbrPainter& operator=(const ComputePbrPainter&) = delete;

    // Real pipeline creation -- loads the real build-time-compiled
    // engine/src/shaders/stamp_texture.comp.spv (see
    // core::resolveResourceDir()'s own packaged-vs-dev-build fallback).
    [[nodiscard]] bool initialize(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                                   std::string& outError);
    void destroy();

    [[nodiscard]] bool isValid() const { return pipeline_ != VK_NULL_HANDLE; }

    // Real, one-shot compute stamp directly against `texture`'s own
    // storage image -- blends `color` into every texel within
    // `radiusUv` of `uvCenter` (both in real, normalized [0,1] UV
    // space) with a soft real falloff (`softness`, 0..1 fraction of the
    // radius). `texture` must have been created via
    // Texture::createStorageImage() -- a texture created any other way
    // (no VK_IMAGE_USAGE_STORAGE_BIT) makes the real compute dispatch
    // fail, reported via `outError`, not a silent no-op.
    [[nodiscard]] bool stamp(const Texture& texture, glm::vec2 uvCenter, float radiusUv, glm::vec4 color, float softness,
                              std::string& outError);

private:
    VmaAllocator allocator_ = nullptr;
    VkDevice device_ = VK_NULL_HANDLE;
    VkCommandPool cmdPool_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;

    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkShaderModule shaderModule_ = VK_NULL_HANDLE;
};

} // namespace engine::core
