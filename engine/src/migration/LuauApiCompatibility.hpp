#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace engine::migration {

// What Kronos does with a given Roblox API a migrated script uses.
enum class ApiMappingStatus : uint8_t {
    // Kronos provides a direct equivalent under a different name.
    Mapped = 0,
    // ScriptCompatShimLoader rewrites the call site automatically.
    Shimmed = 1,
    // No equivalent exists. The script will still LOAD -- these are
    // ordinary globals that simply resolve to nil -- and then fail at the
    // point of use, which is why finding them at import time matters.
    Unmapped = 2,
};

struct ApiCompatibilityFinding {
    std::string identifier;   // the Roblox API as it appeared, e.g. "game" or ":LoadAnimation"
    ApiMappingStatus status = ApiMappingStatus::Unmapped;
    std::string guidance;     // what the author should do instead
    int line = 0;             // 1-based line in the scanned source
};

// Scans Luau source for Roblox APIs that Kronos does not provide.
//
// This exists because of how a migrated script actually fails. Roblox
// globals are not syntax -- `game`, `workspace`, `Instance` are ordinary
// identifiers -- so a script using them compiles and loads perfectly well
// under Kronos's Luau VM and then errors at the moment it dereferences
// nil, potentially deep into a play session. Reporting them at IMPORT
// time turns a mid-session crash into a migration warning, which is the
// entire point.
//
// Detection is token-based, not substring-based: it skips comments and
// string literals and matches whole identifiers only. That matters --
// a substring scan flags "game" inside "gamemode" and inside the comment
// "-- ported from the old game", producing a report full of noise a
// creator learns to ignore.
//
// It is deliberately NOT a full Luau AST pass. Luau.Ast is already a
// dependency and would be the precise tool, but a token scan that
// correctly ignores comments and strings catches the real cases here
// without tying this to compiler internals. Where that trade shows: a
// dynamic access like `_G["game"]` or `local g = game` followed by
// indirect use is reported at the identifier, not through the alias.
class LuauApiCompatibility {
public:
    LuauApiCompatibility();

    [[nodiscard]] std::vector<ApiCompatibilityFinding> scan(const std::string& luauSource) const;

    // Report lines in the shape docs/ARCHITECTURE.md §7 uses:
    //   [warn] <scriptPath>:<line> -- <guidance>
    [[nodiscard]] std::vector<std::string> buildReportLines(const std::string& scriptPath,
                                                             const std::string& luauSource) const;

    [[nodiscard]] size_t registrySize() const { return registry_.size(); }

private:
    struct Entry {
        std::string identifier;
        bool isMethod = false; // matched as ":Name(", not a bare global
        ApiMappingStatus status = ApiMappingStatus::Unmapped;
        std::string guidance;
    };
    std::vector<Entry> registry_;
};

[[nodiscard]] const char* apiMappingStatusName(ApiMappingStatus status);

} // namespace engine::migration
