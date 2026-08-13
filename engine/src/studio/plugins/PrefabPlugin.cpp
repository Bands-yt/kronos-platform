#include "studio/plugins/PrefabPlugin.hpp"

#include <cstdio>

#include <imgui.h>

#include "core/Components.hpp"

namespace engine::studio::plugins {

namespace {
// core::PrefabMeshKind (describes a reusable *template*'s shape) and
// core::MeshSourceKind (describes what a *live entity's* mesh actually
// is, see Components.hpp) are deliberately separate enums -- this is the
// one place that needs to translate between them, when a prefab spawns a
// real entity a scene save later needs to be able to regenerate.
core::MeshSourceKind toMeshSourceKind(core::PrefabMeshKind kind) {
    switch (kind) {
        case core::PrefabMeshKind::Box: return core::MeshSourceKind::Box;
        case core::PrefabMeshKind::Plane: return core::MeshSourceKind::Plane;
        case core::PrefabMeshKind::Capsule: return core::MeshSourceKind::Capsule;
        case core::PrefabMeshKind::Quad: return core::MeshSourceKind::Quad;
    }
    return core::MeshSourceKind::Box;
}
} // namespace

PrefabPlugin::PrefabPlugin(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                            core::MeshLibrary& meshLibrary)
    : allocator_(allocator), device_(device), cmdPool_(cmdPool), queue_(queue), meshLibrary_(&meshLibrary) {}

core::Mesh PrefabPlugin::buildMesh(const core::Prefab& prefab) const {
    switch (prefab.meshKind) {
        case core::PrefabMeshKind::Box:
            return core::Mesh::createBox(allocator_, device_, cmdPool_, queue_, prefab.meshParams);
        case core::PrefabMeshKind::Plane:
            return core::Mesh::createPlane(allocator_, device_, cmdPool_, queue_, prefab.meshParams.x,
                                            prefab.meshParams.z);
        case core::PrefabMeshKind::Capsule:
            return core::Mesh::createCapsule(allocator_, device_, cmdPool_, queue_, prefab.meshParams.x,
                                              prefab.meshParams.y);
        case core::PrefabMeshKind::Quad:
            return core::Mesh::createQuad(allocator_, device_, cmdPool_, queue_, prefab.meshParams.x);
    }
    return core::Mesh{};
}

void PrefabPlugin::spawnInstance(const LoadedPrefab& entry, glm::vec3 position, core::ECS& ecs) const {
    core::EntityId entity = ecs.createEntity(entry.data.name);
    if (auto* transform = ecs.tryGetComponent<core::Transform>(entity)) {
        transform->position = position;
        transform->scale = entry.data.scale;
    }
    auto& renderable = ecs.addComponent<core::Renderable>(entity);
    renderable.meshHandle = entry.meshHandle;
    renderable.baseColor = entry.data.baseColor;
    renderable.metallic = entry.data.metallic;
    renderable.roughness = entry.data.roughness;
    renderable.emissiveColor = entry.data.emissiveColor;
    renderable.emissiveIntensity = entry.data.emissiveIntensity;
    renderable.castsShadow = entry.data.castsShadow;
    renderable.instanced = entry.data.instanced;

    auto& meshSource = ecs.addComponent<core::MeshSource>(entity);
    meshSource.kind = toMeshSourceKind(entry.data.meshKind);
    meshSource.params = entry.data.meshParams;
}

void PrefabPlugin::drawPanel(core::ECS& ecs, core::EntityId /*selected*/,
                              const std::vector<core::EntityId>& /*selectedEntities*/) {
    ImGui::Begin("Prefabs");

    ImGui::InputText("File", pathBuffer_, sizeof(pathBuffer_));
    ImGui::SameLine();
    if (ImGui::Button("New")) {
        library_.emplace_back();
        activeIndex_ = static_cast<int>(library_.size()) - 1;
    }
    ImGui::SameLine();
    if (ImGui::Button("Load")) {
        LoadedPrefab entry;
        if (entry.data.loadFromFile(pathBuffer_)) {
            library_.push_back(std::move(entry));
            activeIndex_ = static_cast<int>(library_.size()) - 1;
        }
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Library (this session)");
    for (size_t i = 0; i < library_.size(); ++i) {
        bool isActive = static_cast<int>(i) == activeIndex_;
        char label[160];
        std::snprintf(label, sizeof(label), "%s##%zu", library_[i].data.name.c_str(), i);
        if (ImGui::Selectable(label, isActive)) {
            activeIndex_ = static_cast<int>(i);
        }
    }

    if (activeIndex_ < 0 || activeIndex_ >= static_cast<int>(library_.size())) {
        ImGui::TextDisabled("No prefab selected -- New or Load one above.");
        ImGui::End();
        return;
    }

    LoadedPrefab& entry = library_[static_cast<size_t>(activeIndex_)];
    core::Prefab& prefab = entry.data;

    ImGui::Separator();
    char nameBuf[128];
    std::snprintf(nameBuf, sizeof(nameBuf), "%s", prefab.name.c_str());
    if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) prefab.name = nameBuf;

    static const char* kKindNames[] = {"Box", "Plane", "Capsule", "Quad"};
    int kindIndex = static_cast<int>(prefab.meshKind);
    if (ImGui::Combo("Shape", &kindIndex, kKindNames, IM_ARRAYSIZE(kKindNames))) {
        prefab.meshKind = static_cast<core::PrefabMeshKind>(kindIndex);
    }
    ImGui::DragFloat3("Shape Params", &prefab.meshParams.x, 0.02f, 0.01f, 20.0f);
    ImGui::DragFloat3("Scale", &prefab.scale.x, 0.02f, 0.01f, 20.0f);
    ImGui::ColorEdit4("Base Color", &prefab.baseColor.x);
    ImGui::SliderFloat("Metallic", &prefab.metallic, 0.0f, 1.0f);
    ImGui::SliderFloat("Roughness", &prefab.roughness, 0.045f, 1.0f);
    ImGui::ColorEdit3("Emissive Color", &prefab.emissiveColor.x);
    ImGui::DragFloat("Emissive Intensity", &prefab.emissiveIntensity, 0.05f, 0.0f, 20.0f);
    ImGui::Checkbox("Casts Shadow", &prefab.castsShadow);
    ImGui::SameLine();
    ImGui::Checkbox("Instanced", &prefab.instanced);

    ImGui::Separator();
    const char* buildLabel = entry.meshHandle == core::Renderable::kInvalidHandle
                                  ? "Build Mesh"
                                  : "Rebuild Mesh (updates existing instances)";
    if (ImGui::Button(buildLabel)) {
        core::Mesh mesh = buildMesh(prefab);
        if (mesh.vertexBuffer() != VK_NULL_HANDLE) {
            if (entry.meshHandle == core::Renderable::kInvalidHandle) {
                entry.meshHandle = meshLibrary_->registerMesh(std::move(mesh));
            } else {
                meshLibrary_->replaceMesh(entry.meshHandle, std::move(mesh), allocator_);
            }
        }
    }

    ImGui::Separator();
    ImGui::DragFloat3("Spawn Position", &spawnPosition_.x, 0.1f);
    bool canSpawn = entry.meshHandle != core::Renderable::kInvalidHandle;
    ImGui::BeginDisabled(!canSpawn);
    if (ImGui::Button("Spawn Instance")) {
        spawnInstance(entry, spawnPosition_, ecs);
    }
    ImGui::EndDisabled();
    if (!canSpawn) ImGui::TextDisabled("Build the mesh first.");

    ImGui::Separator();
    if (ImGui::Button("Save Prefab To File")) {
        (void)prefab.saveToFile(pathBuffer_);
    }

    ImGui::End();
}

} // namespace engine::studio::plugins
