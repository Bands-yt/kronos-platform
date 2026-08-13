#pragma once

#include <vector>

#include "studio/IStudioPlugin.hpp"

namespace engine::studio::plugins {
class TerrainEditorPlugin;
}

namespace engine::studio::plugins {

// Sprint 9 ("Creator Tools Phase 1") task category 5's Creator Console --
// real ImGui rendering only; the actual scan logic lives in
// studio::scanSceneForMessages() (CreatorConsole.hpp), pure and
// headlessly tested, re-run fresh every frame this panel is open (no
// stale cached list).
class CreatorConsolePlugin final : public IStudioPlugin {
public:
    explicit CreatorConsolePlugin(TerrainEditorPlugin& terrainEditor);

    [[nodiscard]] const char* name() const override { return "Creator Console"; }
    [[nodiscard]] const char* category() const override { return "World"; }

    void drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) override;

private:
    TerrainEditorPlugin* terrainEditor_;
    bool showTips_ = true;
    bool showWarnings_ = true;
    bool showErrors_ = true;
};

} // namespace engine::studio::plugins
