#pragma once

#include "core/GameMode.hpp"
#include "trailer/RenderShowcase.hpp"

namespace engine::trailer {

// The Render Showcase: a scripted camera flight through the showcase world.
class RenderShowcaseGameMode final : public core::GameMode {
public:
    explicit RenderShowcaseGameMode(ShowcaseCameraPath path) : path_(std::move(path)) {}

    [[nodiscard]] bool controlsCamera() const override { return true; }
    void tickCamera(core::GameModeContext& ctx, float dt) override;

private:
    ShowcaseCameraPath path_;
    float elapsedSeconds_ = 0.0f;
};

} // namespace engine::trailer
