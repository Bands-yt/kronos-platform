#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "migration/CompatibilityScore.hpp"

// Compatibility Lab: plays an imported place headlessly with physics and
// several players for a while and records what went wrong (docs/COMPAT_LAB.md).
namespace engine::migration {

struct LabOptions {
    double seconds = 30.0; // game time
    int players = 2;
    float dt = 1.0f / 30.0f;
    double checksAfter = 2.0; // when the checks file starts, in game seconds
    std::vector<std::filesystem::path> imageRoots; // where GUI images are looked up (the place's folder)
};

struct LabError {
    double time = 0.0; // first time it happened, in game seconds
    std::string script;
    int line = 0;
    std::string message;
    size_t count = 1;
};

struct LabPlayer {
    std::string name;
    bool character = false; // had a character with a Humanoid at the end
    int deaths = 0;
    int fellOut = 0; // dropped below workspace.FallenPartsDestroyHeight
};

struct LabResult {
    bool ran = false;
    double seconds = 0.0;
    std::vector<LabError> errors;
    std::vector<LabPlayer> players;
    std::vector<std::string> fallenParts; // unanchored parts below FallenPartsDestroyHeight at the end
    std::vector<std::string> missingImages; // GUI Image values with no file (gui::imageFile)
    size_t touches = 0;                   // Touched events from physics contacts
    size_t instancesStart = 0;
    size_t instancesEnd = 0;
    double slowestFrameMs = 0.0;
    bool behaviourRan = false;
    std::vector<CompatBehaviourCheck> behaviour;

    [[nodiscard]] size_t behaviourPassed() const;
};

// checks: the place's .checks.luau. Besides check/touch/player/character/hit
// it gets `players` (every test player) and `seconds` (the run length).
[[nodiscard]] LabResult runLab(const ImportReport& report, const LabOptions& options, const std::string& checks = {});

struct LabPlace {
    std::string file;
    bool imported = false;
    bool crashed = false;
    std::string crash; // "signal 11", "exit code 3", "timed out", ...
    double score = 0.0; // Bridge Doctor's overall percent
    size_t classesSupported = 0;
    size_t classesTotal = 0;
    LabResult result;
};

// Imports the bytes, scores them and runs the lab, in this process.
[[nodiscard]] LabPlace labPlace(const std::string& fileName, const std::string& bytes, const LabOptions& options,
                                const std::string& checks = {});

[[nodiscard]] std::string labReportJson(const std::vector<LabPlace>& places, const LabOptions& options);
[[nodiscard]] std::string labReportMarkdown(const std::vector<LabPlace>& places, const LabOptions& options);
bool readLabReport(const std::string& json, std::vector<LabPlace>& places, std::string& error);

struct LabChange {
    std::string file;
    bool worse = false;
    std::string what;
};
// What changed between an earlier report and this one, worse first.
[[nodiscard]] std::vector<LabChange> compareLab(const std::vector<LabPlace>& baseline, const std::vector<LabPlace>& now);

} // namespace engine::migration
