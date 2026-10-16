#pragma once

#include <algorithm>
#include <cctype>
#include <string>

namespace engine::core {

// Case-insensitive "contains"; every space-separated word of the filter must appear.
inline bool logLineMatches(const std::string& filter, const std::string& line) {
    std::string haystack = line;
    std::transform(haystack.begin(), haystack.end(), haystack.begin(), [](unsigned char c) { return std::tolower(c); });
    size_t start = 0;
    while (start < filter.size()) {
        const size_t end = filter.find(' ', start);
        const size_t stop = end == std::string::npos ? filter.size() : end;
        if (stop > start) {
            std::string word = filter.substr(start, stop - start);
            std::transform(word.begin(), word.end(), word.begin(), [](unsigned char c) { return std::tolower(c); });
            if (haystack.find(word) == std::string::npos) return false;
        }
        start = stop + 1;
    }
    return true;
}

} // namespace engine::core
