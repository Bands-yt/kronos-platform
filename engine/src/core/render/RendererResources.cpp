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

bool Renderer::createCommandPoolAndBuffers() {
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = *queueFamilies_.graphics;
    if (vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreateCommandPool failed.\n");
        return false;
    }

    frames_.resize(framesInFlight_);
    std::vector<VkCommandBuffer> buffers(framesInFlight_);

    VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocInfo.commandPool = commandPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = framesInFlight_;
    if (vkAllocateCommandBuffers(device_, &allocInfo, buffers.data()) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkAllocateCommandBuffers failed.\n");
        return false;
    }
    for (uint32_t i = 0; i < framesInFlight_; ++i) {
        frames_[i].commandBuffer = buffers[i];
    }
    return true;
}

bool Renderer::createSyncObjects() {
    VkSemaphoreCreateInfo semInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (auto& frame : frames_) {
        if (vkCreateSemaphore(device_, &semInfo, nullptr, &frame.imageAvailable) != VK_SUCCESS ||
            vkCreateSemaphore(device_, &semInfo, nullptr, &frame.renderFinished) != VK_SUCCESS ||
            vkCreateFence(device_, &fenceInfo, nullptr, &frame.inFlight) != VK_SUCCESS) {
            std::fprintf(stderr, "Renderer: failed to create per-frame sync objects.\n");
            return false;
        }
    }

    if (!createPerImageSemaphores()) return false;
    return true;
}

// One render-finished semaphore per swapchain image -- see the member's
// own comment for why per-frame was wrong. Recreated with the swapchain,
// since the image count can change on resize.

// Kronos ("Bindless Descriptors"): one global texture array, bound once
// per frame instead of a per-material descriptor set per draw.
bool Renderer::createBindlessResources() {
    if (!bindlessSupported_) return true; // graceful: the per-material path stays in use

    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    if (vkCreateSampler(device_, &samplerInfo, nullptr, &bindlessSampler_) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: failed to create the bindless sampler.\n");
        return false;
    }

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = kBindlessTextureCapacity;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // PARTIALLY_BOUND: not every slot is populated.
    // UPDATE_AFTER_BIND: textures are registered while the set is already
    // bound by in-flight command buffers.
    VkDescriptorBindingFlags bindingFlags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
                                             VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
                                             VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT;
    VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
    flagsInfo.bindingCount = 1;
    flagsInfo.pBindingFlags = &bindingFlags;

    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    layoutInfo.pNext = &flagsInfo;
    if (vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &bindlessSetLayout_) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: failed to create the bindless descriptor set layout.\n");
        return false;
    }

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kBindlessTextureCapacity};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &bindlessPool_) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: failed to create the bindless descriptor pool.\n");
        return false;
    }

    VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.descriptorPool = bindlessPool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &bindlessSetLayout_;
    if (vkAllocateDescriptorSets(device_, &allocInfo, &bindlessSet_) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: failed to allocate the bindless descriptor set.\n");
        return false;
    }

    // Pre-fill every slot with the default white texture. PARTIALLY_BOUND
    // permits unwritten slots, but only while nothing samples them --
    // pre-filling removes that footgun rather than relying on every future
    // shader index being valid.
    //
    // White is the right default for albedo/metallic/roughness/AO, but the
    // WRONG one for a normal map: sampling white decodes to a tangent-space
    // normal of (1,1,1), which visibly wrecks the shading of every entity
    // that simply has no normal map. Slot 1 is therefore reserved and
    // written with the flat-normal default instead, mirroring what
    // getOrCreateMaterialDescriptorSet() already does per-material.
    if (defaultWhiteTexture_.isValid()) {
        std::vector<VkDescriptorImageInfo> infos(kBindlessTextureCapacity,
                                                  VkDescriptorImageInfo{bindlessSampler_, defaultWhiteTexture_.view(),
                                                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = bindlessSet_;
        write.dstBinding = 0;
        write.dstArrayElement = 0;
        write.descriptorCount = kBindlessTextureCapacity;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = infos.data();
        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    }

    // Claim the two reserved keys first, so a fresh table hands them slots
    // 0 and 1 before any real texture can take them.
    (void)bindlessTable_.acquire(kBindlessWhiteKey);
    (void)bindlessTable_.acquire(kBindlessFlatNormalKey);
    bindlessTable_.clearPendingWrites(); // both are written here, not lazily

    if (defaultFlatNormalTexture_.isValid()) {
        VkDescriptorImageInfo normalInfo{bindlessSampler_, defaultFlatNormalTexture_.view(),
                                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = bindlessSet_;
        write.dstBinding = 0;
        write.dstArrayElement = kBindlessFlatNormalSlot;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &normalInfo;
        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    }

    bindlessInitialised_ = true;
    std::fprintf(stderr, "Renderer: bindless texture array active -- %u slots, set 2 (%s frag variant).\n",
                 kBindlessTextureCapacity, rayTracingSupported_ ? "scene_rt_bindless" : "scene_bindless");
    return true;
}

void Renderer::destroyBindlessResources() {
    if (bindlessPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, bindlessPool_, nullptr);
        bindlessPool_ = VK_NULL_HANDLE;
    }
    if (bindlessSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, bindlessSetLayout_, nullptr);
        bindlessSetLayout_ = VK_NULL_HANDLE;
    }
    if (bindlessSampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device_, bindlessSampler_, nullptr);
        bindlessSampler_ = VK_NULL_HANDLE;
    }
    bindlessSet_ = VK_NULL_HANDLE;
    bindlessTable_.clear();
    bindlessInitialised_ = false;
}

uint32_t Renderer::bindlessSlotFor(uint32_t handle, TextureLibrary& textureLibrary, uint32_t defaultSlot) {
    if (!bindlessInitialised_) return defaultSlot;

    const Texture* texture = handle != Renderable::kInvalidHandle ? textureLibrary.get(handle) : nullptr;
    const bool usable = texture != nullptr && texture->isValid();
    // The caller's default slot is already pre-filled; an unusable handle
    // maps there rather than allocating a slot for nothing.
    if (!usable) return defaultSlot;

    // Keys 0 and 1 are the reserved defaults (see createBindlessResources()).
    const uint64_t key = static_cast<uint64_t>(handle) + kBindlessReservedKeyCount;
    const uint32_t slot = bindlessTable_.acquire(key);
    if (slot == BindlessTextureTable::kInvalidSlot) {
        // Exhausted. Degrade to the default rather than indexing out of
        // range, which would sample whatever descriptor happens to sit there.
        return defaultSlot;
    }

    // Write the descriptor only for slots the table reports as new.
    if (!bindlessTable_.pendingWrites().empty()) {
        std::vector<VkWriteDescriptorSet> writes;
        std::vector<VkDescriptorImageInfo> infos;
        writes.reserve(bindlessTable_.pendingWrites().size());
        infos.reserve(bindlessTable_.pendingWrites().size());
        for (uint32_t pending : bindlessTable_.pendingWrites()) {
            if (pending != slot) continue; // only this frame's newly-acquired slot is resolvable here
            infos.push_back(VkDescriptorImageInfo{bindlessSampler_, texture->view(),
                                                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = bindlessSet_;
            write.dstBinding = 0;
            write.dstArrayElement = pending;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pImageInfo = &infos.back();
            writes.push_back(write);
        }
        if (!writes.empty()) vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        bindlessTable_.clearPendingWrites();
    }
    return slot;
}

glm::uvec4 Renderer::packTextureIndices(const Renderable& renderable, TextureLibrary& textureLibrary) {
    if (!bindlessInitialised_) return glm::uvec4(0u);

    const uint32_t albedo    = bindlessSlotFor(renderable.albedoTexture, textureLibrary, kBindlessWhiteSlot);
    const uint32_t normal    = bindlessSlotFor(renderable.normalTexture, textureLibrary, kBindlessFlatNormalSlot);
    const uint32_t metallic  = bindlessSlotFor(renderable.metallicTexture, textureLibrary, kBindlessWhiteSlot);
    const uint32_t roughness = bindlessSlotFor(renderable.roughnessTexture, textureLibrary, kBindlessWhiteSlot);
    const uint32_t ao        = bindlessSlotFor(renderable.aoTexture, textureLibrary, kBindlessWhiteSlot);

    // Two 16-bit slots per component. kBindlessTextureCapacity is 2048, so
    // every slot fits in 16 bits with room to spare -- the packing exists
    // to keep ObjectPushConstants inside the 128-byte guaranteed minimum,
    // not to save memory. Must match the unpacking in scene.frag.
    return glm::uvec4(albedo | (normal << 16), metallic | (roughness << 16), ao, 0u);
}


bool Renderer::initSceneDescriptorResourcesFor(FrameSync& frame) {
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = sizeof(SceneUBO);
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo resultInfo{};
    if (vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo, &frame.sceneUboBuffer, &frame.sceneUboAllocation,
                         &resultInfo) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vmaCreateBuffer (scene UBO) failed.\n");
        return false;
    }
    frame.sceneUboMapped = resultInfo.pMappedData; // persistently mapped -- no per-frame map/unmap needed

    VkDescriptorSetAllocateInfo dsAllocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dsAllocInfo.descriptorPool = sceneDescriptorPool_;
    dsAllocInfo.descriptorSetCount = 1;
    dsAllocInfo.pSetLayouts = &sceneDescriptorSetLayout_;
    if (vkAllocateDescriptorSets(device_, &dsAllocInfo, &frame.sceneDescriptorSet) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkAllocateDescriptorSets (scene) failed.\n");
        return false;
    }

    VkDescriptorBufferInfo bufInfo{frame.sceneUboBuffer, 0, sizeof(SceneUBO)};
    // shadowArrayView is the VK_IMAGE_VIEW_TYPE_2D_ARRAY view over all
    // kCascadeCount layers -- what the fragment shader's sampler2DArray
    // binding actually samples through (see FrameSync's comment). Must
    // already exist -- callers run initShadowResourcesFor(frame) first.
    VkDescriptorImageInfo shadowImageInfo{shadowSampler_, frame.shadowArrayView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

    std::vector<VkWriteDescriptorSet> writes;
    VkWriteDescriptorSet uboWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    uboWrite.dstSet = frame.sceneDescriptorSet;
    uboWrite.dstBinding = 0;
    uboWrite.descriptorCount = 1;
    uboWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboWrite.pBufferInfo = &bufInfo;
    writes.push_back(uboWrite);

    VkWriteDescriptorSet shadowWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    shadowWrite.dstSet = frame.sceneDescriptorSet;
    shadowWrite.dstBinding = 1;
    shadowWrite.descriptorCount = 1;
    shadowWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    shadowWrite.pImageInfo = &shadowImageInfo;
    writes.push_back(shadowWrite);

    // Valid to write a descriptor referencing frame.shadowArrayView
    // before that image has actually been transitioned to
    // SHADER_READ_ONLY_OPTIMAL for the first time -- the descriptor
    // write only records *intent*; the image must be in that layout
    // by the time a draw call actually samples through it, which
    // drawShadowPass()'s post-pass transition guarantees every frame.
    vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

    if (rayTracingSupported_ && !rayTracingScene_.initializeFrame(frame.rayTracing)) {
        std::fprintf(stderr, "Renderer: failed to create a ray-tracing frame.\n");
        return false;
    }
    updateRayTracedShadowDescriptor(frame);
    return true;
}

void Renderer::updateRayTracedShadowDescriptor(FrameSync& frame) {
    if (!rayTracingSupported_ || frame.rayTracing.tlas == VK_NULL_HANDLE) return;

    VkWriteDescriptorSetAccelerationStructureKHR asWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
    asWrite.accelerationStructureCount = 1;
    asWrite.pAccelerationStructures = &frame.rayTracing.tlas;
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.pNext = &asWrite;
    write.dstSet = frame.sceneDescriptorSet;
    write.dstBinding = 2;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

    VkDescriptorBufferInfo recordsInfo{frame.rayTracing.instanceData.buffer, 0, VK_WHOLE_SIZE};
    VkWriteDescriptorSet recordsWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    recordsWrite.dstSet = frame.sceneDescriptorSet;
    recordsWrite.dstBinding = 3;
    recordsWrite.descriptorCount = 1;
    recordsWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    recordsWrite.pBufferInfo = &recordsInfo;

    std::array<VkWriteDescriptorSet, 2> writes{write, recordsWrite};
    vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

void Renderer::recordRayTracingScene(VkCommandBuffer cmd, FrameSync& frame, ECS& ecs, MeshLibrary& meshLibrary,
                                     TextureLibrary& textureLibrary, RiggedMeshLibrary* riggedMeshLibrary) {
    rtMeshInstances_.clear();
    rtSkinnedInstances_.clear();
    auto view = ecs.view<Transform, Renderable>();
    for (auto entity : view) {
        const auto& renderable = view.get<Renderable>(entity);
        if (!renderable.visible || renderable.unlitSilhouette) continue;
        const Mesh* mesh = meshLibrary.get(renderable.meshHandle);
        if (mesh == nullptr || !mesh->rayTracingReady()) continue;
        uint32_t albedoSlot = kBindlessWhiteSlot;
        if (bindlessInitialised_ && !renderable.useTriplanarProjection) {
            albedoSlot = bindlessSlotFor(renderable.albedoTexture, textureLibrary, kBindlessWhiteSlot);
        }
        RtMeshInstance instance;
        instance.mesh = mesh;
        instance.transform = hierarchy::computeWorldMatrix(ecs, entity);
        instance.mask = rtInstanceMask(renderable.castsShadow && renderable.transmission <= 0.0f);
        instance.material = makeRtMaterial(renderable.baseColor, renderable.metallic, renderable.roughness,
                                           renderable.emissiveColor, renderable.emissiveIntensity, albedoSlot);
        rtMeshInstances_.push_back(instance);
    }
    if (riggedMeshLibrary != nullptr) {
        auto skinnedView = ecs.view<Transform, SkinnedRenderable>();
        for (auto entity : skinnedView) {
            const auto& skinned = skinnedView.get<SkinnedRenderable>(entity);
            if (!skinned.visible || skinned.skinningMatrices.empty()) continue;
            const RiggedMesh* rigged = riggedMeshLibrary->get(skinned.riggedMeshHandle);
            if (rigged == nullptr || !rigged->mesh().rayTracingReady()) continue;
            RtSkinnedInstance instance;
            instance.mesh = rigged;
            instance.key = static_cast<uint64_t>(entt::to_integral(entity));
            instance.transform = hierarchy::computeWorldMatrix(ecs, entity);
            instance.palette = skinned.skinningMatrices.data();
            instance.jointCount =
                std::min(static_cast<uint32_t>(skinned.skinningMatrices.size()), kMaxJointsPerSkeleton);
            instance.mask = rtInstanceMask(skinned.castsShadow);
            instance.material = makeRtMaterial(skinned.baseColor, skinned.metallic, skinned.roughness,
                                               skinned.emissiveColor, skinned.emissiveIntensity, kBindlessWhiteSlot);
            rtSkinnedInstances_.push_back(instance);
        }
    }
    if (rayTracingScene_.record(cmd, frame.rayTracing, rtMeshInstances_, rtSkinnedInstances_)) {
        updateRayTracedShadowDescriptor(frame);
    }
    lastRtInstanceCount_ = frame.rayTracing.instanceCount;
}

render::GpuContext Renderer::gpuContext() const {
    return render::GpuContext{device_, allocator_, pipelineCache_,
                              resolveResourceDir(executableDirectory(), "shaders", ENGINE_SHADER_DIR)};
}

bool Renderer::initRenderModules() {
    render::GpuContext ctx = gpuContext();
    if (rayTracingSupported_ && !rayTracingScene_.initializeSkinning(ctx)) {
        std::fprintf(stderr, "Renderer: ray-traced skinning unavailable -- characters are left out of the TLAS.\n");
    }
    if (!ibl_.initialize(ctx)) {
        std::fprintf(stderr, "Renderer: image-based lighting setup failed.\n");
        return false;
    }
    if (!clusteredLighting_.initialize(ctx, sceneDescriptorSetLayout_)) {
        std::fprintf(stderr, "Renderer: clustered lighting setup failed.\n");
        return false;
    }
    if (!temporalAA_.initialize(ctx, framesInFlight_ + static_cast<uint32_t>(kMaxAuxiliaryScenes))) {
        std::fprintf(stderr, "Renderer: temporal AA setup failed.\n");
        return false;
    }
    if (!motionBlur_.initialize(ctx, framesInFlight_ + static_cast<uint32_t>(kMaxAuxiliaryScenes))) {
        std::fprintf(stderr, "Renderer: motion blur setup failed.\n");
        return false;
    }
    mainViewHistory_ = std::make_shared<render::ViewHistory>();
    for (auto& frame : frames_) {
        if (!initFrameRenderResources(frame, true)) return false;
    }
    return true;
}

void Renderer::shutdownRenderModules() {
    render::GpuContext ctx = gpuContext();
    for (auto& frame : frames_) destroyFrameRenderResources(frame);
    motionBlur_.shutdown(ctx);
    temporalAA_.shutdown(ctx);
    clusteredLighting_.shutdown(ctx);
    ibl_.shutdown(ctx);
    if (mainViewHistory_) render::TemporalAA::destroyHistory(ctx, *mainViewHistory_);
    mainViewHistory_.reset();
}

bool Renderer::initFrameRenderResources(FrameSync& frame, bool mainView) {
    render::GpuContext ctx = gpuContext();
    if (!render::ClusteredLighting::createFrameResources(ctx, frame.clusterResources)) return false;
    if (!render::createBuffer(ctx, sizeof(GpuObjectRecord) * kMaxObjectRecords, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                              true, frame.objectRecords)) {
        return false;
    }
    static_cast<GpuObjectRecord*>(frame.objectRecords.mapped)[0] = GpuObjectRecord{};
    frame.objectRecordCount = 1;
    // Frames in flight of one view share a history: frame N+1's resolve must read what frame N wrote.
    frame.viewHistory = mainView ? mainViewHistory_ : std::make_shared<render::ViewHistory>();
    writeSceneModuleDescriptors(frame);
    return true;
}

void Renderer::destroyFrameRenderResources(FrameSync& frame) {
    render::GpuContext ctx = gpuContext();
    render::ClusteredLighting::destroyFrameResources(ctx, frame.clusterResources);
    render::destroyBuffer(ctx, frame.objectRecords);
    render::destroyImage(ctx, frame.velocity);
    motionBlur_.releaseBinding(ctx, frame.motionBlurBinding);
    temporalAA_.releaseBinding(ctx, frame.taaBinding);
    if (frame.viewHistory && frame.viewHistory.use_count() == 1) {
        render::TemporalAA::destroyHistory(ctx, *frame.viewHistory);
    }
    frame.viewHistory.reset();
}

void Renderer::writeSceneModuleDescriptors(FrameSync& frame) {
    VkDescriptorImageInfo compareInfo{shadowCompareSampler_, frame.shadowArrayView,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo envInfo{ibl_.sampler(), ibl_.prefilteredView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo dfgInfo{ibl_.sampler(), ibl_.dfgView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo spotShadowInfo{shadowCompareSampler_, frame.spotShadowMaps.array.view,
                                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    std::array<VkDescriptorBufferInfo, 5> buffers{{
        {ibl_.shBuffer(), 0, VK_WHOLE_SIZE},
        {frame.clusterResources.lights.buffer, 0, VK_WHOLE_SIZE},
        {frame.clusterResources.counts.buffer, 0, VK_WHOLE_SIZE},
        {frame.clusterResources.indices.buffer, 0, VK_WHOLE_SIZE},
        {frame.objectRecords.buffer, 0, VK_WHOLE_SIZE},
    }};

    std::array<VkWriteDescriptorSet, 9> writes{};
    const std::pair<uint32_t, const VkDescriptorImageInfo*> images[] = {
        {4, &compareInfo}, {5, &envInfo}, {6, &dfgInfo}, {12, &spotShadowInfo}};
    for (uint32_t i = 0; i < 4; ++i) {
        writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writes[i].dstSet = frame.sceneDescriptorSet;
        writes[i].dstBinding = images[i].first;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo = images[i].second;
    }
    for (uint32_t i = 0; i < buffers.size(); ++i) {
        VkWriteDescriptorSet& w = writes[4 + i];
        w = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstSet = frame.sceneDescriptorSet;
        w.dstBinding = 7 + i;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w.pBufferInfo = &buffers[i];
    }
    vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

StaticBatchSet* Renderer::prepareStaticBatches(ECS& ecs, MeshLibrary& meshLibrary) {
    auto& batching = ecs.raw().ctx().emplace<StaticBatchSet>();
    if (batching.backend().owner != &meshLibrary) {
        batching = StaticBatchSet{};
        auto freeHandles = std::make_shared<std::vector<uint32_t>>();
        StaticBatchBackend backend;
        backend.owner = &meshLibrary;
        backend.meshVersion = [&meshLibrary](uint32_t handle) -> uint64_t {
            const Mesh* mesh = meshLibrary.get(handle);
            return mesh != nullptr ? mesh->uid() : 0;
        };
        backend.fetch = [this, &meshLibrary](uint32_t handle) -> std::shared_ptr<const HostMesh> {
            const Mesh* mesh = meshLibrary.get(handle);
            auto host = std::make_shared<HostMesh>();
            if (mesh == nullptr ||
                !mesh->downloadToHost(allocator_, device_, commandPool_, graphicsQueue_, host->vertices, host->indices)) {
                return nullptr;
            }
            return host;
        };
        backend.upload = [this, &meshLibrary, freeHandles](const HostMesh& merged) -> uint32_t {
            Mesh mesh;
            if (!mesh.uploadFromHost(allocator_, device_, commandPool_, graphicsQueue_, merged.vertices, merged.indices)) {
                return Renderable::kInvalidHandle;
            }
            if (freeHandles->empty()) return meshLibrary.registerMesh(std::move(mesh));
            const uint32_t handle = freeHandles->back();
            freeHandles->pop_back();
            meshLibrary.replaceMesh(handle, std::move(mesh), allocator_);
            return handle;
        };
        backend.release = [this, &meshLibrary, freeHandles](uint32_t handle) {
            meshLibrary.destroyMesh(handle, allocator_);
            freeHandles->push_back(handle);
        };
        batching.setBackend(std::move(backend));
    }
    batching.update(ecs, frameSerial_);
    return &batching;
}

uint32_t Renderer::pushObjectRecord(FrameSync& frame, EntityId entity, const MaterialLayers& layers,
                                    const glm::mat4& model) {
    const uint32_t key = static_cast<uint32_t>(entt::to_integral(entity));
    render::ViewHistory* history = frame.viewHistory.get();
    const glm::mat4* previous = nullptr;
    if (history != nullptr) {
        history->currentModels[key] = model;
        auto it = history->previousModels.find(key);
        if (it != history->previousModels.end()) previous = &it->second;
    }
    // Static objects with default materials share record 0 -- camera motion alone gives them correct velocity.
    const bool moved = previous != nullptr && *previous != model;
    if ((!moved && layers.isDefault()) || frame.objectRecordCount >= kMaxObjectRecords) return 0;

    GpuObjectRecord record;
    record.prevModel = moved ? *previous : model;
    record.misc.y = moved ? 1.0f : 0.0f;
    const MaterialLayers& l = layers;
    record.clearcoat = {l.clearcoat, l.clearcoatRoughness, l.anisotropy, l.anisotropyRotation};
    record.sheen = {l.sheenColor, l.sheenRoughness};
    record.misc.x = l.specular;
    record.misc.z = l.waterWaves;
    record.misc.w = l.waterFoam;
    record.pattern.x = l.gridSize;

    uint32_t index = frame.objectRecordCount++;
    static_cast<GpuObjectRecord*>(frame.objectRecords.mapped)[index] = record;
    return index;
}

void Renderer::destroySceneDescriptorResourcesFor(FrameSync& frame) {
    rayTracingScene_.destroyFrame(frame.rayTracing);
    if (frame.sceneUboBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, frame.sceneUboBuffer, frame.sceneUboAllocation);
        frame.sceneUboBuffer = VK_NULL_HANDLE;
    }
    // frame.sceneDescriptorSet is NOT individually freed here --
    // sceneDescriptorPool_ was created without
    // VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT (destroying the
    // whole pool is the only way to reclaim its sets, same as every
    // other descriptor pool in this file); it's simply never bound again
    // once its owning frame/auxiliary scene is torn down.
}

bool Renderer::createSceneDescriptorResources() {
    VkDescriptorSetLayoutBinding uboBinding{};
    uboBinding.binding = 0;
    uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding shadowBinding{};
    shadowBinding.binding = 1;
    shadowBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    shadowBinding.descriptorCount = 1;
    shadowBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT; // only scene.frag's computeShadow() samples it

    std::vector<VkDescriptorSetLayoutBinding> bindings{uboBinding, shadowBinding};

    // Sprint 14 ("RTX Upgrade" Phase 2): binding 2, the real TLAS --
    // only added to this layout at all when rayTracingSupported_ (decided
    // once, at device-creation time, before this function ever runs).
    // Every pipeline sharing sceneDescriptorSetLayout_ (shadow/instanced/
    // particle/sky, see createScenePipeline()'s own comment on why they
    // all reuse scenePipelineLayout_) is unaffected either way -- their
    // shaders simply never declare binding 2, which Vulkan doesn't
    // require them to. Only scene.frag's own real ray-tracing-capable
    // variant (scene_rt.frag, selected in createScenePipeline() when
    // rayTracingSupported_) statically references it.
    VkDescriptorSetLayoutBinding tlasBinding{};
    if (rayTracingSupported_) {
        tlasBinding.binding = 2;
        tlasBinding.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        tlasBinding.descriptorCount = 1;
        tlasBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings.push_back(tlasBinding);
    }

    // Kronos ("Rendering Fidelity Foundation" Phase 1.3): binding 3, the
    // real per-instance material storage buffer
    // (RayTracingScene::materialsBuffer()) hybrid RT reflections read to
    // shade a ray-query hit -- same rayTracingSupported_ gating as
    // binding 2 above (this binding is meaningless without a real TLAS
    // to hit-test against in the first place).
    VkDescriptorSetLayoutBinding materialsBinding{};
    if (rayTracingSupported_) {
        materialsBinding.binding = 3;
        materialsBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        materialsBinding.descriptorCount = 1;
        materialsBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings.push_back(materialsBinding);
    }

    auto addBinding = [&](uint32_t binding, VkDescriptorType type, VkShaderStageFlags stages) {
        VkDescriptorSetLayoutBinding b{};
        b.binding = binding;
        b.descriptorType = type;
        b.descriptorCount = 1;
        b.stageFlags = stages;
        bindings.push_back(b);
    };
    constexpr VkShaderStageFlags kComputeFragment = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    addBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);  // shadow compare
    addBinding(5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);  // prefiltered env
    addBinding(6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);  // DFG LUT
    addBinding(7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT);          // irradiance SH
    addBinding(8, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kComputeFragment);                      // lights
    addBinding(9, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kComputeFragment);                      // cluster counts
    addBinding(10, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kComputeFragment);                     // cluster indices
    addBinding(11, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);                   // object records
    addBinding(12, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT); // spot shadow maps

    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &sceneDescriptorSetLayout_) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreateDescriptorSetLayout failed.\n");
        return false;
    }

    // Sized for framesInFlight_ *plus* kMaxAuxiliaryScenes up front --
    // Vulkan descriptor pools can't grow, and createAuxiliaryScene()
    // allocates one more scene descriptor set from this same pool per
    // studio::PreviewScene. See AuxiliarySceneHandle's doc comment.
    uint32_t totalSceneSlots = framesInFlight_ + static_cast<uint32_t>(kMaxAuxiliaryScenes);
    std::vector<VkDescriptorPoolSize> poolSizes{
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, totalSceneSlots},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 5 * totalSceneSlots},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, (rayTracingSupported_ ? 6 : 5) * totalSceneSlots},
    };
    if (rayTracingSupported_) {
        poolSizes.push_back({VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, totalSceneSlots});
    }
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = totalSceneSlots;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &sceneDescriptorPool_) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreateDescriptorPool (scene) failed.\n");
        return false;
    }

    for (auto& frame : frames_) {
        if (!initSceneDescriptorResourcesFor(frame)) return false;
    }
    return true;
}

void Renderer::destroySceneDescriptorResources() {
    for (auto& frame : frames_) {
        destroySceneDescriptorResourcesFor(frame);
    }
    if (sceneDescriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, sceneDescriptorPool_, nullptr);
        sceneDescriptorPool_ = VK_NULL_HANDLE;
    }
    if (sceneDescriptorSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, sceneDescriptorSetLayout_, nullptr);
        sceneDescriptorSetLayout_ = VK_NULL_HANDLE;
    }
}

bool Renderer::initInstanceBufferFor(FrameSync& frame) {
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = sizeof(InstanceData) * kMaxInstancesPerFrame + sizeof(glm::mat4) * kMaxShadowInstancesPerFrame;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo resultInfo{};
    if (vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo, &frame.instanceBuffer, &frame.instanceAllocation,
                         &resultInfo) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vmaCreateBuffer (instance) failed.\n");
        return false;
    }
    frame.instanceMapped = resultInfo.pMappedData; // persistently mapped, same pattern as sceneUboBuffer
    return true;
}

void Renderer::destroyInstanceBufferFor(FrameSync& frame) {
    if (frame.instanceBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, frame.instanceBuffer, frame.instanceAllocation);
        frame.instanceBuffer = VK_NULL_HANDLE;
    }
}

bool Renderer::createInstanceBuffers() {
    for (auto& frame : frames_) {
        if (!initInstanceBufferFor(frame)) return false;
    }
    return true;
}

void Renderer::destroyInstanceBuffers() {
    for (auto& frame : frames_) {
        destroyInstanceBufferFor(frame);
    }
}

bool Renderer::initParticleInstanceBufferFor(FrameSync& frame) {
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = sizeof(ParticleInstanceData) * ParticleSystem::kMaxParticles;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo resultInfo{};
    if (vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo, &frame.particleInstanceBuffer,
                         &frame.particleInstanceAllocation, &resultInfo) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vmaCreateBuffer (particle instance) failed.\n");
        return false;
    }
    frame.particleInstanceMapped = resultInfo.pMappedData;
    return true;
}

void Renderer::destroyParticleInstanceBufferFor(FrameSync& frame) {
    if (frame.particleInstanceBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, frame.particleInstanceBuffer, frame.particleInstanceAllocation);
        frame.particleInstanceBuffer = VK_NULL_HANDLE;
    }
}

bool Renderer::createParticleResources() {
    particleQuadMesh_ = Mesh::createQuad(allocator_, device_, commandPool_, graphicsQueue_, 0.5f);
    if (particleQuadMesh_.vertexBuffer() == VK_NULL_HANDLE) {
        std::fprintf(stderr, "Renderer: Mesh::createQuad (particle) failed.\n");
        return false;
    }

    for (auto& frame : frames_) {
        if (!initParticleInstanceBufferFor(frame)) return false;
    }
    return true;
}

void Renderer::destroyParticleResources() {
    for (auto& frame : frames_) {
        destroyParticleInstanceBufferFor(frame);
    }
    particleQuadMesh_.destroy(allocator_);
}

bool Renderer::initSkinningResourcesFor(FrameSync& frame) {
    for (uint32_t slot = 0; slot < kMaxSkinnedDrawsPerFrame; ++slot) {
        VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bufferInfo.size = kSkinningUboSize;
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo resultInfo{};
        if (vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo, &frame.skinningUboBuffers[slot],
                             &frame.skinningUboAllocations[slot], &resultInfo) != VK_SUCCESS) {
            std::fprintf(stderr, "Renderer: vmaCreateBuffer (skinning UBO slot %u) failed.\n", slot);
            return false;
        }
        frame.skinningUboMapped[slot] = resultInfo.pMappedData;

        VkDescriptorSetAllocateInfo dsAllocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        dsAllocInfo.descriptorPool = skinningDescriptorPool_;
        dsAllocInfo.descriptorSetCount = 1;
        dsAllocInfo.pSetLayouts = &skinningDescriptorSetLayout_;
        if (vkAllocateDescriptorSets(device_, &dsAllocInfo, &frame.skinningDescriptorSets[slot]) != VK_SUCCESS) {
            std::fprintf(stderr, "Renderer: vkAllocateDescriptorSets (skinning slot %u) failed.\n", slot);
            return false;
        }

        VkDescriptorBufferInfo bufInfo{frame.skinningUboBuffers[slot], 0, kSkinningUboSize};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = frame.skinningDescriptorSets[slot];
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.pBufferInfo = &bufInfo;
        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    }
    return true;
}

void Renderer::destroySkinningResourcesFor(FrameSync& frame) {
    for (uint32_t slot = 0; slot < kMaxSkinnedDrawsPerFrame; ++slot) {
        if (frame.skinningUboBuffers[slot] != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator_, frame.skinningUboBuffers[slot], frame.skinningUboAllocations[slot]);
            frame.skinningUboBuffers[slot] = VK_NULL_HANDLE;
        }
    }
}

bool Renderer::createSkinningDescriptorResources() {
    VkDescriptorSetLayoutBinding uboBinding{};
    uboBinding.binding = 0;
    uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &uboBinding;
    if (vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &skinningDescriptorSetLayout_) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreateDescriptorSetLayout (skinning) failed.\n");
        return false;
    }

    // One descriptor set per skinning slot, per FrameSync (frames_[] slot
    // or auxiliaryScenes_[] slot) -- same "sized for framesInFlight_ +
    // kMaxAuxiliaryScenes up front" reasoning as sceneDescriptorPool_/
    // postProcessDescriptorPool_ (Vulkan descriptor pools can't grow).
    uint32_t totalSkinningSlots =
        (framesInFlight_ + static_cast<uint32_t>(kMaxAuxiliaryScenes)) * kMaxSkinnedDrawsPerFrame;
    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, totalSkinningSlots};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = totalSkinningSlots;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &skinningDescriptorPool_) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreateDescriptorPool (skinning) failed.\n");
        return false;
    }

    for (auto& frame : frames_) {
        if (!initSkinningResourcesFor(frame)) return false;
    }
    return true;
}

void Renderer::destroySkinningDescriptorResources() {
    for (auto& frame : frames_) {
        destroySkinningResourcesFor(frame);
    }
    if (skinningDescriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, skinningDescriptorPool_, nullptr);
        skinningDescriptorPool_ = VK_NULL_HANDLE;
    }
    if (skinningDescriptorSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, skinningDescriptorSetLayout_, nullptr);
        skinningDescriptorSetLayout_ = VK_NULL_HANDLE;
    }
}

// Sprint 14 ("Performance Mode"): one real toggle bundling several
// concrete, independently-real rendering-cost reductions -- the lever
// for keeping "stable 180 FPS" reachable once heavier real costs (ray-
// traced shadows) are switched on:
//   - CSM (and scene_rt.frag's own CSM fallback): 3x3 PCF -> single-tap
//     shadow sampling (scene.renderFlags.y, see both fragment shaders).
//   - Bloom: the real extract+blur fragment-shader pass is skipped
//     entirely (frame.bloomImage is real-cleared to black instead --
//     see drawBloomAndComposite()), not just visually zeroed via
//     intensity=0 (which would still pay the full extract-shader cost
//     for no visible result).
//   - Particles: real per-frame draw count clamped to
//     kPerformanceModeMaxParticles, well under ParticleSystem::kMaxParticles
//     -- fewer real instanced quads, less real fragment-shader overdraw.
// Deliberately also forces ray-traced shadows off when enabling: ray
// tracing is a real *additional* cost this toggle exists to make room
// for, not something Performance Mode would ever combine with real
// CSM-cost cuts at the same time.
void Renderer::setPerformanceMode(bool enabled) {
    performanceModeEnabled_ = enabled;
    if (enabled) {
        rayTracedShadowsEnabled_ = false;
        cinematicModeEnabled_ = false; // see setCinematicMode()'s own comment on this mutual exclusion
    }
}

void Renderer::applySceneShadingRate(VkCommandBuffer cmd) const {
    if (!gpuFeatures_.fragmentShadingRate) return;
    const VkExtent2D rate = coarseShadingActive() ? VkExtent2D{2, 2} : VkExtent2D{1, 1};
    const VkFragmentShadingRateCombinerOpKHR combiners[2] = {VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR,
                                                             VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR};
    vkCmdSetFragmentShadingRateKHR(cmd, &rate, combiners);
}

void Renderer::setVsyncEnabled(bool enabled) {
    if (vsyncEnabled_ == enabled) return; // real, honest no-op -- no swapchain rebuild for an unchanged value
    vsyncEnabled_ = enabled;
    if (swapchain_ != VK_NULL_HANDLE) recreateSwapchain();
}

void Renderer::setColorblindMode(int mode) { colorblindModeIndex_ = std::clamp(mode, 0, 3); }

// Sprint 16 ("Cinematic Graphics") -- see this method's own public
// declaration comment in Renderer.hpp for the real mutual-exclusion
// reasoning with Performance Mode.
void Renderer::setCinematicMode(bool enabled) {
    cinematicModeEnabled_ = enabled;
    if (enabled) performanceModeEnabled_ = false;
}

} // namespace engine::core
