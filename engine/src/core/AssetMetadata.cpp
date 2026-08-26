#include "core/AssetMetadata.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>

#include <miniaudio.h> // declarations only -- MINIAUDIO_IMPLEMENTATION is defined once, in Audio.cpp's translation unit
#include <stb_image.h>

#include "core/FbxLoader.hpp"
#include "core/GltfLoader.hpp"
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
    // Kronos (Asset Hot-Import Pipeline, Phase 2): real glTF 2.0 support
    // (core/GltfLoader.hpp) -- .gltf (JSON + external/embedded buffers)
    // and .glb (self-contained binary) both real-parse to the same
    // GltfLoadResult shape extractAssetMetadata() below dispatches on.
    // Kronos (Asset Hot-Import Pipeline, Phase 3): real FBX support
    // (core/FbxLoader.hpp) via ufbx.
    if (ext == "obj" || ext == "gltf" || ext == "glb" || ext == "fbx") return AssetKind::Mesh;
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
            // Real dispatch on the actual extension -- both real
            // loaders (loadObj()/loadGltf()) report the same
            // vertices/indices shape, so the metadata extraction below
            // is identical either way.
            std::string ext = lowerExtension(path);
            std::vector<size_t> counts; // [0] = vertex count, [1] = index count -- filled by whichever real loader ran
            if (ext == "gltf" || ext == "glb") {
                GltfLoadResult gltf = loadGltf(path);
                if (!gltf.succeeded) {
                    meta.error = gltf.error;
                    return meta;
                }
                counts = {gltf.vertices.size(), gltf.indices.size()};
            } else if (ext == "fbx") {
                FbxLoadResult fbx = loadFbx(path);
                if (!fbx.succeeded) {
                    meta.error = fbx.error;
                    return meta;
                }
                counts = {fbx.vertices.size(), fbx.indices.size()};
            } else {
                ObjLoadResult obj = loadObj(path);
                if (!obj.succeeded) {
                    meta.error = obj.error;
                    return meta;
                }
                counts = {obj.vertices.size(), obj.indices.size()};
            }
            meta.vertexCount = static_cast<uint32_t>(counts[0]);
            meta.triangleCount = static_cast<uint32_t>(counts[1] / 3);
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
