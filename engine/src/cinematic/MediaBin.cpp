#include "cinematic/MediaBin.hpp"

#include <filesystem>

#include "core/AssetMetadata.hpp"
#include "core/Texture.hpp"
#include "core/VideoDecoder.hpp"

namespace engine::cinematic {

bool MediaBin::importAsset(const std::string& path, VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool,
                            VkQueue queue, core::TextureLibrary& textureLibrary, core::Audio& audio,
                            std::string& outError) {
    core::AssetMetadata meta = core::extractAssetMetadata(path);
    if (!meta.succeeded) {
        outError = meta.kind == core::AssetKind::Unknown
                       ? "Unrecognized file type. Images (png/jpg/bmp/tga/gif), audio (wav/mp3/flac/ogg), and video "
                         "(mp4/mov/mkv/webm) can be imported."
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
    } else if (meta.kind == core::AssetKind::Video) {
        // Kronos ("CapCut/DaVinci Hybrid NLE Suite" -- real MP4 video
        // decoding): a real first-frame thumbnail, same real GPU-upload
        // path (Texture::createFromPixels) an image import's own texture
        // already uses -- this Bin only needs a still to show in its own
        // list UI. Live, playhead-driven playback is a separate,
        // per-clip core::VideoPlaneComponent with its own independent
        // core::VideoDecoder (see this header's own class comment).
        core::VideoDecoder decoder;
        std::string decodeError;
        if (!decoder.open(path, decodeError)) {
            outError = "failed to open video: " + decodeError;
            return false;
        }
        std::vector<uint8_t> firstFrame;
        if (!decoder.decodeFrameAt(0.0, firstFrame, decodeError)) {
            outError = "failed to decode video's first frame: " + decodeError;
            return false;
        }
        core::Texture thumbnail = core::Texture::createFromPixels(firstFrame.data(), decoder.width(), decoder.height(),
                                                                    /*srgb=*/true, allocator, device, cmdPool, queue);
        if (!thumbnail.isValid()) {
            outError = "failed to create video thumbnail texture";
            return false;
        }
        asset.kind = MediaAssetKind::Video;
        asset.textureHandle = textureLibrary.registerTexture(std::move(thumbnail));
        asset.durationSeconds = decoder.durationSeconds();
        asset.videoWidth = decoder.width();
        asset.videoHeight = decoder.height();
    } else {
        outError = "unsupported media type";
        return false;
    }

    assets_.push_back(std::move(asset));
    return true;
}

} // namespace engine::cinematic
