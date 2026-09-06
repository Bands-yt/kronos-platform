#pragma once

#include <string>

namespace engine::core {

// Removes one trailing UTF-8 character (not just one byte) from `text`,
// so backspace in a text-input box drops a whole multi-byte character
// (e.g. an emoji or accented letter) at once instead of corrupting it
// into an invalid partial sequence. No-op on an empty string.
[[nodiscard]] std::string utf8TrimTrailingCharacter(const std::string& text);

} // namespace engine::core
