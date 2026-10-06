#pragma once

#include <functional>
#include <string>

#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/AssetCache.hpp"
#include "core/AssetMetadata.hpp"
#include "core/Mesh.hpp"
#include "core/ResourceManager.hpp"
#include "studio/IStudioPlugin.hpp"

namespace engine::studio::plugins {

// A real model importer -- Load dispatches on extension to the matching
// real loader (core::loadObj()/ObjLoader.hpp, core::loadFbx()/FbxLoader.hpp,
// or core::loadGltf()/GltfLoader.hpp for .gltf/.glb), uploads the result to
// the GPU (core::Mesh::uploadFromHost(), the same call every procedural
// generator uses), and registers it into Studio's shared MeshLibrary. Deliberately
// does NOT own a second offscreen render target/camera for an isolated
// preview: the loaded mesh is spawned as a real entity (named
// "ModelPreview", reused/updated on subsequent loads rather than
// duplicated) in Studio's actual ECS, so it shows up in the *real*,
// already-existing Viewport panel -- select it there to orbit/inspect it
// with the same camera controls every other entity gets, rather than a
// second bespoke preview viewport duplicating that machinery.
// core::AssetCache avoids re-uploading the same unchanged file to the GPU
// on repeated Load clicks (e.g. re-entering the same path, or a future
// hot-reload watch) -- see its header for exactly what "unchanged" means.
class ModelImporterPlugin final : public IStudioPlugin {
public:
    ModelImporterPlugin(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                         core::MeshLibrary& meshLibrary);

    [[nodiscard]] const char* name() const override { return "Model Importer"; }
    [[nodiscard]] const char* category() const override { return "Assets"; }

    void drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) override;

    void update(float dt, core::ECS& ecs, core::EntityId selected,
                const std::vector<core::EntityId>& selectedEntities) override;

    // Opens a native file dialog without blocking the frame. With
    // `importWhenPicked` the chosen model is added to the scene as a new
    // entity on the next update() and reported through the import callback.
    void browseForFile(bool importWhenPicked = true);

    // With a resource manager the file decodes on a worker and the entity
    // appears (and onImported fires) from update() once it is on the GPU;
    // this returns kNullEntity in that case.
    core::EntityId importModel(core::ECS& ecs, const std::string& path, bool asNewEntity);
    void setResources(core::ResourceManager* resources) { resources_ = resources; }
    [[nodiscard]] bool importInProgress() const { return !pending_.empty(); }

    void setOnImported(std::function<void(core::EntityId, const std::string&)> callback) {
        onImported_ = std::move(callback);
    }

private:
    VmaAllocator allocator_;
    VkDevice device_;
    VkCommandPool cmdPool_;
    VkQueue queue_;
    core::MeshLibrary* meshLibrary_;
    core::AssetCache<uint32_t> meshCache_;
    core::ResourceManager* resources_ = nullptr;
    struct PendingImport {
        core::ResourceHandle resource;
        bool asNewEntity = false;
    };
    std::vector<PendingImport> pending_;
    [[nodiscard]] bool asyncImports() const;

    char pathBuffer_[256] = "";
    std::string statusMessage_;
    core::AssetMetadata lastMetadata_;
    bool hasPreview_ = false;
    std::string pendingImport_;
    std::function<void(core::EntityId, const std::string&)> onImported_;

    core::EntityId placeEntity(core::ECS& ecs, const std::string& path, uint32_t meshHandle, bool asNewEntity);
    void finishPendingImport(core::ECS& ecs);
};

} // namespace engine::studio::plugins
