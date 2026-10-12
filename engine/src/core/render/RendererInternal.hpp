#pragma once

#include "core/Renderer.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace engine::core::renderer_internal {

inline constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";

inline bool layerAvailable(const char* name) {
    uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());
    return std::any_of(layers.begin(), layers.end(), [&](const VkLayerProperties& l) {
        return std::strcmp(l.layerName, name) == 0;
    });
}

inline VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT /*type*/,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void* /*userData*/) {
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        std::fprintf(stderr, "[vulkan] %s\n", data->pMessage);
    }
    return VK_FALSE;
}

inline std::vector<char> readBinaryFile(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        std::fprintf(stderr, "Renderer: could not open shader file \"%s\".\n", path.c_str());
        return {};
    }
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

// Empty `code` (a missing/unreadable/truncated .spv) would otherwise
// reach vkCreateShaderModule with codeSize=0 -- invalid usage that's
// undefined without a validation layer, rather than a clean failure.
inline VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& code) {
    if (code.empty()) {
        std::fprintf(stderr, "Renderer: refusing to create a shader module from empty/missing SPIR-V.\n");
        return VK_NULL_HANDLE;
    }
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = code.size();
    info.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule module = VK_NULL_HANDLE;
    vkCreateShaderModule(device, &info, nullptr, &module);
    return module;
}


} // namespace engine::core::renderer_internal
