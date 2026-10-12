#include "trailer/RenderShowcaseGameMode.hpp"

#include "core/Camera.hpp"
#include "core/Renderer.hpp"
#include "core/ScriptUiApi.hpp"
#include "core/ScriptUiLayoutApi.hpp"
#include "core/UIRenderer.hpp"
#include "core/PerformanceMetrics.hpp"

#include <cstdio>

namespace engine::trailer {

void RenderShowcaseGameMode::tickCamera(core::GameModeContext& ctx, float dt) {
    core::Camera& camera = ctx.camera;
    core::UIRenderer& ui = ctx.ui;
    core::Renderer& renderer = ctx.renderer;
    elapsedSeconds_ += dt;
    ShowcaseCameraSample sample = path_.sample(elapsedSeconds_);
    camera.position = sample.position;
    camera.yawDegrees = sample.yawDegrees;
    camera.pitchDegrees = sample.pitchDegrees;
    camera.rollDegrees = sample.rollDegrees;
    if (!ctx.reducedMotion) camera.verticalFovDegrees = sample.fovDegrees;

    using T = ShowcaseSceneTimes;
    float t = elapsedSeconds_;
    const char* sceneName = "Sunset Ray Tracing";
    const char* sceneDetail = "Warm light. Soft shadows. Real reflections. Atmospheric glow.";

    renderer.setDepthOfFieldParams(40.0f, 40.0f, 1.0f);
    renderer.setDepthOfFieldEnabled(true);
    renderer.setMotionBlurShutterAngle(0.0f);
    renderer.setVolumetricFogEnabled(false);
    renderer.setAtmosphereScatteringEnabled(t < T::kParticleStart);
    renderer.setAtmosphereScatteringParams(1.6f, 0.75f);

    if (t >= T::kEngineToolsStart) {
        sceneName = "Engine Tools";
        sceneDetail = "Live renderer settings. Real-time debugging overlays.";
    } else if (t >= T::kEnvironmentStart) {
        sceneName = "Environment Lighting";
        sceneDetail = "Dynamic shadows. Ambient bounce lighting.";
    } else if (t >= T::kCameraStart) {
        sceneName = "Camera System";
        sceneDetail = "Depth of field. Smooth dolly. Cinematic motion blur.";
        float rack = std::clamp((t - T::kCameraStart) / (T::kEnvironmentStart - T::kCameraStart), 0.0f, 1.0f);
        renderer.setDepthOfFieldParams(glm::mix(6.0f, 24.0f, rack), 6.0f, 9.0f);
        renderer.setMotionBlurShutterAngle(glm::mix(0.0f, 160.0f, std::sin(rack * 3.14159f)));
    } else if (t >= T::kParticleStart) {
        sceneName = "Particle & FX System";
        sceneDetail = "Sparks drifting. Smoke rolling. Fire blooming. Fully real-time.";
    } else if (t >= T::kMaterialStart) {
        sceneName = "Material Showcase";
        sceneDetail = "Metal. Real glass refraction. Water. Neon lighting.";
    }

    ui.beginFrame(VkExtent2D{ctx.windowWidth, ctx.windowHeight});
    ctx.scriptUi.flushInto(ui);
    ctx.scriptUiLayout.renderInto(ui, glm::vec2(static_cast<float>(ctx.windowWidth), static_cast<float>(ctx.windowHeight)));
    ui.drawText(sceneName, glm::vec2(24.0f, 24.0f), 1.3f, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
    ui.drawText(sceneDetail, glm::vec2(24.0f, 54.0f), 0.75f, glm::vec4(0.85f, 0.85f, 0.9f, 1.0f));

    if (t >= T::kEngineToolsStart) {
        float panelY = 96.0f;
        ui.drawRect(glm::vec2(24.0f, panelY), glm::vec2(360.0f, 150.0f), glm::vec4(0.0f, 0.0f, 0.0f, 0.55f));
        ui.drawText("RT Reflections   [ON]", glm::vec2(36.0f, panelY + 10.0f), 0.7f, glm::vec4(0.8f, 1.0f, 0.85f, 1.0f));
        ui.drawText("RT Shadows       [ON]", glm::vec2(36.0f, panelY + 32.0f), 0.7f, glm::vec4(0.8f, 1.0f, 0.85f, 1.0f));
        ui.drawText("Ray-Traced GI    [ON]", glm::vec2(36.0f, panelY + 54.0f), 0.7f, glm::vec4(0.8f, 1.0f, 0.85f, 1.0f));
        ui.drawText("Screen-Space Refl [ON]", glm::vec2(36.0f, panelY + 76.0f), 0.7f, glm::vec4(0.8f, 1.0f, 0.85f, 1.0f));
        char perfLine[128];
        std::snprintf(perfLine, sizeof(perfLine), "Frame %.2f ms (%.0f FPS) | Draws %u | Tris %llu",
                      ctx.performance.frameTimeMs, ctx.performance.fps,
                      ctx.performance.drawCalls,
                      static_cast<unsigned long long>(ctx.performance.triangleCount));
        ui.drawText(perfLine, glm::vec2(36.0f, panelY + 108.0f), 0.65f, glm::vec4(1.0f, 0.85f, 0.5f, 1.0f));
    }}

} // namespace engine::trailer
