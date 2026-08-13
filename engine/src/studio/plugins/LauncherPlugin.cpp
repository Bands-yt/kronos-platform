#include "studio/plugins/LauncherPlugin.hpp"

#include <fstream>
#include <sstream>

#include <imgui.h>
#include <nlohmann/json.hpp>

#include "core/Components.hpp"
#include "studio/PluginChrome.hpp"
#include "tntwars/MapLayout.hpp"
#include "tntwars/MapMaterial.hpp"

namespace engine::studio::plugins {

namespace {
const char* kBiomeNames[] = {"Trenches", "Volcano (Mantle)", "Sky Platforms", "Underwater (Island Sea)", "Space"};
}

LauncherPlugin::LauncherPlugin(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                                 core::MeshLibrary& meshLibrary, core::TextureLibrary& textureLibrary,
                                 core::Renderer& renderer)
    : allocator_(allocator), device_(device), cmdPool_(cmdPool), queue_(queue), meshLibrary_(&meshLibrary),
      materials_(core::ProceduralMaterialLibrary::generate(textureLibrary, allocator, device, cmdPool, queue)),
      renderer_(&renderer) {
    loadPreferences();
}

void LauncherPlugin::loadBiome(core::ECS& ecs, int mapIndex) {
    clearBiome(ecs);
    auto map = static_cast<tntwars::MapId>(mapIndex);
    std::vector<tntwars::MapLayoutPiece> pieces = tntwars::buildMapLayout(map);

    for (const tntwars::MapLayoutPiece& piece : pieces) {
        core::Mesh mesh = piece.shape == tntwars::MapPieceShape::Box
                               ? core::Mesh::createBox(allocator_, device_, cmdPool_, queue_, piece.halfExtents)
                               : core::Mesh::createPlane(allocator_, device_, cmdPool_, queue_, piece.halfExtents.x,
                                                          piece.halfExtents.z);
        if (mesh.vertexBuffer() == VK_NULL_HANDLE) continue;
        uint32_t meshHandle = meshLibrary_->registerMesh(std::move(mesh));

        core::EntityId entity = ecs.createEntity(piece.name);
        if (auto* transform = ecs.tryGetComponent<core::Transform>(entity)) transform->position = piece.position;

        auto& renderable = ecs.addComponent<core::Renderable>(entity);
        renderable.meshHandle = meshHandle;
        renderable.baseColor = glm::vec4(piece.color, 1.0f);
        materials_.applyTo(renderable, tntwars::classifyMapPieceMaterial(piece.name));

        auto& meshSource = ecs.addComponent<core::MeshSource>(entity);
        meshSource.kind = piece.shape == tntwars::MapPieceShape::Box ? core::MeshSourceKind::Box : core::MeshSourceKind::Plane;
        meshSource.params = piece.halfExtents;

        spawnedBiomeEntities_.push_back(entity);
    }

    selectedBiomeIndex_ = mapIndex;
}

void LauncherPlugin::clearBiome(core::ECS& ecs) {
    for (core::EntityId entity : spawnedBiomeEntities_) ecs.destroyEntity(entity);
    spawnedBiomeEntities_.clear();
}

void LauncherPlugin::drawBiomeSelectSection(core::ECS& ecs) {
    if (!ImGui::CollapsingHeader("Biome Selection", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::TextDisabled("Real, procedural preview of each TNT Wars map's own base geometry, loaded into this scene.");
    ImGui::TextDisabled("(Studio's own bring-up scene has no live physics -- this is a flat editor preview, not the");
    ImGui::TextDisabled("full sculpted/collidable map engine_runtime's own --tntwars mode builds.)");

    for (int i = 0; i < IM_ARRAYSIZE(kBiomeNames); ++i) {
        // Real, honest check -- selectedBiomeIndex_ alone can be a
        // restored-but-not-yet-spawned preference (loadPreferences()
        // runs at construction time, before any ecs is available to
        // spawn into), so "already loaded" also requires real spawned
        // entities to exist right now, not just a matching index.
        bool isCurrent = (i == selectedBiomeIndex_) && !spawnedBiomeEntities_.empty();
        if (isCurrent) ImGui::BeginDisabled();
        if (ImGui::Button(kBiomeNames[i])) loadBiome(ecs, i);
        if (isCurrent) ImGui::EndDisabled();
        if (i + 1 < IM_ARRAYSIZE(kBiomeNames)) ImGui::SameLine();
    }

    ImGui::Text("%zu real spawned entities", spawnedBiomeEntities_.size());
    ImGui::BeginDisabled(spawnedBiomeEntities_.empty());
    if (ImGui::Button("Clear Biome")) {
        clearBiome(ecs);
        selectedBiomeIndex_ = -1;
    }
    ImGui::EndDisabled();
}

void LauncherPlugin::applySettingsToRenderer() {
    renderer_->setRTReflectionsEnabled(rtReflectionsEnabled_);
    renderer_->setVolumetricFogEnabled(volumetricFogEnabled_);
    renderer_->setSSREnabled(ssrEnabled_);
    renderer_->setRTGIEnabled(rtGiEnabled_);
    renderer_->setCinematicMode(cinematicModeEnabled_);
}

void LauncherPlugin::drawSettingsSection() {
    if (!ImGui::CollapsingHeader("Settings", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::TextDisabled("Real renderer toggles -- applied immediately to this Studio session's own live preview.");

    bool changed = false;
    changed |= ImGui::Checkbox("RT Reflections", &rtReflectionsEnabled_);
    changed |= ImGui::Checkbox("Volumetric Fog", &volumetricFogEnabled_);
    changed |= ImGui::Checkbox("Screen-Space Reflections", &ssrEnabled_);
    changed |= ImGui::Checkbox("Ray-Traced GI", &rtGiEnabled_);
    changed |= ImGui::Checkbox("Cinematic Mode", &cinematicModeEnabled_);
    if (changed) applySettingsToRenderer();
}

void LauncherPlugin::drawPreferencesSection() {
    if (!ImGui::CollapsingHeader("Preferences", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::TextDisabled("Real JSON-backed save/load -- persists your last biome + settings across Studio sessions.");

    if (ImGui::Button("Save Preferences")) savePreferences();
    ImGui::SameLine();
    if (ImGui::Button("Load Preferences")) loadPreferences();
    if (!lastPreferencesStatus_.empty()) ImGui::TextDisabled("%s", lastPreferencesStatus_.c_str());
}

void LauncherPlugin::savePreferences() {
    nlohmann::json j;
    j["selectedBiomeIndex"] = selectedBiomeIndex_;
    j["rtReflectionsEnabled"] = rtReflectionsEnabled_;
    j["volumetricFogEnabled"] = volumetricFogEnabled_;
    j["ssrEnabled"] = ssrEnabled_;
    j["rtGiEnabled"] = rtGiEnabled_;
    j["cinematicModeEnabled"] = cinematicModeEnabled_;

    std::ofstream out(preferencesPath_, std::ios::trunc);
    if (!out.is_open()) {
        lastPreferencesStatus_ = "Save failed: could not open " + preferencesPath_;
        return;
    }
    out << j.dump(2);
    lastPreferencesStatus_ = out.good() ? "Saved to " + preferencesPath_ : "Save failed: write error";
}

void LauncherPlugin::loadPreferences() {
    std::ifstream in(preferencesPath_);
    if (!in.is_open()) {
        lastPreferencesStatus_ = "No saved preferences yet (" + preferencesPath_ + ").";
        return;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(buffer.str());
    } catch (const nlohmann::json::parse_error&) {
        lastPreferencesStatus_ = "Load failed: malformed JSON in " + preferencesPath_;
        return;
    }

    if (j.contains("selectedBiomeIndex")) selectedBiomeIndex_ = j.at("selectedBiomeIndex").get<int>();
    if (j.contains("rtReflectionsEnabled")) rtReflectionsEnabled_ = j.at("rtReflectionsEnabled").get<bool>();
    if (j.contains("volumetricFogEnabled")) volumetricFogEnabled_ = j.at("volumetricFogEnabled").get<bool>();
    if (j.contains("ssrEnabled")) ssrEnabled_ = j.at("ssrEnabled").get<bool>();
    if (j.contains("rtGiEnabled")) rtGiEnabled_ = j.at("rtGiEnabled").get<bool>();
    if (j.contains("cinematicModeEnabled")) cinematicModeEnabled_ = j.at("cinematicModeEnabled").get<bool>();

    applySettingsToRenderer();
    lastPreferencesStatus_ = "Loaded from " + preferencesPath_ + ".";
}

void LauncherPlugin::drawPanel(core::ECS& ecs, core::EntityId, const std::vector<core::EntityId>&) {
    ImGui::Begin(name());
    drawPluginHeader("Kronos Launcher");
    drawBiomeSelectSection(ecs);
    drawSettingsSection();
    drawPreferencesSection();
    drawPluginFooter(lastPreferencesStatus_.c_str());
    ImGui::End();
}

} // namespace engine::studio::plugins
