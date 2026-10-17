#include "core/Renderer.hpp"
#include "core/SystemTimers.hpp"
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

Renderer::AuxiliarySceneHandle Renderer::createAuxiliaryScene() {
    if (auxiliaryScenes_.size() >= kMaxAuxiliaryScenes) {
        std::fprintf(stderr, "Renderer: createAuxiliaryScene() failed -- kMaxAuxiliaryScenes (%zu) already in use.\n",
                     kMaxAuxiliaryScenes);
        return kInvalidAuxiliaryScene;
    }

    auxiliaryScenes_.emplace_back();
    AuxiliarySceneHandle handle = auxiliaryScenes_.size() - 1;
    FrameSync& frame = auxiliaryScenes_[handle];

    // Same dependency order as initialize()'s createShadowResources() ->
    // createSceneDescriptorResources() call sequence: the scene
    // descriptor set's binding-1 write needs frame.shadowArrayView to
    // already exist. No semaphore/fence/command buffer/post-process
    // targets are created here -- the latter are lazily created by
    // ensurePostProcessTargets() on this slot's first drawSceneInto()
    // call (frame.postProcessExtent defaults to {0,0}, same as every
    // frames_[] slot), and this slot is never submitted/presented on its
    // own so it needs no sync primitives of its own.
    if (!initShadowResourcesFor(frame) || !initSceneDescriptorResourcesFor(frame) || !initInstanceBufferFor(frame) ||
        !initParticleInstanceBufferFor(frame) || !initSkinningResourcesFor(frame) ||
        !initFrameRenderResources(frame, false)) {
        std::fprintf(stderr, "Renderer: createAuxiliaryScene() failed to allocate GPU resources for slot %zu.\n", handle);
        destroyAuxiliaryScene(handle);
        return kInvalidAuxiliaryScene;
    }
    return handle;
}

void Renderer::destroyAuxiliaryScene(AuxiliarySceneHandle handle) {
    if (handle == kInvalidAuxiliaryScene || handle >= auxiliaryScenes_.size()) return;
    FrameSync& frame = auxiliaryScenes_[handle];
    destroyPostProcessTargets(frame);
    destroyFrameRenderResources(frame);
    destroySceneDescriptorResourcesFor(frame);
    destroyShadowResourcesFor(frame);
    destroyInstanceBufferFor(frame);
    destroyParticleInstanceBufferFor(frame);
    destroySkinningResourcesFor(frame);
    // frame itself stays in auxiliaryScenes_ (zeroed out, unusable) --
    // see the handle-stability comment on auxiliaryScenes_'s declaration.
}

void Renderer::drawInstancedBatches(VkCommandBuffer cmd, FrameSync& frame, ECS& ecs, MeshLibrary& meshLibrary,
                                     TextureLibrary& textureLibrary,
                                     const std::unordered_map<uint32_t, std::vector<EntityId>>& automatic,
                                     const std::function<void(EntityId)>& drawSingle) {
    // Bucket by meshHandle -- every entity sharing a mesh in one bucket
    // becomes one draw call, regardless of how many entities that is.
    std::unordered_map<uint32_t, std::vector<InstanceData>> buckets;
    size_t total = 0;

    auto addInstance = [&](EntityId entity, const Renderable& renderable) {
        InstanceData data{};
        const glm::mat4* world = activeCulling_ != nullptr ? activeCulling_->worldMatrix(entity) : nullptr;
        data.model = world != nullptr ? *world : hierarchy::computeWorldMatrix(ecs, entity);
        data.baseColor = renderable.baseColor;
        data.metallicRoughness = glm::vec4(renderable.metallic, renderable.roughness, renderable.normalIntensity,
                                            renderable.useTriplanarProjection ? 1.0f : 0.0f);
        data.emissive = glm::vec4(renderable.emissiveColor, renderable.emissiveIntensity);
        data.textureIndices = packTextureIndices(renderable, textureLibrary);
        data.textureIndices.w = renderable.unlitSilhouette ? 1u : 0u;
        data.textureIndices.z |= pushObjectRecord(frame, entity, renderable.layers, data.model) << 16;
        buckets[renderable.meshHandle].push_back(data);
        ++total;
    };

    auto view = ecs.view<Transform, Renderable>();
    for (auto entity : view) {
        const auto& renderable = view.get<Renderable>(entity);
        if (!renderable.visible || !renderable.instanced) continue;
        if (activeCulling_ != nullptr && !activeCulling_->wasVisible(entity)) continue;
        addInstance(entity, renderable);
    }
    for (const auto& [meshHandle, entities] : automatic) {
        if (entities.empty()) continue;
        if (total + entities.size() > kMaxInstancesPerFrame || meshLibrary.get(meshHandle) == nullptr) {
            for (EntityId entity : entities) drawSingle(entity);
            continue;
        }
        for (EntityId entity : entities) addInstance(entity, view.get<Renderable>(entity));
        ++frameInstancedDraws_;
        frameInstancedObjects_ += static_cast<uint32_t>(entities.size());
    }

    if (buckets.empty()) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, instancedScenePipeline_);
    applySceneShadingRate(cmd);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, scenePipelineLayout_, 0, 1, &frame.sceneDescriptorSet,
                             0, nullptr);
    // Bound here too rather than relying on the opaque loop above: this
    // shares scenePipelineLayout_, but nothing guarantees the opaque loop
    // ran (it returns early when no entity matches).
    if (bindlessInitialised_ && bindlessSet_ != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, scenePipelineLayout_, 2, 1, &bindlessSet_, 0,
                                 nullptr);
    }

    auto* instanceCursor = static_cast<InstanceData*>(frame.instanceMapped);
    uint32_t writtenInstances = 0;

    for (auto& [meshHandle, instances] : buckets) {
        const Mesh* mesh = meshLibrary.get(meshHandle);
        if (mesh == nullptr) continue;

        uint32_t count = static_cast<uint32_t>(instances.size());
        if (writtenInstances + count > kMaxInstancesPerFrame) {
            // Clamp rather than overflow frame.instanceBuffer -- see
            // kMaxInstancesPerFrame's declaration comment. Logged once per
            // offending bucket rather than silently dropping instances.
            std::fprintf(stderr,
                         "Renderer: instanced draw batch for mesh %u exceeds kMaxInstancesPerFrame (%u) -- "
                         "clamping, some instances won't render this frame.\n",
                         meshHandle, kMaxInstancesPerFrame);
            count = kMaxInstancesPerFrame > writtenInstances ? kMaxInstancesPerFrame - writtenInstances : 0;
            if (count == 0) break;
        }

        std::memcpy(instanceCursor + writtenInstances, instances.data(), count * sizeof(InstanceData));

        VkDeviceSize vertexOffset = 0;
        VkBuffer vb = mesh->vertexBuffer();
        VkBuffer instanceVb = frame.instanceBuffer;
        std::array<VkBuffer, 2> vertexBuffers{vb, instanceVb};
        std::array<VkDeviceSize, 2> vertexOffsets{vertexOffset, writtenInstances * sizeof(InstanceData)};
        vkCmdBindVertexBuffers(cmd, 0, 2, vertexBuffers.data(), vertexOffsets.data());
        vkCmdBindIndexBuffer(cmd, mesh->indexBuffer(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, mesh->indexCount(), count, 0, 0, 0);
        recordDraw(mesh->indexCount(), count);

        writtenInstances += count;
    }
}

void Renderer::drawParticles(VkCommandBuffer cmd, FrameSync& frame, const ParticleSystem& particleSystem) {
    render::ViewHistory& history = *frame.viewHistory;
    const double simulationTime = particleSystem.simulationTime();
    const float elapsed =
        history.hasParticleTime ? static_cast<float>(std::max(0.0, simulationTime - history.previousParticleTime)) : 0.0f;
    history.previousParticleTime = simulationTime;
    history.hasParticleTime = true;

    const std::vector<Particle>& particles = particleSystem.liveParticles();
    if (particles.empty()) return;

    // Sprint 14 ("Performance Mode"): a real, direct overdraw reduction
    // -- fewer real instanced quads drawn (and so real fragment-shader
    // invocations for blended-particle fill rate), well under the real
    // ParticleSystem::kMaxParticles ceiling. Real, honest degrade: the
    // real particle *simulation* (ParticleSystem::update()) is unaffected
    // -- only how many of its real live particles this draw call actually
    // renders this frame.
    static constexpr uint32_t kPerformanceModeMaxParticles = 1024;
    size_t particleCap = performanceModeEnabled_
                              ? std::min<size_t>(ParticleSystem::kMaxParticles, kPerformanceModeMaxParticles)
                              : ParticleSystem::kMaxParticles;
    uint32_t count = static_cast<uint32_t>(std::min(particles.size(), particleCap));

    auto* instanceCursor = static_cast<ParticleInstanceData*>(frame.particleInstanceMapped);
    for (uint32_t i = 0; i < count; ++i) {
        const Particle& p = particles[i];
        instanceCursor[i].positionSize = glm::vec4(p.position, p.currentSize());
        instanceCursor[i].color = p.currentColor();
        instanceCursor[i].previousPositionSize = glm::vec4(p.positionBefore(elapsed), p.sizeBefore(elapsed));
        instanceCursor[i].params = glm::vec4(p.occlusion, 0.0f, 0.0f, 0.0f);
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, particlePipeline_);
    // Sprint 16: particlePipelineLayout_ (not scenePipelineLayout_, see
    // that member's own comment) -- set0 the same real scene UBO, set1
    // this frame's real sampled depth (soft-particle fade, see
    // shaders/particle.frag). A null particleDepthDescriptorSet (only
    // possible if ensureParticleDepthDescriptor() failed this frame,
    // already logged there) skips set1's bind -- the shader would sample
    // an unbound descriptor otherwise, undefined behavior -- so this
    // degrades to no particles drawn at all rather than that, matching
    // this engine's own "skip the risky draw, don't crash" convention.
    if (frame.particleDepthDescriptorSet == VK_NULL_HANDLE) return;
    std::array<VkDescriptorSet, 2> particleSets{frame.sceneDescriptorSet, frame.particleDepthDescriptorSet};
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, particlePipelineLayout_, 0,
                             static_cast<uint32_t>(particleSets.size()), particleSets.data(), 0, nullptr);

    VkDeviceSize vertexOffset = 0;
    VkBuffer quadVb = particleQuadMesh_.vertexBuffer();
    std::array<VkBuffer, 2> vertexBuffers{quadVb, frame.particleInstanceBuffer};
    std::array<VkDeviceSize, 2> vertexOffsets{vertexOffset, VkDeviceSize{0}};
    vkCmdBindVertexBuffers(cmd, 0, 2, vertexBuffers.data(), vertexOffsets.data());
    vkCmdBindIndexBuffer(cmd, particleQuadMesh_.indexBuffer(), 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, particleQuadMesh_.indexCount(), count, 0, 0, 0);
    recordDraw(particleQuadMesh_.indexCount(), count);
}

void Renderer::prepareSkinnedDraws(FrameSync& frame, ECS& ecs, RiggedMeshLibrary* riggedMeshLibrary) {
    skinnedDraws_.clear();
    if (riggedMeshLibrary == nullptr) return;
    bool warned = false;
    auto view = ecs.view<Transform, SkinnedRenderable>();
    for (auto entity : view) {
        auto& skinned = view.get<SkinnedRenderable>(entity);
        if (!skinned.visible) continue;
        const RiggedMesh* riggedMesh = riggedMeshLibrary->get(skinned.riggedMeshHandle);
        if (riggedMesh == nullptr) continue;
        const uint32_t slot = static_cast<uint32_t>(skinnedDraws_.size());
        if (slot >= kMaxSkinnedDrawsPerFrame) {
            if (!warned) {
                std::fprintf(stderr, "Renderer: more than %u skinned entities in one view; the rest are skipped.\n",
                             kMaxSkinnedDrawsPerFrame);
                warned = true;
            }
            continue;
        }

        uint32_t jointCount = std::min(static_cast<uint32_t>(skinned.skinningMatrices.size()), kMaxJointsPerSkeleton);
        auto* palettes = static_cast<glm::mat4*>(frame.skinningUboMapped[slot]);
        std::memcpy(palettes, skinned.skinningMatrices.data(), sizeof(glm::mat4) * jointCount);
        render::ViewHistory& history = *frame.viewHistory;
        const uint32_t key = entt::to_integral(entity);
        auto previous = history.previousBones.find(key);
        const bool hasPrevious = previous != history.previousBones.end() && previous->second.size() == jointCount;
        const glm::mat4* previousPalette = hasPrevious ? previous->second.data() : skinned.skinningMatrices.data();
        std::memcpy(palettes + kMaxJointsPerSkeleton, previousPalette, sizeof(glm::mat4) * jointCount);
        history.currentBones[key].assign(skinned.skinningMatrices.begin(),
                                         skinned.skinningMatrices.begin() + jointCount);

        const Mesh& mesh = riggedMesh->mesh();
        SkinnedDraw draw{entity, riggedMesh, slot, hierarchy::computeWorldMatrix(ecs, entity), glm::vec3(0.0f), 0.0f,
                         skinned.castsShadow};
        glm::vec3 localCenter = 0.5f * (mesh.localBoundsMin() + mesh.localBoundsMax());
        draw.center = glm::vec3(draw.model * glm::vec4(localCenter, 1.0f));
        float scale = std::max({glm::length(glm::vec3(draw.model[0])), glm::length(glm::vec3(draw.model[1])),
                                glm::length(glm::vec3(draw.model[2]))});
        // Bind-pose bounds; animation can reach past them (raised arms), so pad generously.
        draw.radius = 1.5f * scale * 0.5f * glm::length(mesh.localBoundsMax() - mesh.localBoundsMin());
        skinnedDraws_.push_back(draw);
    }
}

void Renderer::drawSkinnedEntities(VkCommandBuffer cmd, FrameSync& frame, ECS& ecs, RiggedMeshLibrary* riggedMeshLibrary,
                                   TextureLibrary& textureLibrary) {
    if (riggedMeshLibrary == nullptr || skinnedDraws_.empty()) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skinnedScenePipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skinnedScenePipelineLayout_, 0, 1,
                            &frame.sceneDescriptorSet, 0, nullptr);
    Renderable defaultMaterial{};
    VkDescriptorSet defaultMaterialSet = getOrCreateMaterialDescriptorSet(defaultMaterial, textureLibrary);
    if (defaultMaterialSet != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skinnedScenePipelineLayout_, 1, 1,
                                &defaultMaterialSet, 0, nullptr);
    }

    for (const SkinnedDraw& draw : skinnedDraws_) {
        const auto* skinned = ecs.tryGetComponent<SkinnedRenderable>(draw.entity);
        if (skinned == nullptr) continue;
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skinnedScenePipelineLayout_, 2, 1,
                                &frame.skinningDescriptorSets[draw.slot], 0, nullptr);

        ObjectPushConstants push{};
        push.model = draw.model;
        push.baseColor = skinned->baseColor;
        push.metallicRoughness = glm::vec4(skinned->metallic, skinned->roughness, 1.0f, 0.0f);
        push.emissive = glm::vec4(skinned->emissiveColor, skinned->emissiveIntensity);
        push.textureIndices.z = pushObjectRecord(frame, draw.entity, MaterialLayers{}, push.model) << 16;
        vkCmdPushConstants(cmd, skinnedScenePipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(push), &push);

        const Mesh& mesh = draw.mesh->mesh();
        std::array<VkBuffer, 2> vertexBuffers{mesh.vertexBuffer(), draw.mesh->skinBuffer()};
        std::array<VkDeviceSize, 2> vertexOffsets{0, 0};
        vkCmdBindVertexBuffers(cmd, 0, 2, vertexBuffers.data(), vertexOffsets.data());
        vkCmdBindIndexBuffer(cmd, mesh.indexBuffer(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, mesh.indexCount(), 1, 0, 0, 0);
        recordDraw(mesh.indexCount(), 1);
    }
}

bool Renderer::renderFrame() {
    // Wall-clock time since the previous renderFrame() call -- the actual
    // presented frame rate, not GameLoop's fixed simulation tick rate
    // (those two only coincide if vsync/present rate happens to match the
    // fixed timestep, which isn't guaranteed). First call ever has no
    // previous timestamp to diff against, so frameTimeMs/fps just stay 0
    // for that one frame rather than reporting a nonsensical huge delta.
    auto now = std::chrono::steady_clock::now();
    if (lastFrameTimestamp_.time_since_epoch().count() != 0) {
        float frameTimeMs = std::chrono::duration<float, std::milli>(now - lastFrameTimestamp_).count();
        lastMetrics_.frameTimeMs = frameTimeMs;
        lastMetrics_.fps = frameTimeMs > 0.0f ? 1000.0f / frameTimeMs : 0.0f;
        // Kronos ("Rendering Fidelity Foundation" Phase 1.1): real weather
        // transitions tick on the same real wall-clock delta as the FPS
        // counter above -- swapchain-presented frames only (this mirrors
        // lastFrameTimestamp_'s own existing scope), not GameLoop's fixed
        // simulation tick. AuxiliaryScene-only capture paths (trailer
        // capture) don't call renderFrame() at all, so weather stays
        // static (whatever it last was) during those -- a real, honest
        // limitation, not a bug, matching this frame's own established
        // "AuxiliaryScene is a separate, less-trusted path" precedent.
        tickWeather(weatherState_, frameTimeMs / 1000.0f);
        totalElapsedTimeSeconds_ += frameTimeMs / 1000.0f;
    }
    lastFrameTimestamp_ = now;
    frameDrawCalls_ = 0;
    frameTriangles_ = 0;
    frameObjectsVisible_ = 0;
    frameObjectsCulled_ = 0;
    frameStaticBatches_ = 0;
    frameStaticBatchedObjects_ = 0;
    frameInstancedDraws_ = 0;
    frameInstancedObjects_ = 0;
    ++frameSerial_;

    FrameSync& frame = frames_[currentFrame_];

    vkWaitForFences(device_, 1, &frame.inFlight, VK_TRUE, UINT64_MAX);
    if (frame.timestamps && frame.timestampsWritten) {
        uint64_t ticks[2] = {0, 0};
        if (vkGetQueryPoolResults(device_, frame.timestamps, 0, 2, sizeof(ticks), ticks, sizeof(uint64_t),
                                  VK_QUERY_RESULT_64_BIT) == VK_SUCCESS && ticks[1] >= ticks[0]) {
            SystemTimers::instance().add("GPU frame", static_cast<double>(ticks[1] - ticks[0]) * timestampPeriodNs_ * 1e-6);
        }
        frame.timestampsWritten = false;
    }
    if (!deferredDestroys_.empty()) {
        auto due = std::stable_partition(deferredDestroys_.begin(), deferredDestroys_.end(),
                                         [this](const auto& entry) { return entry.first > frameSerial_; });
        std::vector<std::function<void()>> run;
        for (auto it = due; it != deferredDestroys_.end(); ++it) run.push_back(std::move(it->second));
        deferredDestroys_.erase(due, deferredDestroys_.end());
        for (auto& destroy : run) destroy();
    }

    uint32_t imageIndex = 0;
    VkResult acquireResult = vkAcquireNextImageKHR(
        device_, swapchain_, UINT64_MAX, frame.imageAvailable, VK_NULL_HANDLE, &imageIndex);

    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain();
        return true;
    }
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        std::fprintf(stderr, "Renderer: vkAcquireNextImageKHR failed.\n");
        return false;
    }

    vkResetFences(device_, 1, &frame.inFlight);

    VkCommandBuffer cmd = frame.commandBuffer;
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);
    if (frame.timestamps) {
        vkCmdResetQueryPool(cmd, frame.timestamps, 0, 2);
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, frame.timestamps, 0);
    }

    if (prePassCallback_) {
        prePassCallback_(cmd);
    }

    VkImage image = swapchainImages_[imageIndex];

    if (sceneEcs_ != nullptr && sceneCamera_ != nullptr && sceneMeshLibrary_ != nullptr &&
        sceneParticleSystem_ != nullptr && sceneTextureLibrary_ != nullptr) {
        // Real scene fills the whole swapchain -- engine_runtime's path.
        // Kronos ("Avatar System"): sceneRiggedMeshLibrary_ now real-passed
        // through -- see setScene()'s own header comment for the real bug
        // this fixes (a real, GPU-uploaded skinned avatar body that was
        // never actually submitted to a draw call on this exact path).
        drawSceneInto(cmd, image, swapchainImageViews_[imageIndex], depthImage_, depthImageView_, swapchainExtent_,
                      *sceneCamera_, *sceneEcs_, *sceneMeshLibrary_, *sceneParticleSystem_, *sceneTextureLibrary_,
                      sceneRiggedMeshLibrary_);
    } else {
        // No scene set -- Studio's path: a plain clear behind its docked
        // ImGui panels (its own scene render, if any, already happened in
        // the prePass hook above, targeting Studio's own offscreen
        // viewport target, not the swapchain).
        transitionImage(cmd, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                         VK_ACCESS_2_NONE, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                         VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

        VkRenderingAttachmentInfo colorAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        colorAttachment.imageView = swapchainImageViews_[imageIndex];
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.clearValue.color = {{0.02f, 0.02f, 0.03f, 1.0f}};

        VkRenderingInfo renderingInfo{VK_STRUCTURE_TYPE_RENDERING_INFO};
        renderingInfo.renderArea = {{0, 0}, swapchainExtent_};
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &colorAttachment;

        vkCmdBeginRendering(cmd, &renderingInfo);
        vkCmdEndRendering(cmd);
    }

    if (overlayCallback_ || !pluginOverlayCallbacks_.empty()) {
        VkRenderingAttachmentInfo overlayAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        overlayAttachment.imageView = swapchainImageViews_[imageIndex];
        overlayAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        overlayAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // draw on top, don't clear
        overlayAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        VkRenderingInfo overlayInfo{VK_STRUCTURE_TYPE_RENDERING_INFO};
        overlayInfo.renderArea = {{0, 0}, swapchainExtent_};
        overlayInfo.layerCount = 1;
        overlayInfo.colorAttachmentCount = 1;
        overlayInfo.pColorAttachments = &overlayAttachment;

        vkCmdBeginRendering(cmd, &overlayInfo);
        if (overlayCallback_) overlayCallback_(cmd, swapchainImageViews_[imageIndex], swapchainExtent_);
        // Plugin overlay hooks run AFTER Studio's own overlayCallback_ (its
        // ImGui composite), in the same LOAD-op pass, so a plugin draws on
        // top of the already-composited editor frame instead of racing it
        // for the first draw. See Renderer.hpp's own comment on why this is
        // an additive list rather than reusing overlayCallback_'s single
        // slot, which Studio's core rendering integration already owns.
        for (const auto& [name, callback] : pluginOverlayCallbacks_) {
            callback(cmd, swapchainImageViews_[imageIndex], swapchainExtent_);
        }
        vkCmdEndRendering(cmd);
    }

    transitionImage(cmd, image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                     VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_NONE,
                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);

    if (frame.timestamps) {
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, frame.timestamps, 1);
        frame.timestampsWritten = true;
    }
    if (VkResult endResult = vkEndCommandBuffer(cmd); endResult != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkEndCommandBuffer failed (VkResult=%d).\n", static_cast<int>(endResult));
    }

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &frame.imageAvailable;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    // Per-image, not per-frame: see renderFinishedPerImage_'s comment.
    // Falls back to the per-frame semaphore only if the vector is somehow
    // unsized, which would mean swapchain creation failed anyway.
    VkSemaphore renderFinishedSemaphore = imageIndex < renderFinishedPerImage_.size()
                                               ? renderFinishedPerImage_[imageIndex]
                                               : frame.renderFinished;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &renderFinishedSemaphore;

    if (VkResult submitResult = vkQueueSubmit(graphicsQueue_, 1, &submitInfo, frame.inFlight); submitResult != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkQueueSubmit failed (VkResult=%d).\n", static_cast<int>(submitResult));
        return false;
    }

    VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinishedSemaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain_;
    presentInfo.pImageIndices = &imageIndex;

    VkResult presentResult = vkQueuePresentKHR(presentQueue_, &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR || framebufferResized_ ||
        window_->wasResized()) {
        window_->clearResizedFlag();
        recreateSwapchain();
    } else if (presentResult != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkQueuePresentKHR failed.\n");
        return false;
    }

    lastMetrics_.drawCalls = frameDrawCalls_;
    lastMetrics_.triangleCount = frameTriangles_;
    lastMetrics_.objectsVisible = frameObjectsVisible_;
    lastMetrics_.staticBatches = frameStaticBatches_;
    lastMetrics_.staticBatchedObjects = frameStaticBatchedObjects_;
    lastMetrics_.instancedDraws = frameInstancedDraws_;
    lastMetrics_.instancedObjects = frameInstancedObjects_;
    lastMetrics_.objectsCulled = frameObjectsCulled_;

    // vmaGetHeapBudgets: VMA's own live tracking of what's actually
    // resident/available per memory heap -- real numbers from the driver,
    // not something this engine estimates. Summed across every heap
    // (typically device-local + host-visible on a discrete GPU) for one
    // overall figure; a more detailed view could break this out per heap
    // later if a Studio panel ever wants that granularity.
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memProps);
    std::vector<VmaBudget> budgets(memProps.memoryHeapCount);
    vmaGetHeapBudgets(allocator_, budgets.data());
    uint64_t usedBytes = 0;
    uint64_t budgetBytes = 0;
    for (const VmaBudget& budget : budgets) {
        usedBytes += budget.usage;
        budgetBytes += budget.budget;
    }
    lastMetrics_.gpuMemoryUsedBytes = usedBytes;
    lastMetrics_.gpuMemoryBudgetBytes = budgetBytes;

    if (logMetricsToStdout_ && std::chrono::duration<float>(now - lastMetricsLogTimestamp_).count() >= 1.0f) {
        lastMetricsLogTimestamp_ = now;
        std::fprintf(stdout, "Renderer: %.1f fps (%.2f ms) | %u draw calls | %llu tris | GPU %.0f/%.0f MB\n",
                     lastMetrics_.fps, lastMetrics_.frameTimeMs, lastMetrics_.drawCalls,
                     static_cast<unsigned long long>(lastMetrics_.triangleCount),
                     static_cast<double>(lastMetrics_.gpuMemoryUsedBytes) / (1024.0 * 1024.0),
                     static_cast<double>(lastMetrics_.gpuMemoryBudgetBytes) / (1024.0 * 1024.0));
    }

    currentFrame_ = (currentFrame_ + 1) % framesInFlight_;
    return true;
}

} // namespace engine::core
