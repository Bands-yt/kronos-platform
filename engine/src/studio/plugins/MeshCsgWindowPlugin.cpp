#include "studio/plugins/MeshCsgWindowPlugin.hpp"

#include <algorithm>
#include <cstdio>

#include "core/Components.hpp"

namespace engine::studio::plugins {

namespace {
constexpr float kOrbitSensitivityDegreesPerPixel = 0.3f;
constexpr float kZoomSensitivityPerNotch = 0.5f;
constexpr float kMinOrbitDistance = 1.0f;
constexpr float kMaxOrbitDistance = 50.0f;
constexpr float kMinPitchDegrees = -89.0f;
constexpr float kMaxPitchDegrees = 89.0f;
} // namespace

void MeshCsgWindowPlugin::onOpen(core::Renderer& renderer) {
    core::Window::CreateInfo info;
    info.title = "3D Mesh & CSG Editor";
    info.width = 960;
    info.height = 640;
    info.resizable = true;
    if (!viewport_.initialize(renderer, info)) {
        std::fprintf(stderr, "MeshCsgWindowPlugin: failed to open its standalone window -- closing.\n");
        requestClose(); // picked up by KronosPluginHost::tick() next frame; onClose()/viewport_.shutdown() tolerate a partially-initialized viewport
    }
}

void MeshCsgWindowPlugin::onClose(core::Renderer& renderer) {
    viewport_.shutdown(renderer);
}

void MeshCsgWindowPlugin::tick(float dt, core::ECS& ecs, core::EntityId selected) {
    (void)dt;
    if (selected != core::kNullEntity) {
        if (const auto* transform = ecs.tryGetComponent<core::Transform>(selected)) {
            orbitTarget_ = transform->position;
        }
    }

    camera_.yawDegrees = orbitYawDegrees_;
    camera_.pitchDegrees = orbitPitchDegrees_;
    camera_.position = orbitTarget_ - camera_.forward() * orbitDistance_;

    if (viewport_.closeRequested()) requestClose();
}

void MeshCsgWindowPlugin::renderFrame(core::Renderer& renderer, core::ECS& ecs, core::EntityId selected) {
    (void)selected;
    viewport_.renderFrame(renderer, ecs, *meshLibrary_, *textureLibrary_, camera_);
}

void MeshCsgWindowPlugin::handleEvent(const SDL_Event& event) {
    viewport_.handleEvent(event);
    if (!isOpen()) return;

    const uint32_t myWindowId = viewport_.windowId();
    if (myWindowId == 0) return;

    switch (event.type) {
        case SDL_MOUSEMOTION:
            if (event.motion.windowID == myWindowId && (event.motion.state & SDL_BUTTON_LMASK) != 0) {
                orbitYawDegrees_ += static_cast<float>(event.motion.xrel) * kOrbitSensitivityDegreesPerPixel;
                orbitPitchDegrees_ = std::clamp(
                    orbitPitchDegrees_ - static_cast<float>(event.motion.yrel) * kOrbitSensitivityDegreesPerPixel,
                    kMinPitchDegrees, kMaxPitchDegrees);
            }
            break;
        case SDL_MOUSEWHEEL:
            if (event.wheel.windowID == myWindowId) {
                orbitDistance_ = std::clamp(orbitDistance_ - static_cast<float>(event.wheel.y) * kZoomSensitivityPerNotch,
                                             kMinOrbitDistance, kMaxOrbitDistance);
            }
            break;
        default:
            break;
    }
}

} // namespace engine::studio::plugins
