#include "migration/LuauApiCompatibility.hpp"

#include <cctype>
#include <unordered_map>

namespace engine::migration {
namespace {

bool isIdentifierStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
}
bool isIdentifierChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

// One pass over the source that yields identifier tokens with their line
// numbers, skipping comments and string literals.
//
// Kept local rather than exposed: this is not a general Luau lexer (it
// does not tokenise numbers, operators or long-string levels beyond what
// skipping requires), only enough to know whether an identifier is real
// code or text inside a comment or string.
struct IdentifierToken {
    std::string text;
    int line = 0;
    bool precededByColon = false;
};

std::vector<IdentifierToken> tokenizeIdentifiers(const std::string& src) {
    std::vector<IdentifierToken> tokens;
    int line = 1;
    size_t i = 0;

    auto skipLongBracket = [&](size_t start) -> bool {
        // Handles [[ ... ]] and [==[ ... ]==] for both long strings and
        // long comments. Returns false when this is not a long bracket.
        size_t p = start;
        if (p >= src.size() || src[p] != '[') return false;
        ++p;
        size_t equals = 0;
        while (p < src.size() && src[p] == '=') {
            ++equals;
            ++p;
        }
        if (p >= src.size() || src[p] != '[') return false;
        ++p;
        const std::string closer = "]" + std::string(equals, '=') + "]";
        const size_t end = src.find(closer, p);
        for (size_t k = start; k < (end == std::string::npos ? src.size() : end); ++k) {
            if (src[k] == '\n') ++line;
        }
        i = (end == std::string::npos) ? src.size() : end + closer.size();
        return true;
    };

    while (i < src.size()) {
        const char c = src[i];

        if (c == '\n') {
            ++line;
            ++i;
            continue;
        }

        // Comments, both forms.
        if (c == '-' && i + 1 < src.size() && src[i + 1] == '-') {
            const size_t afterDashes = i + 2;
            if (afterDashes < src.size() && src[afterDashes] == '[' && skipLongBracket(afterDashes)) continue;
            while (i < src.size() && src[i] != '\n') ++i;
            continue;
        }

        // Long strings.
        if (c == '[' && skipLongBracket(i)) continue;

        // Quoted strings, with escapes.
        if (c == '"' || c == '\'') {
            const char quote = c;
            ++i;
            while (i < src.size() && src[i] != quote) {
                if (src[i] == '\\' && i + 1 < src.size()) {
                    if (src[i + 1] == '\n') ++line;
                    i += 2;
                    continue;
                }
                if (src[i] == '\n') ++line; // unterminated string; keep the line count honest
                ++i;
            }
            ++i;
            continue;
        }

        if (isIdentifierStart(c)) {
            const size_t start = i;
            while (i < src.size() && isIdentifierChar(src[i])) ++i;
            IdentifierToken token;
            token.text = src.substr(start, i - start);
            token.line = line;
            // Look back past spaces for a ':' -- that is what separates a
            // method call (h:LoadAnimation()) from a field or a global.
            size_t back = start;
            while (back > 0 && (src[back - 1] == ' ' || src[back - 1] == '\t')) --back;
            token.precededByColon = back > 0 && src[back - 1] == ':';
            tokens.push_back(std::move(token));
            continue;
        }

        ++i;
    }
    return tokens;
}

} // namespace

const char* apiMappingStatusName(ApiMappingStatus status) {
    switch (status) {
        case ApiMappingStatus::Mapped: return "mapped";
        case ApiMappingStatus::Shimmed: return "shimmed";
        case ApiMappingStatus::Unmapped: return "unmapped";
    }
    return "unknown";
}

LuauApiCompatibility::LuauApiCompatibility() {
    // Roblox globals with no Kronos equivalent. Each carries the real
    // alternative rather than just naming the problem -- a migration
    // report that only says "unsupported" leaves the author exactly as
    // stuck as the crash would have.
    registry_.push_back({"game", false, ApiMappingStatus::Unmapped,
                          "Kronos has no `game` DataModel. Use the `world` table for entities and `events` for "
                          "lifecycle hooks (docs/LUA_API.md)."});
    registry_.push_back({"workspace", false, ApiMappingStatus::Unmapped,
                          "No `workspace` global. Entities are reached through `world.findByName()` / "
                          "`world.createEntity()`."});
    registry_.push_back({"Instance", false, ApiMappingStatus::Unmapped,
                          "`Instance.new()` has no equivalent. Use `world.createEntity()` and the `world.set*` "
                          "functions."});
    registry_.push_back({"Enum", false, ApiMappingStatus::Unmapped,
                          "No `Enum` namespace. Kronos APIs take plain strings and numbers."});
    registry_.push_back({"script", false, ApiMappingStatus::Unmapped,
                          "No `script` self-reference. A script's own top-level code runs at load; use the `events` "
                          "table for hooks."});
    registry_.push_back({"UserInputService", false, ApiMappingStatus::Unmapped,
                          "No UserInputService. Input is delivered through `events.onInteract`."});
    registry_.push_back({"TweenService", false, ApiMappingStatus::Unmapped,
                          "No TweenService. Animate from `events.onUpdate`, or author a curve in Studio's Movie "
                          "Mode timeline."});
    registry_.push_back({"RunService", false, ApiMappingStatus::Unmapped,
                          "No RunService. Per-frame work belongs in `events.onUpdate`."});
    registry_.push_back({"ReplicatedStorage", false, ApiMappingStatus::Unmapped,
                          "No ReplicatedStorage. Share modules with `require()` and replicate with "
                          "`network.fireServer()` / `network.fireAllClients()`."});
    registry_.push_back({"Players", false, ApiMappingStatus::Unmapped,
                          "No Players service. Use `events.onPlayerJoin` / `events.onPlayerLeave`."});
    registry_.push_back({"Humanoid", false, ApiMappingStatus::Unmapped,
                          "No Humanoid type. Character state is driven through `world.playAnimation()` and the "
                          "avatar APIs."});

    // Handled automatically by ScriptCompatShimLoader -- reported so the
    // author knows a rewrite happened, not because anything is broken.
    registry_.push_back({"LoadAnimation", true, ApiMappingStatus::Shimmed,
                          "Legacy `Humanoid:LoadAnimation` call site; the compat shim maps it to the Animator-based "
                          "API automatically."});

    // Present and working -- recorded so a scan can distinguish "this
    // script already uses Kronos APIs" from "this script uses nothing we
    // recognise".
    registry_.push_back({"world", false, ApiMappingStatus::Mapped, "Kronos `world` API."});
    registry_.push_back({"network", false, ApiMappingStatus::Mapped, "Kronos `network` API."});
    registry_.push_back({"events", false, ApiMappingStatus::Mapped, "Kronos `events` API."});
    registry_.push_back({"ui", false, ApiMappingStatus::Mapped, "Kronos `ui` API."});
}

std::vector<ApiCompatibilityFinding> LuauApiCompatibility::scan(const std::string& luauSource) const {
    std::unordered_map<std::string, const Entry*> globals;
    std::unordered_map<std::string, const Entry*> methods;
    for (const Entry& entry : registry_) {
        (entry.isMethod ? methods : globals)[entry.identifier] = &entry;
    }

    std::vector<ApiCompatibilityFinding> findings;
    // One finding per API per line: a loop body referencing `game` twice
    // on the same line is one migration problem, not two. Different lines
    // stay separate, because each is a distinct place to edit.
    std::unordered_map<std::string, int> lastReportedLine;

    for (const IdentifierToken& token : tokenizeIdentifiers(luauSource)) {
        const Entry* entry = nullptr;
        std::string reported = token.text;
        if (token.precededByColon) {
            const auto it = methods.find(token.text);
            if (it != methods.end()) {
                entry = it->second;
                reported = ":" + token.text;
            }
        } else {
            const auto it = globals.find(token.text);
            if (it != globals.end()) entry = it->second;
        }
        if (entry == nullptr) continue;

        const auto seen = lastReportedLine.find(reported);
        if (seen != lastReportedLine.end() && seen->second == token.line) continue;
        lastReportedLine[reported] = token.line;

        ApiCompatibilityFinding finding;
        finding.identifier = reported;
        finding.status = entry->status;
        finding.guidance = entry->guidance;
        finding.line = token.line;
        findings.push_back(std::move(finding));
    }
    return findings;
}

std::vector<std::string> LuauApiCompatibility::buildReportLines(const std::string& scriptPath,
                                                                 const std::string& luauSource) const {
    std::vector<std::string> lines;
    for (const ApiCompatibilityFinding& finding : scan(luauSource)) {
        // Mapped APIs are not a migration problem and would only pad the
        // report -- they are scanned so callers can measure coverage, not
        // so a creator reads about them.
        if (finding.status == ApiMappingStatus::Mapped) continue;
        const char* tag = finding.status == ApiMappingStatus::Shimmed ? "[info] " : "[warn] ";
        lines.push_back(tag + scriptPath + ":" + std::to_string(finding.line) + " -- " + finding.identifier + ": " +
                        finding.guidance);
    }
    return lines;
}

} // namespace engine::migration
