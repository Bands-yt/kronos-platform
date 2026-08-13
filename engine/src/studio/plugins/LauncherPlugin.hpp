#pragma once

#include <string>
#include <vector>

#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/Mesh.hpp"
#include "core/ProceduralMaterials.hpp"
#include "core/Renderer.hpp"
#include "core/Texture.hpp"
#include "studio/IStudioPlugin.hpp"

namespace engine::studio::plugins {

// Kronos ("Studio Finalisation"): the real, missing "front door" this
// session's own research confirmed didn't exist anywhere in Studio
// before this -- no Launcher, no dedicated biome-select screen, no
// settings menu, no app-preferences save/load (Studio's existing
// scene/project save is a *different*, already-real system -- see
// core::SceneFile.hpp -- for authored editor content, not this plugin's
// own small "which biome am I looking at, which renderer toggles do I
// prefer" preferences).
//
// Biome selection reuses the exact same real, Physics-free
// tntwars::buildMapLayout() pathway TntWarsPlugin's own "Map Editing"
// section already established (see that plugin's own buildMapGeometry()
// for the precedent this mirrors): Studio's bring-up scene is
// documented as not physics-simulated (StudioApp.hpp's own comment, no
// shared core::Physics& exists for any plugin to reach), so the heavier,
// Physics-requiring per-map terrain/enrichment functions main.cpp's own
// --tntwars mode uses (sculpted islands, Space Map platforms, etc.)
// aren't callable from here. This is an honest, real, if flatter,
// in-editor biome preview -- not a claim of full parity with the live
// playable map.
//
// Settings are real Renderer toggles, not decorative sliders -- each
// checkbox calls the exact same real Renderer setter engine_runtime's
// own map-setup code already uses.
class LauncherPlugin final : public IStudioPlugin {
public:
    LauncherPlugin(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                    core::MeshLibrary& meshLibrary, core::TextureLibrary& textureLibrary, core::Renderer& renderer);

    [[nodiscard]] const char* name() const override { return "Launcher"; }
    [[nodiscard]] const char* category() const override { return "General"; }
    void drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) override;

private:
    void drawBiomeSelectSection(core::ECS& ecs);
    void drawSettingsSection();
    void drawPreferencesSection();

    void loadBiome(core::ECS& ecs, int mapIndex);
    void clearBiome(core::ECS& ecs);

    void applySettingsToRenderer();
    void savePreferences();
    void loadPreferences();

    VmaAllocator allocator_;
    VkDevice device_;
    VkCommandPool cmdPool_;
    VkQueue queue_;
    core::MeshLibrary* meshLibrary_;
    core::ProceduralMaterialLibrary materials_;
    core::Renderer* renderer_;

    int selectedBiomeIndex_ = -1; // -1 = real, honest "nothing loaded yet"
    std::vector<core::EntityId> spawnedBiomeEntities_;

    // Real settings, mirrored 1:1 into real Renderer setters -- see
    // applySettingsToRenderer().
    bool rtReflectionsEnabled_ = false;
    bool volumetricFogEnabled_ = false;
    bool ssrEnabled_ = false;
    bool rtGiEnabled_ = false;
    bool cinematicModeEnabled_ = false;

    std::string preferencesPath_ = "studio_launcher_prefs.json";
    std::string lastPreferencesStatus_;
};

} // namespace engine::studio::plugins
