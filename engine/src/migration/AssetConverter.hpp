#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "core/Mesh.hpp"

namespace engine::migration {

enum class AssetKind { Mesh, Texture, Animation, Sound, Material, Unknown };

struct ConversionResult {
    bool succeeded = false;
    std::string outputPath;  // local asset path once converted -- empty on failure
    std::string message;     // human-readable status, always set (success note or failure reason)
};

// CPU-side geometry produced by convertMesh(). Kept separate from the GPU
// upload on purpose: the upload needs a VmaAllocator, a VkDevice and a
// command pool, and threading those through the converter would make the
// whole conversion path untestable without a device. uploadConvertedMesh()
// below is the one place that needs them.
struct MeshConversionData {
    std::vector<core::Vertex> vertices;
    std::vector<uint32_t> indices;
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};
};

// Decoded RGBA8 pixels produced by convertTexture().
struct TextureConversionData {
    std::vector<uint8_t> rgba; // width * height * 4
    int width = 0;
    int height = 0;
    bool srgb = true;
};

// PBR material parameters, in the exact shape core::Renderable stores
// them, so applying a converted material is a field copy rather than a
// second translation step.
struct MaterialConversionData {
    glm::vec4 baseColor{0.7f, 0.7f, 0.75f, 1.0f};
    float metallic = 0.05f;
    float roughness = 0.6f;
    float normalIntensity = 1.0f;
    // Source paths, not handles: the converter never touches a
    // TextureLibrary. The caller uploads these and assigns the handles.
    std::string albedoPath;
    std::string normalPath;
    std::string metallicPath;
    std::string roughnessPath;
};

// One converted animation channel, ready to become a
// cinematic::TrackChannel.
struct AnimationChannelData {
    std::string jointName;
    std::string channelName; // "position.x", "rotation.y", ...
    std::vector<std::pair<float, float>> keys; // (timeSeconds, value)
};

struct AnimationConversionData {
    std::string name;
    float durationSeconds = 0.0f;
    bool looping = false;
    std::vector<AnimationChannelData> channels;
};

// Converts imported assets into Kronos's own representations.
//
// Each convert*() reports through the same ConversionResult and writes its
// real output into an out-parameter. Failures are always described --
// "unsupported format", "file not found", a parser's own error -- never a
// silent empty result, because an import that quietly produces nothing is
// indistinguishable from one that worked on an empty file.
//
// NOTE for the moderation pass: every entry point takes the source path
// as its first argument, which is the single choke point where a
// proprietary-URI/hash gate belongs. Nothing here fetches anything over
// the network -- a path is read from disk or it is not read at all.
class AssetConverter {
public:
    [[nodiscard]] static AssetKind detectKind(const std::string& sourcePath);

    // Wavefront .obj today, via core::loadObj. Roblox's own binary mesh
    // format and .fbx/.gltf are reported as unsupported rather than
    // half-parsed.
    [[nodiscard]] ConversionResult convertMesh(const std::string& sourcePath, MeshConversionData& outData);
    // Decodes to RGBA8 through the same stb_image path Texture::loadFromFile
    // uses, so a file that converts here is a file the GPU path accepts.
    [[nodiscard]] ConversionResult convertTexture(const std::string& sourcePath, TextureConversionData& outData);
    // Maps Roblox material properties (Color, Reflectance, Transparency,
    // MaterialVariant texture slots) onto Kronos PBR fields.
    [[nodiscard]] ConversionResult convertMaterial(const std::string& sourcePath, MaterialConversionData& outData);
    // KeyframeSequence XML -> per-joint channels.
    [[nodiscard]] ConversionResult convertAnimation(const std::string& sourcePath, AnimationConversionData& outData);
    // Common formats pass through untouched -- the engine's audio backend
    // already decodes them, so re-encoding would only lose quality.
    [[nodiscard]] ConversionResult convertSound(const std::string& sourcePath, const std::string& outputDir);

    // Kept for callers that only care whether an asset is usable.
    [[nodiscard]] ConversionResult convert(const std::string& sourcePath, const std::string& outputDir);

    // The same mapping convertMaterial() applies, but from an already-
    // parsed property map rather than a file -- which is what an imported
    // .rbxlx Part actually has.
    [[nodiscard]] static MaterialConversionData materialFromProperties(
        const std::unordered_map<std::string, std::string>& properties);

private:
    [[nodiscard]] static ConversionResult unsupported(const std::string& sourcePath, const char* kindName,
                                                       const char* reason);
};

} // namespace engine::migration
