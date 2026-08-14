#include "studio/plugins/BlockBuilderPlugin.hpp"

#include <cmath>
#include <cstdio>
#include <string>

#include <glm/gtc/quaternion.hpp>
#include <imgui.h>

#include "studio/PluginChrome.hpp"

namespace engine::studio::plugins {

namespace {
const char* blockShapeName(BlockShape shape) {
    switch (shape) {
        case BlockShape::Cube: return "Cube";
        case BlockShape::Sphere: return "Sphere";
        case BlockShape::Cylinder: return "Cylinder";
        case BlockShape::Wedge: return "Wedge";
    }
    return "Cube";
}
} // namespace

BlockBuilderPlugin::BlockBuilderPlugin(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                                        core::MeshLibrary& meshLibrary, TerrainEditorPlugin& terrainEditor)
    : meshLibrary_(&meshLibrary), terrainEditor_(&terrainEditor) {
    cubeMesh_ = meshLibrary_->registerMesh(core::Mesh::createBox(allocator, device, cmdPool, queue, {0.5f, 0.5f, 0.5f}));
    // Zero half-height capsule == a real sphere -- the same construction
    // main.cpp's own "bouncingSphere" bring-up entity already uses, not a
    // separate sphere generator.
    sphereMesh_ = meshLibrary_->registerMesh(core::Mesh::createCapsule(allocator, device, cmdPool, queue, 0.5f, 0.0f));
    cylinderMesh_ = meshLibrary_->registerMesh(core::Mesh::createCylinder(allocator, device, cmdPool, queue, 0.5f, 0.5f));
    wedgeMesh_ = meshLibrary_->registerMesh(core::Mesh::createWedge(allocator, device, cmdPool, queue, {0.5f, 0.5f, 0.5f}));
}

glm::vec3 BlockBuilderPlugin::effectiveSpawnPosition() const {
    glm::vec3 position = spawnPosition_;
    if (snapToGridEnabled_ && gridSize_ > 0.0f) {
        position.x = std::round(position.x / gridSize_) * gridSize_;
        position.z = std::round(position.z / gridSize_) * gridSize_;
    }
    if (snapToSurfaceEnabled_ && terrainEditor_->hasTerrain()) {
        position.y = terrainEditor_->terrain().heightAt(position.x, position.z);
    }
    return position;
}

core::EntityId BlockBuilderPlugin::spawnBlock(core::ECS& ecs, BlockShape shape) {
    ++blockSpawnCount_;
    std::string name = std::string(blockShapeName(shape)) + " " + std::to_string(blockSpawnCount_);
    core::EntityId entity = ecs.createEntity(name);

    if (auto* transform = ecs.tryGetComponent<core::Transform>(entity)) {
        transform->position = effectiveSpawnPosition();
        transform->rotation = glm::quat(glm::radians(spawnRotationDegrees_));
        transform->scale = spawnScale_;
    }

    auto& renderable = ecs.addComponent<core::Renderable>(entity);
    renderable.baseColor = spawnColor_;
    renderable.metallic = 0.05f;
    renderable.roughness = 0.7f;

    switch (shape) {
        case BlockShape::Cube: {
            renderable.meshHandle = cubeMesh_;
            auto& meshSource = ecs.addComponent<core::MeshSource>(entity);
            meshSource.kind = core::MeshSourceKind::Box;
            meshSource.params = {0.5f, 0.5f, 0.5f};
            break;
        }
        case BlockShape::Sphere: {
            renderable.meshHandle = sphereMesh_;
            auto& meshSource = ecs.addComponent<core::MeshSource>(entity);
            meshSource.kind = core::MeshSourceKind::Capsule;
            meshSource.params = {0.5f, 0.0f, 0.0f};
            break;
        }
        case BlockShape::Cylinder:
            // No MeshSourceKind::Cylinder -- see BlockBuilderPlugin.hpp's
            // own comment. Deliberately no MeshSource component at all
            // here (rather than a wrong/misleading one) -- same honest
            // "can't fully round-trip" gap core::Terrain already has.
            renderable.meshHandle = cylinderMesh_;
            break;
        case BlockShape::Wedge:
            renderable.meshHandle = wedgeMesh_;
            break;
    }

    return entity;
}

void BlockBuilderPlugin::drawPanel(core::ECS& ecs, core::EntityId /*selected*/,
                                    const std::vector<core::EntityId>& /*selectedEntities*/) {
    ImGui::Begin(name());
    drawPluginHeader("Block Builder");

    ImGui::TextUnformatted("Spawn Position");
    ImGui::DragFloat3("##BlockSpawnPosition", &spawnPosition_.x, 0.25f);
    ImGui::Checkbox("Snap to Grid##blockbuilder", &snapToGridEnabled_);
    ImGui::SameLine();
    helpMarker("Rounds the spawn X/Z to the nearest Grid Size multiple. The raw typed position underneath is never "
               "overwritten -- toggle this off to get it back exactly.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    ImGui::DragFloat("Grid Size##blockbuilder", &gridSize_, 0.1f, 0.1f, 100.0f);
    ImGui::SameLine();
    ImGui::BeginDisabled(!terrainEditor_->hasTerrain());
    ImGui::Checkbox("Snap to Surface##blockbuilder", &snapToSurfaceEnabled_);
    ImGui::EndDisabled();
    ImGui::SameLine();
    helpMarker("Overrides spawn Y with the real terrain height at that X/Z. Needs a terrain to exist first.");
    glm::vec3 effectivePosition = effectiveSpawnPosition();
    ImGui::TextDisabled("Effective: (%.2f, %.2f, %.2f)", effectivePosition.x, effectivePosition.y, effectivePosition.z);

    ImGui::TextUnformatted("Spawn Rotation (degrees) / Scale");
    ImGui::DragFloat3("##BlockSpawnRotation", &spawnRotationDegrees_.x, 1.0f);
    ImGui::DragFloat3("##BlockSpawnScale", &spawnScale_.x, 0.05f, 0.01f, 100.0f, "%.2f");
    ImGui::ColorEdit4("Color##blockbuilder", &spawnColor_.x);

    ImGui::Separator();
    ImGui::TextUnformatted("Shape");
    ImGui::TextDisabled("Place, then use the Viewport's own Move/Rotate/Scale gizmo on the selected block to adjust it.");

    constexpr BlockShape kShapes[] = {BlockShape::Cube, BlockShape::Sphere, BlockShape::Cylinder, BlockShape::Wedge};
    for (size_t i = 0; i < 4; ++i) {
        if (i != 0) ImGui::SameLine();
        char label[32];
        std::snprintf(label, sizeof(label), "%s##block", blockShapeName(kShapes[i]));
        if (ImGui::Button(label, ImVec2(90.0f, 0.0f))) {
            spawnBlock(ecs, kShapes[i]);
        }
    }

    ImGui::TextDisabled("Cylinder/Wedge render and move normally this session, but won't survive a Save Scene "
                         "reload with their exact shape yet (no MeshSourceKind for them) -- Cube/Sphere fully "
                         "round-trip.");

    drawPluginFooter("Authoring-only -- no live physics until the scene actually runs.");
    ImGui::End();
}

} // namespace engine::studio::plugins
