#pragma once

#include <string>
#include <vector>

namespace engine::migration {

struct CompatShimEntry {
    std::string deprecatedApiMarker; // substring used to detect a script needs this shim
    std::string luauSource;          // the shim itself
    std::string description;         // human-readable note for the importer report
};

// The "compat-shim ModuleScript library" from docs/ARCHITECTURE.md §7's
// migration example -- the two entries registered in the .cpp are the
// exact two warnings shown in that section's sample importer report, so
// this class is what would actually produce that report, not just an
// illustration of one.
//
// Detection is intentionally a plain substring search over the script's
// source text, not a real AST scan. Roblox already gives us a real parser
// for that upgrade -- Luau.Ast, already a dependency of core::Scripting --
// but wiring detection through it is follow-on precision work, not the
// thing this stub exists to demonstrate (the shim registry and report
// shape are).
class ScriptCompatShimLoader {
public:
    ScriptCompatShimLoader();

    [[nodiscard]] std::vector<CompatShimEntry> detectRequiredShims(const std::string& scriptSource) const;

    // Concatenates every registered shim into one Luau chunk, loadable
    // once via engine::core::Scripting::loadAndRun() and required by any
    // imported script that needs it.
    [[nodiscard]] std::string generateCompatModuleSource() const;

    // Produces report lines in the same shape as
    // docs/ARCHITECTURE.md §7's worked example:
    //   [warn] <scriptPath> -- <description>
    [[nodiscard]] std::vector<std::string> buildReportLines(const std::string& scriptPath, const std::string& scriptSource) const;

private:
    std::vector<CompatShimEntry> registry_;
};

} // namespace engine::migration
