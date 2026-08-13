#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <volk.h>
#include <vk_mem_alloc.h>

namespace engine::core {

// A real, file-backed GPU texture -- loaded via stb_image (vendored,
// external/vendor/stb/stb_image.h), uploaded to a device-local VkImage
// through a staging buffer, one mip level (no mip-chain generation -- a
// real content pipeline would bake mips, not this bring-up loader). This
// is the first thing in this engine that decodes actual image files
// rather than generating pixels/geometry procedurally.
class Texture {
public:
    // `srgb`: true for color data (albedo) that needs hardware sRGB->linear
    // conversion on sample; false for data textures (metallic/roughness/
    // normal maps) that are already linear and must NOT be gamma-decoded.
    // Returns an invalid (isValid()==false) Texture on failure -- bad
    // path, unsupported format, decode failure -- logged to stderr, never
    // a silent partial result.
    [[nodiscard]] static Texture loadFromFile(const std::string& path, VmaAllocator allocator, VkDevice device,
                                               VkCommandPool cmdPool, VkQueue queue, bool srgb);

    // A solid 1x1 texture -- what every material texture slot points at
    // by default (see Components.hpp's MaterialTextures) so scene.frag
    // can always sample *something* without a has-texture branch: a 1x1
    // white pixel multiplies as a no-op against the existing flat
    // baseColor/metallicRoughness values.
    [[nodiscard]] static Texture createSolidColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a, VmaAllocator allocator,
                                                   VkDevice device, VkCommandPool cmdPool, VkQueue queue);

    // Sprint 16: the same real GPU-upload path as loadFromFile(), for
    // procedurally-generated pixel data -- see this method's own .cpp
    // comment. `rgba`: width*height*4 bytes, tightly packed.
    [[nodiscard]] static Texture createFromPixels(const uint8_t* rgba, int width, int height, bool srgb,
                                                   VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool,
                                                   VkQueue queue);

    void destroy(VmaAllocator allocator, VkDevice device);

    [[nodiscard]] bool isValid() const { return image_ != VK_NULL_HANDLE; }
    [[nodiscard]] VkImageView view() const { return view_; }
    [[nodiscard]] int width() const { return width_; }
    [[nodiscard]] int height() const { return height_; }

private:
    [[nodiscard]] static Texture uploadPixels(const uint8_t* rgba, int width, int height, bool srgb,
                                               VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool,
                                               VkQueue queue);

    VkImage image_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = nullptr;
    VkImageView view_ = VK_NULL_HANDLE;
    int width_ = 0;
    int height_ = 0;
};

// Handle-based registry, same shape as MeshLibrary (see its header
// comment for why index-based with no per-handle removal is the honest
// data structure here, not a general-purpose asset manager).
class TextureLibrary {
public:
    static constexpr uint32_t kInvalidHandle = ~0u;

    uint32_t registerTexture(Texture texture);
    [[nodiscard]] const Texture* get(uint32_t handle) const;
    void destroyAll(VmaAllocator allocator, VkDevice device);

private:
    std::vector<Texture> textures_;
};

} // namespace engine::core
