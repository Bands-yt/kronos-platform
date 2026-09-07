#include "cinematic/MediaBin.hpp"

#include <filesystem>

#include "core/AssetMetadata.hpp"
#include "core/Texture.hpp"

namespace engine::cinematic {

bool MediaBin::importAsset(const std::string& path, VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool,
                            VkQueue queue, core::TextureLibrary& textureLibrary, core::Audio& audio,
                            std::string& outError) {
    core::AssetMetadata meta = core::extractAssetMetadata(path);
    if (!meta.succeeded) {
        outError = meta.kind == core::AssetKind::Unknown
                       ? "Video import isn't supported yet -- this engine has no video decoder vendored. "
                         "Only images (png/jpg/bmp/tga/gif) and audio (wav/mp3/flac/ogg) can be imported."
                       : meta.error;
        return false;
    }

    MediaAsset asset;
    asset.path = path;
    asset.displayName = std::filesystem::path(path).filename().string();

    if (meta.kind == core::AssetKind::Texture) {
        core::Texture texture = core::Texture::loadFromFile(path, allocator, device, cmdPool, queue, /*srgb=*/true);
        if (!texture.isValid()) {
            outError = "failed to load image";
            return false;
        }
        asset.kind = MediaAssetKind::Image;
        asset.textureHandle = textureLibrary.registerTexture(std::move(texture));
    } else if (meta.kind == core::AssetKind::Audio) {
        core::SoundHandle handle = audio.loadSound(path);
        if (handle == core::kInvalidSoundHandle) {
            outError = "failed to load audio";
            return false;
        }
        asset.kind = MediaAssetKind::Audio;
        asset.soundHandle = handle;
        asset.durationSeconds = meta.durationSeconds;
    } else {
        outError = "unsupported media type";
        return false;
    }

    assets_.push_back(std::move(asset));
    return true;
}

} // namespace engine::cinematic
