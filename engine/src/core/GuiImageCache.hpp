#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/Texture.hpp"

namespace engine::core {

// Uploads the images Roblox GUI asks for (gui::imageFile) and hands ImGui
// their descriptors. Owned by a host (Player, Studio); needs a live ImGui
// Vulkan backend.
class GuiImageCache {
public:
    static constexpr size_t kMaxImages = 48;

    GuiImageCache(VmaAllocator allocator, VkDevice device, VkCommandPool commandPool, VkQueue queue);
    ~GuiImageCache();
    GuiImageCache(const GuiImageCache&) = delete;
    GuiImageCache& operator=(const GuiImageCache&) = delete;

    // Changing the roots (a new game) drops every image.
    void setRoots(std::vector<std::filesystem::path> roots);
    // nullptr when the image can't be found or loaded (remembered).
    [[nodiscard]] void* get(const std::string& image);
    void clear();

private:
    struct Entry {
        Texture texture;
        VkDescriptorSet descriptor = VK_NULL_HANDLE;
    };
    VmaAllocator allocator_;
    VkDevice device_;
    VkCommandPool commandPool_;
    VkQueue queue_;
    VkSampler sampler_ = VK_NULL_HANDLE;
    std::vector<std::filesystem::path> roots_;
    std::unordered_map<std::string, Entry> entries_;
};

} // namespace engine::core
