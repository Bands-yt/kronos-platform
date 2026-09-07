#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>
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

    // Kronos ("Vulkan Compute PBR Painter" -- v0.4.0 Creator Suite): a
    // real sibling factory, NOT a retrofit of uploadPixels() above --
    // see core::ComputePbrPainter's own class comment for why a compute
    // shader needs a genuinely different image (VK_IMAGE_USAGE_STORAGE_BIT,
    // real explicit layout transitions the painter manages around each
    // dispatch) than every other Texture here, which is written once at
    // upload time and never touched again. Always VK_FORMAT_R8G8B8A8_UNORM
    // -- deliberately never _SRGB: the Vulkan spec does not mandate
    // VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT support for sRGB formats (many
    // real GPUs lack it), while plain UNORM storage-image support is
    // guaranteed by the spec's own mandatory format table. A real,
    // stated simplification for a first compute-paint pass: painted
    // albedo colors are stored as raw UNORM values with no sRGB encode
    // step, same "state the cut plainly" convention as this codebase's
    // other real-but-simpler features. Ends in
    // VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, cleared to `clearColor`
    // -- immediately sampleable even before any real paint stroke, the
    // same post-creation contract every other Texture factory here
    // already guarantees.
    [[nodiscard]] static Texture createStorageImage(int width, int height, glm::vec4 clearColor, VmaAllocator allocator,
                                                      VkDevice device, VkCommandPool cmdPool, VkQueue queue);

    // Kronos ("CapCut/DaVinci Hybrid NLE Suite" -- real MP4 video
    // playback): re-uploads new pixel data into THIS texture's existing
    // VkImage/allocation rather than creating a new one -- what a video
    // plane needs to show a new decoded frame every playhead tick without
    // reallocating a whole GPU image each time. `rgba` must be exactly
    // width()*height()*4 bytes; a size mismatch (e.g. a decoder that
    // changed resolution mid-stream) is a real, honest no-op (returns
    // false), not a silent out-of-bounds copy. Real staging-buffer
    // upload, same SHADER_READ_ONLY_OPTIMAL -> TRANSFER_DST_OPTIMAL ->
    // back round trip uploadPixels() itself already does for the very
    // first upload -- this is that same transition applied to an image
    // that already has real content instead of VK_IMAGE_LAYOUT_UNDEFINED.
    [[nodiscard]] bool updatePixels(const uint8_t* rgba, size_t rgbaBytes, VmaAllocator allocator, VkDevice device,
                                     VkCommandPool cmdPool, VkQueue queue);

    void destroy(VmaAllocator allocator, VkDevice device);

    [[nodiscard]] bool isValid() const { return image_ != VK_NULL_HANDLE; }
    [[nodiscard]] VkImage image() const { return image_; }
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
    // Kronos ("CapCut/DaVinci Hybrid NLE Suite" -- real MP4 video
    // playback): a real, mutable overload -- Texture::updatePixels() is a
    // non-const instance method (it re-uploads into the existing VkImage
    // in place), so a video plane re-fetching its own texture handle
    // every frame needs a non-const pointer, not the const one every
    // read-only sampler (scene.frag's own material bindings) uses.
    [[nodiscard]] Texture* get(uint32_t handle);
    void destroyAll(VmaAllocator allocator, VkDevice device);

private:
    std::vector<Texture> textures_;
};

} // namespace engine::core
