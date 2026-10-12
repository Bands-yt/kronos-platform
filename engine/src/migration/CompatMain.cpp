// kronos_compat: imports .rbxlx/.rbxl places and prints how much of each Kronos can run.
//   kronos_compat [--min PERCENT] [file.rbxlx | file.rbxl | folder]...
// With no paths it scores tests/compat_corpus. --min makes it exit 1 when the
// average overall score is below PERCENT. --lab plays each place instead (docs/COMPAT_LAB.md).
#include <algorithm>
#include <cstdio>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "core/ResourcePaths.hpp"
#include "migration/BridgeDoctor.hpp"
#include "migration/CompatLab.hpp"
#include "migration/CompatibilityScore.hpp"

#ifdef _WIN32
#include <process.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

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

std::string readFile(const fs::path& path, bool& ok) {
    std::ifstream file(path, std::ios::binary);
    ok = static_cast<bool>(file);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string checksFor(const fs::path& place) {
    fs::path checksPath = place;
    checksPath.replace_extension(".checks.luau");
    bool ok = false;
    std::string text = readFile(checksPath, ok);
    return ok ? text : std::string();
}

// Runs one place in a child kronos_compat so a crash or hang only loses that place.
LabPlace runIsolated(const fs::path& place, const LabOptions& options, const fs::path& out) {
    LabPlace result;
    result.file = place.filename().string();
    const std::string self = engine::core::siblingExecutable("kronos_compat");
    const std::string seconds = std::to_string(options.seconds);
    const std::string players = std::to_string(options.players);
    const std::string placeArg = place.string();
    const std::string outArg = out.string();
    std::error_code ec;
    fs::remove(out, ec);
    const char* args[] = {self.c_str(), "--lab-one", placeArg.c_str(), "--lab-out", outArg.c_str(),
                          "--seconds", seconds.c_str(), "--players", players.c_str(), nullptr};
#ifdef _WIN32
    const intptr_t code = _spawnv(_P_WAIT, self.c_str(), args);
    if (code != 0) {
        result.crashed = true;
        char text[64];
        std::snprintf(text, sizeof(text), "exit code 0x%llX", static_cast<unsigned long long>(code));
        result.crash = text;
        return result;
    }
#else
    const pid_t pid = fork();
    if (pid == 0) {
        alarm(static_cast<unsigned>(120 + options.seconds * 4));
        execv(self.c_str(), const_cast<char* const*>(args));
        _exit(127);
    }
    int status = 0;
    if (pid < 0 || waitpid(pid, &status, 0) < 0) {
        result.crashed = true;
        result.crash = "could not start";
        return result;
    }
    if (WIFSIGNALED(status)) {
        result.crashed = true;
        result.crash = WTERMSIG(status) == SIGALRM ? "timed out" : "signal " + std::to_string(WTERMSIG(status));
        return result;
    }
    if (WEXITSTATUS(status) != 0) {
        result.crashed = true;
        result.crash = "exit code " + std::to_string(WEXITSTATUS(status));
        return result;
    }
#endif
    bool ok = false;
    const std::string json = readFile(out, ok);
    std::vector<LabPlace> places;
    std::string error;
    if (!ok || !readLabReport(json, places, error) || places.size() != 1) {
        result.crashed = true;
        result.crash = "no report";
        return result;
    }
    fs::remove(out, ec);
    return places.front();
}

int labOne(const fs::path& place, const fs::path& out, const LabOptions& options) {
    bool ok = false;
    const std::string bytes = readFile(place, ok);
    if (!ok) return 2;
    LabOptions placeOptions = options;
    placeOptions.imageRoots = {fs::absolute(place).parent_path()};
    const LabPlace result = labPlace(place.filename().string(), bytes, placeOptions, checksFor(place));
    std::string error;
    return writeDoctorFile(out.string(), labReportJson({result}, options), error) ? 0 : 3;
}

int runLabMode(const std::vector<fs::path>& places, const LabOptions& options, bool isolate,
               const std::string& jsonPath, const std::string& markdownPath, const std::string& baselinePath) {
    std::vector<LabPlace> results;
    const fs::path scratch = fs::temp_directory_path() / ("kronos_lab_" + std::to_string(
#ifdef _WIN32
                                                              _getpid()
#else
                                                              getpid()
#endif
                                                              ) + ".json");
    for (const fs::path& place : places) {
        LabPlace result;
        if (isolate) {
            result = runIsolated(place, options, scratch);
        } else {
            bool ok = false;
            const std::string bytes = readFile(place, ok);
            LabOptions placeOptions = options;
            placeOptions.imageRoots = {fs::absolute(place).parent_path()};
            result = labPlace(place.filename().string(), bytes, placeOptions, checksFor(place));
        }
        const LabResult& r = result.result;
        if (result.crashed) {
            std::printf("%s: CRASHED (%s)\n", result.file.c_str(), result.crash.c_str());
        } else if (!result.imported) {
            std::printf("%s: did not import\n", result.file.c_str());
        } else {
            int deaths = 0;
            for (const LabPlayer& p : r.players) deaths += p.deaths;
            std::printf("%s: %.0f%%, played %.0f s, %zu errors, %zu touches, %d deaths, %zu parts fell out", result.file.c_str(),
                        result.score, r.seconds, r.errors.size(), r.touches, deaths, r.fallenParts.size());
            if (r.behaviourRan) std::printf(", checks %zu/%zu", r.behaviourPassed(), r.behaviour.size());
            std::printf("\n");
            for (const LabError& e : r.errors) {
                std::printf("    %.1f s  %s", e.time, e.script.c_str());
                if (e.line > 0) std::printf(":%d", e.line);
                std::printf("  %s%s\n", e.message.c_str(), e.count > 1 ? (" (x" + std::to_string(e.count) + ")").c_str() : "");
            }
            for (const CompatBehaviourCheck& c : r.behaviour) {
                if (!c.passed) std::printf("    check failed  %s %s\n", c.name.c_str(), c.detail.c_str());
            }
        }
        results.push_back(std::move(result));
    }

    int code = 0;
    std::string error;
    if (!jsonPath.empty() && !writeDoctorFile(jsonPath, labReportJson(results, options), error)) {
        std::fprintf(stderr, "kronos_compat: %s\n", error.c_str());
        code = 3;
    }
    if (!markdownPath.empty() && !writeDoctorFile(markdownPath, labReportMarkdown(results, options), error)) {
        std::fprintf(stderr, "kronos_compat: %s\n", error.c_str());
        code = 3;
    }
    if (!baselinePath.empty()) {
        bool ok = false;
        const std::string text = readFile(baselinePath, ok);
        std::vector<LabPlace> baseline;
        if (!ok || !readLabReport(text, baseline, error)) {
            std::fprintf(stderr, "kronos_compat: can't read the baseline %s\n", baselinePath.c_str());
            return 2;
        }
        const std::vector<LabChange> changes = compareLab(baseline, results);
        size_t worse = 0;
        std::printf("\nCompared with %s:\n", baselinePath.c_str());
        if (changes.empty()) std::printf("  no changes\n");
        for (const LabChange& change : changes) {
            worse += change.worse ? 1 : 0;
            std::printf("  %s %s: %s\n", change.worse ? "WORSE" : "better", change.file.c_str(), change.what.c_str());
        }
        if (worse > 0 && code == 0) code = 4;
    }
    for (const LabPlace& r : results) {
        if (r.crashed && code == 0) code = 5;
    }
    return code;
}

} // namespace

int main(int argc, char** argv) {
    double minimum = -1.0;
    std::string jsonPath;
    std::string markdownPath;
    std::vector<fs::path> inputs;
    bool lab = false;
    bool isolate = true;
    LabOptions labOptions;
    std::string labOneOut;
    std::string baselinePath;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--lab") {
            lab = true;
        } else if (arg == "--no-isolate") {
            isolate = false;
        } else if (arg == "--seconds" && i + 1 < argc) {
            labOptions.seconds = std::max(1.0, std::atof(argv[++i]));
        } else if (arg == "--players" && i + 1 < argc) {
            labOptions.players = std::clamp(std::atoi(argv[++i]), 0, 8);
        } else if (arg == "--baseline" && i + 1 < argc) {
            baselinePath = argv[++i];
        } else if (arg == "--lab-one" && i + 1 < argc) {
            inputs.emplace_back(argv[++i]);
        } else if (arg == "--lab-out" && i + 1 < argc) {
            labOneOut = argv[++i];
        } else if (arg == "--min" && i + 1 < argc) {
            minimum = std::atof(argv[++i]);
        } else if (arg == "--report-json" && i + 1 < argc) {
            jsonPath = argv[++i];
        } else if (arg == "--report-md" && i + 1 < argc) {
            markdownPath = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::printf("usage: kronos_compat [--min PERCENT] [--report-json FILE] [--report-md FILE] "
                        "[file.rbxlx | file.rbxl | folder]...\n"
                        "       kronos_compat --lab [--seconds N] [--players N] [--report-json FILE] [--report-md FILE]\n"
                        "                     [--baseline OLD.json] [--no-isolate] [places...]\n"
                        "A place's behaviour checks are read from <place>.checks.luau next to it.\n");
            return 0;
        } else {
            inputs.emplace_back(arg);
        }
    }
    if (!labOneOut.empty() && inputs.size() == 1) return labOne(inputs.front(), labOneOut, labOptions);
    if (inputs.empty()) inputs.emplace_back("tests/compat_corpus");

    std::vector<fs::path> places;
    for (const fs::path& input : inputs) {
        if (fs::is_directory(input)) {
            for (const auto& entry : fs::directory_iterator(input)) {
                const auto ext = entry.path().extension();
                if (ext == ".rbxlx" || ext == ".rbxl" || ext == ".rbxm" || ext == ".rbxmx") places.push_back(entry.path());
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

    if (lab) return runLabMode(places, labOptions, isolate, jsonPath, markdownPath, baselinePath);

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
