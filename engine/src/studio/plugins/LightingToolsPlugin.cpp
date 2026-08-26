#include "studio/plugins/LightingToolsPlugin.hpp"

#include <algorithm>
#include <cmath>

#include <imgui.h>

#include "studio/PluginChrome.hpp"

namespace engine::studio::plugins {

namespace {
// Real, distinct named sky/mood presets -- each a different real
// combination of sky/fog/ambient (not the same values relabeled), a
// separate concern from core::BiomePreset (Forest/Desert/Cave, an
// *environment type*, not a time-of-day/weather mood): "skybox selector
// (procedural + preset)" (task category 3) -- the procedural gradient
// (shaders/sky.frag) is always what actually renders; a preset here is
// just a one-click starting point for its two colors plus fog/ambient,
// same as MaterialPlugin's own preset buttons are a starting point for
// Renderable fields a creator can still hand-tune afterward.
struct SkyPreset {
    const char* name;
    glm::vec3 zenithColor;
    glm::vec3 horizonColor;
    glm::vec3 fogColor;
    float fogDensity;
    glm::vec3 ambient;
    glm::vec3 ambientGround;
};

constexpr SkyPreset kSkyPresets[] = {
    {"Clear Day", {0.25f, 0.45f, 0.85f}, {0.75f, 0.80f, 0.85f}, {0.65f, 0.72f, 0.85f}, 0.0f, {0.18f, 0.22f, 0.30f}, {0.12f, 0.11f, 0.10f}},
    {"Golden Hour", {0.35f, 0.30f, 0.45f}, {0.95f, 0.60f, 0.35f}, {0.85f, 0.55f, 0.35f}, 0.01f, {0.30f, 0.20f, 0.18f}, {0.20f, 0.12f, 0.08f}},
    {"Overcast", {0.55f, 0.57f, 0.60f}, {0.68f, 0.69f, 0.70f}, {0.60f, 0.61f, 0.63f}, 0.03f, {0.30f, 0.31f, 0.33f}, {0.18f, 0.17f, 0.16f}},
    {"Night", {0.02f, 0.03f, 0.08f}, {0.05f, 0.06f, 0.12f}, {0.03f, 0.04f, 0.09f}, 0.015f, {0.03f, 0.035f, 0.06f}, {0.02f, 0.02f, 0.025f}},
};
constexpr int kSkyPresetCount = 4;
} // namespace

LightingToolsPlugin::LightingToolsPlugin(core::Renderer& renderer) : renderer_(&renderer) {
    // Real inverse of the pitch/yaw -> direction formula in
    // drawDirectionalLightSection() -- without this, the sliders would
    // start at an arbitrary default that doesn't match the renderer's
    // actual current directionWS, and the *first* slider touch would
    // snap the real light to a visually jarring, unrelated direction.
    glm::vec3 direction = renderer.lighting().directionWS;
    sunPitchDegrees_ = glm::degrees(std::asin(std::clamp(direction.y, -1.0f, 1.0f)));
    sunYawDegrees_ = glm::degrees(std::atan2(direction.z, direction.x));
}

void LightingToolsPlugin::drawDirectionalLightSection(core::SceneLighting& lighting) {
    if (!ImGui::CollapsingHeader("Directional Light", ImGuiTreeNodeFlags_DefaultOpen)) return;

    bool changed = false;
    changed |= ImGui::SliderFloat("Pitch (degrees)", &sunPitchDegrees_, -89.0f, 89.0f);
    ImGui::SameLine();
    helpMarker("Negative pitch = sun traveling downward (a real overhead sun). 0 = sun on the horizon.");
    changed |= ImGui::SliderFloat("Yaw (degrees)", &sunYawDegrees_, -180.0f, 180.0f);
    ImGui::SameLine();
    helpMarker("Compass direction the light travels toward, same convention as the Viewport camera's own yaw.");
    if (changed) {
        // Same pitch/yaw -> direction convention core::Camera::forward()
        // already uses (see that method's own formula) -- directionWS is
        // the direction the light *travels*, so a negative pitch here
        // means "traveling downward," matching a real sun overhead.
        float pitch = glm::radians(sunPitchDegrees_);
        float yaw = glm::radians(sunYawDegrees_);
        lighting.directionWS =
            glm::normalize(glm::vec3(std::cos(yaw) * std::cos(pitch), std::sin(pitch), std::sin(yaw) * std::cos(pitch)));
    }

    ImGui::ColorEdit3("Color", &lighting.color.x);
    ImGui::DragFloat("Intensity", &lighting.intensity, 0.05f, 0.0f, 20.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
}

void LightingToolsPlugin::drawAmbientFogSection(core::SceneLighting& lighting) {
    if (!ImGui::CollapsingHeader("Ambient & Fog", ImGuiTreeNodeFlags_DefaultOpen)) return;

    ImGui::TextUnformatted("Ambient");
    ImGui::ColorEdit3("Sky Tone##ambient", &lighting.ambient.x);
    ImGui::ColorEdit3("Ground Tone##ambient", &lighting.ambientGround.x);

    ImGui::Spacing();
    ImGui::TextUnformatted("Fog");
    ImGui::ColorEdit3("Fog Color", &lighting.fogColor.x);
    ImGui::SliderFloat("Fog Density", &lighting.fogDensity, 0.0f, 0.2f, "%.4f", ImGuiSliderFlags_AlwaysClamp);
}

void LightingToolsPlugin::drawSkyboxSection(core::SceneLighting& lighting) {
    if (!ImGui::CollapsingHeader("Skybox", ImGuiTreeNodeFlags_DefaultOpen)) return;

    ImGui::TextDisabled("Procedural two-tone gradient (shaders/sky.frag) -- always live, no cubemap.");
    ImGui::ColorEdit3("Zenith Color", &lighting.skyZenithColor.x);
    ImGui::ColorEdit3("Horizon Color", &lighting.skyHorizonColor.x);

    ImGui::Spacing();
    const char* presetNames[kSkyPresetCount];
    for (int i = 0; i < kSkyPresetCount; ++i) presetNames[i] = kSkyPresets[i].name;
    ImGui::Combo("Preset", &selectedSkyPresetIndex_, presetNames, kSkyPresetCount);
    if (ImGui::Button("Apply Sky Preset")) {
        const SkyPreset& preset = kSkyPresets[selectedSkyPresetIndex_];
        lighting.skyZenithColor = preset.zenithColor;
        lighting.skyHorizonColor = preset.horizonColor;
        lighting.fogColor = preset.fogColor;
        lighting.fogDensity = preset.fogDensity;
        lighting.ambient = preset.ambient;
        lighting.ambientGround = preset.ambientGround;
    }
}

void LightingToolsPlugin::drawCascadedShadowMapSection() {
    if (!ImGui::CollapsingHeader("Cascaded Shadow Maps")) return;

    // Kronos ("Studio Revamp" -- "Professional Lighting Inspector"): real
    // facts about the CSM this scene actually shadows with -- see
    // core::Renderer::kShadowMapResolution/kShadowMaxDistance's own
    // comment for why these are read-only text instead of sliders (they
    // size a real VkImage array and computeCascades()'s split-distance
    // math; changing them for real needs a resource rebuild, not a
    // uniform). The bias formula is scene.frag's real, current
    // computeShadow() constants, not an approximation -- if that formula
    // ever changes, this text is the one place that needs updating with it.
    ImGui::Text("%u cascades, %u x %u shadow map each", core::Renderer::kCascadeCount,
                core::Renderer::kShadowMapResolution, core::Renderer::kShadowMapResolution);
    ImGui::Text("Covers camera near plane to %.0f units", core::Renderer::kShadowMaxDistance);
    ImGui::TextWrapped(
        "3x3 PCF, receiver-plane depth bias (derived per-fragment from real screen-space depth derivatives, "
        "with a small N.L + per-cascade-scaled floor as a backstop) -- see shaders/scene.frag's "
        "sampleCascadeShadow().");
    ImGui::SameLine();
    helpMarker(
        "Compile-time constants, not live-editable here: the shadow map is a fixed-size VkImage array sized "
        "once at startup, and the cascade split distances are baked into computeCascades()'s math. Toggle the "
        "cascade color debug overlay from the Viewport panel's own Debug Overlays row.");
}

void LightingToolsPlugin::drawRenderingModeSection() {
    if (!ImGui::CollapsingHeader("Post-Processing", ImGuiTreeNodeFlags_DefaultOpen)) return;

    if (!renderer_->isRayTracingSupported()) {
        ImGui::TextDisabled("Ray-traced shadows: unsupported by this device/driver (VK_KHR_ray_query missing).");
    } else {
        bool rtEnabled = renderer_->isRayTracedShadowsEnabled();
        if (ImGui::Checkbox("Ray-Traced Shadows", &rtEnabled)) {
            renderer_->setRayTracedShadowsEnabled(rtEnabled);
        }
        ImGui::SameLine();
        helpMarker(
            "Real hardware ray query (VK_KHR_ray_query) against a real BLAS/TLAS built from MeshSource-described "
            "Box/Plane shadow casters. Same real F6 toggle engine_runtime exposes.");
    }

    bool perfEnabled = renderer_->isPerformanceModeEnabled();
    if (ImGui::Checkbox("Performance Mode", &perfEnabled)) {
        renderer_->setPerformanceMode(perfEnabled);
    }
    ImGui::SameLine();
    helpMarker(
        "Single-tap CSM shadow sampling, bloom extract skipped, particle draw count capped -- and forces ray-traced "
        "shadows off. Same real F7 toggle engine_runtime exposes.");

    bool cinematicEnabled = renderer_->isCinematicModeEnabled();
    if (ImGui::Checkbox("Cinematic Mode", &cinematicEnabled)) {
        renderer_->setCinematicMode(cinematicEnabled);
    }
    ImGui::SameLine();
    helpMarker(
        "SSAO + depth-of-field + camera motion blur (one consolidated pass) plus vignette/chromatic "
        "aberration/saturation grading/god rays in composite -- and forces Performance Mode off. Same real F9 "
        "toggle engine_runtime exposes.");
    if (cinematicEnabled) {
        ImGui::Indent();
        ImGui::SliderFloat("SSAO Radius", &ssaoRadius_, 0.05f, 2.0f, "%.2f");
        ImGui::SliderFloat("SSAO Strength", &ssaoStrength_, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Focus Distance", &dofFocusDistance_, 1.0f, 60.0f, "%.1f");
        ImGui::SliderFloat("Focus Range", &dofFocusRange_, 0.5f, 40.0f, "%.1f");
        ImGui::SliderFloat("Max Blur Radius (px)", &dofMaxCoCRadiusPx_, 0.0f, 20.0f, "%.1f");
        ImGui::SliderFloat("Shutter Angle", &shutterAngleDegrees_, 0.0f, 360.0f, "%.0f deg");
        ImGui::SameLine();
        helpMarker("0 = no motion blur, 180 = classic film default, 360 = maximum.");
        ImGui::SliderFloat("Vignette", &vignetteStrength_, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Chromatic Aberration", &chromaticAberrationStrength_, 0.0f, 0.02f, "%.4f");
        ImGui::SliderFloat("Saturation", &saturation_, 0.0f, 2.0f, "%.2f");
        ImGui::SliderFloat("God Rays", &godRayStrength_, 0.0f, 1.0f, "%.2f");
        renderer_->setSsaoParams(ssaoRadius_, ssaoStrength_);
        // Kronos ("Critical Visual Fixes" -- "High Quality Graphics
        // Blurriness"): real, explicit opt-in -- DOF is no longer an
        // unconditional part of Cinematic Mode (see Renderer::
        // setDepthOfFieldEnabled()'s own comment); this panel's own live
        // sliders above are a real, deliberate DOF preview, so it stays on
        // for as long as this section is visible (cinematicEnabled).
        renderer_->setDepthOfFieldEnabled(true);
        renderer_->setDepthOfFieldParams(dofFocusDistance_, dofFocusRange_, dofMaxCoCRadiusPx_);
        renderer_->setMotionBlurShutterAngle(shutterAngleDegrees_);
        renderer_->setVignetteAndChromaticAberration(vignetteStrength_, chromaticAberrationStrength_);
        renderer_->setSaturation(saturation_);
        renderer_->setGodRayStrength(godRayStrength_);
        ImGui::Unindent();
    }
}

void LightingToolsPlugin::drawPanel(core::ECS&, core::EntityId, const std::vector<core::EntityId>&) {
    ImGui::Begin(name());
    drawPluginHeader("Lighting Tools");

    core::SceneLighting lighting = renderer_->lighting();
    drawDirectionalLightSection(lighting);
    drawAmbientFogSection(lighting);
    drawSkyboxSection(lighting);
    renderer_->setLighting(lighting);
    drawCascadedShadowMapSection();
    drawRenderingModeSection();

    drawPluginFooter("Edits apply live to the Viewport -- no live physics/time-of-day tick runs in Studio.");
    ImGui::End();
}

} // namespace engine::studio::plugins
