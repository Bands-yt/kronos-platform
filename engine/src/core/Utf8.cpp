#include "core/Utf8.hpp"

namespace engine::core {

std::string utf8TrimTrailingCharacter(const std::string& text) {
    if (text.empty()) return text;
    size_t cut = text.size() - 1;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
    return text.substr(0, cut);
}

} // namespace engine::core
