#include "core/Renderer.hpp"
#include "core/render/RendererInternal.hpp"
#include "core/SceneSpatialIndex.hpp"
#include "core/StaticBatching.hpp"
#include "core/GpuFeatures.hpp"

#include "core/CascadeSplitMath.hpp"
#include "core/Hierarchy.hpp"
#include "core/ResourcePaths.hpp"
#include "core/SwapchainSelection.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <set>
#include <unordered_map>


namespace engine::core {

using namespace renderer_internal;

void Renderer::refreshTextureDescriptors(uint32_t handle, TextureLibrary& textureLibrary) {
    const Texture* texture = textureLibrary.get(handle);
    const bool usable = texture != nullptr && texture->isValid();

    std::vector<VkDescriptorImageInfo> infos;
    std::vector<VkWriteDescriptorSet> writes;
    infos.reserve(materialDescriptorCache_.size() * 5 + 1);
    const Texture* fallbacks[5] = {&defaultWhiteTexture_, &defaultFlatNormalTexture_, &defaultWhiteTexture_,
                                   &defaultWhiteTexture_, &defaultWhiteTexture_};
    for (const auto& [key, set] : materialDescriptorCache_) {
        for (uint32_t binding = 0; binding < key.size(); ++binding) {
            if (key[binding] != handle || set == VK_NULL_HANDLE) continue;
            infos.push_back({materialSampler_, usable ? texture->view() : fallbacks[binding]->view(),
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = set;
            write.dstBinding = binding;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pImageInfo = &infos.back();
            writes.push_back(write);
        }
    }

    if (bindlessInitialised_) {
        const uint64_t key = static_cast<uint64_t>(handle) + kBindlessReservedKeyCount;
        const uint32_t slot = bindlessTable_.lookup(key);
        if (slot != BindlessTextureTable::kInvalidSlot) {
            if (usable) {
                infos.push_back({bindlessSampler_, texture->view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
                VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                write.dstSet = bindlessSet_;
                write.dstBinding = 0;
                write.dstArrayElement = slot;
                write.descriptorCount = 1;
                write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                write.pImageInfo = &infos.back();
                writes.push_back(write);
            } else {
                bindlessTable_.release(key);
            }
        }
    }
    if (writes.empty()) return;
    vkDeviceWaitIdle(device_);
    vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

VkDescriptorSet Renderer::getOrCreateMaterialDescriptorSet(const Renderable& renderable, TextureLibrary& textureLibrary) {
    std::array<uint32_t, 5> key{renderable.albedoTexture, renderable.normalTexture, renderable.metallicTexture,
                                 renderable.roughnessTexture, renderable.aoTexture};
    auto cached = materialDescriptorCache_.find(key);
    if (cached != materialDescriptorCache_.end()) return cached->second;

    VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.descriptorPool = materialDescriptorPool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &materialDescriptorSetLayout_;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(device_, &allocInfo, &set) != VK_SUCCESS) {
        std::fprintf(stderr,
                     "Renderer: vkAllocateDescriptorSets (material) failed -- out of kMaxMaterialDescriptorSets "
                     "(%u)? Falling back to whatever set=1 was last bound for this entity.\n",
                     kMaxMaterialDescriptorSets);
        return VK_NULL_HANDLE;
    }

    auto resolveView = [&](uint32_t handle, const Texture& fallback) -> VkImageView {
        const Texture* tex = handle != Renderable::kInvalidHandle ? textureLibrary.get(handle) : nullptr;
        return (tex != nullptr && tex->isValid()) ? tex->view() : fallback.view();
    };

    std::array<VkDescriptorImageInfo, 5> imageInfos{};
    imageInfos[0] = {materialSampler_, resolveView(renderable.albedoTexture, defaultWhiteTexture_),
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    imageInfos[1] = {materialSampler_, resolveView(renderable.normalTexture, defaultFlatNormalTexture_),
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    imageInfos[2] = {materialSampler_, resolveView(renderable.metallicTexture, defaultWhiteTexture_),
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    imageInfos[3] = {materialSampler_, resolveView(renderable.roughnessTexture, defaultWhiteTexture_),
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    imageInfos[4] = {materialSampler_, resolveView(renderable.aoTexture, defaultWhiteTexture_),
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

    std::array<VkWriteDescriptorSet, 5> writes{};
    for (uint32_t i = 0; i < writes.size(); ++i) {
        writes[i] = VkWriteDescriptorSet{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writes[i].dstSet = set;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo = &imageInfos[i];
    }
    vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

    materialDescriptorCache_[key] = set;
    return set;
}

bool Renderer::createPostProcessResources() {
    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(device_, &samplerInfo, nullptr, &postProcessSampler_) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreateSampler (post-process) failed.\n");
        return false;
    }

    VkDescriptorSetLayoutBinding singleBinding{};
    singleBinding.binding = 0;
    singleBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    singleBinding.descriptorCount = 1;
    singleBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo singleLayoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    singleLayoutInfo.bindingCount = 1;
    singleLayoutInfo.pBindings = &singleBinding;
    if (vkCreateDescriptorSetLayout(device_, &singleLayoutInfo, nullptr, &postProcessSingleSetLayout_) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreateDescriptorSetLayout (post-process single) failed.\n");
        return false;
    }

    std::array<VkDescriptorSetLayoutBinding, 2> dualBindings{singleBinding, singleBinding};
    dualBindings[1].binding = 1;

    // Kronos ("Cinematic Camera Physics & Post-Processing Pipeline" --
    // real 3D LUT support): composite's own real, separate 3-binding
    // array (HDR + bloom + LUT) -- deliberately NOT dualBindings above,
    // which cinematicDescriptorSetLayout_ below also uses as-is (2
    // bindings: hdrColor + depth). Mutating dualBindings itself to 3
    // bindings for composite's sake would have silently grown
    // cinematicDescriptorSetLayout_ to 3 bindings too (it's created from
    // the same local variable a few lines down) even though
    // drawCinematicPass() only ever writes 2 descriptors into it --
    // exactly the kind of "descriptor declared but never written" bug
    // this codebase's own comments elsewhere warn is silent with no
    // validation layer installed.
    std::array<VkDescriptorSetLayoutBinding, 3> compositeBindings{singleBinding, singleBinding, singleBinding};
    compositeBindings[1].binding = 1;
    compositeBindings[2].binding = 2; // the LUT sampler3D -- see Renderer::loadColorGradingLut()

    VkDescriptorSetLayoutCreateInfo compositeLayoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    compositeLayoutInfo.bindingCount = static_cast<uint32_t>(compositeBindings.size());
    compositeLayoutInfo.pBindings = compositeBindings.data();
    if (vkCreateDescriptorSetLayout(device_, &compositeLayoutInfo, nullptr, &postProcessCompositeSetLayout_) !=
        VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreateDescriptorSetLayout (post-process composite) failed.\n");
        return false;
    }

    VkDescriptorSetLayoutCreateInfo dualLayoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dualLayoutInfo.bindingCount = static_cast<uint32_t>(dualBindings.size());
    dualLayoutInfo.pBindings = dualBindings.data();

    // Sprint 16 ("Cinematic Graphics"): a dedicated NEAREST-filtered
    // sampler for reading depth back as a texture (drawCinematicPass())
    // -- see depthSampler_'s own declaration comment for why this can't
    // just reuse postProcessSampler_ (LINEAR). Its descriptor set layout
    // reuses dualBindings' shape (2 combined-image-sampler bindings) but
    // gets its own distinct VkDescriptorSetLayout object since it's
    // conceptually a different pass's inputs (hdrColor + depth, not HDR +
    // bloom), even though the binding shape happens to match.
    VkSamplerCreateInfo depthSamplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    depthSamplerInfo.magFilter = VK_FILTER_NEAREST;
    depthSamplerInfo.minFilter = VK_FILTER_NEAREST;
    depthSamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    depthSamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    depthSamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(device_, &depthSamplerInfo, nullptr, &depthSampler_) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreateSampler (depth) failed.\n");
        return false;
    }
    if (vkCreateDescriptorSetLayout(device_, &dualLayoutInfo, nullptr, &cinematicDescriptorSetLayout_) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreateDescriptorSetLayout (cinematic) failed.\n");
        return false;
    }

    // 7 sets (bloomExtract + composite + cinematic + luminance +
    // particleDepth + Kronos "Rendering Fidelity Foundation" Phase 1.2's
    // volumetric-fog input + Kronos "Rendering Fidelity" SSR-fallback's
    // own input) per frame-in-flight *plus* per auxiliary scene slot (see
    // AuxiliarySceneHandle's doc comment -- each studio::PreviewScene's
    // ensurePostProcessTargets() call draws from this same pool); 12
    // combined-image-sampler descriptors per slot (1 for bloomExtract's
    // single binding, 3 for composite's HDR+bloom+LUT bindings -- Kronos
    // "Cinematic Camera Physics & Post-Processing Pipeline", real 3D LUT
    // support, raised from 2 -- 2 for cinematic's dual bindings, 1 for
    // luminance's single binding, 1 for particleDepth's single binding,
    // 2 for the fog input's own dual bindings, 2 for the SSR input's own
    // dual bindings -- see FrameSync::fogInputDescriptorSet's/
    // ssrInputDescriptorSet's own comments).
    uint32_t totalPostProcessSlots = framesInFlight_ + static_cast<uint32_t>(kMaxAuxiliaryScenes);
    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, totalPostProcessSlots * 12};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    // FREE_DESCRIPTOR_SET_BIT: ensurePostProcessTargets() frees and
    // reallocates these sets on every resize (window resize, Studio
    // viewport panel resize), not just once at startup.
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = totalPostProcessSlots * 7;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &postProcessDescriptorPool_) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreateDescriptorPool (post-process) failed.\n");
        return false;
    }
    return true;
}

void Renderer::destroyColorGradingLutImage() {
    if (lutImageView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, lutImageView_, nullptr);
        lutImageView_ = VK_NULL_HANDLE;
    }
    if (lutImage_ != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator_, lutImage_, lutAllocation_);
        lutImage_ = VK_NULL_HANDLE;
        lutAllocation_ = nullptr;
    }
    lutSize_ = 0;
}

bool Renderer::uploadColorGradingLut(const CubeLutData& data, std::string& outError) {
    if (!data.isValid()) {
        outError = "CubeLutData is not valid (size/rgb mismatch).";
        return false;
    }

    // Real RGBA8 conversion -- an 8-bit-per-channel 3D LUT is the same
    // real, standard precision most real-time engines ship (Unity/
    // Unreal's own default LUT formats), plenty for a display "look"
    // grade; higher precision (R16G16B16A16_SFLOAT) is real, separate
    // scope if banding is ever actually observed.
    const uint32_t size = data.size;
    std::vector<uint8_t> rgba8(static_cast<size_t>(size) * size * size * 4);
    for (size_t i = 0; i < static_cast<size_t>(size) * size * size; ++i) {
        rgba8[i * 4 + 0] = static_cast<uint8_t>(std::clamp(data.rgb[i * 3 + 0], 0.0f, 1.0f) * 255.0f + 0.5f);
        rgba8[i * 4 + 1] = static_cast<uint8_t>(std::clamp(data.rgb[i * 3 + 1], 0.0f, 1.0f) * 255.0f + 0.5f);
        rgba8[i * 4 + 2] = static_cast<uint8_t>(std::clamp(data.rgb[i * 3 + 2], 0.0f, 1.0f) * 255.0f + 0.5f);
        rgba8[i * 4 + 3] = 255;
    }

    const VkDeviceSize imageBytes = static_cast<VkDeviceSize>(rgba8.size());
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAllocation = nullptr;
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = imageBytes;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo stagingAllocInfo{};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    if (vmaCreateBuffer(allocator_, &bufferInfo, &stagingAllocInfo, &stagingBuffer, &stagingAllocation, nullptr) !=
        VK_SUCCESS) {
        outError = "Failed to create the LUT upload staging buffer.";
        return false;
    }
    VmaAllocationInfo stagingInfo{};
    vmaGetAllocationInfo(allocator_, stagingAllocation, &stagingInfo);
    std::memcpy(stagingInfo.pMappedData, rgba8.data(), static_cast<size_t>(imageBytes));

    VkImage newImage = VK_NULL_HANDLE;
    VmaAllocation newAllocation = nullptr;
    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_3D;
    imageInfo.extent = {size, size, size};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo imageAllocInfo{};
    imageAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    if (vmaCreateImage(allocator_, &imageInfo, &imageAllocInfo, &newImage, &newAllocation, nullptr) != VK_SUCCESS) {
        outError = "vmaCreateImage (3D LUT) failed.";
        vmaDestroyBuffer(allocator_, stagingBuffer, stagingAllocation);
        return false;
    }

    VkCommandBufferAllocateInfo cmdAllocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmdAllocInfo.commandPool = commandPool_;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device_, &cmdAllocInfo, &cmd) != VK_SUCCESS) {
        outError = "vkAllocateCommandBuffers failed while uploading the LUT.";
        vmaDestroyImage(allocator_, newImage, newAllocation);
        vmaDestroyBuffer(allocator_, stagingBuffer, stagingAllocation);
        return false;
    }

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    transitionImage(cmd, newImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_2_NONE,
                     VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT);

    VkBufferImageCopy copyRegion{};
    copyRegion.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copyRegion.imageExtent = {size, size, size};
    vkCmdCopyBufferToImage(cmd, stagingBuffer, newImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

    transitionImage(cmd, newImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                     VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(graphicsQueue_, 1, &submitInfo, VK_NULL_HANDLE);
    // One-shot, synchronous -- this runs at initialize() and whenever a
    // creator explicitly loads a new .cube file, never per-frame, so the
    // same real tradeoff Texture::uploadPixels()/trailer::CaptureRig's
    // one-shot renders already make applies here too.
    vkQueueWaitIdle(graphicsQueue_);
    vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);
    vmaDestroyBuffer(allocator_, stagingBuffer, stagingAllocation);

    VkImageView newView = VK_NULL_HANDLE;
    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = newImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_3D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(device_, &viewInfo, nullptr, &newView) != VK_SUCCESS) {
        outError = "vkCreateImageView (3D LUT) failed.";
        vmaDestroyImage(allocator_, newImage, newAllocation);
        return false;
    }

    // Only now, with the new image/view fully real and valid, do we
    // touch the previous one -- see this method's own .hpp comment on
    // why a failure above must never leave the previous LUT half-replaced.
    destroyColorGradingLutImage();
    lutImage_ = newImage;
    lutAllocation_ = newAllocation;
    lutImageView_ = newView;
    lutSize_ = size;
    rewriteCompositeLutBindings();
    return true;
}

void Renderer::rewriteCompositeLutBindings() {
    VkDescriptorImageInfo lutInfo{postProcessSampler_, lutImageView_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstBinding = 2;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &lutInfo;

    for (FrameSync& frame : frames_) {
        if (frame.compositeDescriptorSet == VK_NULL_HANDLE) continue;
        write.dstSet = frame.compositeDescriptorSet;
        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    }
    for (FrameSync& scene : auxiliaryScenes_) {
        if (scene.compositeDescriptorSet == VK_NULL_HANDLE) continue;
        write.dstSet = scene.compositeDescriptorSet;
        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    }
}

bool Renderer::loadColorGradingLut(const std::string& cubeFilePath, std::string& outError) {
    CubeLutData data;
    if (!parseCubeLutFile(cubeFilePath, data, outError)) return false;
    return uploadColorGradingLut(data, outError);
}

bool Renderer::resetColorGradingLutToIdentity(std::string& outError) {
    constexpr uint32_t kDefaultLutSize = 16; // matches the real default seeded at initialize()
    return uploadColorGradingLut(generateIdentityCubeLut(kDefaultLutSize), outError);
}

void Renderer::destroyPostProcessResources() {
    destroyColorGradingLutImage();
    if (postProcessDescriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, postProcessDescriptorPool_, nullptr);
        postProcessDescriptorPool_ = VK_NULL_HANDLE;
    }
    if (cinematicDescriptorSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, cinematicDescriptorSetLayout_, nullptr);
        cinematicDescriptorSetLayout_ = VK_NULL_HANDLE;
    }
    if (depthSampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device_, depthSampler_, nullptr);
        depthSampler_ = VK_NULL_HANDLE;
    }
    if (postProcessCompositeSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, postProcessCompositeSetLayout_, nullptr);
        postProcessCompositeSetLayout_ = VK_NULL_HANDLE;
    }
    if (postProcessSingleSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, postProcessSingleSetLayout_, nullptr);
        postProcessSingleSetLayout_ = VK_NULL_HANDLE;
    }
    if (postProcessSampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device_, postProcessSampler_, nullptr);
        postProcessSampler_ = VK_NULL_HANDLE;
    }
}

bool Renderer::createPostProcessPipelines() {
    std::string shaderDir = resolveResourceDir(executableDirectory(), "shaders", ENGINE_SHADER_DIR);
    auto vertCode = readBinaryFile(shaderDir + "/fullscreen.vert.spv");
    if (vertCode.empty()) {
        std::fprintf(stderr, "Renderer: failed to read compiled shader \"%s/fullscreen.vert.spv\".\n", shaderDir.c_str());
        return false;
    }
    VkShaderModule vertModule = createShaderModule(device_, vertCode);
    if (vertModule == VK_NULL_HANDLE) {
        std::fprintf(stderr, "Renderer: vkCreateShaderModule failed (fullscreen.vert).\n");
        return false;
    }

    // Fixed-function state shared by both full-screen-triangle passes: no
    // vertex input at all (see shaders/fullscreen.vert), no depth test/
    // write, no blending (bloom_extract writes a fresh half-res target;
    // composite overwrites the destination outright).
    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    // --- Bloom extract: writes into the half-res HDR bloom target ---
    VkFormat hdrFormat = kHDRFormat;
    bool ok = true;
    {
        auto fragCode = readBinaryFile(shaderDir + "/bloom_extract.frag.spv");
        VkShaderModule fragModule = fragCode.empty() ? VK_NULL_HANDLE : createShaderModule(device_, fragCode);
        if (fragCode.empty() || fragModule == VK_NULL_HANDLE) {
            std::fprintf(stderr, "Renderer: failed to load shaders for bloom extract pipeline.\n");
            ok = false;
        } else {
            VkPushConstantRange pushRange{};
            pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            pushRange.offset = 0;
            pushRange.size = sizeof(BloomPushConstants);

            VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            layoutInfo.setLayoutCount = 1;
            layoutInfo.pSetLayouts = &postProcessSingleSetLayout_;
            layoutInfo.pushConstantRangeCount = 1;
            layoutInfo.pPushConstantRanges = &pushRange;
            ok = vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &bloomExtractPipelineLayout_) == VK_SUCCESS;

            if (ok) {
                VkPipelineShaderStageCreateInfo stages[2]{};
                stages[0] = VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
                stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
                stages[0].module = vertModule;
                stages[0].pName = "main";
                stages[1] = VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
                stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
                stages[1].module = fragModule;
                stages[1].pName = "main";

                VkPipelineRenderingCreateInfo renderingInfo{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
                renderingInfo.colorAttachmentCount = 1;
                renderingInfo.pColorAttachmentFormats = &hdrFormat;

                VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
                pipelineInfo.pNext = &renderingInfo;
                pipelineInfo.stageCount = 2;
                pipelineInfo.pStages = stages;
                pipelineInfo.pVertexInputState = &vertexInput;
                pipelineInfo.pInputAssemblyState = &inputAssembly;
                pipelineInfo.pViewportState = &viewportState;
                pipelineInfo.pRasterizationState = &rasterizer;
                pipelineInfo.pMultisampleState = &multisampling;
                pipelineInfo.pDepthStencilState = &depthStencil;
                pipelineInfo.pColorBlendState = &colorBlending;
                pipelineInfo.pDynamicState = &dynamicState;
                pipelineInfo.layout = bloomExtractPipelineLayout_;

                ok = vkCreateGraphicsPipelines(device_, pipelineCache_, 1, &pipelineInfo, nullptr, &bloomExtractPipeline_) ==
                     VK_SUCCESS;
            }
            vkDestroyShaderModule(device_, fragModule, nullptr);
        }
    }
    if (!ok) {
        std::fprintf(stderr, "Renderer: bloom extract pipeline creation failed.\n");
        vkDestroyShaderModule(device_, vertModule, nullptr);
        return false;
    }

    // --- Composite: writes into the caller-provided final color target
    // (engine_runtime's swapchain image or Studio's OffscreenTarget --
    // both always swapchainFormat_, see OffscreenTarget::ensureSize()'s
    // call site in StudioApp.cpp) ---
    {
        auto fragCode = readBinaryFile(shaderDir + "/composite.frag.spv");
        VkShaderModule fragModule = fragCode.empty() ? VK_NULL_HANDLE : createShaderModule(device_, fragCode);
        if (fragCode.empty() || fragModule == VK_NULL_HANDLE) {
            std::fprintf(stderr, "Renderer: failed to load shaders for composite pipeline.\n");
            ok = false;
        } else {
            VkPushConstantRange pushRange{};
            pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            pushRange.offset = 0;
            pushRange.size = sizeof(CompositePushConstants);

            VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            layoutInfo.setLayoutCount = 1;
            layoutInfo.pSetLayouts = &postProcessCompositeSetLayout_;
            layoutInfo.pushConstantRangeCount = 1;
            layoutInfo.pPushConstantRanges = &pushRange;
            ok = vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &compositePipelineLayout_) == VK_SUCCESS;

            if (ok) {
                VkPipelineShaderStageCreateInfo stages[2]{};
                stages[0] = VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
                stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
                stages[0].module = vertModule;
                stages[0].pName = "main";
                stages[1] = VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
                stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
                stages[1].module = fragModule;
                stages[1].pName = "main";

                VkPipelineRenderingCreateInfo renderingInfo{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
                renderingInfo.colorAttachmentCount = 1;
                renderingInfo.pColorAttachmentFormats = &swapchainFormat_;

                VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
                pipelineInfo.pNext = &renderingInfo;
                pipelineInfo.stageCount = 2;
                pipelineInfo.pStages = stages;
                pipelineInfo.pVertexInputState = &vertexInput;
                pipelineInfo.pInputAssemblyState = &inputAssembly;
                pipelineInfo.pViewportState = &viewportState;
                pipelineInfo.pRasterizationState = &rasterizer;
                pipelineInfo.pMultisampleState = &multisampling;
                pipelineInfo.pDepthStencilState = &depthStencil;
                pipelineInfo.pColorBlendState = &colorBlending;
                pipelineInfo.pDynamicState = &dynamicState;
                pipelineInfo.layout = compositePipelineLayout_;

                ok = vkCreateGraphicsPipelines(device_, pipelineCache_, 1, &pipelineInfo, nullptr, &compositePipeline_) ==
                     VK_SUCCESS;
            }
            vkDestroyShaderModule(device_, fragModule, nullptr);
        }
    }

    if (!ok) {
        vkDestroyShaderModule(device_, vertModule, nullptr);
        std::fprintf(stderr, "Renderer: composite pipeline creation failed.\n");
        return false;
    }

    // --- Sprint 16 ("Cinematic Graphics") cinematic pass: frame.hdrImage +
    // depth (read) -> frame.cinematicImage (write, full-res kHDRFormat --
    // see FrameSync's own comment) ---
    {
        auto fragCode = readBinaryFile(shaderDir + "/cinematic.frag.spv");
        VkShaderModule fragModule = fragCode.empty() ? VK_NULL_HANDLE : createShaderModule(device_, fragCode);
        if (fragCode.empty() || fragModule == VK_NULL_HANDLE) {
            std::fprintf(stderr, "Renderer: failed to load shaders for cinematic pipeline.\n");
            ok = false;
        } else {
            VkPushConstantRange pushRange{};
            pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            pushRange.offset = 0;
            pushRange.size = sizeof(CinematicPushConstants);

            std::array<VkDescriptorSetLayout, 3> setLayouts{sceneDescriptorSetLayout_, cinematicDescriptorSetLayout_,
                                                            motionBlur_.sampleSetLayout()};
            VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
            layoutInfo.pSetLayouts = setLayouts.data();
            layoutInfo.pushConstantRangeCount = 1;
            layoutInfo.pPushConstantRanges = &pushRange;
            ok = vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &cinematicPipelineLayout_) == VK_SUCCESS;

            if (ok) {
                VkPipelineShaderStageCreateInfo stages[2]{};
                stages[0] = VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
                stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
                stages[0].module = vertModule;
                stages[0].pName = "main";
                stages[1] = VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
                stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
                stages[1].module = fragModule;
                stages[1].pName = "main";

                VkPipelineRenderingCreateInfo renderingInfo{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
                renderingInfo.colorAttachmentCount = 1;
                renderingInfo.pColorAttachmentFormats = &hdrFormat;

                VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
                pipelineInfo.pNext = &renderingInfo;
                pipelineInfo.stageCount = 2;
                pipelineInfo.pStages = stages;
                pipelineInfo.pVertexInputState = &vertexInput;
                pipelineInfo.pInputAssemblyState = &inputAssembly;
                pipelineInfo.pViewportState = &viewportState;
                pipelineInfo.pRasterizationState = &rasterizer;
                pipelineInfo.pMultisampleState = &multisampling;
                pipelineInfo.pDepthStencilState = &depthStencil;
                pipelineInfo.pColorBlendState = &colorBlending;
                pipelineInfo.pDynamicState = &dynamicState;
                pipelineInfo.layout = cinematicPipelineLayout_;

                ok = vkCreateGraphicsPipelines(device_, pipelineCache_, 1, &pipelineInfo, nullptr, &cinematicPipeline_) ==
                     VK_SUCCESS;
            }
            vkDestroyShaderModule(device_, fragModule, nullptr);
        }
    }

    if (!ok) {
        vkDestroyShaderModule(device_, vertModule, nullptr);
        std::fprintf(stderr, "Renderer: cinematic pipeline creation failed.\n");
        return false;
    }

    // --- Kronos ("Rendering Fidelity Foundation" Phase 1.2): real
    // raymarched volumetric fog + light shafts. frame.hdrImage + depth
    // (read) -> frame.fogImage (write, full-res kHDRFormat) -- see
    // shaders/volumetric_fog.frag's own header comment. Own pipeline
    // layout (own push-constant range/struct), but reuses
    // cinematicDescriptorSetLayout_ as-is for set=1 (same real 2-binding
    // hdrColor+sceneDepth shape this pass needs). ---
    {
        auto fragCode = readBinaryFile(shaderDir + "/volumetric_fog.frag.spv");
        VkShaderModule fragModule = fragCode.empty() ? VK_NULL_HANDLE : createShaderModule(device_, fragCode);
        if (fragCode.empty() || fragModule == VK_NULL_HANDLE) {
            std::fprintf(stderr, "Renderer: failed to load shaders for volumetric fog pipeline.\n");
            ok = false;
        } else {
            VkPushConstantRange pushRange{};
            pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            pushRange.offset = 0;
            pushRange.size = sizeof(VolumetricFogPushConstants);

            std::array<VkDescriptorSetLayout, 2> setLayouts{sceneDescriptorSetLayout_, cinematicDescriptorSetLayout_};
            VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
            layoutInfo.pSetLayouts = setLayouts.data();
            layoutInfo.pushConstantRangeCount = 1;
            layoutInfo.pPushConstantRanges = &pushRange;
            ok = vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &volumetricFogPipelineLayout_) == VK_SUCCESS;

            if (ok) {
                VkPipelineShaderStageCreateInfo stages[2]{};
                stages[0] = VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
                stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
                stages[0].module = vertModule;
                stages[0].pName = "main";
                stages[1] = VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
                stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
                stages[1].module = fragModule;
                stages[1].pName = "main";

                VkPipelineRenderingCreateInfo renderingInfo{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
                renderingInfo.colorAttachmentCount = 1;
                renderingInfo.pColorAttachmentFormats = &hdrFormat;

                VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
                pipelineInfo.pNext = &renderingInfo;
                pipelineInfo.stageCount = 2;
                pipelineInfo.pStages = stages;
                pipelineInfo.pVertexInputState = &vertexInput;
                pipelineInfo.pInputAssemblyState = &inputAssembly;
                pipelineInfo.pViewportState = &viewportState;
                pipelineInfo.pRasterizationState = &rasterizer;
                pipelineInfo.pMultisampleState = &multisampling;
                pipelineInfo.pDepthStencilState = &depthStencil;
                pipelineInfo.pColorBlendState = &colorBlending;
                pipelineInfo.pDynamicState = &dynamicState;
                pipelineInfo.layout = volumetricFogPipelineLayout_;

                ok = vkCreateGraphicsPipelines(device_, pipelineCache_, 1, &pipelineInfo, nullptr,
                                                &volumetricFogPipeline_) == VK_SUCCESS;
            }
            vkDestroyShaderModule(device_, fragModule, nullptr);
        }
    }

    if (!ok) {
        vkDestroyShaderModule(device_, vertModule, nullptr);
        std::fprintf(stderr, "Renderer: volumetric fog pipeline creation failed.\n");
        return false;
    }

    // --- Kronos ("Rendering Fidelity" -- SSR fallback pass): real
    // screen-space reflections. frame.hdrImage + depth (read) ->
    // frame.ssrImage (write, full-res kHDRFormat) -- see
    // shaders/ssr.frag's own header comment. Own pipeline layout (own
    // push-constant range/struct), reuses cinematicDescriptorSetLayout_
    // as-is for set=1, same real 2-binding hdrColor+sceneDepth shape
    // volumetricFogPipelineLayout_ above already reuses it for. ---
    {
        auto fragCode = readBinaryFile(shaderDir + "/ssr.frag.spv");
        VkShaderModule fragModule = fragCode.empty() ? VK_NULL_HANDLE : createShaderModule(device_, fragCode);
        if (fragCode.empty() || fragModule == VK_NULL_HANDLE) {
            std::fprintf(stderr, "Renderer: failed to load shaders for SSR pipeline.\n");
            ok = false;
        } else {
            VkPushConstantRange pushRange{};
            pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            pushRange.offset = 0;
            pushRange.size = sizeof(SSRPushConstants);

            std::array<VkDescriptorSetLayout, 2> setLayouts{sceneDescriptorSetLayout_, cinematicDescriptorSetLayout_};
            VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
            layoutInfo.pSetLayouts = setLayouts.data();
            layoutInfo.pushConstantRangeCount = 1;
            layoutInfo.pPushConstantRanges = &pushRange;
            ok = vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &ssrPipelineLayout_) == VK_SUCCESS;

            if (ok) {
                VkPipelineShaderStageCreateInfo stages[2]{};
                stages[0] = VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
                stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
                stages[0].module = vertModule;
                stages[0].pName = "main";
                stages[1] = VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
                stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
                stages[1].module = fragModule;
                stages[1].pName = "main";

                VkPipelineRenderingCreateInfo renderingInfo{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
                renderingInfo.colorAttachmentCount = 1;
                renderingInfo.pColorAttachmentFormats = &hdrFormat;

                VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
                pipelineInfo.pNext = &renderingInfo;
                pipelineInfo.stageCount = 2;
                pipelineInfo.pStages = stages;
                pipelineInfo.pVertexInputState = &vertexInput;
                pipelineInfo.pInputAssemblyState = &inputAssembly;
                pipelineInfo.pViewportState = &viewportState;
                pipelineInfo.pRasterizationState = &rasterizer;
                pipelineInfo.pMultisampleState = &multisampling;
                pipelineInfo.pDepthStencilState = &depthStencil;
                pipelineInfo.pColorBlendState = &colorBlending;
                pipelineInfo.pDynamicState = &dynamicState;
                pipelineInfo.layout = ssrPipelineLayout_;

                ok = vkCreateGraphicsPipelines(device_, pipelineCache_, 1, &pipelineInfo, nullptr, &ssrPipeline_) ==
                     VK_SUCCESS;
            }
            vkDestroyShaderModule(device_, fragModule, nullptr);
        }
    }

    if (!ok) {
        vkDestroyShaderModule(device_, vertModule, nullptr);
        std::fprintf(stderr, "Renderer: SSR pipeline creation failed.\n");
        return false;
    }

    // --- Sprint 16 auto-exposure: frame.hdrImage (read) -> a real 1x1
    // luminance target (write) -- see FrameSync's own comment and
    // Renderer::drawLuminancePass(). Reuses postProcessSingleSetLayout_
    // as-is (bloom extract's own layout shape: one combined-image-sampler
    // binding) rather than a bespoke layout. ---
    {
        auto fragCode = readBinaryFile(shaderDir + "/luminance.frag.spv");
        VkShaderModule fragModule = fragCode.empty() ? VK_NULL_HANDLE : createShaderModule(device_, fragCode);
        if (fragCode.empty() || fragModule == VK_NULL_HANDLE) {
            std::fprintf(stderr, "Renderer: failed to load shaders for luminance pipeline.\n");
            ok = false;
        } else {
            VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            layoutInfo.setLayoutCount = 1;
            layoutInfo.pSetLayouts = &postProcessSingleSetLayout_;
            ok = vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &luminancePipelineLayout_) == VK_SUCCESS;

            if (ok) {
                VkPipelineShaderStageCreateInfo stages[2]{};
                stages[0] = VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
                stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
                stages[0].module = vertModule;
                stages[0].pName = "main";
                stages[1] = VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
                stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
                stages[1].module = fragModule;
                stages[1].pName = "main";

                VkFormat luminanceFormat = VK_FORMAT_R32_SFLOAT;
                VkPipelineRenderingCreateInfo renderingInfo{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
                renderingInfo.colorAttachmentCount = 1;
                renderingInfo.pColorAttachmentFormats = &luminanceFormat;

                VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
                pipelineInfo.pNext = &renderingInfo;
                pipelineInfo.stageCount = 2;
                pipelineInfo.pStages = stages;
                pipelineInfo.pVertexInputState = &vertexInput;
                pipelineInfo.pInputAssemblyState = &inputAssembly;
                pipelineInfo.pViewportState = &viewportState;
                pipelineInfo.pRasterizationState = &rasterizer;
                pipelineInfo.pMultisampleState = &multisampling;
                pipelineInfo.pDepthStencilState = &depthStencil;
                pipelineInfo.pColorBlendState = &colorBlending;
                pipelineInfo.pDynamicState = &dynamicState;
                pipelineInfo.layout = luminancePipelineLayout_;

                ok = vkCreateGraphicsPipelines(device_, pipelineCache_, 1, &pipelineInfo, nullptr, &luminancePipeline_) ==
                     VK_SUCCESS;
            }
            vkDestroyShaderModule(device_, fragModule, nullptr);
        }
    }

    vkDestroyShaderModule(device_, vertModule, nullptr);

    if (!ok) {
        std::fprintf(stderr, "Renderer: luminance pipeline creation failed.\n");
        return false;
    }
    return true;
}

bool Renderer::createSkyPipeline() {
    std::string shaderDir = resolveResourceDir(executableDirectory(), "shaders", ENGINE_SHADER_DIR);
    auto vertCode = readBinaryFile(shaderDir + "/fullscreen.vert.spv");
    auto fragCode = readBinaryFile(shaderDir + "/sky.frag.spv");
    if (vertCode.empty() || fragCode.empty()) {
        std::fprintf(stderr, "Renderer: failed to read compiled shaders for the sky pipeline.\n");
        return false;
    }
    VkShaderModule vertModule = createShaderModule(device_, vertCode);
    VkShaderModule fragModule = createShaderModule(device_, fragCode);
    if (vertModule == VK_NULL_HANDLE || fragModule == VK_NULL_HANDLE) {
        std::fprintf(stderr, "Renderer: vkCreateShaderModule failed for the sky pipeline.\n");
        if (vertModule != VK_NULL_HANDLE) vkDestroyShaderModule(device_, vertModule, nullptr);
        if (fragModule != VK_NULL_HANDLE) vkDestroyShaderModule(device_, fragModule, nullptr);
        return false;
    }

    // Same set 0 every scene/shadow pipeline already binds (SceneUBO +
    // the shadow map array) -- sky.frag only reads a handful of SceneUBO
    // fields (invViewProj, viewPositionWS, skyZenithColor/skyHorizonColor)
    // and never touches binding 1 at all, the same "declares/binds more
    // than it reads" shape createShadowPipeline() already established
    // for this exact set layout.
    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &sceneDescriptorSetLayout_;
    bool ok = vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &skyPipelineLayout_) == VK_SUCCESS;

    if (ok) {
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertModule;
        stages[0].pName = "main";
        stages[1] = VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragModule;
        stages[1].pName = "main";

        // No vertex input at all (shaders/fullscreen.vert), no depth
        // test/write (a background pass must never occlude or be
        // occluded by depth -- scene geometry drawn afterward with a
        // normal depth test naturally overdraws whatever it covers), no
        // blending (this pass writes every covered pixel outright).
        VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rasterizer{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                               VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_FALSE;
        VkPipelineColorBlendStateCreateInfo colorBlending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        std::array<VkPipelineColorBlendAttachmentState, 2> blendAttachments{colorBlendAttachment, {}};
        blendAttachments[1].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT;
        colorBlending.attachmentCount = 2;
        colorBlending.pAttachments = blendAttachments.data();
        VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamicState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamicState.dynamicStateCount = 2;
        dynamicState.pDynamicStates = dynamicStates;

        VkFormat hdrFormat = kHDRFormat; // writes into frame.hdrView, same target scene geometry draws into
        std::array<VkFormat, 2> sceneColorFormats{hdrFormat, kVelocityFormat};
        VkPipelineRenderingCreateInfo renderingInfo{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        renderingInfo.colorAttachmentCount = 2;
        renderingInfo.pColorAttachmentFormats = sceneColorFormats.data();
        renderingInfo.depthAttachmentFormat = depthFormat_; // must match the render scope's real depth attachment format even though this pipeline never tests/writes it

        VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pipelineInfo.pNext = &renderingInfo;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = stages;
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = skyPipelineLayout_;

        ok = vkCreateGraphicsPipelines(device_, pipelineCache_, 1, &pipelineInfo, nullptr, &skyPipeline_) == VK_SUCCESS;
    }

    vkDestroyShaderModule(device_, vertModule, nullptr);
    vkDestroyShaderModule(device_, fragModule, nullptr);

    if (!ok) {
        std::fprintf(stderr, "Renderer: sky pipeline creation failed.\n");
        return false;
    }
    return true;
}

void Renderer::destroySkyPipeline() {
    if (skyPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, skyPipeline_, nullptr);
        skyPipeline_ = VK_NULL_HANDLE;
    }
    if (skyPipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, skyPipelineLayout_, nullptr);
        skyPipelineLayout_ = VK_NULL_HANDLE;
    }
}

void Renderer::destroyPostProcessPipelines() {
    if (luminancePipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, luminancePipeline_, nullptr);
        luminancePipeline_ = VK_NULL_HANDLE;
    }
    if (luminancePipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, luminancePipelineLayout_, nullptr);
        luminancePipelineLayout_ = VK_NULL_HANDLE;
    }
    if (cinematicPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, cinematicPipeline_, nullptr);
        cinematicPipeline_ = VK_NULL_HANDLE;
    }
    if (cinematicPipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, cinematicPipelineLayout_, nullptr);
        cinematicPipelineLayout_ = VK_NULL_HANDLE;
    }
    if (volumetricFogPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, volumetricFogPipeline_, nullptr);
        volumetricFogPipeline_ = VK_NULL_HANDLE;
    }
    if (volumetricFogPipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, volumetricFogPipelineLayout_, nullptr);
        volumetricFogPipelineLayout_ = VK_NULL_HANDLE;
    }
    if (ssrPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, ssrPipeline_, nullptr);
        ssrPipeline_ = VK_NULL_HANDLE;
    }
    if (ssrPipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, ssrPipelineLayout_, nullptr);
        ssrPipelineLayout_ = VK_NULL_HANDLE;
    }
    if (compositePipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, compositePipeline_, nullptr);
        compositePipeline_ = VK_NULL_HANDLE;
    }
    if (compositePipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, compositePipelineLayout_, nullptr);
        compositePipelineLayout_ = VK_NULL_HANDLE;
    }
    if (bloomExtractPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, bloomExtractPipeline_, nullptr);
        bloomExtractPipeline_ = VK_NULL_HANDLE;
    }
    if (bloomExtractPipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, bloomExtractPipelineLayout_, nullptr);
        bloomExtractPipelineLayout_ = VK_NULL_HANDLE;
    }
}

} // namespace engine::core
