#pragma once

#include "core/Mesh.hpp"
#include "core/ResourceManager.hpp"

namespace engine::core {

class Audio;
class Renderer;
class TextureLibrary;

struct RenderResourceContext {
    Renderer* renderer = nullptr;
    MeshLibrary* meshes = nullptr;
    TextureLibrary* textures = nullptr;
    Audio* audio = nullptr; // optional; audio resources fail to load without it
};

// Installs Mesh (.obj/.gltf/.glb/.fbx), Texture/DataTexture (anything
// stb_image reads) and Audio (wav/mp3/flac/ogg) loaders. Files decode on
// the manager's workers; GPU uploads happen in ResourceManager::update().
// Hot reloads wait for the device to go idle, then swap in place, so every
// Renderable and AudioSource holding the handle picks up the new data.
void installRenderResourceLoaders(ResourceManager& resources, const RenderResourceContext& context);

// Host-only decoders, shared with tests and tools.
struct DecodedMesh final : ResourcePayload {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};
[[nodiscard]] std::unique_ptr<ResourcePayload> decodeMeshFile(const std::string& path, std::string& error);

struct DecodedImage final : ResourcePayload {
    std::vector<uint8_t> rgba;
    int width = 0;
    int height = 0;
};
[[nodiscard]] std::unique_ptr<ResourcePayload> decodeImageFile(const std::string& path, std::string& error);

struct DecodedAudio final : ResourcePayload {
    std::vector<float> interleaved;
    uint32_t channels = 0;
    uint32_t sampleRate = 0;
};
[[nodiscard]] std::unique_ptr<ResourcePayload> decodeAudioResource(const std::string& path, std::string& error);

} // namespace engine::core
