#include "migration/AssetConverter.hpp"

#include <algorithm>
#include <cctype>

namespace engine::migration {

namespace {
std::string lowerExtension(const std::string& path) {
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return "";
    std::string ext = path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    return ext;
}
}

AssetKind AssetConverter::detectKind(const std::string& sourcePath) {
    std::string ext = lowerExtension(sourcePath);
    if (ext == "fbx" || ext == "obj" || ext == "gltf" || ext == "glb" || ext == "mesh") return AssetKind::Mesh;
    if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "tga" || ext == "dds" || ext == "ktx2") return AssetKind::Texture;
    if (ext == "fbxanim" || ext == "anim" || ext == "keyframesequence") return AssetKind::Animation;
    if (ext == "wav" || ext == "mp3" || ext == "ogg" || ext == "flac") return AssetKind::Sound;
    return AssetKind::Unknown;
}

ConversionResult AssetConverter::notYetImplemented(const std::string& sourcePath, const char* kindName) {
    ConversionResult result;
    result.succeeded = false;
    result.message = std::string(kindName) + " conversion for \"" + sourcePath +
                      "\" is not implemented yet -- see docs/ARCHITECTURE.md §7's asset converter row.";
    return result;
}

ConversionResult AssetConverter::convertMesh(const std::string& sourcePath, const std::string& /*outputDir*/) {
    // TODO: .fbx/.obj/.gltf via a real mesh-import library, plus a direct
    // parser for Roblox's own mesh format (documented by community tools);
    // re-triangulate/re-pack into the engine's runtime mesh format and
    // write it under outputDir.
    return notYetImplemented(sourcePath, "Mesh");
}

ConversionResult AssetConverter::convertTexture(const std::string& sourcePath, const std::string& /*outputDir*/) {
    // TODO: decode source image, re-encode to the platform-appropriate
    // compressed format from docs/ARCHITECTURE.md §8.4 (ASTC/BC7/ETC2),
    // and register the rbxassetid:// -> local-path remapping mentioned in §7.
    return notYetImplemented(sourcePath, "Texture");
}

ConversionResult AssetConverter::convertAnimation(const std::string& sourcePath, const std::string& /*outputDir*/) {
    // TODO: KeyframeSequence -> local animation clip format, preserving
    // R15/R6 joint names against the engine's built-in compatible rigs (§7).
    return notYetImplemented(sourcePath, "Animation");
}

ConversionResult AssetConverter::convertSound(const std::string& sourcePath, const std::string& /*outputDir*/) {
    // Sounds are the one asset kind §7 calls "direct passthrough for
    // common formats" -- still stubbed here rather than implemented,
    // since even a passthrough needs the output-directory/asset-id
    // bookkeeping the other three converters do, and that bookkeeping is
    // itself a TODO (see AssetConverter.hpp).
    return notYetImplemented(sourcePath, "Sound");
}

ConversionResult AssetConverter::convert(const std::string& sourcePath, const std::string& outputDir) {
    switch (detectKind(sourcePath)) {
        case AssetKind::Mesh: return convertMesh(sourcePath, outputDir);
        case AssetKind::Texture: return convertTexture(sourcePath, outputDir);
        case AssetKind::Animation: return convertAnimation(sourcePath, outputDir);
        case AssetKind::Sound: return convertSound(sourcePath, outputDir);
        case AssetKind::Unknown: default: return notYetImplemented(sourcePath, "Unknown-kind");
    }
}

} // namespace engine::migration
