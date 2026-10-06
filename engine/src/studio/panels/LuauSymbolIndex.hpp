#pragma once

#include <optional>
#include <string>
#include <vector>

namespace engine::studio::luau_symbols {

// 0-based line, byte columns, single line.
struct Range {
    int line = 0;
    int column = 0;
    int endColumn = 0;

    bool operator==(const Range&) const = default;
};

// A name that means the same thing in every script: a global, or a member
// of a global table (`Utils.lerp`).
struct GlobalRef {
    std::string global;
    std::string member;

    bool operator==(const GlobalRef&) const = default;
};

struct SymbolInfo {
    std::string name;
    std::optional<Range> definition;
    std::vector<Range> occurrences; // every use, the definition included, in source order
    std::optional<GlobalRef> globalRef; // set for globals and members of globals
};

// Scope-aware lookup of the identifier at (line, byteColumn): locals resolve
// through Luau's own binding, so shadowed names stay separate.
[[nodiscard]] std::optional<SymbolInfo> symbolAt(const std::string& source, int line, int byteColumn);

[[nodiscard]] std::vector<Range> occurrencesOf(const std::string& source, const GlobalRef& ref);
[[nodiscard]] std::optional<Range> definitionOf(const std::string& source, const GlobalRef& ref);

// Replaces every range with `replacement`; ranges may come in any order.
[[nodiscard]] std::string replaceRanges(const std::string& source, std::vector<Range> ranges, const std::string& replacement);

[[nodiscard]] bool isValidIdentifier(const std::string& name);

struct TextHit {
    int line = 0;
    int byteStart = 0;
    int byteEnd = 0;
};
[[nodiscard]] std::vector<TextHit> findText(const std::string& source, const std::string& query, bool matchCase, bool wholeWord);

} // namespace engine::studio::luau_symbols
