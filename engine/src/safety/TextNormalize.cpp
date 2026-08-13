#include "safety/TextNormalize.hpp"

#include <cctype>

namespace engine::safety {

char normalizeChar(char raw) {
    char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(raw)));
    switch (lower) {
        case '0': return 'o';
        case '1': return 'l';
        case '3': return 'e';
        case '4': return 'a';
        case '5': return 's';
        case '7': return 't';
        case '@': return 'a';
        case '$': return 's';
        default: break;
    }
    return std::isalnum(static_cast<unsigned char>(lower)) != 0 ? lower : '\0';
}

std::string normalizeAscii(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char raw : text) {
        char c = normalizeChar(raw);
        if (c != '\0') out.push_back(c);
    }
    return out;
}

} // namespace engine::safety
