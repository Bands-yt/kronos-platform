#include "core/GuiImageCache.hpp"

#include <backends/imgui_impl_vulkan.h>

#include "core/RobloxGui.hpp"

namespace engine::core {

GuiImageCache::GuiImageCache(VmaAllocator allocator, VkDevice device, VkCommandPool commandPool, VkQueue queue)
    : allocator_(allocator), device_(device), commandPool_(commandPool), queue_(queue) {
    VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    info.magFilter = VK_FILTER_LINEAR;
    info.minFilter = VK_FILTER_LINEAR;
    info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(device_, &info, nullptr, &sampler_) != VK_SUCCESS) sampler_ = VK_NULL_HANDLE;
}

GuiImageCache::~GuiImageCache() {
    clear();
    if (sampler_ != VK_NULL_HANDLE) vkDestroySampler(device_, sampler_, nullptr);
}

void GuiImageCache::setRoots(std::vector<std::filesystem::path> roots) {
    if (roots == roots_) return;
    clear();
    roots_ = std::move(roots);
}

void* GuiImageCache::get(const std::string& image) {
    if (const auto found = entries_.find(image); found != entries_.end()) {
        return found->second.descriptor == VK_NULL_HANDLE ? nullptr : found->second.descriptor;
    }
    if (entries_.size() >= kMaxImages || sampler_ == VK_NULL_HANDLE) return nullptr;
    Entry& entry = entries_[image];
    const std::string file = gui::imageFile(image, roots_);
    if (file.empty()) return nullptr;
    entry.texture = Texture::loadFromFile(file, allocator_, device_, commandPool_, queue_, true);
    if (!entry.texture.isValid()) return nullptr;
    entry.descriptor = ImGui_ImplVulkan_AddTexture(sampler_, entry.texture.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    return entry.descriptor;
}

void GuiImageCache::clear() {
    if (entries_.empty()) return;
    vkDeviceWaitIdle(device_);
    for (auto& [image, entry] : entries_) {
        if (entry.descriptor != VK_NULL_HANDLE) ImGui_ImplVulkan_RemoveTexture(entry.descriptor);
        if (entry.texture.isValid()) entry.texture.destroy(allocator_, device_);
    }
    entries_.clear();
}

} // namespace engine::core
