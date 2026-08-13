#include "studio/plugins/ModelImporterPlugin.hpp"

#include <imgui.h>

#include "core/Components.hpp"
#include "core/ObjLoader.hpp"

namespace engine::studio::plugins {

namespace {
core::EntityId findEntityByName(core::ECS& ecs, const std::string& name) {
    for (auto entity : ecs.view<core::Name>()) {
        const auto* nameComp = ecs.tryGetComponent<core::Name>(entity);
        if (nameComp != nullptr && nameComp->value == name) return entity;
    }
    return core::kNullEntity;
}
} // namespace

ModelImporterPlugin::ModelImporterPlugin(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                                           core::MeshLibrary& meshLibrary)
    : allocator_(allocator), device_(device), cmdPool_(cmdPool), queue_(queue), meshLibrary_(&meshLibrary) {}

void ModelImporterPlugin::drawPanel(core::ECS& ecs, core::EntityId /*selected*/,
                                      const std::vector<core::EntityId>& /*selectedEntities*/) {
    ImGui::Begin("Model Importer");

    ImGui::TextWrapped("Import a Wavefront .obj file -- loads onto a real \"ModelPreview\" entity you can select "
                        "and orbit in the Viewport panel, the same as any other entity.");
    ImGui::SetNextItemWidth(320.0f);
    ImGui::InputText("Path", pathBuffer_, sizeof(pathBuffer_));
    ImGui::SameLine();
    if (ImGui::Button("Load")) {
        std::string path = pathBuffer_;
        lastMetadata_ = core::extractAssetMetadata(path);

        if (!lastMetadata_.succeeded || lastMetadata_.kind != core::AssetKind::Mesh) {
            statusMessage_ = lastMetadata_.succeeded ? "Not a recognized mesh file (expected .obj)." : ("Failed: " + lastMetadata_.error);
        } else {
            uint32_t cachedHandle = 0;
            uint32_t meshHandle;
            if (meshCache_.tryGet(path, cachedHandle)) {
                meshHandle = cachedHandle;
                statusMessage_ = "Loaded from cache (file unchanged on disk).";
            } else {
                core::ObjLoadResult obj = core::loadObj(path);
                if (!obj.succeeded) {
                    statusMessage_ = "Parse failed: " + obj.error;
                    meshHandle = core::Renderable::kInvalidHandle;
                } else {
                    core::Mesh mesh;
                    if (!mesh.uploadFromHost(allocator_, device_, cmdPool_, queue_, obj.vertices, obj.indices)) {
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
                meshSource.kind = core::MeshSourceKind::Obj;
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
