#include "migration/AssetConverter.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <functional>
#include <unordered_map>

#include <stb_image.h>
#include <zlib.h>

#include "safety/GeminiModerationClient.hpp"

#include "core/ObjLoader.hpp"
#include "migration/PropertyDecoder.hpp"
#include "migration/RbxlxParser.hpp"

namespace engine::migration {

namespace {
std::string lowerExtension(const std::string& path) {
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return "";
    std::string ext = path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    return ext;
}

bool fileExists(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    return file.good();
}

std::string readWholeFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}
} // namespace

AssetKind AssetConverter::detectKind(const std::string& sourcePath) {
    std::string ext = lowerExtension(sourcePath);
    if (ext == "fbx" || ext == "obj" || ext == "gltf" || ext == "glb" || ext == "mesh") return AssetKind::Mesh;
    if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "tga" || ext == "dds" || ext == "ktx2") return AssetKind::Texture;
    if (ext == "fbxanim" || ext == "anim" || ext == "keyframesequence" || ext == "rbxmx") return AssetKind::Animation;
    if (ext == "wav" || ext == "mp3" || ext == "ogg" || ext == "flac") return AssetKind::Sound;
    if (ext == "mtl" || ext == "material") return AssetKind::Material;
    return AssetKind::Unknown;
}

ConversionResult AssetConverter::unsupported(const std::string& sourcePath, const char* kindName,
                                              const char* reason) {
    ConversionResult result;
    result.succeeded = false;
    result.message = std::string(kindName) + " conversion for \"" + sourcePath + "\": " + reason;
    return result;
}


namespace {
// A unit cube standing in for a blocked mesh. Generated rather than loaded
// so the placeholder itself can never be an infringement, and so a blocked
// mesh still occupies the right rough volume in the scene instead of
// leaving a hole the author has to hunt for.
MeshConversionData unitCubePlaceholderData() {
    MeshConversionData data;
    const glm::vec3 corners[8] = {{-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, -0.5f},
                                   {-0.5f, 0.5f, -0.5f},  {-0.5f, -0.5f, 0.5f}, {0.5f, -0.5f, 0.5f},
                                   {0.5f, 0.5f, 0.5f},    {-0.5f, 0.5f, 0.5f}};
    static const int faces[6][4] = {{0, 1, 2, 3}, {5, 4, 7, 6}, {4, 0, 3, 7},
                                     {1, 5, 6, 2}, {3, 2, 6, 7}, {4, 5, 1, 0}};
    static const glm::vec3 normals[6] = {{0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    for (int f = 0; f < 6; ++f) {
        const auto base = static_cast<uint32_t>(data.vertices.size());
        for (int v = 0; v < 4; ++v) {
            core::Vertex vertex{};
            vertex.position = corners[faces[f][v]];
            vertex.normal = normals[f];
            vertex.uv = glm::vec2(v == 1 || v == 2 ? 1.0f : 0.0f, v >= 2 ? 1.0f : 0.0f);
            data.vertices.push_back(vertex);
        }
        for (uint32_t index : {0u, 1u, 2u, 0u, 2u, 3u}) data.indices.push_back(base + index);
    }
    data.boundsMin = glm::vec3(-0.5f);
    data.boundsMax = glm::vec3(0.5f);
    return data;
}
} // namespace

MeshConversionData unitCubePlaceholder() { return unitCubePlaceholderData(); }


bool AssetConverter::refusedByModeration(const std::string& sourcePath, ConversionResult& outResult) {
    if (moderation_ == nullptr) return false;

    const ModerationFinding finding = moderation_->evaluateReference(sourcePath, sourcePath);
    if (finding.code == ModerationReasonCode::Allowed) {
        ++moderationReport_.allowedCount;
        return false;
    }

    moderationReport_.findings.push_back(finding);
    ++moderationReport_.blockedCount;

    // Succeeded, with a placeholder -- see ConversionResult::usedPlaceholder.
    outResult = ConversionResult{};
    outResult.succeeded = true;
    outResult.usedPlaceholder = true;
    outResult.moderationCode = finding.code;
    outResult.outputPath.clear();
    outResult.message = std::string(moderationReasonCodeName(finding.code)) + ": " + finding.detail;
    return true;
}

// --- mesh -------------------------------------------------------------------

ConversionResult AssetConverter::convertMesh(const std::string& sourcePath, MeshConversionData& outData) {
    outData = MeshConversionData{};

    // The gate runs BEFORE the extension check and before any file access,
    // which is the whole point: a blocked rbxassetid:// reference must
    // never be resolved, not resolved-then-discarded.
    ConversionResult moderated;
    if (refusedByModeration(sourcePath, moderated)) {
        outData = unitCubePlaceholder();
        return moderated;
    }

    const std::string ext = lowerExtension(sourcePath);

    if (ext != "obj") {
        // Stated rather than half-attempted. Roblox's .mesh is a separate
        // binary container and .fbx/.gltf need real importers; pretending
        // to convert them would produce an empty mesh that looks like a
        // rendering bug rather than an unsupported format.
        return unsupported(sourcePath, "Mesh",
                            ext.empty() ? "no file extension, so the format cannot be determined"
                                        : "only Wavefront .obj is supported today (.fbx/.gltf/.mesh need importers "
                                          "that do not exist yet)");
    }
    if (!fileExists(sourcePath)) return unsupported(sourcePath, "Mesh", "file not found");

    core::ObjLoadResult loaded = core::loadObj(sourcePath);
    if (!loaded.succeeded) {
        return unsupported(sourcePath, "Mesh", loaded.error.empty() ? "the .obj parser rejected this file"
                                                                     : loaded.error.c_str());
    }
    if (loaded.vertices.empty() || loaded.indices.empty()) {
        return unsupported(sourcePath, "Mesh", "parsed successfully but contains no geometry");
    }

    outData.vertices = std::move(loaded.vertices);
    outData.indices = std::move(loaded.indices);

    outData.boundsMin = outData.vertices.front().position;
    outData.boundsMax = outData.vertices.front().position;
    for (const core::Vertex& vertex : outData.vertices) {
        outData.boundsMin = glm::min(outData.boundsMin, vertex.position);
        outData.boundsMax = glm::max(outData.boundsMax, vertex.position);
    }

    ConversionResult result;
    result.succeeded = true;
    result.outputPath = sourcePath;
    result.message = "converted " + std::to_string(outData.vertices.size()) + " vertices / " +
                      std::to_string(outData.indices.size() / 3) + " triangles";
    return result;
}

// --- texture ----------------------------------------------------------------

ConversionResult AssetConverter::convertTexture(const std::string& sourcePath, TextureConversionData& outData) {
    outData = TextureConversionData{};

    ConversionResult moderated;
    if (refusedByModeration(sourcePath, moderated)) {
        outData.rgba = AssetModerationFilter::generateCheckerboard();
        outData.width = 64;
        outData.height = 64;
        outData.srgb = true;
        return moderated;
    }

    if (!fileExists(sourcePath)) return unsupported(sourcePath, "Texture", "file not found");

    int width = 0;
    int height = 0;
    int sourceChannels = 0;
    // Forced to 4 channels for the same reason Texture::loadFromFile does:
    // every Kronos sampler expects RGBA8, so normalising here means a
    // converted texture is always uploadable without a second conversion.
    stbi_uc* pixels = stbi_load(sourcePath.c_str(), &width, &height, &sourceChannels, STBI_rgb_alpha);
    if (pixels == nullptr) {
        const char* reason = stbi_failure_reason();
        return unsupported(sourcePath, "Texture", reason != nullptr ? reason : "the image decoder rejected this file");
    }
    if (width <= 0 || height <= 0) {
        stbi_image_free(pixels);
        return unsupported(sourcePath, "Texture", "decoded to a zero-sized image");
    }

    const size_t byteCount = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
    outData.rgba.assign(pixels, pixels + byteCount);
    outData.width = width;
    outData.height = height;
    // Colour maps are authored in sRGB; the caller overrides this for
    // normal/metallic/roughness maps, which are linear data and would be
    // visibly wrong if gamma-decoded.
    outData.srgb = true;
    stbi_image_free(pixels);

    ConversionResult result;
    result.succeeded = true;
    result.outputPath = sourcePath;
    result.message = "decoded " + std::to_string(width) + "x" + std::to_string(height) + " RGBA8 (" +
                      std::to_string(sourceChannels) + " source channels)";
    return result;
}

// --- material ---------------------------------------------------------------

MaterialConversionData AssetConverter::materialFromProperties(
    const std::unordered_map<std::string, std::string>& properties) {
    MaterialConversionData material;

    const glm::vec3 color = hasProperty(properties, "Color3uint8")
                                 ? decodeColor3(properties, "Color3uint8", glm::vec3(0.64f))
                                 : decodeColor3(properties, "Color", glm::vec3(0.64f));
    const float transparency = std::clamp(decodeFloat(properties, "Transparency", 0.0f), 0.0f, 1.0f);
    material.baseColor = glm::vec4(color, 1.0f - transparency);

    // Roblox's Reflectance is a single "how mirror-like" scalar rather than
    // a metalness value, but it is the only PBR-adjacent signal a legacy
    // part carries. Driving both roughness and a little metallic from it
    // preserves the author's intent better than dropping it.
    const float reflectance = std::clamp(decodeFloat(properties, "Reflectance", 0.0f), 0.0f, 1.0f);
    material.roughness = std::clamp(0.85f - reflectance * 0.7f, 0.05f, 1.0f);
    material.metallic = std::clamp(reflectance * 0.6f, 0.0f, 1.0f);

    // MaterialVariant/SurfaceAppearance texture slots, when present.
    material.albedoPath = decodeString(properties, "ColorMap", decodeString(properties, "Texture"));
    material.normalPath = decodeString(properties, "NormalMap");
    material.metallicPath = decodeString(properties, "MetalnessMap");
    material.roughnessPath = decodeString(properties, "RoughnessMap");
    return material;
}

ConversionResult AssetConverter::convertMaterial(const std::string& sourcePath, MaterialConversionData& outData) {
    outData = MaterialConversionData{};

    ConversionResult moderated;
    if (refusedByModeration(sourcePath, moderated)) {
        // Neutral grey, and every texture slot cleared so no blocked map
        // survives into the material.
        outData = MaterialConversionData{};
        return moderated;
    }

    if (!fileExists(sourcePath)) return unsupported(sourcePath, "Material", "file not found");

    // A material file here is an .rbxmx fragment describing a Part or a
    // MaterialVariant -- the same XML the place parser already reads.
    const std::string source = readWholeFile(sourcePath);
    auto document = RbxlxParser::parse(source);
    if (!document.has_value()) {
        return unsupported(sourcePath, "Material", "could not be parsed as Roblox XML");
    }

    // First Item with any material-ish property wins; a material file
    // describing nothing is a failure, not an all-defaults material that
    // would silently repaint whatever it is applied to.
    bool found = false;
    std::function<void(const XmlNode&)> visit = [&](const XmlNode& node) {
        if (found) return;
        if (node.tag == "Item") {
            PropertyMap props;
            for (const auto& child : node.children) {
                if (child.tag != "Properties") continue;
                for (const auto& prop : child.children) {
                    const auto* nameAttr = prop.attribute("name");
                    if (nameAttr == nullptr) continue;
                    props[*nameAttr] = prop.text;
                    for (const auto& field : prop.children) {
                        props[*nameAttr + "." + field.tag] = field.text;
                    }
                    props["@type." + *nameAttr] = prop.tag;
                }
            }
            for (const char* key : {"Color", "Color3uint8", "Reflectance", "Transparency", "ColorMap", "NormalMap"}) {
                if (hasProperty(props, key)) {
                    outData = materialFromProperties(props);
                    found = true;
                    return;
                }
            }
        }
        for (const auto& child : node.children) visit(child);
    };
    visit(*document);

    if (!found) return unsupported(sourcePath, "Material", "no material properties found in the document");

    ConversionResult result;
    result.succeeded = true;
    result.outputPath = sourcePath;
    result.message = "mapped to PBR (metallic " + std::to_string(outData.metallic) + ", roughness " +
                      std::to_string(outData.roughness) + ")";
    return result;
}

// --- animation --------------------------------------------------------------

ConversionResult AssetConverter::convertAnimation(const std::string& sourcePath, AnimationConversionData& outData) {
    outData = AnimationConversionData{};

    ConversionResult moderated;
    if (refusedByModeration(sourcePath, moderated)) return moderated;

    if (!fileExists(sourcePath)) return unsupported(sourcePath, "Animation", "file not found");

    const std::string source = readWholeFile(sourcePath);
    auto document = RbxlxParser::parse(source);
    if (!document.has_value()) {
        return unsupported(sourcePath, "Animation", "could not be parsed as Roblox XML (KeyframeSequence)");
    }

    // A KeyframeSequence is <KeyframeSequence> -> <Keyframe Time=..> ->
    // <Pose Name=.. CFrame=..>. Each Pose contributes position and
    // rotation samples for one joint at that keyframe's time.
    std::unordered_map<std::string, AnimationChannelData> channels;
    float duration = 0.0f;

    std::function<void(const XmlNode&, float)> visit = [&](const XmlNode& node, float inheritedTime) {
        float time = inheritedTime;
        PropertyMap props;
        for (const auto& child : node.children) {
            if (child.tag != "Properties") continue;
            for (const auto& prop : child.children) {
                const auto* nameAttr = prop.attribute("name");
                if (nameAttr == nullptr) continue;
                props[*nameAttr] = prop.text;
                for (const auto& field : prop.children) {
                    props[*nameAttr + "." + field.tag] = field.text;
                }
                props["@type." + *nameAttr] = prop.tag;
            }
        }

        const std::string className = node.attribute("class") != nullptr ? *node.attribute("class") : std::string{};
        if (className == "KeyframeSequence") {
            outData.name = decodeString(props, "Name", "ImportedAnimation");
            outData.looping = decodeBool(props, "Loop", false);
        } else if (className == "Keyframe") {
            time = decodeFloat(props, "Time", inheritedTime);
            duration = std::max(duration, time);
        } else if (className == "Pose") {
            const std::string joint = decodeString(props, "Name");
            if (!joint.empty() && hasProperty(props, "CFrame")) {
                const glm::vec3 position = decodeCFramePosition(props, "CFrame");
                const glm::quat rotation = decodeCFrameRotation(props, "CFrame");
                const std::pair<const char*, float> samples[] = {
                    {"position.x", position.x}, {"position.y", position.y}, {"position.z", position.z},
                    {"rotation.x", rotation.x}, {"rotation.y", rotation.y}, {"rotation.z", rotation.z},
                    {"rotation.w", rotation.w},
                };
                for (const auto& [channelName, value] : samples) {
                    const std::string key = joint + "/" + channelName;
                    AnimationChannelData& channel = channels[key];
                    if (channel.jointName.empty()) {
                        channel.jointName = joint;
                        channel.channelName = channelName;
                    }
                    channel.keys.emplace_back(time, value);
                }
            }
        }

        for (const auto& child : node.children) visit(child, time);
    };
    visit(*document, 0.0f);

    if (channels.empty()) {
        return unsupported(sourcePath, "Animation", "no Pose keyframes found in the document");
    }

    outData.durationSeconds = duration;
    outData.channels.reserve(channels.size());
    for (auto& [key, channel] : channels) {
        // Sorted by time: the sequencer's samplers assume sorted keys and
        // say so rather than re-sorting on every evaluation.
        std::sort(channel.keys.begin(), channel.keys.end(),
                   [](const auto& a, const auto& b) { return a.first < b.first; });
        outData.channels.push_back(std::move(channel));
    }
    std::sort(outData.channels.begin(), outData.channels.end(), [](const auto& a, const auto& b) {
        return a.jointName == b.jointName ? a.channelName < b.channelName : a.jointName < b.jointName;
    });

    ConversionResult result;
    result.succeeded = true;
    result.outputPath = sourcePath;
    result.message = "converted " + std::to_string(outData.channels.size()) + " channels over " +
                      std::to_string(outData.durationSeconds) + "s";
    return result;
}

// --- sound ------------------------------------------------------------------

ConversionResult AssetConverter::convertSound(const std::string& sourcePath, const std::string& /*outputDir*/) {
    ConversionResult moderated;
    if (refusedByModeration(sourcePath, moderated)) return moderated;

    if (!fileExists(sourcePath)) return unsupported(sourcePath, "Sound", "file not found");
    if (detectKind(sourcePath) != AssetKind::Sound) {
        return unsupported(sourcePath, "Sound", "not a recognised audio format");
    }
    // Passthrough by design: the audio backend (miniaudio) already decodes
    // wav/mp3/ogg/flac, so re-encoding would cost quality for nothing.
    ConversionResult result;
    result.succeeded = true;
    result.outputPath = sourcePath;
    result.message = "passthrough -- the audio backend decodes this format natively";
    return result;
}

ConversionResult AssetConverter::convert(const std::string& sourcePath, const std::string& outputDir) {
    switch (detectKind(sourcePath)) {
        case AssetKind::Mesh: {
            MeshConversionData data;
            return convertMesh(sourcePath, data);
        }
        case AssetKind::Texture: {
            TextureConversionData data;
            return convertTexture(sourcePath, data);
        }
        case AssetKind::Material: {
            MaterialConversionData data;
            return convertMaterial(sourcePath, data);
        }
        case AssetKind::Animation: {
            AnimationConversionData data;
            return convertAnimation(sourcePath, data);
        }
        case AssetKind::Sound: return convertSound(sourcePath, outputDir);
        case AssetKind::Unknown: break;
    }
    return unsupported(sourcePath, "Asset", "unrecognised file extension");
}


namespace {

void appendBigEndian32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}

// PNG chunks are length + type + data + CRC32 over (type + data). zlib
// already provides the exact CRC32 PNG specifies, so this does not carry
// its own table.
void appendPngChunk(std::vector<uint8_t>& out, const char type[4], const std::vector<uint8_t>& data) {
    appendBigEndian32(out, static_cast<uint32_t>(data.size()));
    const size_t crcStart = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    const uLong crc = crc32(crc32(0L, Z_NULL, 0), out.data() + crcStart, static_cast<uInt>(out.size() - crcStart));
    appendBigEndian32(out, static_cast<uint32_t>(crc));
}

} // namespace

bool AssetConverter::encodeRgbaAsPng(const std::vector<uint8_t>& rgba, int width, int height,
                                      std::vector<uint8_t>& outPng) {
    outPng.clear();
    if (width <= 0 || height <= 0) return false;
    const size_t expected = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
    if (rgba.size() < expected) return false;

    // Raw scanlines, each prefixed with PNG's per-row filter byte. Filter
    // 0 (None) is used throughout: filtering exists to help compression,
    // and this image is about to be classified, not shipped, so the
    // simplicity is worth more than the bytes.
    std::vector<uint8_t> raw;
    raw.reserve(expected + static_cast<size_t>(height));
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);
        const size_t rowStart = static_cast<size_t>(y) * static_cast<size_t>(width) * 4u;
        raw.insert(raw.end(), rgba.begin() + static_cast<long>(rowStart),
                    rgba.begin() + static_cast<long>(rowStart + static_cast<size_t>(width) * 4u));
    }

    uLongf compressedSize = compressBound(static_cast<uLong>(raw.size()));
    std::vector<uint8_t> compressed(compressedSize);
    if (compress2(compressed.data(), &compressedSize, raw.data(), static_cast<uLong>(raw.size()),
                   Z_DEFAULT_COMPRESSION) != Z_OK) {
        return false;
    }
    compressed.resize(compressedSize);

    static const uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    outPng.insert(outPng.end(), kSignature, kSignature + 8);

    std::vector<uint8_t> ihdr;
    appendBigEndian32(ihdr, static_cast<uint32_t>(width));
    appendBigEndian32(ihdr, static_cast<uint32_t>(height));
    ihdr.push_back(8); // bit depth
    ihdr.push_back(6); // colour type 6 = RGBA
    ihdr.push_back(0); // deflate
    ihdr.push_back(0); // adaptive filtering
    ihdr.push_back(0); // no interlace
    appendPngChunk(outPng, "IHDR", ihdr);
    appendPngChunk(outPng, "IDAT", compressed);
    appendPngChunk(outPng, "IEND", {});
    return true;
}

bool AssetConverter::encodeTextureForInspection(const TextureConversionData& texture, std::string& outBase64Png,
                                                 int maxDimension) {
    outBase64Png.clear();
    if (texture.width <= 0 || texture.height <= 0) return false;
    const size_t expected = static_cast<size_t>(texture.width) * static_cast<size_t>(texture.height) * 4u;
    if (texture.rgba.size() < expected) return false;

    // Nearest neighbour rather than a box filter: this is fed to a
    // classifier, not a display, and nearest neighbour cannot invent
    // colours that were not in the source -- which matters when the thing
    // being detected is branding.
    int outWidth = texture.width;
    int outHeight = texture.height;
    if (maxDimension > 0 && (outWidth > maxDimension || outHeight > maxDimension)) {
        const double scale = static_cast<double>(maxDimension) / static_cast<double>(std::max(outWidth, outHeight));
        outWidth = std::max(1, static_cast<int>(outWidth * scale));
        outHeight = std::max(1, static_cast<int>(outHeight * scale));
    }

    std::vector<uint8_t> resized(static_cast<size_t>(outWidth) * static_cast<size_t>(outHeight) * 4u);
    for (int y = 0; y < outHeight; ++y) {
        const int sourceY = std::min(texture.height - 1, y * texture.height / outHeight);
        for (int x = 0; x < outWidth; ++x) {
            const int sourceX = std::min(texture.width - 1, x * texture.width / outWidth);
            const size_t source = (static_cast<size_t>(sourceY) * static_cast<size_t>(texture.width) +
                                    static_cast<size_t>(sourceX)) * 4u;
            const size_t destination = (static_cast<size_t>(y) * static_cast<size_t>(outWidth) +
                                         static_cast<size_t>(x)) * 4u;
            for (int c = 0; c < 4; ++c) resized[destination + c] = texture.rgba[source + c];
        }
    }

    std::vector<uint8_t> png;
    if (!encodeRgbaAsPng(resized, outWidth, outHeight, png)) return false;
    outBase64Png = safety::GeminiModerationClient::base64Encode(png);
    return !outBase64Png.empty();
}

} // namespace engine::migration
