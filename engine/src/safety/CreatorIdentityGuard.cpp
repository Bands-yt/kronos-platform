#include "safety/CreatorIdentityGuard.hpp"

#include <cstdint>

#include "safety/TextNormalize.hpp"

namespace engine::safety {

namespace {

// Words that claim official/authoritative standing -- on their own,
// perfectly ordinary (a clan calling its leader "Admin", a game calling
// a role "Support"); combined with a reference to the platform's own
// brand, the classic phishing-account pattern (see header comment).
// Matched as a substring of the normalized (leetspeak-collapsed) name,
// same convention as IPInfringementScanner's term matching, so "4dm1n"
// still counts as "admin".
constexpr const char* kAuthorityWords[] = {
    "admin", "administrator", "staff", "official", "support", "moderator",
    "team",  "verified",      "founder", "ceo",     "security", "helpdesk",
};

bool containsAuthorityWord(const std::string& normalizedText) {
    for (const char* word : kAuthorityWords) {
        if (normalizedText.find(word) != std::string::npos) return true;
    }
    return false;
}

// One decoded Unicode codepoint from a UTF-8 byte sequence, and how many
// bytes it consumed. Malformed/truncated sequences degrade to consuming
// exactly one byte and returning it as its own codepoint value -- this
// runs on untrusted, arbitrary creator input, so it must never throw or
// read past the end of the string, only ever produce a best-effort
// result.
struct DecodedCodepoint {
    uint32_t value = 0;
    size_t byteLength = 1;
};

DecodedCodepoint decodeUtf8At(const std::string& text, size_t index) {
    unsigned char lead = static_cast<unsigned char>(text[index]);
    if (lead < 0x80) return {lead, 1};

    int continuationBytes = 0;
    uint32_t value = 0;
    if ((lead & 0xE0) == 0xC0) { continuationBytes = 1; value = lead & 0x1Fu; }
    else if ((lead & 0xF0) == 0xE0) { continuationBytes = 2; value = lead & 0x0Fu; }
    else if ((lead & 0xF8) == 0xF0) { continuationBytes = 3; value = lead & 0x07u; }
    else return {lead, 1}; // invalid leading byte -- treat as a raw (bogus) codepoint, one byte consumed

    if (index + static_cast<size_t>(continuationBytes) >= text.size()) return {lead, 1}; // truncated at end of string

    uint32_t assembled = value;
    for (int k = 1; k <= continuationBytes; ++k) {
        unsigned char cont = static_cast<unsigned char>(text[index + static_cast<size_t>(k)]);
        if ((cont & 0xC0) != 0x80) return {lead, 1}; // malformed continuation byte
        assembled = (assembled << 6) | (cont & 0x3Fu);
    }
    return {assembled, static_cast<size_t>(continuationBytes + 1)};
}

// Zero-width/invisible codepoints sometimes inserted mid-word to defeat a
// plain substring scan ("Rob​ux") while looking untouched to a human
// reader. Dropped outright when building the ASCII skeleton.
bool isZeroWidth(uint32_t codepoint) {
    switch (codepoint) {
        case 0x200B: // zero width space
        case 0x200C: // zero width non-joiner
        case 0x200D: // zero width joiner
        case 0x2060: // word joiner
        case 0xFEFF: // zero width no-break space / BOM
            return true;
        default:
            return false;
    }
}

// A small, hand-picked table of the Cyrillic/Greek letters most commonly
// used to spoof Latin text (the classic "Cyrillic homograph attack" —
// e.g. registering а-with-Cyrillic-a instead of Latin a to impersonate a
// domain/username). NOT a general Unicode confusables table -- see
// header comment.
struct Confusable {
    uint32_t codepoint;
    char replacement;
};
constexpr Confusable kConfusables[] = {
    // Cyrillic lowercase lookalikes
    {0x0430, 'a'}, {0x0435, 'e'}, {0x043E, 'o'}, {0x0440, 'p'}, {0x0441, 'c'},
    {0x0443, 'y'}, {0x0445, 'x'}, {0x0456, 'i'}, {0x0455, 's'}, {0x0458, 'j'},
    {0x0433, 'r'},
    // Cyrillic uppercase lookalikes
    {0x0410, 'a'}, {0x0415, 'e'}, {0x041E, 'o'}, {0x0420, 'p'}, {0x0421, 'c'},
    {0x0425, 'x'}, {0x0412, 'b'}, {0x041D, 'h'}, {0x041C, 'm'}, {0x0422, 't'},
    // Greek lookalikes
    {0x03BF, 'o'}, {0x03B1, 'a'}, {0x03BD, 'v'}, {0x0391, 'a'}, {0x039F, 'o'},
    {0x0392, 'b'}, {0x0395, 'e'}, {0x0399, 'i'}, {0x039A, 'k'}, {0x03A1, 'p'},
};

char confusableReplacement(uint32_t codepoint) {
    for (const auto& entry : kConfusables) {
        if (entry.codepoint == codepoint) return entry.replacement;
    }
    return '\0';
}

// Reduces `text` to an ASCII "skeleton": ASCII characters pass through
// unchanged, known confusable codepoints become their Latin equivalent,
// zero-width codepoints are dropped, and any other non-ASCII codepoint
// (a genuinely non-Latin display name with no lookalike involved) is
// also dropped -- it can't be spelling out a Latin protected word, and
// this skeleton only exists to feed IPInfringementScanner's ASCII-only
// matching, not to preserve the name's actual appearance. Also reports
// whether any substitution/drop actually happened, so callers can tell
// "this name is legitimately non-Latin" apart from "this name was
// deliberately obfuscated."
struct Skeleton {
    std::string ascii;
    bool sawObfuscation = false;
};

Skeleton buildSkeleton(const std::string& text) {
    Skeleton result;
    result.ascii.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        DecodedCodepoint decoded = decodeUtf8At(text, i);
        i += decoded.byteLength;

        if (decoded.value < 128) {
            result.ascii.push_back(static_cast<char>(decoded.value));
            continue;
        }
        if (isZeroWidth(decoded.value)) {
            result.sawObfuscation = true;
            continue;
        }
        char replacement = confusableReplacement(decoded.value);
        if (replacement != '\0') {
            result.ascii.push_back(replacement);
            result.sawObfuscation = true;
            continue;
        }
        // Non-ASCII, not a known confusable, not zero-width: a real
        // character in some other script. Drop it from the skeleton
        // (nothing to substitute it with) without flagging obfuscation --
        // this is the "legitimately non-Latin name" case.
    }
    return result;
}

bool matchesRobloxBrand(const IPInfringementScanner& scanner, const std::string& skeleton) {
    IPInfringementResult result = scanner.scan(skeleton);
    for (const auto& match : result.matches) {
        if (match.matchedTerm == "Roblox" || match.matchedTerm == "Robux") return true;
    }
    return false;
}

} // namespace

IdentityCheckResult CreatorIdentityGuard::checkDisplayName(const std::string& displayName) const {
    IdentityCheckResult result;

    Skeleton skeleton = buildSkeleton(displayName);
    bool claimsRobloxBrand = matchesRobloxBrand(ipScanner_, skeleton.ascii);
    bool claimsAuthority = containsAuthorityWord(normalizeAscii(skeleton.ascii));

    // Ordered strongest-signal-first: the first branch that applies wins,
    // matching the same "flag on one ambiguous signal, block only on a
    // strong compound one" policy IPInfringementScanner's own hard-block-
    // vs-review split uses.
    if (claimsRobloxBrand && claimsAuthority) {
        result.flagged = true;
        result.blocked = true;
        result.reason = skeleton.sawObfuscation
                             ? "Homoglyph/zero-width-obfuscated name claims Roblox brand + official authority"
                             : "Display name claims Roblox brand + official authority";
    } else if (claimsAuthority && skeleton.sawObfuscation) {
        result.flagged = true;
        result.blocked = false;
        result.reason = "Display name uses look-alike characters together with an authority claim";
    } else if (claimsAuthority) {
        result.flagged = true;
        result.blocked = false;
        result.reason = "Display name claims official/authority status -- review for impersonation";
    } else if (claimsRobloxBrand) {
        result.flagged = true;
        result.blocked = false;
        result.reason = "Display name references the Roblox brand";
    }

    return result;
}

} // namespace engine::safety
