#pragma once

#include <vector>

#include "studio/IStudioPlugin.hpp"

namespace engine::studio::plugins {

// A common Studio utility-plugin category -- align selected parts to the
// primary selection, distribute them evenly along an axis, snap to grid.
// Original implementation (no third-party plugin's code, UI, or name
// reused -- see AnimatorPlugin.hpp's header note on why that matters
// here). Operates over ExplorerPanel's multi-selection, which is why that
// panel grew ctrl/shift-click support alongside this plugin.
class AlignPlugin final : public IStudioPlugin {
public:
    [[nodiscard]] const char* name() const override { return "Align & Distribute"; }
    [[nodiscard]] const char* category() const override { return "Utility"; }

    void drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) override;

private:
    // axis: 0=X, 1=Y, 2=Z.
    static void alignAxis(core::ECS& ecs, const std::vector<core::EntityId>& selection, core::EntityId primary, int axis);
    static void distributeAxis(core::ECS& ecs, const std::vector<core::EntityId>& selection, int axis);
    void snapToGrid(core::ECS& ecs, const std::vector<core::EntityId>& selection) const;

    float gridSize_ = 1.0f;
};

} // namespace engine::studio::plugins
