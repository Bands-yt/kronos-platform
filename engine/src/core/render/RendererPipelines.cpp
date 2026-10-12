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

bool Renderer::createScenePipeline() {
    std::string shaderDir = resolveResourceDir(executableDirectory(), "shaders", ENGINE_SHADER_DIR); // see src/CMakeLists.txt's shader-compile step
    auto vertCode = readBinaryFile(shaderDir + "/scene.vert.spv");
    // Sprint 14 ("RTX Upgrade" Phase 2): scene_rt.frag (the real ray-query-
    // capable variant, see that file's own header comment) is chosen
    // once here, at pipeline-creation time, purely from real hardware
    // capability (rayTracingSupported_, decided in createLogicalDevice()
    // before this function ever runs) -- never re-decided per frame. The
    // real runtime ON/OFF toggle (setRayTracedShadowsEnabled()) is a
    // dynamic branch *inside* scene_rt.frag itself (see its
    // computeShadow()), not a pipeline swap -- one pipeline object either
    // way, for the entire lifetime of this Renderer.
    // Kronos ("Bindless Descriptors"): the same one-time, capability-driven
    // choice, now across two axes. bindlessInitialised_ (not merely
    // bindlessSupported_) is the condition, so a device that supports
    // bindless but failed to create the descriptor resources still gets a
    // shader whose set=1 material bindings actually exist.
    std::string fragShaderName;
    if (rayTracingSupported_) {
        fragShaderName = bindlessInitialised_ ? "scene_rt_bindless.frag.spv" : "scene_rt.frag.spv";
    } else {
        fragShaderName = bindlessInitialised_ ? "scene_bindless.frag.spv" : "scene.frag.spv";
    }
    auto fragCode = readBinaryFile(shaderDir + "/" + fragShaderName);
    if (vertCode.empty() || fragCode.empty()) {
        std::fprintf(stderr, "Renderer: failed to read compiled shaders from \"%s\" -- did the build run engine_shaders?\n",
                     shaderDir.c_str());
        return false;
    }

    VkShaderModule vertModule = createShaderModule(device_, vertCode);
    VkShaderModule fragModule = createShaderModule(device_, fragCode);
    if (vertModule == VK_NULL_HANDLE || fragModule == VK_NULL_HANDLE) {
        std::fprintf(stderr, "Renderer: vkCreateShaderModule failed.\n");
        return false;
    }

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(ObjectPushConstants);

    // Two sets: 0 = per-frame scene data (UBO + shadow sampler, unchanged),
    // 1 = per-material textures (see createMaterialResources()) -- reused
    // as-is by createInstancedScenePipeline()/createParticlePipeline(),
    // whose shaders simply never declare a set=1 binding, so Vulkan never
    // requires it to be bound for those draws (only createScenePipeline()'s
    // individually-drawn entities bind set=1, see drawSceneInto()'s loop).
    // Set 2 is the global bindless texture array, added only when the
    // device supports it. Set 1 stays declared so the non-bindless path,
    // and the other pipelines that share this layout, are unchanged.
    std::vector<VkDescriptorSetLayout> setLayouts{sceneDescriptorSetLayout_, materialDescriptorSetLayout_};
    if (bindlessInitialised_ && bindlessSetLayout_ != VK_NULL_HANDLE) setLayouts.push_back(bindlessSetLayout_);

    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    layoutInfo.pSetLayouts = setLayouts.data();
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    if (vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &scenePipelineLayout_) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreatePipelineLayout failed.\n");
        vkDestroyShaderModule(device_, vertModule, nullptr);
        vkDestroyShaderModule(device_, fragModule, nullptr);
        return false;
    }

    scenePipeline_ = buildScenePipeline(vertModule, fragModule);
    if (gpuFeatures_.shaderClock) {
        const std::string costName = fragShaderName.substr(0, fragShaderName.size() - std::string(".frag.spv").size()) +
                                     "_cost.frag.spv";
        auto costCode = readBinaryFile(shaderDir + "/" + costName);
        VkShaderModule costModule = costCode.empty() ? VK_NULL_HANDLE : createShaderModule(device_, costCode);
        if (costModule != VK_NULL_HANDLE) {
            shaderCostPipeline_ = buildScenePipeline(vertModule, costModule);
            vkDestroyShaderModule(device_, costModule, nullptr);
        }
    }

    vkDestroyShaderModule(device_, vertModule, nullptr);
    vkDestroyShaderModule(device_, fragModule, nullptr);

    if (scenePipeline_ == VK_NULL_HANDLE) {
        std::fprintf(stderr, "Renderer: vkCreateGraphicsPipelines failed.\n");
        return false;
    }
    return true;
}

VkPipeline Renderer::buildScenePipeline(VkShaderModule vertModule, VkShaderModule fragModule) {
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1] = VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

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
    // CULL_NONE: correctness over an (unverified) winding-order
    // optimization -- see this class's header comment. Purely a perf
    // trade for a closed mesh; zero visual difference either way.
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT; // no MSAA yet -- §4.1 post-stack TODO

    VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE; // no transparency pass yet, §4.1 TODO

    VkPipelineColorBlendStateCreateInfo colorBlending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    std::array<VkPipelineColorBlendAttachmentState, 2> blendAttachments{colorBlendAttachment, {}};
    blendAttachments[1].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT;
    colorBlending.attachmentCount = 2;
    colorBlending.pAttachments = blendAttachments.data();

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                      VK_DYNAMIC_STATE_FRAGMENT_SHADING_RATE_KHR};
    VkPipelineDynamicStateCreateInfo dynamicState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamicState.dynamicStateCount = gpuFeatures_.fragmentShadingRate ? 3 : 2; // see applySceneShadingRate()
    dynamicState.pDynamicStates = dynamicStates;

    VkPipelineRenderingCreateInfo renderingInfo{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    renderingInfo.colorAttachmentCount = 2;
    VkFormat hdrFormat = kHDRFormat; // renders into the internal HDR intermediate, not the final presentable image
    std::array<VkFormat, 2> sceneColorFormats{hdrFormat, kVelocityFormat};
    renderingInfo.pColorAttachmentFormats = sceneColorFormats.data();
    renderingInfo.depthAttachmentFormat = depthFormat_;

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
    pipelineInfo.layout = scenePipelineLayout_;

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(device_, pipelineCache_, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return pipeline;
}

Renderer::SurfaceShaderTarget Renderer::surfaceShaderTarget() const {
    return {rayTracingSupported_, bindlessInitialised_,
            resolveResourceDir(executableDirectory(), "shaders", ENGINE_SHADER_DIR)};
}

uint32_t Renderer::createSurfacePipeline(const std::vector<uint32_t>& fragmentSpirv) {
    if (device_ == VK_NULL_HANDLE || scenePipelineLayout_ == VK_NULL_HANDLE || fragmentSpirv.empty()) {
        return kInvalidSurfacePipeline;
    }
    auto vertCode = readBinaryFile(surfaceShaderTarget().shaderDirectory + "/scene.vert.spv");
    std::vector<char> fragCode(fragmentSpirv.size() * sizeof(uint32_t));
    std::memcpy(fragCode.data(), fragmentSpirv.data(), fragCode.size());
    VkShaderModule vertModule = vertCode.empty() ? VK_NULL_HANDLE : createShaderModule(device_, vertCode);
    VkShaderModule fragModule = createShaderModule(device_, fragCode);
    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vertModule != VK_NULL_HANDLE && fragModule != VK_NULL_HANDLE) pipeline = buildScenePipeline(vertModule, fragModule);
    if (vertModule != VK_NULL_HANDLE) vkDestroyShaderModule(device_, vertModule, nullptr);
    if (fragModule != VK_NULL_HANDLE) vkDestroyShaderModule(device_, fragModule, nullptr);
    if (pipeline == VK_NULL_HANDLE) return kInvalidSurfacePipeline;
    const uint32_t handle = nextSurfacePipeline_++;
    surfacePipelines_[handle] = pipeline;
    return handle;
}

void Renderer::destroySurfacePipeline(uint32_t handle) {
    auto it = surfacePipelines_.find(handle);
    if (it == surfacePipelines_.end()) return;
    // Rare (a graph re-applied or dropped), so a full idle beats tracking
    // which in-flight frame last used it.
    vkDeviceWaitIdle(device_);
    vkDestroyPipeline(device_, it->second, nullptr);
    surfacePipelines_.erase(it);
}

void Renderer::destroySurfacePipelines() {
    for (auto& [handle, pipeline] : surfacePipelines_) vkDestroyPipeline(device_, pipeline, nullptr);
    surfacePipelines_.clear();
}

void Renderer::destroyScenePipeline() {
    if (shaderCostPipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(device_, shaderCostPipeline_, nullptr);
    shaderCostPipeline_ = VK_NULL_HANDLE;
    if (scenePipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, scenePipeline_, nullptr);
        scenePipeline_ = VK_NULL_HANDLE;
    }
    if (scenePipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, scenePipelineLayout_, nullptr);
        scenePipelineLayout_ = VK_NULL_HANDLE;
    }
}

// Kronos ("Real-Time Rendering Evolved" trailer) -- see shaders/glass.frag's
// own header comment for the real Fresnel+refract() technique this
// pipeline drives. Deliberately mirrors createScenePipeline()'s own
// shape (same vertex input layout -- glass meshes are ordinary
// Mesh::createPlane() output, same depth test/write, opaque overwrite,
// no blending: the shader itself computes the final "what you see
// through/off the glass" color directly, so there's nothing to blend),
// just with a smaller, dedicated pipeline layout (set=0 only, no
// material-texture set=1) and its own, smaller push constant range.
bool Renderer::createGlassPipeline() {
    std::string shaderDir = resolveResourceDir(executableDirectory(), "shaders", ENGINE_SHADER_DIR);
    auto vertCode = readBinaryFile(shaderDir + "/glass.vert.spv");
    auto fragCode = readBinaryFile(shaderDir + "/glass.frag.spv");
    if (vertCode.empty() || fragCode.empty()) {
        std::fprintf(stderr, "Renderer: failed to read compiled glass shaders from \"%s\".\n", shaderDir.c_str());
        return false;
    }

    VkShaderModule vertModule = createShaderModule(device_, vertCode);
    VkShaderModule fragModule = createShaderModule(device_, fragCode);
    if (vertModule == VK_NULL_HANDLE || fragModule == VK_NULL_HANDLE) {
        std::fprintf(stderr, "Renderer: vkCreateShaderModule (glass) failed.\n");
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1] = VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

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

    VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE; // the shader computes the final color directly -- see this function's own comment

    VkPipelineColorBlendStateCreateInfo colorBlending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    // Transparent glass keeps the opaque surface's velocity underneath it.
    std::array<VkPipelineColorBlendAttachmentState, 2> blendAttachments{colorBlendAttachment, {}};
    blendAttachments[1].colorWriteMask = 0;
    colorBlending.attachmentCount = 2;
    colorBlending.pAttachments = blendAttachments.data();

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(GlassPushConstants);

    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &sceneDescriptorSetLayout_;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    if (vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &glassPipelineLayout_) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreatePipelineLayout (glass) failed.\n");
        vkDestroyShaderModule(device_, vertModule, nullptr);
        vkDestroyShaderModule(device_, fragModule, nullptr);
        return false;
    }

    VkPipelineRenderingCreateInfo renderingInfo{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    renderingInfo.colorAttachmentCount = 2;
    VkFormat hdrFormat = kHDRFormat;
    std::array<VkFormat, 2> sceneColorFormats{hdrFormat, kVelocityFormat};
    renderingInfo.pColorAttachmentFormats = sceneColorFormats.data();
    renderingInfo.depthAttachmentFormat = depthFormat_;

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
    pipelineInfo.layout = glassPipelineLayout_;

    VkResult result = vkCreateGraphicsPipelines(device_, pipelineCache_, 1, &pipelineInfo, nullptr, &glassPipeline_);

    vkDestroyShaderModule(device_, vertModule, nullptr);
    vkDestroyShaderModule(device_, fragModule, nullptr);

    if (result != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreateGraphicsPipelines (glass) failed.\n");
        return false;
    }
    return true;
}

void Renderer::destroyGlassPipeline() {
    if (glassPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, glassPipeline_, nullptr);
        glassPipeline_ = VK_NULL_HANDLE;
    }
    if (glassPipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, glassPipelineLayout_, nullptr);
        glassPipelineLayout_ = VK_NULL_HANDLE;
    }
}

bool Renderer::createSkinnedScenePipeline() {
    std::string shaderDir = resolveResourceDir(executableDirectory(), "shaders", ENGINE_SHADER_DIR);
    auto vertCode = readBinaryFile(shaderDir + "/scene_skinned.vert.spv");
    // Deliberately NOT the bindless variant, unlike the opaque and
    // instanced pipelines. skinnedScenePipelineLayout_ already uses set 2
    // for the per-entity skinning UBO, and the bindless array is declared
    // at set 2 -- they cannot coexist. Nothing is lost: skinned entities
    // have no per-entity textured materials by design (see the default
    // material set bound in drawSkinnedEntities()), so there is nothing
    // for a bindless lookup to find.
    auto fragCode = readBinaryFile(shaderDir + "/scene.frag.spv");
    if (vertCode.empty() || fragCode.empty()) {
        std::fprintf(stderr, "Renderer: failed to read compiled shaders from \"%s\" (skinned).\n", shaderDir.c_str());
        return false;
    }

    VkShaderModule vertModule = createShaderModule(device_, vertCode);
    VkShaderModule fragModule = createShaderModule(device_, fragCode);
    if (vertModule == VK_NULL_HANDLE || fragModule == VK_NULL_HANDLE) {
        std::fprintf(stderr, "Renderer: vkCreateShaderModule failed (skinned).\n");
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1] = VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    // Two vertex bindings: 0 = base Vertex (position/normal/uv/tangent),
    // 1 = GpuSkinVertex (joint indices/weights) -- see RiggedMesh.hpp.
    std::array<VkVertexInputBindingDescription, 2> bindingDescs{Vertex::bindingDescription(),
                                                                  GpuSkinVertex::bindingDescription()};
    auto vertexAttrs = Vertex::attributeDescriptions();
    auto skinAttrs = GpuSkinVertex::attributeDescriptions(4); // locations 0-3 are Vertex's -- see that call's comment
    std::vector<VkVertexInputAttributeDescription> attrDescs;
    attrDescs.reserve(vertexAttrs.size() + skinAttrs.size());
    attrDescs.insert(attrDescs.end(), vertexAttrs.begin(), vertexAttrs.end());
    attrDescs.insert(attrDescs.end(), skinAttrs.begin(), skinAttrs.end());

    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescs.size());
    vertexInput.pVertexBindingDescriptions = bindingDescs.data();
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrDescs.size());
    vertexInput.pVertexAttributeDescriptions = attrDescs.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE; // same correctness-over-perf reasoning as createScenePipeline()
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

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

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(ObjectPushConstants);

    // Three sets: 0 = scene UBO+shadow (sceneDescriptorSetLayout_, shared
    // with every other opaque pipeline), 1 = material textures
    // (materialDescriptorSetLayout_, also shared), 2 = this draw's bone
    // matrices (skinningDescriptorSetLayout_, new) -- a genuinely
    // different set signature from scenePipelineLayout_'s, hence its own
    // VkPipelineLayout object rather than reusing that one (see this
    // member's declaration comment in Renderer.hpp).
    std::array<VkDescriptorSetLayout, 3> setLayouts{sceneDescriptorSetLayout_, materialDescriptorSetLayout_,
                                                      skinningDescriptorSetLayout_};
    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    layoutInfo.pSetLayouts = setLayouts.data();
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    if (vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &skinnedScenePipelineLayout_) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreatePipelineLayout failed (skinned).\n");
        vkDestroyShaderModule(device_, vertModule, nullptr);
        vkDestroyShaderModule(device_, fragModule, nullptr);
        return false;
    }

    VkPipelineRenderingCreateInfo renderingInfo{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    renderingInfo.colorAttachmentCount = 2;
    VkFormat hdrFormat = kHDRFormat;
    std::array<VkFormat, 2> sceneColorFormats{hdrFormat, kVelocityFormat};
    renderingInfo.pColorAttachmentFormats = sceneColorFormats.data();
    renderingInfo.depthAttachmentFormat = depthFormat_;

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
    pipelineInfo.layout = skinnedScenePipelineLayout_;

    VkResult result =
        vkCreateGraphicsPipelines(device_, pipelineCache_, 1, &pipelineInfo, nullptr, &skinnedScenePipeline_);

    vkDestroyShaderModule(device_, vertModule, nullptr);
    vkDestroyShaderModule(device_, fragModule, nullptr);

    if (result != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreateGraphicsPipelines failed (skinned).\n");
        return false;
    }
    return true;
}

void Renderer::destroySkinnedScenePipeline() {
    if (skinnedScenePipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, skinnedScenePipeline_, nullptr);
        skinnedScenePipeline_ = VK_NULL_HANDLE;
    }
    if (skinnedScenePipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, skinnedScenePipelineLayout_, nullptr);
        skinnedScenePipelineLayout_ = VK_NULL_HANDLE;
    }
}

bool Renderer::createInstancedScenePipeline() {
    // Reuses scenePipelineLayout_ rather than creating its own: same
    // descriptor set layout (the UBO/shadow-sampler binding), and while
    // the layout's push-constant range is sized for ObjectPushConstants,
    // scene_instanced.vert never declares a push_constant block at all --
    // an unused reservation in the layout costs nothing, and a pipeline
    // is free to not consume every resource its layout makes available.
    // Not reusing the layout would mean a second, functionally-identical
    // VkPipelineLayout object for no benefit.
    std::string shaderDir = resolveResourceDir(executableDirectory(), "shaders", ENGINE_SHADER_DIR);
    auto vertCode = readBinaryFile(shaderDir + "/scene_instanced.vert.spv");
    // Same bindless variant selection as createScenePipeline() -- see there.
    auto fragCode = readBinaryFile(shaderDir + (bindlessInitialised_ ? "/scene_bindless.frag.spv" : "/scene.frag.spv"));
    if (vertCode.empty() || fragCode.empty()) {
        std::fprintf(stderr, "Renderer: failed to read compiled shaders from \"%s\" (instanced).\n", shaderDir.c_str());
        return false;
    }

    VkShaderModule vertModule = createShaderModule(device_, vertCode);
    VkShaderModule fragModule = createShaderModule(device_, fragCode);
    if (vertModule == VK_NULL_HANDLE || fragModule == VK_NULL_HANDLE) {
        std::fprintf(stderr, "Renderer: vkCreateShaderModule failed (instanced).\n");
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1] = VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    // Two vertex input bindings: 0 = per-vertex Vertex data (rate=VERTEX,
    // shared with the non-instanced pipeline), 1 = per-instance InstanceData
    // (rate=INSTANCE) -- see SceneTypes.hpp's InstanceData and its
    // binding/attribute description methods.
    auto vertexBinding = Vertex::bindingDescription();
    auto instanceBinding = InstanceData::bindingDescription();
    std::array<VkVertexInputBindingDescription, 2> bindings{vertexBinding, instanceBinding};

    auto vertexAttrs = Vertex::attributeDescriptions();
    auto instanceAttrs = InstanceData::attributeDescriptions();
    std::vector<VkVertexInputAttributeDescription> allAttrs;
    allAttrs.reserve(vertexAttrs.size() + instanceAttrs.size());
    allAttrs.insert(allAttrs.end(), vertexAttrs.begin(), vertexAttrs.end());
    allAttrs.insert(allAttrs.end(), instanceAttrs.begin(), instanceAttrs.end());

    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size());
    vertexInput.pVertexBindingDescriptions = bindings.data();
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(allAttrs.size());
    vertexInput.pVertexAttributeDescriptions = allAttrs.data();

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
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    std::array<VkPipelineColorBlendAttachmentState, 2> blendAttachments{colorBlendAttachment, {}};
    blendAttachments[1].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT;
    colorBlending.attachmentCount = 2;
    colorBlending.pAttachments = blendAttachments.data();

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                      VK_DYNAMIC_STATE_FRAGMENT_SHADING_RATE_KHR};
    VkPipelineDynamicStateCreateInfo dynamicState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamicState.dynamicStateCount = gpuFeatures_.fragmentShadingRate ? 3 : 2; // see applySceneShadingRate()
    dynamicState.pDynamicStates = dynamicStates;

    VkPipelineRenderingCreateInfo renderingInfo{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    renderingInfo.colorAttachmentCount = 2;
    VkFormat hdrFormat = kHDRFormat; // renders into the internal HDR intermediate, not the final presentable image
    std::array<VkFormat, 2> sceneColorFormats{hdrFormat, kVelocityFormat};
    renderingInfo.pColorAttachmentFormats = sceneColorFormats.data();
    renderingInfo.depthAttachmentFormat = depthFormat_;

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
    pipelineInfo.layout = scenePipelineLayout_;

    VkResult result =
        vkCreateGraphicsPipelines(device_, pipelineCache_, 1, &pipelineInfo, nullptr, &instancedScenePipeline_);

    vkDestroyShaderModule(device_, vertModule, nullptr);
    vkDestroyShaderModule(device_, fragModule, nullptr);

    if (result != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreateGraphicsPipelines failed (instanced).\n");
        return false;
    }
    return true;
}

void Renderer::destroyInstancedScenePipeline() {
    if (instancedScenePipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, instancedScenePipeline_, nullptr);
        instancedScenePipeline_ = VK_NULL_HANDLE;
    }
}

bool Renderer::createParticlePipeline() {
    // Reuses scenePipelineLayout_ -- same reasoning as
    // createInstancedScenePipeline(): all per-particle data comes through
    // instanced vertex attributes (ParticleInstanceData), so particle.vert
    // declares no push_constant block at all.
    std::string shaderDir = resolveResourceDir(executableDirectory(), "shaders", ENGINE_SHADER_DIR);
    auto vertCode = readBinaryFile(shaderDir + "/particle.vert.spv");
    auto fragCode = readBinaryFile(shaderDir + "/particle.frag.spv");
    if (vertCode.empty() || fragCode.empty()) {
        std::fprintf(stderr, "Renderer: failed to read compiled shaders from \"%s\" (particle).\n", shaderDir.c_str());
        return false;
    }

    VkShaderModule vertModule = createShaderModule(device_, vertCode);
    VkShaderModule fragModule = createShaderModule(device_, fragCode);
    if (vertModule == VK_NULL_HANDLE || fragModule == VK_NULL_HANDLE) {
        std::fprintf(stderr, "Renderer: vkCreateShaderModule failed (particle).\n");
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1] = VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    auto vertexBinding = Vertex::bindingDescription();
    auto instanceBinding = ParticleInstanceData::bindingDescription();
    std::array<VkVertexInputBindingDescription, 2> bindings{vertexBinding, instanceBinding};

    auto vertexAttrs = Vertex::attributeDescriptions();
    auto instanceAttrs = ParticleInstanceData::attributeDescriptions();
    std::vector<VkVertexInputAttributeDescription> allAttrs;
    allAttrs.reserve(vertexAttrs.size() + instanceAttrs.size());
    allAttrs.insert(allAttrs.end(), vertexAttrs.begin(), vertexAttrs.end());
    allAttrs.insert(allAttrs.end(), instanceAttrs.begin(), instanceAttrs.end());

    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size());
    vertexInput.pVertexBindingDescriptions = bindings.data();
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(allAttrs.size());
    vertexInput.pVertexAttributeDescriptions = allAttrs.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE; // billboards always face the camera, but NONE costs nothing extra here
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depthStencil.depthTestEnable = VK_TRUE;   // still occluded by solid world geometry
    depthStencil.depthWriteEnable = VK_FALSE; // particles don't occlude each other or write depth -- standard for blended fx
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    // Premultiplied blending: particle.frag outputs alpha 0 for additive
    // particles (dst * 1, the classic glow) and their coverage for occluding
    // ones (an over-blend), so both kinds share one unsorted pass.
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                           VK_COLOR_COMPONENT_B_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    // Velocity is an over-blend weighted by coverage, so faint particles
    // mostly keep the motion of the surface behind them.
    VkPipelineColorBlendAttachmentState velocityBlendAttachment{};
    velocityBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT;
    velocityBlendAttachment.blendEnable = VK_TRUE;
    velocityBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    velocityBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    velocityBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    velocityBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    velocityBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    velocityBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    std::array<VkPipelineColorBlendAttachmentState, 2> blendAttachments{colorBlendAttachment, velocityBlendAttachment};
    VkPipelineColorBlendStateCreateInfo colorBlending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlending.attachmentCount = static_cast<uint32_t>(blendAttachments.size());
    colorBlending.pAttachments = blendAttachments.data();

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkPipelineRenderingCreateInfo renderingInfo{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    // Renders into the internal HDR intermediate and the scene velocity buffer.
    const std::array<VkFormat, 2> colorFormats{kHDRFormat, kVelocityFormat};
    renderingInfo.colorAttachmentCount = static_cast<uint32_t>(colorFormats.size());
    renderingInfo.pColorAttachmentFormats = colorFormats.data();
    renderingInfo.depthAttachmentFormat = depthFormat_;

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

    // Sprint 16: dedicated pipeline layout (not scenePipelineLayout_) --
    // set0 is the same real sceneDescriptorSetLayout_ every opaque
    // pipeline binds, set1 is postProcessSingleSetLayout_ (one combined-
    // image-sampler, reused as-is -- the exact same binding *shape*
    // luminance's own set already uses, just pointed at depth here) for
    // real soft-particle depth sampling. See this pipeline's own header
    // comment on why it doesn't extend scenePipelineLayout_ instead.
    std::array<VkDescriptorSetLayout, 2> particleSetLayouts{sceneDescriptorSetLayout_, postProcessSingleSetLayout_};
    VkPipelineLayoutCreateInfo particleLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    particleLayoutInfo.setLayoutCount = static_cast<uint32_t>(particleSetLayouts.size());
    particleLayoutInfo.pSetLayouts = particleSetLayouts.data();
    if (vkCreatePipelineLayout(device_, &particleLayoutInfo, nullptr, &particlePipelineLayout_) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreatePipelineLayout failed (particle).\n");
        vkDestroyShaderModule(device_, vertModule, nullptr);
        vkDestroyShaderModule(device_, fragModule, nullptr);
        return false;
    }
    pipelineInfo.layout = particlePipelineLayout_;

    VkResult result = vkCreateGraphicsPipelines(device_, pipelineCache_, 1, &pipelineInfo, nullptr, &particlePipeline_);

    vkDestroyShaderModule(device_, vertModule, nullptr);
    vkDestroyShaderModule(device_, fragModule, nullptr);

    if (result != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreateGraphicsPipelines failed (particle).\n");
        return false;
    }
    return true;
}

void Renderer::destroyParticlePipeline() {
    if (particlePipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, particlePipelineLayout_, nullptr);
        particlePipelineLayout_ = VK_NULL_HANDLE;
    }
    if (particlePipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, particlePipeline_, nullptr);
        particlePipeline_ = VK_NULL_HANDLE;
    }
}

bool Renderer::createMaterialResources() {
    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    if (vkCreateSampler(device_, &samplerInfo, nullptr, &materialSampler_) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreateSampler (material) failed.\n");
        return false;
    }

    VkDescriptorSetLayoutBinding binding{};
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // 0=albedo, 1=normal, 2=metallic, 3=roughness, 4=ao -- see Components.hpp's Renderable texture fields.
    std::array<VkDescriptorSetLayoutBinding, 5> bindings{binding, binding, binding, binding, binding};
    for (uint32_t i = 0; i < bindings.size(); ++i) bindings[i].binding = i;

    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &materialDescriptorSetLayout_) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreateDescriptorSetLayout (material) failed.\n");
        return false;
    }

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxMaterialDescriptorSets * 5};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = kMaxMaterialDescriptorSets;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &materialDescriptorPool_) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreateDescriptorPool (material) failed.\n");
        return false;
    }

    defaultWhiteTexture_ =
        Texture::createSolidColor(255, 255, 255, 255, allocator_, device_, commandPool_, graphicsQueue_);
    defaultFlatNormalTexture_ =
        Texture::createSolidColor(128, 128, 255, 255, allocator_, device_, commandPool_, graphicsQueue_);
    if (!defaultWhiteTexture_.isValid() || !defaultFlatNormalTexture_.isValid()) {
        std::fprintf(stderr, "Renderer: default material texture creation failed.\n");
        return false;
    }
    return true;
}

void Renderer::destroyMaterialResources() {
    materialDescriptorCache_.clear(); // the sets themselves are freed when materialDescriptorPool_ is destroyed below
    defaultWhiteTexture_.destroy(allocator_, device_);
    defaultFlatNormalTexture_.destroy(allocator_, device_);
    if (materialDescriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, materialDescriptorPool_, nullptr);
        materialDescriptorPool_ = VK_NULL_HANDLE;
    }
    if (materialDescriptorSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, materialDescriptorSetLayout_, nullptr);
        materialDescriptorSetLayout_ = VK_NULL_HANDLE;
    }
    if (materialSampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device_, materialSampler_, nullptr);
        materialSampler_ = VK_NULL_HANDLE;
    }
}

} // namespace engine::core
