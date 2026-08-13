#pragma once

#include <string>

#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/AssetCache.hpp"
#include "core/AssetMetadata.hpp"
#include "core/Mesh.hpp"
#include "studio/IStudioPlugin.hpp"

namespace engine::studio::plugins {

// A real model importer -- Load reads an actual .obj file
// (core::loadObj(), ObjLoader.hpp) off disk, uploads it to the GPU
// (core::Mesh::uploadFromHost(), the same call every procedural generator
// uses), and registers it into Studio's shared MeshLibrary. Deliberately
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

private:
    VmaAllocator allocator_;
    VkDevice device_;
    VkCommandPool cmdPool_;
    VkQueue queue_;
    core::MeshLibrary* meshLibrary_;
    core::AssetCache<uint32_t> meshCache_;

    char pathBuffer_[256] = "";
    std::string statusMessage_;
    core::AssetMetadata lastMetadata_;
    bool hasPreview_ = false;
};

} // namespace engine::studio::plugins
