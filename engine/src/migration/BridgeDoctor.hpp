#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "migration/CompatibilityScore.hpp"

// Bridge Doctor: turns an import + compatibility run into a report a creator
// can act on (docs/BRIDGE_DOCTOR.md).
namespace engine::migration {

enum class DoctorCategory : uint8_t {
    Blocker = 0,      // a script can't run (compile/runtime error) or the import failed
    GameplayRisk = 1, // something the game relies on is missing or behaves differently
    VisualAudio = 2,  // looks or sounds different (assets, effects)
    Info = 3,
};

struct DoctorFinding {
    DoctorCategory category = DoctorCategory::Info;
    std::string code;    // stable id, e.g. "missing-api"
    std::string subject; // instance path or class name
    std::string script;  // script path, when the finding is in a script
    int line = 0;        // 1-based; 0 when the engine gave no line
    size_t count = 1;    // how many times it occurs
    std::string message;
    std::string why;
    std::string help; // supported alternative or doc page
};

struct DoctorPlace {
    std::string file;
    std::string format; // "rbxlx (XML)", "rbxl/rbxm (binary)" or "unknown"
    bool parsed = false;
    bool blocked = false;
    bool scriptsRan = false;
    CompatibilityScore score;
    std::vector<DoctorFinding> findings; // sorted: category, code, script/subject, line

    [[nodiscard]] size_t countOf(DoctorCategory category) const;
};

struct DoctorReport {
    std::string kronosVersion;
    std::vector<DoctorPlace> places;
};

[[nodiscard]] const char* doctorCategoryName(DoctorCategory category);
[[nodiscard]] std::string detectPlaceFormat(const std::string& bytes);

// Imports, scores, runs the scripts (and checks, if given) and classifies.
[[nodiscard]] DoctorPlace diagnosePlace(const std::string& fileName, const std::string& bytes,
                                        const std::string& checks = {});
// The classification step alone, for results that already exist.
[[nodiscard]] std::vector<DoctorFinding> classifyFindings(const ImportReport& report, const CompatibilityScore& score);

// Both are byte-for-byte stable for the same input (no times or paths
// beyond the given file names).
[[nodiscard]] std::string doctorReportJson(const DoctorReport& report);
[[nodiscard]] std::string doctorReportMarkdown(const DoctorReport& report);

// False with a reason when the file can't be written.
bool writeDoctorFile(const std::string& path, const std::string& text, std::string& error);

// Shared with Validate Project.
[[nodiscard]] std::string doctorJsonEscape(const std::string& text);
[[nodiscard]] std::string doctorMarkdownCell(const std::string& text);
[[nodiscard]] int doctorLineFromError(const std::string& error, const std::string& script);

[[nodiscard]] const std::vector<std::string>& doctorChecksPerformed();
[[nodiscard]] const std::vector<std::string>& doctorKnownLimits();

} // namespace engine::migration
