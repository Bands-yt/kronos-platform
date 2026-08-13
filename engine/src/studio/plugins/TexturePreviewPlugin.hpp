#pragma once

#include <string>

#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/AssetMetadata.hpp"
#include "core/Texture.hpp"
#include "studio/IStudioPlugin.hpp"

namespace engine::studio::plugins {

// A real texture previewer -- Load reads an actual image file via
// core::Texture::loadFromFile() (vendored stb_image, the same decoder
// the Material Editor's texture slots use) and displays it with
// ImGui::Image(), the same ImGui_ImplVulkan_AddTexture()-backed technique
// studio::OffscreenTarget uses for the Viewport panel's rendered scene --
// a real GPU-resident texture, not a thumbnail generated some other way.
// The previous preview's ImGui descriptor is torn down synchronously
// when a new one loads (safe here, unlike OffscreenTarget's resize bug
// earlier this project: the swap happens entirely within one frame's
// panel-drawing phase, with no pre-pass/overlay split for a stale
// reference to survive across -- see OffscreenTarget.cpp's ensureSize()
// comment for the actual hazard that doesn't apply here).
class TexturePreviewPlugin final : public IStudioPlugin {
public:
    TexturePreviewPlugin(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue);
    ~TexturePreviewPlugin() override;

    TexturePreviewPlugin(const TexturePreviewPlugin&) = delete;
    TexturePreviewPlugin& operator=(const TexturePreviewPlugin&) = delete;

    // Unlike MaterialPlugin/ModelImporterPlugin (which only ever register
    // textures/meshes into StudioApp's *shared* TextureLibrary/
    // MeshLibrary, destroyed at the right point in StudioApp::shutdown()
    // already), this plugin owns its preview texture, ImGui descriptor,
    // and sampler *directly*. Those are real Vulkan/VMA resources that
    // must be destroyed before the device/allocator they came from --
    // but pluginManager_ (and therefore this plugin's destructor) doesn't
    // run until ~StudioApp(), *after* StudioApp::shutdown() has already
    // torn down the device. So StudioApp calls this explicitly, before
    // that teardown, the same ordering fix
    // OffscreenTarget.cpp's ensureSize()/StudioApp::shutdown() needed
    // for the exact same class of bug. Safe to call more than once (the
    // destructor calls it again as a backstop) -- every teardown call
    // inside null-checks its own handle first.
    void shutdown();

    [[nodiscard]] const char* name() const override { return "Texture Previewer"; }
    [[nodiscard]] const char* category() const override { return "Assets"; }

    void drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) override;

private:
    void releasePreview();

    VmaAllocator allocator_;
    VkDevice device_;
    VkCommandPool cmdPool_;
    VkQueue queue_;

    VkSampler sampler_ = VK_NULL_HANDLE; // persistent for the plugin's lifetime, created once
    char pathBuffer_[256] = "";
    std::string statusMessage_;
    core::AssetMetadata lastMetadata_;

    core::Texture previewTexture_;
    VkDescriptorSet previewDescriptor_ = VK_NULL_HANDLE;
};

} // namespace engine::studio::plugins
