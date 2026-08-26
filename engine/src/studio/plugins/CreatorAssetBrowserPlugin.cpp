#include "studio/plugins/CreatorAssetBrowserPlugin.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>

#include <imgui.h>

#include "core/Components.hpp"
#include "studio/CreatorToolsSpawning.hpp"
#include "studio/MaterialPresets.hpp"
#include "studio/ParticlePresets.hpp"
#include "studio/PluginChrome.hpp"

namespace engine::studio::plugins {

namespace {
// Real, distinct tags per prop kind -- what a substring search actually
// matches against beyond the bare name (e.g. searching "nature" finds
// Tree/Rock/Bush; searching "container" finds Crate/Barrel).
const char* propTags(core::WorldPropKind kind) {
    switch (kind) {
        case core::WorldPropKind::Tree: return "nature, outdoor, foliage";
        case core::WorldPropKind::Rock: return "nature, outdoor, terrain";
        case core::WorldPropKind::Crate: return "container, interactive, wood";
        case core::WorldPropKind::Barrel: return "container, interactive";
        case core::WorldPropKind::Lamp: return "light, interactive, urban";
        case core::WorldPropKind::Bush: return "nature, outdoor, foliage";
    }
    return "";
}

std::string toLower(const std::string& s) {
    std::string result = s;
    for (char& c : result) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return result;
}
} // namespace

CreatorAssetBrowserPlugin::CreatorAssetBrowserPlugin(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool,
                                                       VkQueue queue, core::MeshLibrary& meshLibrary,
                                                       TerrainEditorPlugin& terrainEditor)
    : meshLibrary_(&meshLibrary), terrainEditor_(&terrainEditor) {
    boxMesh_ = meshLibrary_->registerMesh(core::Mesh::createBox(allocator, device, cmdPool, queue, {0.5f, 0.5f, 0.5f}));
    capsuleMesh_ = meshLibrary_->registerMesh(core::Mesh::createCapsule(allocator, device, cmdPool, queue, 0.35f, 1.0f));
}

void CreatorAssetBrowserPlugin::update(float /*dt*/, core::ECS& /*ecs*/, core::EntityId /*selected*/,
                                        const std::vector<core::EntityId>& /*selectedEntities*/) {
    // Real, non-blocking poll -- called every frame regardless of
    // whether this panel is even open (see IStudioPlugin::update()'s own
    // doc comment), so a drag-and-drop import started while the Asset
    // Browser is closed still completes and lands in assetRegistry_.
    for (const core::AssetImportQueue::Result& result : importQueue_.poll()) {
        assetRegistry_.adoptMetadata(result.path, result.metadata);
        importStatusMessage_ = result.metadata.succeeded ? (std::string("Imported \"") + result.path + "\"")
                                                           : (std::string("Import failed: ") + result.metadata.error);
    }
}

void CreatorAssetBrowserPlugin::submitDroppedFile(const std::string& path) {
    importQueue_.submit(path);
    importStatusMessage_ = std::string("Importing \"") + path + "\" in the background...";
}

bool CreatorAssetBrowserPlugin::matchesSearch(const char* nameText, const char* tags) const {
    if (searchBuffer_[0] == '\0') return true;
    std::string needle = toLower(searchBuffer_);
    return toLower(nameText).find(needle) != std::string::npos || toLower(tags).find(needle) != std::string::npos;
}

void CreatorAssetBrowserPlugin::drawPropEntries(core::ECS& ecs) {
    constexpr core::WorldPropKind kKinds[] = {core::WorldPropKind::Tree,   core::WorldPropKind::Rock,
                                               core::WorldPropKind::Crate, core::WorldPropKind::Barrel,
                                               core::WorldPropKind::Lamp,  core::WorldPropKind::Bush};
    for (auto kind : kKinds) {
        const char* entryName = core::worldPropKindName(kind);
        const char* tags = propTags(kind);
        if (!matchesSearch(entryName, tags)) continue;
        ImGui::PushID(entryName);
        ImGui::Text("[Prop] %s", entryName);
        // Kronos ("Studio Asset Drag-and-Drop"): real drag source --
        // carries the real core::WorldPropKind by value (same raw-copy
        // convention "ASSET_MATERIAL_PRESET" above already uses).
        // ViewportPanel is the real drop target -- see that file's own
        // drop-target block for the real drop-position raycast.
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
            ImGui::SetDragDropPayload("ASSET_WORLD_PROP", &kind, sizeof(core::WorldPropKind));
            ImGui::Text("Place \"%s\"", entryName);
            ImGui::EndDragDropSource();
        }
        ImGui::TextDisabled("%s", tags);
        ImGui::SameLine();
        if (ImGui::SmallButton("Use")) {
            ++propSpawnCount_;
            spawnPropAuthoring(ecs, kind, {0.0f, 1.0f, 0.0f}, propSpawnCount_, boxMesh_, capsuleMesh_);
        }
        ImGui::PopID();
    }
    ImGui::TextDisabled("Drag a prop onto the Viewport to place it there, or click \"Use\" to spawn it at the origin.");
}

void CreatorAssetBrowserPlugin::drawMaterialEntries(core::ECS& ecs, core::EntityId selected) {
    auto* renderable = selected != core::kNullEntity ? ecs.tryGetComponent<core::Renderable>(selected) : nullptr;
    for (int i = 0; i < kMaterialPresetCount; ++i) {
        const MaterialPresetInfo& preset = kMaterialPresets[i];
        const char* tags = "material, PBR";
        if (!matchesSearch(preset.label, tags)) continue;
        ImGui::PushID(preset.label);
        ImGui::Text("[Material] %s", preset.label);
        // Kronos (Alpha Completion Checklist, "Editor UX Polish" --
        // "Stable drag-and-drop in Asset Browser"): a real drag source,
        // carrying the real preset's index into kMaterialPresets (the
        // same real, shared table "Use" above already reads from -- no
        // second, could-drift copy of the preset data crosses the drag/
        // drop boundary, just an index). ExplorerPanel's own entity rows
        // are the real drop target -- see that file's own drawEntityNode().
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
            ImGui::SetDragDropPayload("ASSET_MATERIAL_PRESET", &i, sizeof(int));
            ImGui::Text("Apply \"%s\" material", preset.label);
            ImGui::EndDragDropSource();
        }
        ImGui::TextDisabled("%s", tags);
        ImGui::SameLine();
        ImGui::BeginDisabled(renderable == nullptr);
        if (ImGui::SmallButton("Use") && renderable != nullptr) {
            renderable->baseColor = preset.baseColor;
            renderable->metallic = preset.metallic;
            renderable->roughness = preset.roughness;
            renderable->emissiveColor = preset.emissiveColor;
            renderable->emissiveIntensity = preset.emissiveIntensity;
        }
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    if (renderable == nullptr) {
        ImGui::TextDisabled("Select an entity with a Renderable to apply a material, or drag one onto an entity in "
                             "the Explorer tree.");
    }
}

void CreatorAssetBrowserPlugin::drawParticleEntries(core::ECS& ecs, core::EntityId selected) {
    constexpr ParticlePresetId kPresetIds[] = {ParticlePresetId::Fire,   ParticlePresetId::Smoke, ParticlePresetId::Sparkle,
                                                ParticlePresetId::Snow,  ParticlePresetId::Glow,  ParticlePresetId::Burst};
    for (auto presetId : kPresetIds) {
        const char* entryName = particlePresetName(presetId);
        const char* tags = "particle, effect, VFX";
        if (!matchesSearch(entryName, tags)) continue;
        ImGui::PushID(entryName);
        ImGui::Text("[Particle] %s", entryName);
        ImGui::TextDisabled("%s", tags);
        ImGui::SameLine();
        ImGui::BeginDisabled(selected == core::kNullEntity);
        if (ImGui::SmallButton("Use") && selected != core::kNullEntity) {
            auto* emitter = ecs.tryGetComponent<core::ParticleEmitter>(selected);
            if (emitter == nullptr) emitter = &ecs.addComponent<core::ParticleEmitter>(selected);
            applyParticlePreset(presetId, emitter->settings);
        }
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    if (selected == core::kNullEntity) {
        ImGui::TextDisabled("Select an entity to attach a particle effect to.");
    }
}

void CreatorAssetBrowserPlugin::drawTerrainEntries() {
    struct TerrainPresetEntry {
        const char* name;
        core::Terrain::Preset preset;
        const char* tags;
    };
    constexpr TerrainPresetEntry kEntries[] = {
        {"Rolling Hills", core::Terrain::Preset::RollingHills, "terrain, outdoor, gentle"},
        {"Flat Plains", core::Terrain::Preset::FlatPlains, "terrain, outdoor, flat"},
        {"Rocky Canyon", core::Terrain::Preset::RockyCanyon, "terrain, outdoor, steep"},
    };
    for (const auto& entry : kEntries) {
        if (!matchesSearch(entry.name, entry.tags)) continue;
        ImGui::PushID(entry.name);
        ImGui::Text("[Terrain] %s", entry.name);
        ImGui::TextDisabled("%s", entry.tags);
        ImGui::SameLine();
        ImGui::BeginDisabled(!terrainEditor_->hasTerrain());
        if (ImGui::SmallButton("Use") && terrainEditor_->hasTerrain()) {
            terrainEditor_->terrain().applyPreset(entry.preset);
        }
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    if (!terrainEditor_->hasTerrain()) {
        ImGui::TextDisabled("No terrain yet -- create one in the Terrain Editor first.");
    }
}

void CreatorAssetBrowserPlugin::drawImportedAssetsSection() {
    ImGui::TextUnformatted("Imported Assets");
    ImGui::TextDisabled(
        "Creator-imported files (.obj/.gltf/.glb/.fbx/.png/.wav/...) -- distinct from the built-in presets above. "
        "Drag a file onto the Studio window to import it too.");

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70.0f);
    ImGui::InputTextWithHint("##import_path", "path/to/asset.obj", importPathBuffer_, sizeof(importPathBuffer_));
    ImGui::SameLine();
    if (ImGui::Button("Import") && importPathBuffer_[0] != '\0') {
        importQueue_.submit(importPathBuffer_);
        importStatusMessage_ = std::string("Importing \"") + importPathBuffer_ + "\" in the background...";
    }
    ImGui::SameLine();
    helpMarker("Re-importing an already-registered path real-replaces its entry (re-reads real metadata) rather than "
               "creating a duplicate -- use this after editing a file on disk. Runs in the background -- Studio "
               "keeps rendering while a large file imports.");
    size_t pending = importQueue_.pendingCount();
    if (pending > 0) {
        ImGui::TextDisabled("%zu asset%s importing...", pending, pending == 1 ? "" : "s");
    } else if (!importStatusMessage_.empty()) {
        ImGui::TextDisabled("%s", importStatusMessage_.c_str());
    }

    for (const core::AssetRegistryEntry& entry : assetRegistry_.entries()) {
        const char* kindLabel = "Asset";
        switch (entry.kind) {
            case core::AssetKind::Mesh: kindLabel = "Mesh"; break;
            case core::AssetKind::Texture: kindLabel = "Texture"; break;
            case core::AssetKind::Audio: kindLabel = "Audio"; break;
            case core::AssetKind::Unknown: kindLabel = "Asset"; break;
        }
        if (!matchesSearch(entry.path.c_str(), kindLabel)) continue;
        ImGui::PushID(entry.path.c_str());
        ImGui::Text("[%s] %s", kindLabel, entry.path.c_str());
        switch (entry.kind) {
            case core::AssetKind::Mesh:
                ImGui::TextDisabled("%u vertices, %u triangles, %llu bytes", entry.vertexCount, entry.triangleCount,
                                     static_cast<unsigned long long>(entry.fileSizeBytes));
                break;
            case core::AssetKind::Texture:
                ImGui::TextDisabled("%dx%d, %d channels, %llu bytes", entry.width, entry.height, entry.channels,
                                     static_cast<unsigned long long>(entry.fileSizeBytes));
                break;
            case core::AssetKind::Audio:
                ImGui::TextDisabled("%.1fs, %u Hz, %u ch, %llu bytes", entry.durationSeconds, entry.sampleRate,
                                     entry.channelCount, static_cast<unsigned long long>(entry.fileSizeBytes));
                break;
            case core::AssetKind::Unknown:
                ImGui::TextDisabled("%llu bytes", static_cast<unsigned long long>(entry.fileSizeBytes));
                break;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) assetRegistry_.removeAsset(entry.path);
        ImGui::PopID();
    }
    if (assetRegistry_.size() == 0) ImGui::TextDisabled("No assets imported yet.");
}

void CreatorAssetBrowserPlugin::drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>&) {
    ImGui::Begin(name());
    drawPluginHeader("Asset Browser");

    ImGui::InputTextWithHint("##search", "Search by name or tag...", searchBuffer_, sizeof(searchBuffer_));
    ImGui::SameLine();
    helpMarker("Matches against each entry's real name and its tags (shown in grey under the name), e.g. \"container\" finds Crate and Barrel.");
    const char* categoryNames[] = {"All", "Props", "Materials", "Particles", "Terrain", "Imported"};
    ImGui::Combo("Category", &categoryFilter_, categoryNames, 6);

    ImGui::Separator();
    ImGui::BeginChild("##asset_browser_scroll", ImVec2(0.0f, 0.0f), false);
    if (categoryFilter_ == 0 || categoryFilter_ == 1) drawPropEntries(ecs);
    if (categoryFilter_ == 0 || categoryFilter_ == 2) drawMaterialEntries(ecs, selected);
    if (categoryFilter_ == 0 || categoryFilter_ == 3) drawParticleEntries(ecs, selected);
    if (categoryFilter_ == 0 || categoryFilter_ == 4) drawTerrainEntries();
    if (categoryFilter_ == 0 || categoryFilter_ == 5) drawImportedAssetsSection();
    ImGui::EndChild();

    drawPluginFooter("Every entry here calls the same real function its dedicated tool does -- not a separate asset system.");
    ImGui::End();
}

} // namespace engine::studio::plugins
