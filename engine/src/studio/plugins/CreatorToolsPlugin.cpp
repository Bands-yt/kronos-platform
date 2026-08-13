#include "studio/plugins/CreatorToolsPlugin.hpp"

#include <cmath>
#include <cstdio>
#include <string>

#include <glm/gtc/quaternion.hpp>
#include <imgui.h>

#include "studio/CreatorToolsSpawning.hpp"
#include "studio/PluginChrome.hpp"

namespace engine::studio::plugins {

CreatorToolsPlugin::CreatorToolsPlugin(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                                        core::MeshLibrary& meshLibrary, core::Renderer& renderer,
                                        TerrainEditorPlugin& terrainEditor)
    : meshLibrary_(&meshLibrary), renderer_(&renderer), terrainEditor_(&terrainEditor) {
    boxMesh_ = meshLibrary_->registerMesh(core::Mesh::createBox(allocator, device, cmdPool, queue, {0.5f, 0.5f, 0.5f}));
    capsuleMesh_ = meshLibrary_->registerMesh(core::Mesh::createCapsule(allocator, device, cmdPool, queue, 0.35f, 1.0f));
}

glm::vec3 CreatorToolsPlugin::effectiveSpawnPosition() const {
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

void CreatorToolsPlugin::drawPanel(core::ECS& ecs, core::EntityId /*selected*/,
                                    const std::vector<core::EntityId>& /*selectedEntities*/) {
    ImGui::Begin(name());
    drawPluginHeader("Creator Tools");

    ImGui::TextUnformatted("Spawn Position");
    ImGui::DragFloat3("##SpawnPosition", &spawnPosition_.x, 0.25f);
    ImGui::Checkbox("Snap to Grid##sprint9", &snapToGridEnabled_);
    ImGui::SameLine();
    helpMarker("Rounds the spawn X/Z to the nearest Grid Size multiple. The raw typed position underneath is never overwritten -- toggle this off to get it back exactly.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    ImGui::DragFloat("Grid Size##sprint9", &gridSize_, 0.1f, 0.1f, 100.0f);
    ImGui::SameLine();
    ImGui::BeginDisabled(!terrainEditor_->hasTerrain());
    ImGui::Checkbox("Snap to Surface##sprint9", &snapToSurfaceEnabled_);
    ImGui::EndDisabled();
    ImGui::SameLine();
    helpMarker("Overrides spawn Y with the real terrain height at that X/Z. Needs a terrain to exist first (see the Terrain Presets section below or the Terrain Editor).");
    glm::vec3 effectivePosition = effectiveSpawnPosition();
    ImGui::TextDisabled("Effective: (%.2f, %.2f, %.2f)", effectivePosition.x, effectivePosition.y, effectivePosition.z);

    ImGui::TextUnformatted("Spawn Rotation (degrees) / Uniform Scale");
    ImGui::DragFloat3("##SpawnRotation", &spawnRotationDegrees_.x, 1.0f);
    ImGui::DragFloat("##SpawnScale", &spawnScale_, 0.05f, 0.01f, 100.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);

    ImGui::Separator();
    drawPropSection(ecs);
    ImGui::Separator();
    drawTeleportSection(ecs);
    ImGui::Separator();
    drawNavMarkerSection(ecs);
    ImGui::Separator();
    drawPointLightSection(ecs);
    ImGui::Separator();
    drawTerrainSection();
    ImGui::Separator();
    drawBiomeSection();

    drawPluginFooter("Props/pads are authoring-only -- no live physics until the scene actually runs.");
    ImGui::End();
}

void CreatorToolsPlugin::drawPropSection(core::ECS& ecs) {
    ImGui::TextUnformatted("World Props");
    constexpr core::WorldPropKind kKinds[] = {core::WorldPropKind::Tree,   core::WorldPropKind::Rock,
                                               core::WorldPropKind::Crate, core::WorldPropKind::Barrel,
                                               core::WorldPropKind::Lamp,  core::WorldPropKind::Bush};
    for (size_t i = 0; i < 6; ++i) {
        if (i % 3 != 0) ImGui::SameLine();
        char label[32];
        std::snprintf(label, sizeof(label), "%s##prop", core::worldPropKindName(kKinds[i]));
        if (ImGui::Button(label, ImVec2(90.0f, 0.0f))) {
            ++propSpawnCount_;
            core::EntityId entity = spawnPropAuthoring(ecs, kKinds[i], effectiveSpawnPosition(), propSpawnCount_,
                                                         boxMesh_, capsuleMesh_);
            // Real rotation/scale applied at placement time (task
            // category 2) -- spawnPropAuthoring() itself stays position-
            // only (see CreatorToolsSpawning.hpp's own comment), so this
            // is a real, direct post-spawn Transform edit rather than
            // widening that already-tested function's signature for a
            // Studio-UI-only concern.
            if (auto* transform = ecs.tryGetComponent<core::Transform>(entity)) {
                transform->rotation = glm::quat(glm::radians(spawnRotationDegrees_));
                transform->scale = glm::vec3(spawnScale_);
            }
        }
    }
}

void CreatorToolsPlugin::drawTeleportSection(core::ECS& ecs) {
    ImGui::TextUnformatted("Teleport Pad");
    ImGui::InputText("Link Tag", teleportTagBuffer_, sizeof(teleportTagBuffer_));
    if (teleportTagBuffer_[0] == '\0') {
        ImGui::TextColored(ImVec4(0.85f, 0.65f, 0.25f, 1.0f),
                            "No link tag set -- this pad won't auto-link to another pad.");
    }
    ImGui::DragFloat3("Destination", &teleportDestination_.x, 0.25f);
    if (ImGui::Button("Spawn Teleport Pad")) {
        ++teleportSpawnCount_;
        spawnTeleportPadAuthoring(ecs, effectiveSpawnPosition(), teleportDestination_, teleportTagBuffer_,
                                   teleportSpawnCount_, boxMesh_);
    }
}

void CreatorToolsPlugin::drawNavMarkerSection(core::ECS& ecs) {
    ImGui::TextUnformatted("Nav Marker");
    constexpr core::NavMarkerKind kKinds[] = {core::NavMarkerKind::Spawn, core::NavMarkerKind::Shop,
                                               core::NavMarkerKind::UpgradeKiosk, core::NavMarkerKind::TeleportPad,
                                               core::NavMarkerKind::Custom};
    int kindIndex = 0;
    for (int i = 0; i < 5; ++i) {
        if (kKinds[i] == selectedNavMarkerKind_) kindIndex = i;
    }
    const char* kindNames[5];
    for (int i = 0; i < 5; ++i) kindNames[i] = core::navMarkerKindName(kKinds[i]);
    if (ImGui::Combo("Kind##navmarker", &kindIndex, kindNames, 5)) selectedNavMarkerKind_ = kKinds[kindIndex];
    ImGui::InputText("Label##navmarker", navMarkerLabelBuffer_, sizeof(navMarkerLabelBuffer_));
    if (ImGui::Button("Spawn Nav Marker")) {
        ++navMarkerSpawnCount_;
        spawnNavMarkerAuthoring(ecs, effectiveSpawnPosition(), selectedNavMarkerKind_, navMarkerLabelBuffer_,
                                 navMarkerSpawnCount_);
    }
    ImGui::TextDisabled("A real, invisible location tag -- no physical presence, matching the Spawn/Shop markers main.cpp itself creates.");
}

void CreatorToolsPlugin::drawPointLightSection(core::ECS& ecs) {
    ImGui::TextUnformatted("Point Light");
    if (ImGui::Button("Spawn Point Light")) {
        ++pointLightSpawnCount_;
        spawnPointLightAuthoring(ecs, effectiveSpawnPosition(), pointLightSpawnCount_, boxMesh_);
    }
    ImGui::TextDisabled(
        "A real, entity-driven core::Light -- select it afterward and edit color/intensity/radius in the "
        "Inspector's Light section.");
}

void CreatorToolsPlugin::drawTerrainSection() {
    ImGui::TextUnformatted("Terrain Presets");
    if (!terrainEditor_->hasTerrain()) {
        ImGui::TextDisabled("No terrain yet -- create one in the Terrain Editor first.");
        return;
    }
    const char* presetNames[] = {"Rolling Hills", "Flat Plains", "Rocky Canyon"};
    int currentIndex = static_cast<int>(selectedPreset_);
    if (ImGui::Combo("Preset", &currentIndex, presetNames, 3)) {
        selectedPreset_ = static_cast<core::Terrain::Preset>(currentIndex);
    }
    if (ImGui::Button("Apply Terrain Preset")) {
        terrainEditor_->terrain().applyPreset(selectedPreset_);
    }
}

void CreatorToolsPlugin::drawBiomeSection() {
    ImGui::TextUnformatted("Biome Lighting Preview");
    const char* biomeNames[] = {core::biomeName(core::BiomeId::Forest), core::biomeName(core::BiomeId::Desert),
                                 core::biomeName(core::BiomeId::Cave)};
    int currentIndex = static_cast<int>(selectedBiome_);
    if (ImGui::Combo("Biome", &currentIndex, biomeNames, 3)) {
        selectedBiome_ = static_cast<core::BiomeId>(currentIndex);
    }
    // Applies onto the *current* lighting rather than a fresh default --
    // applyBiomeLighting() deliberately leaves directionWS untouched (see
    // its own doc comment: a biome has a lighting mood, not its own sun
    // angle), so this is a real, additive preview, not a full reset.
    if (ImGui::Button("Apply Biome Lighting to Viewport")) {
        core::SceneLighting lighting = renderer_->lighting();
        core::applyBiomeLighting(selectedBiome_, lighting);
        renderer_->setLighting(lighting);
    }
}

} // namespace engine::studio::plugins
