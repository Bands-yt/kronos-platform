#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/ECS.hpp"
#include "migration/ProjectValidator.hpp"

namespace engine::studio::panels {

// File > Validate Project: runs migration::validateProject on the open scene.
// draw() returns the finding clicked this frame; StudioApp selects it.
class ValidateProjectPanel {
public:
    struct Jump {
        core::EntityId entity;
        int line = 0; // 0 when the finding has no line
    };

    void open() { open_ = runRequested_ = true; }
    // Call every frame; remembers the script errors logged during the last Play.
    void notePlayState(bool playing);
    [[nodiscard]] std::optional<Jump> draw(core::ECS& ecs, const std::vector<std::string>& assetRoots);

private:
    void run(core::ECS& ecs, const std::vector<std::string>& assetRoots);
    [[nodiscard]] std::vector<std::string> playErrorsSince(float mark) const;

    bool open_ = false;
    bool runRequested_ = false;
    bool playing_ = false;
    bool played_ = false;
    float playMark_ = -1.0f;
    std::vector<std::string> lastPlayErrors_;

    std::optional<migration::ValidationReport> report_;
    std::optional<migration::ValidationReport> previous_;
    std::vector<std::string> added_; // stable ids new since the previous run
    size_t resolved_ = 0;
    int categoryFilter_ = -1; // -1 = all
    char textFilter_[128] = "";
    char exportPath_[512] = "validate_report.md";
    std::string exportStatus_;
};

} // namespace engine::studio::panels
