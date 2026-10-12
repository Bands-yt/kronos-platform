#pragma once

#include <string>

namespace engine::core {

enum class ScriptLanguage { Luau, Python };

// A script is Python when its first non-blank line is a `#` comment (Luau comments start with `--`,
// so no Luau script can begin that way).
[[nodiscard]] inline ScriptLanguage scriptLanguageOf(const std::string& source) {
    for (char c : source) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        return c == '#' ? ScriptLanguage::Python : ScriptLanguage::Luau;
    }
    return ScriptLanguage::Luau;
}

} // namespace engine::core
