#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/Audio.hpp"

namespace engine::core {
class TextureLibrary;
}

namespace engine::cinematic {

enum class MediaAssetKind : uint8_t { Image, Audio };

struct MediaAsset {
    std::string path;
    std::string displayName; // filename only, for the bin's own list UI
    MediaAssetKind kind = MediaAssetKind::Image;
    double durationSeconds = 0.0; // 0 for a still image
    uint32_t textureHandle = ~0u; // Image only -- matches core::TextureLibrary's own kInvalidHandle value
    core::SoundHandle soundHandle = core::kInvalidSoundHandle; // Audio only
};

// A creator's imported-media pool for the NLE timeline -- the "Media
// Bin" a clip's assetPath (ClipTimeline.hpp) is expected to name an
// entry of.
//
// Real dispatch on core::detectAssetKind()/extractAssetMetadata(): a
// still image decodes through the same core::Texture::loadFromFile()
// path every other Studio texture slot uses; audio decodes through
// core::Audio::loadSound() (miniaudio's own built-in WAV/MP3/FLAC/OGG
// decoders -- no separate vendoring needed). Video (.mp4 and friends)
// is a real, honest rejection: this engine has no video decoder
// vendored anywhere (see AssetMetadata.cpp's own detectAssetKind(),
// which does not recognize any video extension), so there is nothing
// for this to decode into -- not a stub that silently does nothing.
class MediaBin {
public:
    [[nodiscard]] bool importAsset(const std::string& path, VmaAllocator allocator, VkDevice device,
                                    VkCommandPool cmdPool, VkQueue queue, core::TextureLibrary& textureLibrary,
                                    core::Audio& audio, std::string& outError);

    [[nodiscard]] const std::vector<MediaAsset>& assets() const { return assets_; }

private:
    std::vector<MediaAsset> assets_;
};

} // namespace engine::cinematic
