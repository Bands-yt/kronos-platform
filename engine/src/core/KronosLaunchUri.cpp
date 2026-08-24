#include "core/KronosLaunchUri.hpp"

#include <cctype>

namespace engine::core {
namespace {

int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Same real percent-decoding LoopbackHttpServer.cpp's own OAuth
// query-string parser applies to `code`/`state` -- kept as a small,
// separate copy rather than shared, since exposing that file's
// anonymous-namespace helper across a translation unit boundary is a
// bigger, riskier change to OAuth-critical code than this warrants.
std::string percentDecode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            int hi = hexVal(s[i + 1]);
            int lo = hexVal(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out += static_cast<char>((hi << 4) | lo);
                i += 2;
                continue;
            }
        } else if (s[i] == '+') {
            out += ' ';
            continue;
        }
        out += s[i];
    }
    return out;
}

bool iequalsAscii(const std::string& a, const char* b) {
    size_t i = 0;
    for (; i < a.size() && b[i] != '\0'; ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return i == a.size() && b[i] == '\0';
}

// Real, minimal query-param lookup: find "<key>=", take up to the next
// '&' (or end of string), percent-decode. Deliberately does not build a
// general key/value map -- this URI has exactly two parameters this
// code cares about, both looked up the same simple way. Returns empty
// for "not present", which callers here treat as "absent", not "present
// but blank" -- there is no real difference between the two for either
// field this parses.
std::string findQueryParam(const std::string& query, const std::string& key) {
    const std::string needle = key + "=";
    const size_t start = query.find(needle);
    if (start == std::string::npos) return {};
    const size_t valueStart = start + needle.size();
    size_t valueEnd = query.find('&', valueStart);
    if (valueEnd == std::string::npos) valueEnd = query.size();
    return percentDecode(query.substr(valueStart, valueEnd - valueStart));
}

} // namespace

bool parseKronosLaunchUri(const std::string& uri, KronosLaunchRequest& outRequest) {
    // Scheme match is case-insensitive: Windows' own URL-handling can
    // normalise a registered scheme's casing, and requiring an exact
    // lowercase match would make this fragile against something outside
    // this code's control.
    constexpr const char* kSchemePrefix = "kronos://";
    constexpr size_t kSchemeLen = 9; // strlen("kronos://")
    if (uri.size() < kSchemeLen) return false;
    for (size_t i = 0; i < kSchemeLen; ++i) {
        if (std::tolower(static_cast<unsigned char>(uri[i])) !=
            std::tolower(static_cast<unsigned char>(kSchemePrefix[i]))) {
            return false;
        }
    }

    const std::string rest = uri.substr(kSchemeLen); // "launch?game=<slug>..."
    const size_t queryStart = rest.find('?');
    const std::string action = rest.substr(0, queryStart);
    if (!iequalsAscii(action, "launch")) return false;
    if (queryStart == std::string::npos) return false; // "launch" with no query at all -- nothing to launch

    const std::string query = rest.substr(queryStart + 1);
    const std::string slug = findQueryParam(query, "game");
    if (slug.empty()) return false;

    outRequest.gameSlug = slug;
    // Optional: an absent handoff= is not a parse failure, see
    // KronosLaunchRequest::handoffCode's own doc comment for why.
    outRequest.handoffCode = findQueryParam(query, "handoff");
    return true;
}

} // namespace engine::core
