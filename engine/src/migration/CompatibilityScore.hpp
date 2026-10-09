#pragma once

#include <map>
#include <string>
#include <vector>

#include "migration/ProjectImporter.hpp"

namespace engine::migration {

// How much of an imported Roblox place Kronos can run today (4.3 step 1).
struct CompatScriptRun {
    std::string path;
    std::string className;
    bool ok = false;
    bool started = true; // false: Roblox's run rules never start it in a test session
    std::string error;
};

struct CompatibilityScore {
    size_t instances = 0;
    size_t instancesMapped = 0;
    size_t apiUses = 0;
    size_t apiSupported = 0; // mapped or shimmed
    size_t scriptsRun = 0;
    size_t scriptsOk = 0;
    std::map<std::string, size_t> missingApis;        // identifier -> uses
    std::map<std::string, size_t> unmappedClasses;    // class -> instances
    std::vector<CompatScriptRun> scripts;

    [[nodiscard]] double instancePercent() const;
    [[nodiscard]] double apiPercent() const;
    [[nodiscard]] double scriptPercent() const;
    // Average of the three; a part with nothing to measure counts as 100%.
    [[nodiscard]] double overallPercent() const;
    [[nodiscard]] std::string summary() const;
};

// Counts mapped instances and supported API uses in an import report.
[[nodiscard]] CompatibilityScore scoreImport(const ImportReport& report);

// Plays the place headlessly with Roblox's run rules (server and client
// contexts, a test player joining, Starter copies) and records which Scripts
// and LocalScripts run without an error. ModuleScripts are compiled here and
// run when something requires them.
void runImportedScripts(const ImportReport& report, CompatibilityScore& score);

} // namespace engine::migration
