// kronos_compat: imports .rbxlx/.rbxl places and prints how much of each Kronos can run.
//   kronos_compat [--min PERCENT] [file.rbxlx | file.rbxl | folder]...
// With no paths it scores tests/compat_corpus. --min makes it exit 1 when the
// average overall score is below PERCENT.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "migration/BridgeDoctor.hpp"
#include "migration/CompatibilityScore.hpp"

namespace fs = std::filesystem;
using namespace engine::migration;

namespace {

std::vector<std::pair<std::string, size_t>> topEntries(const std::map<std::string, size_t>& counts, size_t limit) {
    std::vector<std::pair<std::string, size_t>> sorted(counts.begin(), counts.end());
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
        return a.second != b.second ? a.second > b.second : a.first < b.first;
    });
    if (sorted.size() > limit) sorted.resize(limit);
    return sorted;
}

} // namespace

int main(int argc, char** argv) {
    double minimum = -1.0;
    std::string jsonPath;
    std::string markdownPath;
    std::vector<fs::path> inputs;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--min" && i + 1 < argc) {
            minimum = std::atof(argv[++i]);
        } else if (arg == "--report-json" && i + 1 < argc) {
            jsonPath = argv[++i];
        } else if (arg == "--report-md" && i + 1 < argc) {
            markdownPath = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::printf("usage: kronos_compat [--min PERCENT] [--report-json FILE] [--report-md FILE] "
                        "[file.rbxlx | file.rbxl | folder]...\n"
                        "A place's behaviour checks are read from <place>.checks.luau next to it.\n");
            return 0;
        } else {
            inputs.emplace_back(arg);
        }
    }
    if (inputs.empty()) inputs.emplace_back("tests/compat_corpus");

    std::vector<fs::path> places;
    for (const fs::path& input : inputs) {
        if (fs::is_directory(input)) {
            for (const auto& entry : fs::directory_iterator(input)) {
                const auto ext = entry.path().extension();
                if (ext == ".rbxlx" || ext == ".rbxl") places.push_back(entry.path());
            }
        } else {
            places.push_back(input);
        }
    }
    std::sort(places.begin(), places.end());
    if (places.empty()) {
        std::fprintf(stderr, "kronos_compat: no .rbxlx/.rbxl places found\n");
        return 2;
    }

    DoctorReport doctor;
    CompatibilityScore total;
    size_t checksPassed = 0;
    size_t checksRun = 0;
    double overallSum = 0.0;
    size_t scored = 0;

    for (const fs::path& place : places) {
        std::ifstream file(place, std::ios::binary);
        if (!file) {
            std::printf("%s: cannot open\n", place.string().c_str());
            continue;
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string checks;
        fs::path checksPath = place;
        checksPath.replace_extension(".checks.luau");
        if (std::ifstream checksFile(checksPath); checksFile) {
            std::stringstream text;
            text << checksFile.rdbuf();
            checks = text.str();
        }
        doctor.places.push_back(diagnosePlace(place.filename().string(), buffer.str(), checks));
        const DoctorPlace& diagnosed = doctor.places.back();
        if (!diagnosed.parsed || diagnosed.blocked) {
            std::printf("%s: %s\n", place.filename().string().c_str(), diagnosed.blocked ? "blocked" : "did not parse");
            continue;
        }

        const CompatibilityScore& score = diagnosed.score;
        std::printf("%s: %s\n", place.filename().string().c_str(), score.summary().c_str());
        for (const CompatScriptRun& run : score.scripts) {
            if (!run.started) std::printf("    not started   %s (Roblox wouldn't run it here)\n", run.path.c_str());
            if (!run.ok) std::printf("    script error  %s\n", run.error.c_str());
        }
        if (score.behaviourRan) {
            size_t passed = 0;
            for (const CompatBehaviourCheck& check : score.behaviour) {
                passed += check.passed ? 1 : 0;
                if (!check.passed) {
                    std::printf("    check failed  %s %s\n", check.name.c_str(), check.detail.c_str());
                }
            }
            std::printf("    behaviour checks %zu/%zu passed\n", passed, score.behaviour.size());
            checksPassed += passed;
            checksRun += score.behaviour.size();
        }

        overallSum += score.overallPercent();
        ++scored;
        total.instances += score.instances;
        total.instancesMapped += score.instancesMapped;
        total.apiUses += score.apiUses;
        total.apiSupported += score.apiSupported;
        total.scriptsRun += score.scriptsRun;
        total.scriptsOk += score.scriptsOk;
        for (const auto& [name, count] : score.missingApis) total.missingApis[name] += count;
        for (const auto& [name, count] : score.unmappedClasses) total.unmappedClasses[name] += count;
    }

    int writeFailures = 0;
    auto writeReport = [&](const std::string& path, const std::string& text) {
        if (path.empty()) return;
        std::string error;
        if (writeDoctorFile(path, text, error)) {
            std::printf("Bridge Doctor report written to %s\n", path.c_str());
        } else {
            std::fprintf(stderr, "kronos_compat: %s\n", error.c_str());
            ++writeFailures;
        }
    };
    if (!jsonPath.empty()) writeReport(jsonPath, doctorReportJson(doctor));
    if (!markdownPath.empty()) writeReport(markdownPath, doctorReportMarkdown(doctor));
    if (writeFailures > 0) return 3;
    if (scored == 0) return 2;
    const double average = overallSum / static_cast<double>(scored);
    std::printf("\nAll places (%zu): %s\n", scored, total.summary().c_str());
    std::printf("Average score: %.1f%%\n", average);
    if (checksRun > 0) std::printf("Behaviour checks: %zu/%zu passed (not part of the score)\n", checksPassed, checksRun);

    std::printf("\nMost-used missing APIs:\n");
    for (const auto& [name, count] : topEntries(total.missingApis, 15)) std::printf("  %-28s %zu\n", name.c_str(), count);
    std::printf("\nMost common unmapped classes:\n");
    for (const auto& [name, count] : topEntries(total.unmappedClasses, 10)) {
        std::printf("  %-28s %zu\n", name.c_str(), count);
    }

    if (minimum >= 0.0 && average < minimum) {
        std::printf("\nBelow the --min of %.1f%%\n", minimum);
        return 1;
    }
    return 0;
}
