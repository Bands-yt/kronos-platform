#include "core/AssetMetadata.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>

#include <miniaudio.h> // declarations only -- MINIAUDIO_IMPLEMENTATION is defined once, in Audio.cpp's translation unit
#include <stb_image.h>

#include "core/ObjLoader.hpp"

namespace engine::core {

namespace {
std::string lowerExtension(const std::string& path) {
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return "";
    std::string ext = path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}
} // namespace

AssetKind detectAssetKind(const std::string& path) {
    std::string ext = lowerExtension(path);
    if (ext == "obj") return AssetKind::Mesh;
    if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "bmp" || ext == "tga" || ext == "gif") {
        return AssetKind::Texture;
    }
    if (ext == "wav" || ext == "mp3" || ext == "flac" || ext == "ogg") return AssetKind::Audio;
    return AssetKind::Unknown;
}

AssetMetadata extractAssetMetadata(const std::string& path) {
    AssetMetadata meta;
    meta.kind = detectAssetKind(path);

    std::error_code ec;
    uintmax_t fileSize = std::filesystem::file_size(path, ec);
    if (ec) {
        meta.error = "could not stat file: " + ec.message();
        return meta;
    }
    meta.fileSizeBytes = static_cast<uint64_t>(fileSize);

    switch (meta.kind) {
        case AssetKind::Mesh: {
            ObjLoadResult obj = loadObj(path);
            if (!obj.succeeded) {
                meta.error = obj.error;
                return meta;
            }
            meta.vertexCount = static_cast<uint32_t>(obj.vertices.size());
            meta.triangleCount = static_cast<uint32_t>(obj.indices.size() / 3);
            meta.succeeded = true;
            break;
        }
        case AssetKind::Texture: {
            int width = 0, height = 0, sourceChannels = 0;
            if (stbi_info(path.c_str(), &width, &height, &sourceChannels) == 0) {
                const char* reason = stbi_failure_reason();
                meta.error = reason != nullptr ? reason : "stbi_info failed";
                return meta;
            }
            meta.width = width;
            meta.height = height;
            meta.channels = sourceChannels;
            meta.succeeded = true;
            break;
        }
        case AssetKind::Audio: {
            ma_decoder decoder;
            if (ma_decoder_init_file(path.c_str(), nullptr, &decoder) != MA_SUCCESS) {
                meta.error = "could not open audio file (unsupported format or malformed data)";
                return meta;
            }
            ma_uint64 frameCount = 0;
            ma_decoder_get_length_in_pcm_frames(&decoder, &frameCount);
            meta.sampleRate = decoder.outputSampleRate;
            meta.channelCount = decoder.outputChannels;
            meta.durationSeconds =
                meta.sampleRate > 0 ? static_cast<double>(frameCount) / static_cast<double>(meta.sampleRate) : 0.0;
            ma_decoder_uninit(&decoder);
            meta.succeeded = true;
            break;
        }
        case AssetKind::Unknown:
            meta.error = "unrecognized file extension";
            break;
    }
    return meta;
}

} // namespace engine::core
