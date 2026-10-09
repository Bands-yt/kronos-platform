// kronos_compat: imports .rbxlx places and prints how much of each Kronos can run.
//   kronos_compat [--min PERCENT] [file.rbxlx | folder]...
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

#include "migration/CompatibilityScore.hpp"
#include "safety/IPInfringementScanner.hpp"

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
    std::vector<fs::path> inputs;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--min" && i + 1 < argc) {
            minimum = std::atof(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            std::printf("usage: kronos_compat [--min PERCENT] [file.rbxlx | folder]...\n");
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
                if (entry.path().extension() == ".rbxlx") places.push_back(entry.path());
            }
        } else {
            places.push_back(input);
        }
    }
    std::sort(places.begin(), places.end());
    if (places.empty()) {
        std::fprintf(stderr, "kronos_compat: no .rbxlx places found\n");
        return 2;
    }

    ProjectImporter importer;
    engine::safety::IPInfringementScanner scanner;
    CompatibilityScore total;
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
        const ImportReport report = importer.importDocument(buffer.str(), scanner);
        if (!report.parsed || report.blocked) {
            std::printf("%s: %s\n", place.filename().string().c_str(), report.blocked ? "blocked" : "did not parse");
            continue;
        }

        CompatibilityScore score = scoreImport(report);
        runImportedScripts(report, score);
        std::printf("%s: %s\n", place.filename().string().c_str(), score.summary().c_str());
        for (const CompatScriptRun& run : score.scripts) {
            if (!run.started) std::printf("    not started   %s (Roblox wouldn't run it here)\n", run.path.c_str());
            if (!run.ok) std::printf("    script error  %s\n", run.error.c_str());
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

    if (scored == 0) return 2;
    const double average = overallSum / static_cast<double>(scored);
    std::printf("\nAll places (%zu): %s\n", scored, total.summary().c_str());
    std::printf("Average score: %.1f%%\n", average);

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
