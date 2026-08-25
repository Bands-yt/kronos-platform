#pragma once

#include <vector>

#include "core/Renderer.hpp"
#include "studio/IStudioPlugin.hpp"

namespace engine::studio::plugins {

// Sprint 9 ("Creator Tools Phase 1") task category 3: a real directional
// light editor, ambient/fog controls, and a skybox selector -- all direct,
// live edits to the same real core::SceneLighting the Viewport already
// renders from (core::Renderer::lighting()/setLighting()), so every
// change here is immediately visible, not a preview mockup. Deliberately
// a separate plugin from CreatorToolsPlugin's own one-click "Biome
// Lighting Preview" button (Sprint 7/World Systems) -- that one applies a
// whole real core::BiomePreset in one click; this one is the fuller,
// field-by-field editor for direct control, the same "one-click preset
// vs. full editor" split MaterialPlugin/InspectorPanel already have for
// Renderable properties.
class LightingToolsPlugin final : public IStudioPlugin {
public:
    explicit LightingToolsPlugin(core::Renderer& renderer);

    [[nodiscard]] const char* name() const override { return "Lighting Tools"; }
    [[nodiscard]] const char* category() const override { return "World"; }

    void drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) override;

private:
    void drawDirectionalLightSection(core::SceneLighting& lighting);
    void drawAmbientFogSection(core::SceneLighting& lighting);
    void drawSkyboxSection(core::SceneLighting& lighting);
    // Kronos ("Studio Revamp" -- "Professional Lighting Inspector"): real
    // info about the actual cascaded shadow map this scene renders with
    // -- see the .cpp for why this is read-only rather than sliders.
    void drawCascadedShadowMapSection();
    // Sprint 14 ("RTX Upgrade" Phase 2 / "Performance Mode"): the real
    // Studio-side counterpart to engine_runtime's F6/F7 keybinds -- both
    // real, live toggles on the same renderer_ this panel already edits.
    void drawRenderingModeSection();

    core::Renderer* renderer_;

    // Real pitch/yaw controls for directionWS -- a spherical
    // elevation/azimuth pair is a far more intuitive real UI than
    // dragging three raw XYZ components of a normalized vector (the same
    // reasoning core::TimeOfDay's own elevation/azimuth sun model already
    // uses), converted to/from directionWS on every edit.
    float sunPitchDegrees_ = 55.0f;
    float sunYawDegrees_ = 200.0f;

    int selectedSkyPresetIndex_ = 0;

    // Sprint 16 ("Cinematic Graphics") -- local mirrors of Renderer's own
    // cinematic tuning knobs, edited here and pushed live via the
    // matching setters every drawRenderingModeSection() call while the
    // section is expanded (same "local UI state, pushed to the real
    // owner every frame" pattern drawDirectionalLightSection() already
    // uses for lighting fields, just against Renderer's setters instead
    // of a SceneLighting& out-param). Defaults mirror Renderer's own
    // member-initializer defaults so the sliders don't visibly jump on
    // first open.
    float ssaoRadius_ = 0.5f;
    float ssaoStrength_ = 1.0f;
    float dofFocusDistance_ = 15.0f;
    float dofFocusRange_ = 10.0f;
    float dofMaxCoCRadiusPx_ = 6.0f;
    float shutterAngleDegrees_ = 0.0f;
    float vignetteStrength_ = 0.35f;
    float chromaticAberrationStrength_ = 0.0015f;
    float saturation_ = 1.05f;
    float godRayStrength_ = 0.15f;
};

} // namespace engine::studio::plugins
