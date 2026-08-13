#include "studio/plugins/TexturePreviewPlugin.hpp"

#include <algorithm>

#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>

namespace engine::studio::plugins {

TexturePreviewPlugin::TexturePreviewPlugin(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue)
    : allocator_(allocator), device_(device), cmdPool_(cmdPool), queue_(queue) {
    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCreateSampler(device_, &samplerInfo, nullptr, &sampler_);
}

TexturePreviewPlugin::~TexturePreviewPlugin() { shutdown(); }

void TexturePreviewPlugin::shutdown() {
    releasePreview();
    if (sampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device_, sampler_, nullptr);
        sampler_ = VK_NULL_HANDLE;
    }
}

void TexturePreviewPlugin::releasePreview() {
    if (previewDescriptor_ != VK_NULL_HANDLE) {
        ImGui_ImplVulkan_RemoveTexture(previewDescriptor_);
        previewDescriptor_ = VK_NULL_HANDLE;
    }
    if (previewTexture_.isValid()) {
        previewTexture_.destroy(allocator_, device_);
    }
}

void TexturePreviewPlugin::drawPanel(core::ECS& /*ecs*/, core::EntityId /*selected*/,
                                       const std::vector<core::EntityId>& /*selectedEntities*/) {
    ImGui::Begin("Texture Previewer");

    ImGui::TextWrapped("Load an image file to see it decoded and uploaded to the GPU exactly as the Material "
                        "Editor's texture slots would.");
    ImGui::SetNextItemWidth(320.0f);
    ImGui::InputText("Path", pathBuffer_, sizeof(pathBuffer_));
    ImGui::SameLine();
    if (ImGui::Button("Load")) {
        std::string path = pathBuffer_;
        lastMetadata_ = core::extractAssetMetadata(path);

        if (!lastMetadata_.succeeded || lastMetadata_.kind != core::AssetKind::Texture) {
            statusMessage_ = lastMetadata_.succeeded ? "Not a recognized image file." : ("Failed: " + lastMetadata_.error);
        } else {
            core::Texture loaded = core::Texture::loadFromFile(path, allocator_, device_, cmdPool_, queue_, /*srgb=*/true);
            if (!loaded.isValid()) {
                statusMessage_ = "Decode/upload failed -- see stderr for stb_image's reason.";
            } else {
                releasePreview();
                previewTexture_ = std::move(loaded);
                previewDescriptor_ = ImGui_ImplVulkan_AddTexture(sampler_, previewTexture_.view(),
                                                                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                statusMessage_ = "Loaded and uploaded to GPU.";
            }
        }
    }

    if (!statusMessage_.empty()) {
        ImGui::TextDisabled("%s", statusMessage_.c_str());
    }

    if (lastMetadata_.succeeded && lastMetadata_.kind == core::AssetKind::Texture) {
        ImGui::SeparatorText("Metadata");
        ImGui::Text("File size: %llu bytes", static_cast<unsigned long long>(lastMetadata_.fileSizeBytes));
        ImGui::Text("Dimensions: %d x %d", lastMetadata_.width, lastMetadata_.height);
        ImGui::Text("Source channels: %d", lastMetadata_.channels);
    }

    if (previewDescriptor_ != VK_NULL_HANDLE) {
        ImGui::SeparatorText("Preview");
        float maxWidth = ImGui::GetContentRegionAvail().x;
        float aspect = previewTexture_.height() > 0
                           ? static_cast<float>(previewTexture_.width()) / static_cast<float>(previewTexture_.height())
                           : 1.0f;
        float displayWidth = std::min(maxWidth, static_cast<float>(previewTexture_.width()));
        float displayHeight = displayWidth / std::max(aspect, 0.0001f);
        ImGui::Image(previewDescriptor_, ImVec2(displayWidth, displayHeight));
    }

    ImGui::End();
}

} // namespace engine::studio::plugins
