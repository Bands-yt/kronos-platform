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

bool Renderer::initShadowResourcesFor(FrameSync& frame) {
    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = {kShadowMapResolution, kShadowMapResolution, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = kCascadeCount; // one array image, kCascadeCount layers -- see FrameSync's comment
    imageInfo.format = kShadowFormat;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

    if (vmaCreateImage(allocator_, &imageInfo, &allocInfo, &frame.shadowImage, &frame.shadowAllocation, nullptr) !=
        VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vmaCreateImage (shadow cascade array) failed.\n");
        return false;
    }

    VkImageViewCreateInfo arrayViewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    arrayViewInfo.image = frame.shadowImage;
    arrayViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    arrayViewInfo.format = kShadowFormat;
    arrayViewInfo.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, kCascadeCount};
    if (vkCreateImageView(device_, &arrayViewInfo, nullptr, &frame.shadowArrayView) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreateImageView (shadow array) failed.\n");
        return false;
    }

    for (uint32_t cascade = 0; cascade < kCascadeCount; ++cascade) {
        VkImageViewCreateInfo layerViewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        layerViewInfo.image = frame.shadowImage;
        layerViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        layerViewInfo.format = kShadowFormat;
        layerViewInfo.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, cascade, 1};
        if (vkCreateImageView(device_, &layerViewInfo, nullptr, &frame.shadowCascadeViews[cascade]) != VK_SUCCESS) {
            std::fprintf(stderr, "Renderer: vkCreateImageView (shadow cascade %u) failed.\n", cascade);
            return false;
        }
    }
    if (!render::createSpotShadowMaps(gpuContext(), kShadowFormat, frame.spotShadowMaps)) {
        std::fprintf(stderr, "Renderer: spot shadow map creation failed.\n");
        return false;
    }
    return true;
}

void Renderer::destroyShadowResourcesFor(FrameSync& frame) {
    render::destroySpotShadowMaps(gpuContext(), frame.spotShadowMaps);
    for (auto& cascadeView : frame.shadowCascadeViews) {
        if (cascadeView != VK_NULL_HANDLE) {
            vkDestroyImageView(device_, cascadeView, nullptr);
            cascadeView = VK_NULL_HANDLE;
        }
    }
    if (frame.shadowArrayView != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, frame.shadowArrayView, nullptr);
        frame.shadowArrayView = VK_NULL_HANDLE;
    }
    if (frame.shadowImage != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator_, frame.shadowImage, frame.shadowAllocation);
        frame.shadowImage = VK_NULL_HANDLE;
    }
}

bool Renderer::createShadowResources() {
    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    // CLAMP_TO_EDGE rather than CLAMP_TO_BORDER: scene.frag's computeShadow()
    // checks the projected UV against [0,1] itself and returns "fully lit"
    // outside that range, so the sampler's own edge behavior never
    // actually matters -- no border color setup needed.
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(device_, &samplerInfo, nullptr, &shadowSampler_) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreateSampler (shadow) failed.\n");
        return false;
    }
    // Hardware PCF: each tap returns the bilinearly-weighted fraction of
    // the 2x2 footprint that passes, so the soft filter needs far fewer taps.
    samplerInfo.compareEnable = VK_TRUE;
    samplerInfo.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    if (vkCreateSampler(device_, &samplerInfo, nullptr, &shadowCompareSampler_) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreateSampler (shadow compare) failed.\n");
        return false;
    }

    for (auto& frame : frames_) {
        if (!initShadowResourcesFor(frame)) return false;
    }

    std::fprintf(stdout, "Renderer: shadow resources created (%ux%u x%u cascades, %u frame(s))\n", kShadowMapResolution,
                 kShadowMapResolution, kCascadeCount, framesInFlight_);
    return true;
}

void Renderer::destroyShadowResources() {
    for (auto& frame : frames_) {
        destroyShadowResourcesFor(frame);
    }
    if (shadowSampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device_, shadowSampler_, nullptr);
        shadowSampler_ = VK_NULL_HANDLE;
    }
    if (shadowCompareSampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device_, shadowCompareSampler_, nullptr);
        shadowCompareSampler_ = VK_NULL_HANDLE;
    }
}

bool Renderer::createShadowPipeline() {
    // Own pipeline layout, not scenePipelineLayout_ -- CSM's shadow pass
    // needs to tell shadow.vert which cascade it's rendering into
    // (ShadowPushConstants::viewIndex, to pick a SceneUBO shadow matrix),
    // a different push-constant shape than the main pass's
    // ObjectPushConstants. Same descriptor set layout either way (both
    // need the UBO). See SceneTypes.hpp's ShadowPushConstants comment.
    VkPushConstantRange shadowPushRange{};
    shadowPushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    shadowPushRange.offset = 0;
    shadowPushRange.size = sizeof(ShadowPushConstants);

    VkPipelineLayoutCreateInfo shadowLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    shadowLayoutInfo.setLayoutCount = 1;
    shadowLayoutInfo.pSetLayouts = &sceneDescriptorSetLayout_;
    shadowLayoutInfo.pushConstantRangeCount = 1;
    shadowLayoutInfo.pPushConstantRanges = &shadowPushRange;
    if (vkCreatePipelineLayout(device_, &shadowLayoutInfo, nullptr, &shadowPipelineLayout_) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreatePipelineLayout (shadow) failed.\n");
        return false;
    }

    std::string shaderDir = resolveResourceDir(executableDirectory(), "shaders", ENGINE_SHADER_DIR);
    auto vertCode = readBinaryFile(shaderDir + "/shadow.vert.spv");
    if (vertCode.empty()) {
        std::fprintf(stderr, "Renderer: failed to read compiled shader \"%s/shadow.vert.spv\".\n", shaderDir.c_str());
        return false;
    }
    VkShaderModule vertModule = createShaderModule(device_, vertCode);
    if (vertModule == VK_NULL_HANDLE) {
        std::fprintf(stderr, "Renderer: vkCreateShaderModule (shadow) failed.\n");
        return false;
    }

    VkPipelineShaderStageCreateInfo stage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    stage.module = vertModule;
    stage.pName = "main";

    auto bindingDesc = Vertex::bindingDescription();
    auto attrDescs = Vertex::attributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &bindingDesc;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrDescs.size());
    vertexInput.pVertexAttributeDescriptions = attrDescs.data();

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
    // Depth bias: pushes shadow-caster depth slightly away from the light
    // so a surface doesn't self-shadow from floating-point/precision
    // error alone. Kept small and deliberately NOT the primary defense
    // against grazing-angle self-shadowing -- that used to be split
    // between this constant/slope factor AND a full NdotL slope-scaled
    // bias in scene.frag's computeShadow(), and the two stacked (this
    // pass pushes the *stored* shadow-map depth away from the light,
    // then the shader-side bias pushed the *comparison* depth again),
    // which is exactly the kind of over-biasing that shows up as
    // shadows visibly detached from their casters ("peter-panning").
    // scene.frag's sampleCascadeShadow() now derives its bias from the
    // real receiver-plane depth gradient per fragment (see its own
    // comment), so this only needs to cover residual float/rasterizer
    // precision error, not compensate for slope on its own.
    rasterizer.depthBiasEnable = VK_TRUE;
    rasterizer.depthBiasConstantFactor = 0.5f;
    rasterizer.depthBiasSlopeFactor = 0.75f;
    rasterizer.depthClampEnable = depthClampSupported_ ? VK_TRUE : VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    // No color attachments at all for a depth-only pass -- an empty
    // (attachmentCount=0) color-blend state is valid and correct here,
    // not a placeholder.
    VkPipelineColorBlendStateCreateInfo colorBlending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkPipelineRenderingCreateInfo renderingInfo{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    renderingInfo.colorAttachmentCount = 0;
    renderingInfo.depthAttachmentFormat = kShadowFormat;

    VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineInfo.pNext = &renderingInfo;
    pipelineInfo.stageCount = 1; // vertex only -- see shadow.vert's own comment on why this is valid
    pipelineInfo.pStages = &stage;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = shadowPipelineLayout_;

    VkResult result = vkCreateGraphicsPipelines(device_, pipelineCache_, 1, &pipelineInfo, nullptr, &shadowPipeline_);
    vkDestroyShaderModule(device_, vertModule, nullptr);
    if (result == VK_SUCCESS && !createSkinnedShadowPipeline(pipelineInfo, shaderDir)) {
        std::fprintf(stderr, "Renderer: skinned shadow pipeline failed -- characters will not cast shadow-map shadows.\n");
    }
    if (result == VK_SUCCESS && !createInstancedShadowPipeline(pipelineInfo, shaderDir)) {
        std::fprintf(stderr, "Renderer: instanced shadow pipeline failed -- shadow casters draw one by one.\n");
    }

    if (result != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreateGraphicsPipelines (shadow) failed.\n");
        return false;
    }
    return true;
}

bool Renderer::createSkinnedShadowPipeline(VkGraphicsPipelineCreateInfo pipelineInfo, const std::string& shaderDir) {
    VkPushConstantRange pushRange{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ShadowPushConstants)};
    std::array<VkDescriptorSetLayout, 2> setLayouts{sceneDescriptorSetLayout_, skinningDescriptorSetLayout_};
    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    layoutInfo.pSetLayouts = setLayouts.data();
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    if (vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &skinnedShadowPipelineLayout_) != VK_SUCCESS) return false;

    auto code = readBinaryFile(shaderDir + "/shadow_skinned.vert.spv");
    VkShaderModule module = code.empty() ? VK_NULL_HANDLE : createShaderModule(device_, code);
    if (module == VK_NULL_HANDLE) return false;
    VkPipelineShaderStageCreateInfo stage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    stage.module = module;
    stage.pName = "main";

    std::array<VkVertexInputBindingDescription, 2> bindings{Vertex::bindingDescription(),
                                                            GpuSkinVertex::bindingDescription()};
    std::vector<VkVertexInputAttributeDescription> attributes;
    for (const auto& attribute : Vertex::attributeDescriptions()) {
        if (attribute.location == 0) attributes.push_back(attribute);
    }
    for (const auto& attribute : GpuSkinVertex::attributeDescriptions(4)) attributes.push_back(attribute);
    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size());
    vertexInput.pVertexBindingDescriptions = bindings.data();
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
    vertexInput.pVertexAttributeDescriptions = attributes.data();

    pipelineInfo.pStages = &stage;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.layout = skinnedShadowPipelineLayout_;
    VkResult result =
        vkCreateGraphicsPipelines(device_, pipelineCache_, 1, &pipelineInfo, nullptr, &skinnedShadowPipeline_);
    vkDestroyShaderModule(device_, module, nullptr);
    return result == VK_SUCCESS;
}

bool Renderer::createInstancedShadowPipeline(VkGraphicsPipelineCreateInfo pipelineInfo, const std::string& shaderDir) {
    auto code = readBinaryFile(shaderDir + "/shadow_instanced.vert.spv");
    VkShaderModule module = code.empty() ? VK_NULL_HANDLE : createShaderModule(device_, code);
    if (module == VK_NULL_HANDLE) return false;
    VkPipelineShaderStageCreateInfo stage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    stage.module = module;
    stage.pName = "main";

    std::array<VkVertexInputBindingDescription, 2> bindings{
        Vertex::bindingDescription(), VkVertexInputBindingDescription{1, sizeof(glm::mat4), VK_VERTEX_INPUT_RATE_INSTANCE}};
    std::vector<VkVertexInputAttributeDescription> attributes;
    for (const auto& attribute : Vertex::attributeDescriptions()) {
        if (attribute.location == 0) attributes.push_back(attribute);
    }
    for (uint32_t column = 0; column < 4; ++column) {
        attributes.push_back({4 + column, 1, VK_FORMAT_R32G32B32A32_SFLOAT,
                              static_cast<uint32_t>(sizeof(glm::vec4) * column)});
    }
    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size());
    vertexInput.pVertexBindingDescriptions = bindings.data();
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
    vertexInput.pVertexAttributeDescriptions = attributes.data();

    pipelineInfo.pStages = &stage;
    pipelineInfo.pVertexInputState = &vertexInput;
    VkResult result =
        vkCreateGraphicsPipelines(device_, pipelineCache_, 1, &pipelineInfo, nullptr, &instancedShadowPipeline_);
    vkDestroyShaderModule(device_, module, nullptr);
    return result == VK_SUCCESS;
}

void Renderer::destroyShadowPipeline() {
    if (instancedShadowPipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(device_, instancedShadowPipeline_, nullptr);
    instancedShadowPipeline_ = VK_NULL_HANDLE;
    if (skinnedShadowPipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(device_, skinnedShadowPipeline_, nullptr);
    if (skinnedShadowPipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, skinnedShadowPipelineLayout_, nullptr);
    }
    skinnedShadowPipeline_ = VK_NULL_HANDLE;
    skinnedShadowPipelineLayout_ = VK_NULL_HANDLE;
    if (shadowPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, shadowPipeline_, nullptr);
        shadowPipeline_ = VK_NULL_HANDLE;
    }
    if (shadowPipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, shadowPipelineLayout_, nullptr);
        shadowPipelineLayout_ = VK_NULL_HANDLE;
    }
}

render::CascadeFits Renderer::computeCascades(const Camera& camera, float aspectRatio) const {
    render::CascadeSettings settings;
    settings.nearPlane = camera.nearPlane;
    settings.maxDistance = kShadowMaxDistance;
    settings.verticalFovRadians = glm::radians(camera.verticalFovDegrees);
    settings.aspectRatio = aspectRatio;
    settings.resolution = kShadowMapResolution;
    settings.casterPadding = kShadowDepthPadding;
    return render::fitCascades(camera.viewMatrix(), lighting_.directionWS, settings);
}

void Renderer::drawShadowPass(VkCommandBuffer cmd, FrameSync& frame, ECS& ecs, MeshLibrary& meshLibrary,
                              const render::CascadeFits& cascades, const render::SpotShadowSet& spotShadows) {
    transitionImage(cmd, frame.shadowImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                     VK_ACCESS_2_NONE, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                     VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
                     VK_IMAGE_ASPECT_DEPTH_BIT, kCascadeCount);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipelineLayout_, 0, 1,
                             &frame.sceneDescriptorSet, 0, nullptr);

    struct Caster {
        glm::mat4 model;
        glm::vec3 center;
        float radius;
        const Mesh* mesh;
    };
    std::vector<Caster> casters;
    auto view = ecs.view<Transform, Renderable>();
    for (auto entity : view) {
        const auto& renderable = view.get<Renderable>(entity);
        if (!renderable.visible || !renderable.castsShadow) continue;
        if (activeBatching_ != nullptr && activeBatching_->isBatched(entity)) continue;
        const Mesh* mesh = meshLibrary.get(renderable.meshHandle);
        if (!mesh) continue;
        glm::mat4 model = hierarchy::computeWorldMatrix(ecs, entity);
        glm::vec3 localCenter = 0.5f * (mesh->localBoundsMin() + mesh->localBoundsMax());
        float localRadius = 0.5f * glm::length(mesh->localBoundsMax() - mesh->localBoundsMin());
        float maxScale = std::max({glm::length(glm::vec3(model[0])), glm::length(glm::vec3(model[1])),
                                   glm::length(glm::vec3(model[2]))});
        casters.push_back({model, glm::vec3(model * glm::vec4(localCenter, 1.0f)), localRadius * maxScale, mesh});
    }

    if (activeBatching_ != nullptr) {
        for (const StaticBatchSet::Batch& batch : activeBatching_->batches()) {
            if (!batch.active || !batch.material.castsShadow) continue;
            const Mesh* mesh = meshLibrary.get(batch.meshHandle);
            if (!mesh) continue;
            casters.push_back({glm::mat4(1.0f), batch.bounds.center(),
                               0.5f * glm::length(batch.bounds.max - batch.bounds.min), mesh});
        }
    }

    // Casters sharing a mesh with enough others are drawn instanced, per view.
    std::unordered_map<const Mesh*, uint32_t> castersPerMesh;
    for (const Caster& caster : casters) ++castersPerMesh[caster.mesh];
    std::vector<bool> instancedCaster(casters.size(), false);
    if (automaticInstancing_ && instancedShadowPipeline_ != VK_NULL_HANDLE) {
        for (size_t i = 0; i < casters.size(); ++i) {
            instancedCaster[i] = castersPerMesh[casters[i].mesh] >= kMinAutomaticInstances;
        }
    }
    auto* shadowInstances = reinterpret_cast<glm::mat4*>(static_cast<char*>(frame.instanceMapped) +
                                                         sizeof(InstanceData) * kMaxInstancesPerFrame);
    uint32_t shadowInstanceCount = 0;
    std::unordered_map<const Mesh*, std::vector<glm::mat4>> instancedThisView;

    auto drawCasters = [&](VkImageView target, uint32_t extent, int32_t viewIndex, auto&& visible) {
        VkRenderingAttachmentInfo depthAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        depthAttachment.imageView = target;
        depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthAttachment.clearValue.depthStencil = {1.0f, 0};

        VkRenderingInfo renderingInfo{VK_STRUCTURE_TYPE_RENDERING_INFO};
        renderingInfo.renderArea = {{0, 0}, {extent, extent}};
        renderingInfo.layerCount = 1;
        renderingInfo.pDepthAttachment = &depthAttachment;

        VkViewport viewport{0.0f, 0.0f, static_cast<float>(extent), static_cast<float>(extent), 0.0f, 1.0f};
        VkRect2D scissor{{0, 0}, {extent, extent}};
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        vkCmdBeginRendering(cmd, &renderingInfo);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline_);
        for (auto& [mesh, models] : instancedThisView) models.clear();
        for (size_t i = 0; i < casters.size(); ++i) {
            const Caster& caster = casters[i];
            if (!visible(caster)) continue;
            if (instancedCaster[i]) {
                instancedThisView[caster.mesh].push_back(caster.model);
                continue;
            }
            ShadowPushConstants push{};
            push.model = caster.model;
            push.viewIndex = viewIndex;
            vkCmdPushConstants(cmd, shadowPipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);

            VkDeviceSize offset = 0;
            VkBuffer vb = caster.mesh->vertexBuffer();
            vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);
            vkCmdBindIndexBuffer(cmd, caster.mesh->indexBuffer(), 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, caster.mesh->indexCount(), 1, 0, 0, 0);
            recordDraw(caster.mesh->indexCount(), 1);
        }
        bool instancedBound = false;
        for (const auto& [mesh, models] : instancedThisView) {
            if (models.empty()) continue;
            const auto count = static_cast<uint32_t>(models.size());
            ShadowPushConstants push{};
            push.viewIndex = viewIndex;
            if (shadowInstanceCount + count > kMaxShadowInstancesPerFrame) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline_);
                instancedBound = false;
                VkDeviceSize offset = 0;
                VkBuffer vb = mesh->vertexBuffer();
                vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);
                vkCmdBindIndexBuffer(cmd, mesh->indexBuffer(), 0, VK_INDEX_TYPE_UINT32);
                for (const glm::mat4& model : models) {
                    push.model = model;
                    vkCmdPushConstants(cmd, shadowPipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
                    vkCmdDrawIndexed(cmd, mesh->indexCount(), 1, 0, 0, 0);
                    recordDraw(mesh->indexCount(), 1);
                }
                continue;
            }
            if (!instancedBound) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, instancedShadowPipeline_);
                instancedBound = true;
            }
            vkCmdPushConstants(cmd, shadowPipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
            std::memcpy(shadowInstances + shadowInstanceCount, models.data(), count * sizeof(glm::mat4));
            std::array<VkBuffer, 2> buffers{mesh->vertexBuffer(), frame.instanceBuffer};
            std::array<VkDeviceSize, 2> offsets{
                0, sizeof(InstanceData) * kMaxInstancesPerFrame + sizeof(glm::mat4) * shadowInstanceCount};
            vkCmdBindVertexBuffers(cmd, 0, 2, buffers.data(), offsets.data());
            vkCmdBindIndexBuffer(cmd, mesh->indexBuffer(), 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, mesh->indexCount(), count, 0, 0, 0);
            recordDraw(mesh->indexCount(), count);
            shadowInstanceCount += count;
        }
        if (skinnedShadowPipeline_ != VK_NULL_HANDLE) {
            bool bound = false;
            for (const SkinnedDraw& draw : skinnedDraws_) {
                if (!draw.castsShadow || !visible(Caster{draw.model, draw.center, draw.radius, nullptr})) continue;
                if (!bound) {
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skinnedShadowPipeline_);
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skinnedShadowPipelineLayout_, 0, 1,
                                            &frame.sceneDescriptorSet, 0, nullptr);
                    bound = true;
                }
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skinnedShadowPipelineLayout_, 1, 1,
                                        &frame.skinningDescriptorSets[draw.slot], 0, nullptr);
                ShadowPushConstants push{};
                push.model = draw.model;
                push.viewIndex = viewIndex;
                vkCmdPushConstants(cmd, skinnedShadowPipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push),
                                   &push);
                const Mesh& mesh = draw.mesh->mesh();
                std::array<VkBuffer, 2> buffers{mesh.vertexBuffer(), draw.mesh->skinBuffer()};
                std::array<VkDeviceSize, 2> offsets{0, 0};
                vkCmdBindVertexBuffers(cmd, 0, 2, buffers.data(), offsets.data());
                vkCmdBindIndexBuffer(cmd, mesh.indexBuffer(), 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(cmd, mesh.indexCount(), 1, 0, 0, 0);
                recordDraw(mesh.indexCount(), 1);
            }
        }
        vkCmdEndRendering(cmd);
    };

    for (uint32_t cascade = 0; cascade < kCascadeCount; ++cascade) {
        drawCasters(frame.shadowCascadeViews[cascade], kShadowMapResolution, static_cast<int32_t>(cascade),
                    [&](const Caster& caster) {
                        return render::sphereIntersectsCascade(cascades[cascade], lighting_.directionWS,
                                                               caster.center, caster.radius);
                    });
    }

    transitionImage(cmd, frame.shadowImage, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                     VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                     VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, kCascadeCount);

    // Unused layers are only transitioned: the forward pass never samples a slot that was not drawn.
    VkImage spotImage = frame.spotShadowMaps.array.image;
    transitionImage(cmd, spotImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                     VK_ACCESS_2_NONE, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                     VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
                     VK_IMAGE_ASPECT_DEPTH_BIT, kMaxShadowedSpotLights);
    for (uint32_t slot = 0; slot < spotShadows.count; ++slot) {
        const glm::mat4& viewProj = spotShadows.viewProj[slot];
        drawCasters(frame.spotShadowMaps.layerViews[slot], render::kSpotShadowResolution,
                    static_cast<int32_t>(kCascadeCount + slot), [&](const Caster& caster) {
                        return render::sphereInFrustum(viewProj, caster.center, caster.radius);
                    });
    }
    transitionImage(cmd, spotImage, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_SHADER_READ_BIT,
                     VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                     VK_IMAGE_ASPECT_DEPTH_BIT, kMaxShadowedSpotLights);
}

} // namespace engine::core
