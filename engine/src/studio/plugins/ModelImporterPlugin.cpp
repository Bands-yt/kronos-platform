#include "studio/plugins/ModelImporterPlugin.hpp"

#include <imgui.h>

#include "core/Components.hpp"
#include "core/FbxLoader.hpp"
#include "core/GltfLoader.hpp"
#include "core/NativeFileDialog.hpp"
#include "core/ObjLoader.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>

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

void ModelImporterPlugin::browseForFile(bool importWhenPicked) {
    core::FileDialogOptions options{"Import 3D Asset", {"*.gltf", "*.glb", "*.obj", "*.fbx"}, "3D models"};
    const bool started = core::openFileDialogAsync(options, [this, importWhenPicked](const std::string& path) {
        std::snprintf(pathBuffer_, sizeof(pathBuffer_), "%s", path.c_str());
        if (importWhenPicked) pendingImport_ = path;
    });
    if (!started) statusMessage_ = "Could not open a file dialog: " + core::fileDialogError();
}

void ModelImporterPlugin::update(float /*dt*/, core::ECS& ecs, core::EntityId /*selected*/,
                                 const std::vector<core::EntityId>& /*selectedEntities*/) {
    finishPendingImport(ecs);
    if (pendingImport_.empty()) return;
    const std::string path = std::move(pendingImport_);
    pendingImport_.clear();
    const core::EntityId entity = importModel(ecs, path, true);
    if (!asyncImports() && onImported_) onImported_(entity, statusMessage_);
}

bool ModelImporterPlugin::asyncImports() const {
    return resources_ != nullptr && resources_->hasLoader(core::ResourceKind::Mesh);
}

void ModelImporterPlugin::finishPendingImport(core::ECS& ecs) {
    for (size_t i = 0; i < pending_.size();) {
        if (pending_[i].resource.state() == core::ResourceState::Loading) {
            ++i;
            continue;
        }
        PendingImport done = std::move(pending_[i]);
        pending_.erase(pending_.begin() + static_cast<std::ptrdiff_t>(i));
        const std::string path = done.resource.path();
        if (done.resource.state() == core::ResourceState::Failed) {
            statusMessage_ = "Import failed: " + done.resource.error();
            lastMetadata_ = core::AssetMetadata{};
            if (done.asNewEntity && onImported_) onImported_(core::kNullEntity, statusMessage_);
            continue;
        }
        lastMetadata_ = core::AssetMetadata{};
        lastMetadata_.succeeded = true;
        lastMetadata_.kind = core::AssetKind::Mesh;
        std::error_code ec;
        lastMetadata_.fileSizeBytes = std::filesystem::file_size(path, ec);
        if (const core::Mesh* mesh = meshLibrary_->get(done.resource.get())) {
            lastMetadata_.vertexCount = mesh->vertexCount();
            lastMetadata_.triangleCount = mesh->indexCount() / 3;
        }
        const core::EntityId entity = placeEntity(ecs, path, done.resource.get(), done.asNewEntity);
        ecs.raw().get_or_emplace<core::ResourceRefs>(entity).handles = {done.resource};
        if (done.asNewEntity && onImported_) onImported_(entity, statusMessage_);
    }
}

core::EntityId ModelImporterPlugin::importModel(core::ECS& ecs, const std::string& path, bool asNewEntity) {
    if (asyncImports()) {
        std::error_code ec;
        if (!hasExtension(path, ".obj") && !hasExtension(path, ".gltf") && !hasExtension(path, ".glb") &&
            !hasExtension(path, ".fbx")) {
            statusMessage_ = "Not a recognized mesh file (expected .obj/.gltf/.glb/.fbx).";
        } else if (!std::filesystem::is_regular_file(path, ec)) {
            statusMessage_ = "Failed: file not found: " + path;
        } else {
            pending_.push_back({resources_->acquire(core::ResourceKind::Mesh, path), asNewEntity});
            statusMessage_ = "Loading " + std::filesystem::path(path).filename().string() + "...";
            finishPendingImport(ecs);
            return core::kNullEntity;
        }
        if (asNewEntity && onImported_) onImported_(core::kNullEntity, statusMessage_);
        return core::kNullEntity;
    }

    lastMetadata_ = core::extractAssetMetadata(path);
    if (!lastMetadata_.succeeded || lastMetadata_.kind != core::AssetKind::Mesh) {
        statusMessage_ = lastMetadata_.succeeded ? "Not a recognized mesh file (expected .obj/.gltf/.glb/.fbx)."
                                                  : ("Failed: " + lastMetadata_.error);
        return core::kNullEntity;
    }

    core::MeshSourceKind sourceKind = core::MeshSourceKind::Obj;
    if (hasExtension(path, ".gltf") || hasExtension(path, ".glb")) sourceKind = core::MeshSourceKind::Gltf;
    else if (hasExtension(path, ".fbx")) sourceKind = core::MeshSourceKind::Fbx;

    uint32_t meshHandle = core::Renderable::kInvalidHandle;
    if (meshCache_.tryGet(path, meshHandle)) {
        statusMessage_ = "Loaded from cache (file unchanged on disk).";
    } else {
        std::vector<core::Vertex> vertices;
        std::vector<uint32_t> indices;
        std::string parseError;
        bool parsed;
        if (sourceKind == core::MeshSourceKind::Gltf) {
            core::GltfLoadResult gltf = core::loadGltf(path);
            parsed = gltf.succeeded;
            parseError = gltf.error;
            vertices = std::move(gltf.vertices);
            indices = std::move(gltf.indices);
        } else if (sourceKind == core::MeshSourceKind::Fbx) {
            core::FbxLoadResult fbx = core::loadFbx(path);
            parsed = fbx.succeeded;
            parseError = fbx.error;
            vertices = std::move(fbx.vertices);
            indices = std::move(fbx.indices);
        } else {
            core::ObjLoadResult obj = core::loadObj(path);
            parsed = obj.succeeded;
            parseError = obj.error;
            vertices = std::move(obj.vertices);
            indices = std::move(obj.indices);
        }
        if (!parsed) {
            statusMessage_ = "Parse failed: " + parseError;
            return core::kNullEntity;
        }
        core::Mesh mesh;
        if (!mesh.uploadFromHost(allocator_, device_, cmdPool_, queue_, vertices, indices)) {
            statusMessage_ = "GPU upload failed.";
            return core::kNullEntity;
        }
        meshHandle = meshLibrary_->registerMesh(std::move(mesh));
        meshCache_.put(path, meshHandle);
        statusMessage_ = "Loaded and uploaded to GPU.";
    }

    return placeEntity(ecs, path, meshHandle, asNewEntity);
}

core::EntityId ModelImporterPlugin::placeEntity(core::ECS& ecs, const std::string& path, uint32_t meshHandle,
                                                bool asNewEntity) {
    core::MeshSourceKind sourceKind = core::MeshSourceKind::Obj;
    if (hasExtension(path, ".gltf") || hasExtension(path, ".glb")) sourceKind = core::MeshSourceKind::Gltf;
    else if (hasExtension(path, ".fbx")) sourceKind = core::MeshSourceKind::Fbx;

    core::EntityId entity = core::kNullEntity;
    if (asNewEntity) {
        std::string base = std::filesystem::path(path).stem().string();
        if (base.empty()) base = "Model";
        std::string name = base;
        for (int suffix = 2; findEntityByName(ecs, name) != core::kNullEntity; ++suffix) name = base + std::to_string(suffix);
        entity = ecs.createEntity(name);
        statusMessage_ = "Imported \"" + name + "\".";
    } else {
        entity = findEntityByName(ecs, "ModelPreview");
        if (entity == core::kNullEntity) entity = ecs.createEntity("ModelPreview");
        hasPreview_ = true;
    }
    if (!ecs.hasComponent<core::Transform>(entity)) ecs.addComponent<core::Transform>(entity);
    auto& renderable = ecs.addComponent<core::Renderable>(entity);
    renderable.meshHandle = meshHandle;
    auto& meshSource = ecs.addComponent<core::MeshSource>(entity);
    meshSource.kind = sourceKind;
    meshSource.path = path;
    return entity;
}

void ModelImporterPlugin::drawPanel(core::ECS& ecs, core::EntityId /*selected*/,
                                      const std::vector<core::EntityId>& /*selectedEntities*/) {
    ImGui::Begin("Model Importer");

    ImGui::TextWrapped("Import a Wavefront .obj, glTF 2.0 (.gltf/.glb) or FBX file. It loads onto a \"ModelPreview\" entity you can select and orbit in the Viewport.");
    ImGui::SetNextItemWidth(320.0f);
    ImGui::InputText("Path", pathBuffer_, sizeof(pathBuffer_));
    ImGui::SameLine();
    ImGui::BeginDisabled(core::fileDialogOpen());
    if (ImGui::Button("Browse...")) browseForFile(false);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Load")) importModel(ecs, pathBuffer_, false);

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
