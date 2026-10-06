#pragma once

#include <functional>
#include <string>
#include <vector>

#include "core/Camera.hpp"
#include "core/WorldStreaming.hpp"
#include "studio/IStudioPlugin.hpp"

namespace engine::studio::plugins {

// Splits a scene into streamed cells and shows which ones are loaded
// around the editor camera.
class WorldStreamingPlugin final : public IStudioPlugin {
public:
    using CreateWorld = std::function<bool(const core::WorldManifest& settings, std::string& error)>;

    WorldStreamingPlugin(core::WorldStreamer& streamer, const core::Camera& camera, CreateWorld createWorld,
                         std::function<void()> markDirty)
        : streamer_(&streamer), camera_(&camera), createWorld_(std::move(createWorld)), markDirty_(std::move(markDirty)) {}

    [[nodiscard]] const char* name() const override { return "World Streaming"; }
    [[nodiscard]] const char* category() const override { return "World"; }

    void drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) override;

private:
    void drawMap();

    core::WorldStreamer* streamer_;
    const core::Camera* camera_;
    CreateWorld createWorld_;
    std::function<void()> markDirty_;
    core::WorldManifest settings_;
    std::string status_;
    float mapZoom_ = 1.0f;
};

} // namespace engine::studio::plugins
