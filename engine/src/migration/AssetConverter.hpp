#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "core/Mesh.hpp"
#include "migration/AssetModeration.hpp"

namespace engine::migration {

enum class AssetKind { Mesh, Texture, Animation, Sound, Material, Unknown };

struct ConversionResult {
    bool succeeded = false;
    std::string outputPath;  // local asset path once converted -- empty on failure
    std::string message;     // human-readable status, always set (success note or failure reason)
    // Set when the moderation filter refused the source and a generated
    // placeholder was produced instead. succeeded stays TRUE in that case:
    // the import genuinely yielded usable data, it is simply not the
    // proprietary data that was asked for, and treating it as a failure
    // would stop a whole place importing over one blocked decal.
    bool usedPlaceholder = false;
    ModerationReasonCode moderationCode = ModerationReasonCode::Allowed;
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
// The generated stand-in geometry a blocked mesh reference becomes.
[[nodiscard]] MeshConversionData unitCubePlaceholder();

class AssetConverter {
public:
    AssetConverter() = default;
    // Installs the moderation gate. Without one, conversion runs
    // unfiltered -- correct for a unit test converting a local .obj, which
    // is why this is explicit rather than defaulted on.
    explicit AssetConverter(const AssetModerationFilter* moderation) : moderation_(moderation) {}

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

    // --- multimodal inspection ---------------------------------------------
    // Encodes decoded RGBA8 pixels as a base64 PNG payload for a vision
    // endpoint. Re-encoded to PNG rather than shipped as raw RGBA because
    // a vision API needs a real image container, and because a 4K raw
    // buffer is ~33 MB, which is not a sensible request body.
    //
    // `maxDimension` downscales first. Moderation does not need full
    // resolution, and sending 4K images makes every check slow and
    // expensive without changing what the classifier sees.
    [[nodiscard]] static bool encodeTextureForInspection(const TextureConversionData& texture,
                                                          std::string& outBase64Png, int maxDimension = 512);
    // The PNG bytes behind the above, exposed so a test can verify the
    // container is real rather than only that some base64 came back.
    [[nodiscard]] static bool encodeRgbaAsPng(const std::vector<uint8_t>& rgba, int width, int height,
                                               std::vector<uint8_t>& outPng);

    // Findings accumulated across every convert*() call on this instance,
    // so one import produces one audit rather than a scatter of messages.
    [[nodiscard]] const ModerationReport& moderationReport() const { return moderationReport_; }
    void resetModerationReport() { moderationReport_ = ModerationReport{}; }

private:
    [[nodiscard]] static ConversionResult unsupported(const std::string& sourcePath, const char* kindName,
                                                       const char* reason);
    // Runs the gate for `sourcePath`. Returns true when conversion must
    // NOT proceed, having already recorded the finding and filled
    // `outResult` with the placeholder outcome.
    [[nodiscard]] bool refusedByModeration(const std::string& sourcePath, ConversionResult& outResult);

    const AssetModerationFilter* moderation_ = nullptr;
    ModerationReport moderationReport_;
};

} // namespace engine::migration
