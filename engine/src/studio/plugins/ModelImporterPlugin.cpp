#include "studio/plugins/ModelImporterPlugin.hpp"

#include <imgui.h>

#include "core/Components.hpp"
#include "core/GltfLoader.hpp"
#include "core/ObjLoader.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace engine::studio::plugins {

namespace {
core::EntityId findEntityByName(core::ECS& ecs, const std::string& name) {
    for (auto entity : ecs.view<core::Name>()) {
        const auto* nameComp = ecs.tryGetComponent<core::Name>(entity);
        if (nameComp != nullptr && nameComp->value == name) return entity;
    }
    return core::kNullEntity;
}

// Real, case-insensitive check -- same real dispatch need
// AssetMetadata.cpp's own lowerExtension() serves there, small enough
// (one helper, one call site) that duplicating it beats sharing a
// header across two otherwise-unrelated translation units for it.
bool hasExtension(const std::string& path, const char* ext) {
    size_t extLen = std::strlen(ext);
    if (path.size() < extLen) return false;
    return std::equal(path.end() - static_cast<std::ptrdiff_t>(extLen), path.end(), ext,
                       [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == b; });
}
} // namespace

ModelImporterPlugin::ModelImporterPlugin(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                                           core::MeshLibrary& meshLibrary)
    : allocator_(allocator), device_(device), cmdPool_(cmdPool), queue_(queue), meshLibrary_(&meshLibrary) {}

void ModelImporterPlugin::drawPanel(core::ECS& ecs, core::EntityId /*selected*/,
                                      const std::vector<core::EntityId>& /*selectedEntities*/) {
    ImGui::Begin("Model Importer");

    ImGui::TextWrapped("Import a Wavefront .obj or glTF 2.0 (.gltf/.glb) file -- loads onto a real \"ModelPreview\" "
                        "entity you can select and orbit in the Viewport panel, the same as any other entity.");
    ImGui::SetNextItemWidth(320.0f);
    ImGui::InputText("Path", pathBuffer_, sizeof(pathBuffer_));
    ImGui::SameLine();
    if (ImGui::Button("Load")) {
        std::string path = pathBuffer_;
        lastMetadata_ = core::extractAssetMetadata(path);

        if (!lastMetadata_.succeeded || lastMetadata_.kind != core::AssetKind::Mesh) {
            statusMessage_ = lastMetadata_.succeeded ? "Not a recognized mesh file (expected .obj/.gltf/.glb)."
                                                      : ("Failed: " + lastMetadata_.error);
        } else {
            // Real dispatch on the actual extension -- see
            // core/AssetMetadata.cpp's own identical dispatch for why
            // (both real loaders report the same vertices/indices shape).
            bool isGltf = hasExtension(path, ".gltf") || hasExtension(path, ".glb");

            uint32_t cachedHandle = 0;
            uint32_t meshHandle;
            if (meshCache_.tryGet(path, cachedHandle)) {
                meshHandle = cachedHandle;
                statusMessage_ = "Loaded from cache (file unchanged on disk).";
            } else {
                std::vector<core::Vertex> vertices;
                std::vector<uint32_t> indices;
                std::string parseError;
                bool parsed;
                if (isGltf) {
                    core::GltfLoadResult gltf = core::loadGltf(path);
                    parsed = gltf.succeeded;
                    parseError = gltf.error;
                    vertices = std::move(gltf.vertices);
                    indices = std::move(gltf.indices);
                } else {
                    core::ObjLoadResult obj = core::loadObj(path);
                    parsed = obj.succeeded;
                    parseError = obj.error;
                    vertices = std::move(obj.vertices);
                    indices = std::move(obj.indices);
                }

                if (!parsed) {
                    statusMessage_ = "Parse failed: " + parseError;
                    meshHandle = core::Renderable::kInvalidHandle;
                } else {
                    core::Mesh mesh;
                    if (!mesh.uploadFromHost(allocator_, device_, cmdPool_, queue_, vertices, indices)) {
                        statusMessage_ = "GPU upload failed.";
                        meshHandle = core::Renderable::kInvalidHandle;
                    } else {
                        meshHandle = meshLibrary_->registerMesh(std::move(mesh));
                        meshCache_.put(path, meshHandle);
                        statusMessage_ = "Loaded and uploaded to GPU.";
                    }
                }
            }

            if (meshHandle != core::Renderable::kInvalidHandle) {
                core::EntityId entity = findEntityByName(ecs, "ModelPreview");
                if (entity == core::kNullEntity) entity = ecs.createEntity("ModelPreview");
                auto& renderable = ecs.addComponent<core::Renderable>(entity);
                renderable.meshHandle = meshHandle;

                auto& meshSource = ecs.addComponent<core::MeshSource>(entity);
                meshSource.kind = isGltf ? core::MeshSourceKind::Gltf : core::MeshSourceKind::Obj;
                meshSource.path = path;
                hasPreview_ = true;
            }
        }
    }

    if (!statusMessage_.empty()) {
        ImGui::TextDisabled("%s", statusMessage_.c_str());
    }

    if (lastMetadata_.succeeded && lastMetadata_.kind == core::AssetKind::Mesh) {
        ImGui::SeparatorText("Metadata");
        ImGui::Text("File size: %llu bytes", static_cast<unsigned long long>(lastMetadata_.fileSizeBytes));
        ImGui::Text("Vertices: %u", lastMetadata_.vertexCount);
        ImGui::Text("Triangles: %u", lastMetadata_.triangleCount);
    }

    if (hasPreview_) {
        ImGui::Separator();
        ImGui::TextDisabled("Select \"ModelPreview\" in the Explorer panel to view it in the Viewport.");
    }

    ImGui::End();
}

} // namespace engine::studio::plugins
