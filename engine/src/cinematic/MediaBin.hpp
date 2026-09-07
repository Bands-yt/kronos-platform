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

enum class MediaAssetKind : uint8_t { Image, Audio, Video };

struct MediaAsset {
    std::string path;
    std::string displayName; // filename only, for the bin's own list UI
    MediaAssetKind kind = MediaAssetKind::Image;
    double durationSeconds = 0.0; // 0 for a still image
    uint32_t textureHandle = ~0u; // Image/Video (thumbnail) -- matches core::TextureLibrary's own kInvalidHandle value
    core::SoundHandle soundHandle = core::kInvalidSoundHandle; // Audio only
    int videoWidth = 0;  // Video only
    int videoHeight = 0; // Video only
};

// A creator's imported-media pool for the NLE timeline -- the "Media
// Bin" a clip's assetPath (ClipTimeline.hpp) is expected to name an
// entry of.
//
// Real dispatch on core::detectAssetKind()/extractAssetMetadata(): a
// still image decodes through the same core::Texture::loadFromFile()
// path every other Studio texture slot uses; audio decodes through
// core::Audio::loadSound() (miniaudio's own built-in WAV/MP3/FLAC/OGG
// decoders -- no separate vendoring needed); video (.mp4/.mov/.mkv/
// .webm) decodes its real first frame via core::VideoDecoder (libav) as
// a real thumbnail texture, same as a still image's own texture slot --
// live playback during timeline scrubbing/playback is a separate,
// per-clip core::VideoPlaneComponent (see studio::plugins::
// NleTimelinePlugin's own playback driver), not owned by this Bin.
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
