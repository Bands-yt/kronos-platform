#include "core/UIRenderer.hpp"

#include "core/ResourcePaths.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace engine::core {

namespace {
std::vector<char> readBinaryFile(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return {};
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<char> data(static_cast<size_t>(size > 0 ? size : 0));
    if (!data.empty()) {
        size_t read = std::fread(data.data(), 1, data.size(), f);
        (void)read;
    }
    std::fclose(f);
    return data;
}

VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& code) {
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = code.size();
    info.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule module = VK_NULL_HANDLE;
    vkCreateShaderModule(device, &info, nullptr, &module);
    return module;
}
} // namespace

bool UIRenderer::initialize(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                             VkFormat colorFormat, const std::string& fontAtlasPath) {
    allocator_ = allocator;
    device_ = device;

    // srgb=false: the atlas's red channel is a real alpha mask (glyph
    // coverage), not display color -- gamma-decoding it on sample would
    // real-distort every glyph's own edge falloff.
    fontAtlas_ = Texture::loadFromFile(fontAtlasPath, allocator, device, cmdPool, queue, false);
    if (!fontAtlas_.isValid()) {
        std::fprintf(stderr, "UIRenderer: failed to load font atlas \"%s\".\n", fontAtlasPath.c_str());
        return false;
    }

    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(device_, &samplerInfo, nullptr, &sampler_) != VK_SUCCESS) {
        std::fprintf(stderr, "UIRenderer: vkCreateSampler failed.\n");
        return false;
    }

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &setLayout_) != VK_SUCCESS) {
        std::fprintf(stderr, "UIRenderer: vkCreateDescriptorSetLayout failed.\n");
        return false;
    }

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS) {
        std::fprintf(stderr, "UIRenderer: vkCreateDescriptorPool failed.\n");
        return false;
    }

    VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.descriptorPool = descriptorPool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &setLayout_;
    if (vkAllocateDescriptorSets(device_, &allocInfo, &descriptorSet_) != VK_SUCCESS) {
        std::fprintf(stderr, "UIRenderer: vkAllocateDescriptorSets failed.\n");
        return false;
    }

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = fontAtlas_.view();
    imageInfo.sampler = sampler_;
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = descriptorSet_;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);

    for (int i = 0; i < kFramesInFlight; ++i) {
        VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bufferInfo.size = kMaxVertices * 8 * sizeof(float); // 8 floats/vertex: vec2 pos + vec2 uv + vec4 color
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo vmaAllocInfo{};
        vmaAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        vmaAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo resultInfo{};
        if (vmaCreateBuffer(allocator_, &bufferInfo, &vmaAllocInfo, &frameBuffers_[i].buffer,
                             &frameBuffers_[i].allocation, &resultInfo) != VK_SUCCESS) {
            std::fprintf(stderr, "UIRenderer: vmaCreateBuffer (frame %d) failed.\n", i);
            return false;
        }
        frameBuffers_[i].mapped = resultInfo.pMappedData;
    }

    std::string shaderDir = resolveResourceDir(executableDirectory(), "shaders", ENGINE_SHADER_DIR);
    auto vertCode = readBinaryFile(shaderDir + "/ui.vert.spv");
    auto fragCode = readBinaryFile(shaderDir + "/ui.frag.spv");
    if (vertCode.empty() || fragCode.empty()) {
        std::fprintf(stderr, "UIRenderer: failed to read compiled ui.vert/ui.frag shaders from \"%s\".\n",
                     shaderDir.c_str());
        return false;
    }
    VkShaderModule vertModule = createShaderModule(device_, vertCode);
    VkShaderModule fragModule = createShaderModule(device_, fragCode);
    if (vertModule == VK_NULL_HANDLE || fragModule == VK_NULL_HANDLE) {
        std::fprintf(stderr, "UIRenderer: vkCreateShaderModule failed.\n");
        return false;
    }

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &setLayout_;
    bool ok = vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &pipelineLayout_) == VK_SUCCESS;

    if (ok) {
        VkVertexInputBindingDescription binding0{};
        binding0.binding = 0;
        binding0.stride = 8 * sizeof(float);
        binding0.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription attrs[3]{};
        attrs[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, 0};
        attrs[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, 2 * sizeof(float)};
        attrs[2] = {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 4 * sizeof(float)};

        VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &binding0;
        vertexInput.vertexAttributeDescriptionCount = 3;
        vertexInput.pVertexAttributeDescriptions = attrs;

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

        // Kronos ("User Interface" world-building): the real, first
        // alpha-blended pipeline in this renderer -- standard "over"
        // blending, appropriate now that UI text/panels are a real,
        // deliberate transparency use case (see this class's own header
        // comment on why the main scene pass itself still has none).
        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                               VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_TRUE;
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

        VkPipelineColorBlendStateCreateInfo colorBlending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamicState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamicState.dynamicStateCount = 2;
        dynamicState.pDynamicStates = dynamicStates;

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
        renderingInfo.pColorAttachmentFormats = &colorFormat;

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
        pipelineInfo.layout = pipelineLayout_;

        ok = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline_) == VK_SUCCESS;
    }
    vkDestroyShaderModule(device_, vertModule, nullptr);
    vkDestroyShaderModule(device_, fragModule, nullptr);

    if (!ok) {
        std::fprintf(stderr, "UIRenderer: pipeline creation failed.\n");
        return false;
    }

    vertexData_.reserve(kMaxVertices * 8);
    return true;
}

void UIRenderer::shutdown() {
    if (device_ == nullptr) return;
    for (auto& fb : frameBuffers_) {
        if (fb.buffer != VK_NULL_HANDLE) vmaDestroyBuffer(allocator_, fb.buffer, fb.allocation);
        fb.buffer = VK_NULL_HANDLE;
    }
    if (pipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(device_, pipeline_, nullptr);
    if (pipelineLayout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
    if (descriptorPool_ != VK_NULL_HANDLE) vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
    if (setLayout_ != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device_, setLayout_, nullptr);
    if (sampler_ != VK_NULL_HANDLE) vkDestroySampler(device_, sampler_, nullptr);
    fontAtlas_.destroy(allocator_, device_);
    pipeline_ = VK_NULL_HANDLE;
    device_ = nullptr;
}

void UIRenderer::beginFrame(VkExtent2D extent) {
    currentExtent_ = extent;
    vertexData_.clear();
}

void UIRenderer::pushQuad(glm::vec2 topLeftPx, glm::vec2 sizePx, glm::vec2 uv0, glm::vec2 uv1, glm::vec4 color) {
    if (currentExtent_.width == 0 || currentExtent_.height == 0) return;
    if (vertexData_.size() + 6 * 8 > vertexData_.capacity()) return; // real, honest drop past the real vertex budget

    float w = static_cast<float>(currentExtent_.width);
    float h = static_cast<float>(currentExtent_.height);
    auto toNdc = [&](glm::vec2 px) { return glm::vec2((px.x / w) * 2.0f - 1.0f, (px.y / h) * 2.0f - 1.0f); };

    glm::vec2 p0 = toNdc(topLeftPx);
    glm::vec2 p1 = toNdc(topLeftPx + glm::vec2(sizePx.x, 0.0f));
    glm::vec2 p2 = toNdc(topLeftPx + sizePx);
    glm::vec2 p3 = toNdc(topLeftPx + glm::vec2(0.0f, sizePx.y));

    glm::vec2 uvTL(uv0.x, uv0.y);
    glm::vec2 uvTR(uv1.x, uv0.y);
    glm::vec2 uvBR(uv1.x, uv1.y);
    glm::vec2 uvBL(uv0.x, uv1.y);

    auto push = [&](glm::vec2 pos, glm::vec2 uv) {
        vertexData_.insert(vertexData_.end(), {pos.x, pos.y, uv.x, uv.y, color.r, color.g, color.b, color.a});
    };
    push(p0, uvTL);
    push(p1, uvTR);
    push(p2, uvBR);
    push(p0, uvTL);
    push(p2, uvBR);
    push(p3, uvBL);
}

void UIRenderer::drawRect(glm::vec2 topLeftPx, glm::vec2 sizePx, glm::vec4 color) {
    pushQuad(topLeftPx, sizePx, glm::vec2(-1.0f, -1.0f), glm::vec2(-1.0f, -1.0f), color);
}

void UIRenderer::drawText(const std::string& text, glm::vec2 topLeftPx, float scale, glm::vec4 color) {
    constexpr float atlasW = kAtlasColumns * kCellWidthPx;
    float atlasRows = static_cast<float>((kLastChar - kFirstChar + 1 + kAtlasColumns - 1) / kAtlasColumns);
    float atlasH = atlasRows * kCellHeightPx;

    glm::vec2 cursor = topLeftPx;
    glm::vec2 glyphSize(kGlyphAdvancePx * scale, kGlyphHeightPx * scale);
    for (char c : text) {
        if (c == '\n') {
            cursor.x = topLeftPx.x;
            cursor.y += kGlyphHeightPx * scale * 1.2f;
            continue;
        }
        int code = static_cast<unsigned char>(c);
        if (code >= kFirstChar && code <= kLastChar) {
            int index = code - kFirstChar;
            int col = index % kAtlasColumns;
            int row = index / kAtlasColumns;
            glm::vec2 uv0(static_cast<float>(col) * kCellWidthPx / atlasW, static_cast<float>(row) * kCellHeightPx / atlasH);
            glm::vec2 uv1(uv0.x + kCellWidthPx / atlasW, uv0.y + kCellHeightPx / atlasH);
            pushQuad(cursor, glyphSize, uv0, uv1, color);
        }
        cursor.x += kGlyphAdvancePx * scale;
    }
}

glm::vec2 UIRenderer::measureText(const std::string& text, float scale) const {
    float maxWidth = 0.0f;
    float lineWidth = 0.0f;
    float lines = 1.0f;
    for (char c : text) {
        if (c == '\n') {
            maxWidth = std::max(maxWidth, lineWidth);
            lineWidth = 0.0f;
            lines += 1.0f;
            continue;
        }
        lineWidth += kGlyphAdvancePx * scale;
    }
    maxWidth = std::max(maxWidth, lineWidth);
    return glm::vec2(maxWidth, lines * kGlyphHeightPx * scale * (lines > 1.0f ? 1.2f : 1.0f));
}

void UIRenderer::draw(VkCommandBuffer cmd, VkImageView /*targetView*/, VkExtent2D extent) {
    if (pipeline_ == VK_NULL_HANDLE || vertexData_.empty()) return;

    FrameBuffer& fb = frameBuffers_[currentFrameIndex_];
    currentFrameIndex_ = (currentFrameIndex_ + 1) % kFramesInFlight;

    size_t byteSize = vertexData_.size() * sizeof(float);
    std::memcpy(fb.mapped, vertexData_.data(), byteSize);

    VkViewport viewport{0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &descriptorSet_, 0, nullptr);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &fb.buffer, &offset);
    vkCmdDraw(cmd, static_cast<uint32_t>(vertexData_.size() / 8), 1, 0, 0);
}

} // namespace engine::core
