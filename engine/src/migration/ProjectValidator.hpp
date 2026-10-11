#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/ECS.hpp"
#include "migration/BridgeDoctor.hpp"

// Validate Project: checks the open scene in Studio (docs/VALIDATE_PROJECT.md).
namespace engine::migration {

enum class CheckStatus : uint8_t { Pass, Fail, Warning, NotRun, Unavailable, Inconclusive };

struct ProjectCheck {
    std::string id;     // stable, e.g. "scripts-compile"
    std::string title;
    CheckStatus status = CheckStatus::NotRun;
    std::string detail;
};

struct ProjectFinding {
    DoctorFinding finding;
    uint32_t entity = ~0u; // entt id of the object to select, ~0u when none
    std::string checkId;
    // code + script + subject (no line, so editing above it keeps the id).
    [[nodiscard]] std::string stableId() const;
};

struct ValidationInput {
    // Error lines from the last Play; nullopt when nothing was played.
    std::optional<std::vector<std::string>> lastPlayErrors;
    // Relative asset paths are looked up here, in order ("" = current folder).
    std::vector<std::string> assetRoots{""};
};

struct ValidationReport {
    std::string kronosVersion;
    size_t scripts = 0;
    size_t instances = 0;
    std::vector<ProjectCheck> checks;
    std::vector<ProjectFinding> findings; // sorted like Bridge Doctor's

    [[nodiscard]] CheckStatus overall() const;
    [[nodiscard]] const ProjectCheck* check(const std::string& id) const;
};

struct ValidationDiff {
    std::vector<std::string> added;    // stable ids new in this run
    std::vector<std::string> resolved; // stable ids gone since the previous run
    size_t unchanged = 0;
};

[[nodiscard]] const char* checkStatusName(CheckStatus status);

[[nodiscard]] ValidationReport validateProject(core::ECS& ecs, const ValidationInput& input = {});
[[nodiscard]] ValidationDiff compareValidations(const ValidationReport& previous, const ValidationReport& current);

// Byte-for-byte stable for the same scene.
[[nodiscard]] std::string validationJson(const ValidationReport& report);
[[nodiscard]] std::string validationMarkdown(const ValidationReport& report);

// Error lines ("runtime error in ..." / "compile error ...") from log text.
[[nodiscard]] bool isScriptErrorLine(const std::string& line);

} // namespace engine::migration
