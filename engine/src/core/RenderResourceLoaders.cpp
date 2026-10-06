#include "core/RenderResourceLoaders.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>

#include <stb_image.h>

#include "core/Audio.hpp"
#include "core/FbxLoader.hpp"
#include "core/GltfLoader.hpp"
#include "core/ObjLoader.hpp"
#include "core/Renderer.hpp"
#include "core/Texture.hpp"

namespace engine::core {

namespace {

std::string lowerExtension(const std::string& path) {
    std::string extension = std::filesystem::path(path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension;
}

template <typename Result>
std::unique_ptr<ResourcePayload> takeMesh(Result&& result, std::string& error) {
    if (!result.succeeded) {
        error = result.error.empty() ? "mesh import failed" : result.error;
        return nullptr;
    }
    if (result.vertices.empty() || result.indices.empty()) {
        error = "mesh has no triangles";
        return nullptr;
    }
    auto mesh = std::make_unique<DecodedMesh>();
    mesh->vertices = std::move(result.vertices);
    mesh->indices = std::move(result.indices);
    return mesh;
}

ResourceLoader textureLoader(const RenderResourceContext& context, bool srgb) {
    ResourceLoader loader;
    loader.reserve = [context] { return context.textures->registerTexture(Texture{}); };
    loader.decode = decodeImageFile;
    loader.commit = [context, srgb](ResourcePayload& payload, uint32_t handle, bool, std::string& error) {
        auto& image = static_cast<DecodedImage&>(payload);
        Renderer& renderer = *context.renderer;
        Texture texture = Texture::createFromPixels(image.rgba.data(), image.width, image.height, srgb,
                                                    renderer.allocator(), renderer.device(), renderer.commandPool(),
                                                    renderer.graphicsQueue());
        if (!texture.isValid()) {
            error = "GPU upload failed";
            return false;
        }
        if (const Texture* old = context.textures->get(handle); old != nullptr && old->isValid()) renderer.waitIdle();
        context.textures->replaceTexture(handle, std::move(texture), renderer.allocator(), renderer.device());
        renderer.refreshTextureDescriptors(handle, *context.textures);
        return true;
    };
    loader.release = [context](uint32_t handle) {
        Renderer& renderer = *context.renderer;
        renderer.waitIdle();
        context.textures->destroyTexture(handle, renderer.allocator(), renderer.device());
        renderer.refreshTextureDescriptors(handle, *context.textures);
    };
    return loader;
}

} // namespace

std::unique_ptr<ResourcePayload> decodeMeshFile(const std::string& path, std::string& error) {
    const std::string extension = lowerExtension(path);
    if (extension == ".obj") return takeMesh(loadObj(path), error);
    if (extension == ".gltf" || extension == ".glb") return takeMesh(loadGltf(path), error);
    if (extension == ".fbx") return takeMesh(loadFbx(path), error);
    error = "unsupported mesh format \"" + extension + "\" (use .obj, .gltf, .glb or .fbx)";
    return nullptr;
}

std::unique_ptr<ResourcePayload> decodeImageFile(const std::string& path, std::string& error) {
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (pixels == nullptr) {
        const char* reason = stbi_failure_reason();
        error = reason ? reason : "image decode failed";
        return nullptr;
    }
    auto image = std::make_unique<DecodedImage>();
    image->width = width;
    image->height = height;
    image->rgba.assign(pixels, pixels + static_cast<size_t>(width) * height * 4);
    stbi_image_free(pixels);
    return image;
}

std::unique_ptr<ResourcePayload> decodeAudioResource(const std::string& path, std::string& error) {
    auto audio = std::make_unique<DecodedAudio>();
    if (!decodeAudioFileToFloat(path, audio->interleaved, audio->channels, audio->sampleRate, &error)) return nullptr;
    return audio;
}

void installRenderResourceLoaders(ResourceManager& resources, const RenderResourceContext& context) {
    if (context.renderer != nullptr && context.meshes != nullptr) {
        ResourceLoader mesh;
        mesh.reserve = [context] { return context.meshes->registerMesh(Mesh{}); };
        mesh.decode = decodeMeshFile;
        mesh.commit = [context](ResourcePayload& payload, uint32_t handle, bool reloading, std::string& error) {
            auto& decoded = static_cast<DecodedMesh&>(payload);
            Renderer& renderer = *context.renderer;
            Mesh uploaded;
            if (!uploaded.uploadFromHost(renderer.allocator(), renderer.device(), renderer.commandPool(),
                                         renderer.graphicsQueue(), decoded.vertices, decoded.indices)) {
                error = "GPU upload failed";
                return false;
            }
            if (reloading) renderer.waitIdle();
            context.meshes->replaceMesh(handle, std::move(uploaded), renderer.allocator());
            return true;
        };
        mesh.release = [context](uint32_t handle) {
            context.renderer->deferDestroy(
                [context, handle] { context.meshes->destroyMesh(handle, context.renderer->allocator()); });
        };
        resources.setLoader(ResourceKind::Mesh, std::move(mesh));
    }

    if (context.renderer != nullptr && context.textures != nullptr) {
        resources.setLoader(ResourceKind::Texture, textureLoader(context, true));
        resources.setLoader(ResourceKind::DataTexture, textureLoader(context, false));
    }

    if (context.audio != nullptr) {
        ResourceLoader audio;
        audio.reserve = [context] { return context.audio->reserveSound(); };
        audio.decode = decodeAudioResource;
        audio.commit = [context](ResourcePayload& payload, uint32_t handle, bool, std::string& error) {
            auto& decoded = static_cast<DecodedAudio&>(payload);
            if (!context.audio->isInitialized()) {
                error = "no audio device";
                return false;
            }
            const uint64_t frames = decoded.interleaved.size() / decoded.channels;
            if (!context.audio->setSoundPcm(handle, decoded.interleaved.data(), frames, decoded.channels,
                                            decoded.sampleRate)) {
                error = "could not create the sound";
                return false;
            }
            return true;
        };
        audio.release = [context](uint32_t handle) { context.audio->unloadSound(handle); };
        resources.setLoader(ResourceKind::Audio, std::move(audio));
    }
}

} // namespace engine::core
