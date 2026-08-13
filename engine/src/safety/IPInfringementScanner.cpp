#include "safety/IPInfringementScanner.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_set>
#include <vector>

#include "safety/TextNormalize.hpp"

namespace engine::safety {

namespace {

// Roblox Corporation's own trademarks -- a creator's content/listing name
// has no legitimate reason to contain these.
constexpr const char* kHardBlockTerms[] = {
    "Roblox",
    "Robux",
};

// Well-known Roblox-*original* (first-party or breakout UGC) experience
// titles -- distinctive enough that a match is a real signal, but title
// collision alone isn't proof of infringement (a genuinely original game
// could coincidentally share a word), so these flag for human review
// instead of an automatic hard block. Stored in their natural, spaced
// form (not pre-concatenated) so tokenize() below can recover per-word
// tokens for multi-token/phonetic matching -- normalize() still collapses
// them to the same flat strings the old scanner matched against
// ("Adopt Me" -> "adoptme"), so Exact-match behavior is unchanged.
// Illustrative, not exhaustive -- a real deployment maintains this
// against legal's actual trademark watch list, not an engineering guess.
constexpr const char* kReviewTerms[] = {
    "Adopt Me",     "Brookhaven",     "Bloxburg",     "Piggy",         "Meep City",     "Jailbreak",
    "Blox Fruits",  "Tower Of Hell",  "Murder Mystery", "Royale High", "Phantom Forces", "Arsenal",
};

// Function words dropped when tokenizing a *term* into its significant
// words for MultiToken/Phonetic matching -- e.g. "Tower Of Hell" only
// needs "tower" and "hell" to both show up, not "of". Deliberately does
// NOT include short-but-load-bearing words like "me" ("Adopt Me" needs
// both "adopt" and "me" present, or MultiToken would degrade to a
// single-word "adopt" check and false-positive on any unrelated sentence
// containing the common verb "adopt").
bool isStopword(const std::string& token) {
    static const std::unordered_set<std::string> kStopwords = {"of", "the", "and", "a", "an", "to", "in", "on", "is", "it", "at"};
    return kStopwords.count(token) > 0;
}

// normalize()/tokenize() both run on TextNormalize.hpp's shared
// normalizeChar() (lowercases, de-leetspeaks, returns '\0' for anything
// non-alphanumeric after substitution) -- normalize() drops those
// characters entirely (collapsing spacing/punctuation tricks), tokenize()
// treats '\0' as a word boundary instead. CreatorIdentityGuard uses the
// same normalizeChar() so both scanners agree on what a leetspeak
// substitution is.
std::string normalize(const std::string& text) { return normalizeAscii(text); }

// Splits text into normalized words (same per-character substitution as
// normalize(), but word boundaries are kept instead of collapsed), for
// MultiToken/Phonetic matching. Single-character tokens are dropped as
// noise (mostly stray initials/punctuation remnants); everything else,
// including short-but-real words, is kept -- see isStopword()'s comment
// on why the stopword list, not a length cutoff, does the real filtering
// for term tokens.
std::vector<std::string> tokenize(const std::string& text) {
    std::vector<std::string> tokens;
    std::string current;
    auto flush = [&]() {
        if (current.size() >= 2) tokens.push_back(current);
        current.clear();
    };
    for (char raw : text) {
        char c = normalizeChar(raw);
        if (c != '\0') {
            current.push_back(c);
        } else {
            flush();
        }
    }
    flush();
    return tokens;
}

// The significant (non-stopword) tokens of a term, e.g. "Tower Of Hell"
// -> {"tower", "hell"}. Used for both MultiToken and Phonetic matching.
std::vector<std::string> significantTokens(const std::string& term) {
    std::vector<std::string> tokens;
    for (auto& token : tokenize(term)) {
        if (!isStopword(token)) tokens.push_back(token);
    }
    return tokens;
}

// Classic Levenshtein edit distance (insert/delete/substitute, unit
// cost), O(n*m) DP with a rolling two-row buffer. Real implementation,
// not a stub -- correctness matters here since it's a security/legal
// heuristic, not cosmetic.
size_t levenshtein(const std::string& a, const std::string& b) {
    const size_t n = a.size();
    const size_t m = b.size();
    std::vector<size_t> prev(m + 1);
    std::vector<size_t> curr(m + 1);
    for (size_t j = 0; j <= m; ++j) prev[j] = j;
    for (size_t i = 1; i <= n; ++i) {
        curr[0] = i;
        for (size_t j = 1; j <= m; ++j) {
            size_t substitutionCost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            curr[j] = std::min({prev[j] + 1, curr[j - 1] + 1, prev[j - 1] + substitutionCost});
        }
        std::swap(prev, curr);
    }
    return prev[m];
}

// True if some substring of `haystack` is within `maxDistance` edits of
// `needle`. Slides a window over every length in
// [needle.size() - maxDistance, needle.size() + maxDistance] rather than
// just needle.size() -- Levenshtein distance already accounts for
// inserted/deleted characters, but only if the compared window is
// allowed to be a different length than needle itself (a fixed-length
// window would miss e.g. needle "piggy" matching haystack substring
// "pigy" at distance 1). Brute-force (checks every window position at
// every candidate length); fine for this scanner's tiny (~12 entry) term
// list and short creator-supplied text -- not something ever run over
// large documents.
bool fuzzyContains(const std::string& haystack, const std::string& needle, size_t maxDistance) {
    if (maxDistance == 0 || needle.empty()) return haystack.find(needle) != std::string::npos;
    size_t minLen = needle.size() > maxDistance ? needle.size() - maxDistance : 1;
    size_t maxLen = needle.size() + maxDistance;
    for (size_t windowLen = minLen; windowLen <= maxLen; ++windowLen) {
        if (windowLen == 0 || windowLen > haystack.size()) continue;
        for (size_t start = 0; start + windowLen <= haystack.size(); ++start) {
            if (levenshtein(haystack.substr(start, windowLen), needle) <= maxDistance) return true;
        }
    }
    return false;
}

// American Soundex: first letter kept as-is, remaining letters mapped to
// one of 6 digit classes (vowels/H/W/Y are unmapped separators),
// adjacent-duplicate codes collapsed, padded/truncated to a 4-character
// code. Standard textbook algorithm, not a custom approximation --
// deliberately the loosest/noisiest of the four matching strategies (see
// header comment), which is why it only ever flags, never blocks.
std::string soundex(const std::string& token) {
    if (token.empty()) return "";
    auto digitFor = [](char c) -> char {
        switch (c) {
            case 'b': case 'f': case 'p': case 'v': return '1';
            case 'c': case 'g': case 'j': case 'k': case 'q': case 's': case 'x': case 'z': return '2';
            case 'd': case 't': return '3';
            case 'l': return '4';
            case 'm': case 'n': return '5';
            case 'r': return '6';
            default: return '0'; // vowels, h, w, y
        }
    };
    std::string code;
    code.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(token[0]))));
    char prevDigit = digitFor(token[0]);
    for (size_t i = 1; i < token.size() && code.size() < 4; ++i) {
        char digit = digitFor(token[i]);
        if (digit != '0' && digit != prevDigit) code.push_back(digit);
        prevDigit = digit;
    }
    while (code.size() < 4) code.push_back('0');
    return code;
}

// True if every term in `termTokens` has some input token that either
// equals it (MultiToken) or Soundex-matches it (Phonetic), selected by
// `usePhonetic`. Shared implementation since the two strategies differ
// only in how a single pair of tokens is compared -- both require *all*
// term tokens to be satisfied, not just one, which is what keeps a term
// like "Adopt Me" from firing on any text that merely contains the
// common word "adopt" or the pronoun "me" alone (see header comment).
bool allTermTokensPresent(const std::vector<std::string>& termTokens, const std::vector<std::string>& inputTokens,
                           bool usePhonetic) {
    if (termTokens.size() < 2) return false; // single-word terms: Exact/Fuzzy/Phonetic-on-whole-term cover it
    for (const auto& termToken : termTokens) {
        bool satisfied = false;
        for (const auto& inputToken : inputTokens) {
            if (usePhonetic ? soundex(inputToken) == soundex(termToken) : inputToken == termToken) {
                satisfied = true;
                break;
            }
        }
        if (!satisfied) return false;
    }
    return true;
}

// Fuzzy match tolerance scales with term length: short terms have too
// many plausible 1-edit-distance false positives among ordinary words to
// safely fuzzy-match at all (maxDistance 0 makes fuzzyContains() degrade
// to a plain substring check), longer terms can afford a wider net.
size_t fuzzyToleranceFor(const std::string& normalizedTerm) {
    if (normalizedTerm.size() >= 8) return 2;
    if (normalizedTerm.size() >= 5) return 1;
    return 0;
}

void addMatch(IPInfringementResult& result, const std::string& term, float confidence, IPInfringementMatchKind kind,
              bool block) {
    result.flagged = true;
    result.blocked = result.blocked || block;
    result.matches.push_back(IPInfringementMatch{term, confidence, kind});
}

} // namespace

IPInfringementResult IPInfringementScanner::scan(const std::string& text) const {
    IPInfringementResult result;
    std::string normalizedText = normalize(text);
    if (normalizedText.empty()) return result;
    std::vector<std::string> inputTokens = tokenize(text);

    for (const char* rawTerm : kHardBlockTerms) {
        std::string term(rawTerm);
        std::string normalizedTerm = normalize(term);

        if (normalizedText.find(normalizedTerm) != std::string::npos) {
            addMatch(result, term, 0.95f, IPInfringementMatchKind::Exact, /*block=*/true);
            continue; // strongest possible signal for this term already found -- skip weaker strategies
        }

        size_t tolerance = fuzzyToleranceFor(normalizedTerm);
        if (tolerance > 0 && fuzzyContains(normalizedText, normalizedTerm, tolerance)) {
            // Still blocks: a bounded-edit-distance near-miss on a
            // trademark this short and this specific ("roblox", "robux")
            // is deliberate obfuscation far more often than coincidence.
            addMatch(result, term, 0.8f, IPInfringementMatchKind::Fuzzy, /*block=*/true);
            continue;
        }

        // Phonetic-only hit on a hard-block term: flag for human review,
        // don't auto-block -- "sounds like Roblox" alone is too weak a
        // signal to reject a submission outright (see header comment on
        // why Phonetic is the noisiest strategy).
        for (const auto& inputToken : inputTokens) {
            if (soundex(inputToken) == soundex(normalizedTerm)) {
                addMatch(result, term, 0.45f, IPInfringementMatchKind::Phonetic, /*block=*/false);
                break;
            }
        }
    }

    for (const char* rawTerm : kReviewTerms) {
        std::string term(rawTerm);
        std::string normalizedTerm = normalize(term);

        if (normalizedText.find(normalizedTerm) != std::string::npos) {
            addMatch(result, term, 0.7f, IPInfringementMatchKind::Exact, /*block=*/false);
            continue;
        }

        size_t tolerance = fuzzyToleranceFor(normalizedTerm);
        if (tolerance > 0 && fuzzyContains(normalizedText, normalizedTerm, tolerance)) {
            addMatch(result, term, 0.55f, IPInfringementMatchKind::Fuzzy, /*block=*/false);
            continue;
        }

        std::vector<std::string> termTokens = significantTokens(term);
        if (allTermTokensPresent(termTokens, inputTokens, /*usePhonetic=*/false)) {
            addMatch(result, term, 0.6f, IPInfringementMatchKind::MultiToken, /*block=*/false);
            continue;
        }

        if (allTermTokensPresent(termTokens, inputTokens, /*usePhonetic=*/true)) {
            addMatch(result, term, 0.4f, IPInfringementMatchKind::Phonetic, /*block=*/false);
            continue;
        }

        if (termTokens.size() < 2) {
            // Single-word review term (Piggy, Jailbreak, ...): fall back
            // to a whole-term Soundex check the way hard-block terms get
            // above, since allTermTokensPresent() intentionally declines
            // terms with fewer than 2 tokens.
            for (const auto& inputToken : inputTokens) {
                if (soundex(inputToken) == soundex(normalizedTerm)) {
                    addMatch(result, term, 0.4f, IPInfringementMatchKind::Phonetic, /*block=*/false);
                    break;
                }
            }
        }
    }

    return result;
}

} // namespace engine::safety
