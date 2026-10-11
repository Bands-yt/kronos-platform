#include "moderation/ProfanityFilter.hpp"

#include <array>
#include <cstdint>
#include <unordered_set>
#include <vector>

namespace engine::moderation {

namespace {

// Caught inside longer words too. Kept to words with no innocent use as part of another word.
constexpr const char* kAnywhere[] = {
    "fuck", "phuck", "shit", "bitch", "cunt", "nigger", "nigga", "whore", "slut", "porn", "retard",
};

// Caught alone or with an ending (see kEndings).
constexpr const char* kWords[] = {
    "damn", "dammit", "goddamn", "hell", "crap", "ass", "arse", "asshole", "arsehole", "dumbass",
    "jackass", "asshat", "asswipe", "bastard", "dick", "cock", "prick", "pussy", "pussies", "twat",
    "wank", "wanker", "tit", "boob", "piss", "bollock", "bollocks", "sex", "nude", "horny", "cum",
    "dildo", "penis", "vagina", "hoe", "thot", "milf", "rape", "rapist", "fag", "faggot", "dyke",
    "kike", "spic", "chink", "tranny", "wtf", "stfu", "gtfo", "fuk", "fuq", "fck", "fcuk", "fk",
    "sht", "shyt", "btch", "biatch", "biotch",
};

constexpr const char* kEndings[] = {
    "s", "es", "ed", "er", "ers", "ing", "y", "ie", "ies", "head", "heads", "hole", "holes",
    "face", "faces", "bag", "bags", "wit", "wits", "it", "ish", "ery",
};

// Ordinary words that look like a listed word plus an ending, or contain an `anywhere` word.
const std::unordered_set<std::string>& allowed() {
    static const std::unordered_set<std::string> words = {
        "scunthorpe", "shitake", "shiitake", "matsushita", "niggard", "niggardly", "retardant",
        "spicy", "cocky", "booby", "boobies",
    };
    return words;
}

// Reads one UTF-8 code point; returns 0 for a malformed byte (which is then skipped).
uint32_t decode(const std::string& text, size_t& i) {
    const auto byte = [&](size_t k) { return static_cast<unsigned char>(text[k]); };
    unsigned char c = byte(i);
    int extra = c < 0x80 ? 0 : (c >> 5) == 0x6 ? 1 : (c >> 4) == 0xE ? 2 : (c >> 3) == 0x1E ? 3 : -1;
    if (extra < 0 || i + extra >= text.size() + (extra == 0 ? 1 : 0)) {
        ++i;
        return 0;
    }
    uint32_t cp = extra == 0 ? c : c & (0x3F >> extra);
    for (int k = 1; k <= extra; ++k) {
        if ((byte(i + k) & 0xC0) != 0x80) {
            ++i;
            return 0;
        }
        cp = (cp << 6) | (byte(i + k) & 0x3F);
    }
    i += static_cast<size_t>(extra) + 1;
    return cp;
}

// Folds a code point to a lowercase ASCII letter. 'I' marks a symbol that reads as either i or l.
// Returns 0 for a separator and 1 for an invisible character that should be skipped.
char fold(uint32_t cp) {
    if (cp >= 'A' && cp <= 'Z') return static_cast<char>(cp - 'A' + 'a');
    if (cp >= 'a' && cp <= 'z') return static_cast<char>(cp);
    switch (cp) {
        case '0': return 'o';
        case '1': case '|': return 'I';
        case '!': return 'i';
        case '3': return 'e';
        case '4': case '@': return 'a';
        case '5': case '$': return 's';
        case '7': case '+': return 't';
        case '8': return 'b';
        case '9': return 'g';
        case 0x00AD: case 0x200B: case 0x200C: case 0x200D: case 0x2060: case 0xFEFF: return 1;
        default: break;
    }
    if (cp >= 0xFF21 && cp <= 0xFF3A) return static_cast<char>(cp - 0xFF21 + 'a');
    if (cp >= 0xFF41 && cp <= 0xFF5A) return static_cast<char>(cp - 0xFF41 + 'a');
    if (cp >= 0xC0 && cp <= 0xFF) {
        static const char latin1[] = "aaaaaaaceeeeiiiidnooooo\0ouuuuyts" // C0-DF
                                     "aaaaaaaceeeeiiiidnooooo\0ouuuuyty"; // E0-FF
        return latin1[cp - 0xC0];
    }
    switch (cp) {
        case 0x0430: case 0x0410: return 'a';
        case 0x0432: case 0x0412: return 'b';
        case 0x0435: case 0x0415: case 0x0451: return 'e';
        case 0x043A: case 0x041A: return 'k';
        case 0x043C: case 0x041C: return 'm';
        case 0x043D: case 0x041D: return 'h';
        case 0x043E: case 0x041E: return 'o';
        case 0x0440: case 0x0420: return 'p';
        case 0x0441: case 0x0421: return 'c';
        case 0x0442: case 0x0422: return 't';
        case 0x0443: case 0x0423: return 'y';
        case 0x0445: case 0x0425: return 'x';
        case 0x0455: case 0x0405: return 's';
        case 0x0456: case 0x0406: return 'i';
        case 0x03B1: return 'a';
        case 0x03BF: case 0x039F: return 'o';
        default: return 0;
    }
}

struct Token {
    size_t begin = 0;
    size_t end = 0;
    std::string letters;          // folded, with 'I' for i/l look-alikes
    bool afterApostrophe = false; // "he'll" must not be joined into "hell"
};

std::vector<Token> tokenize(const std::string& text) {
    std::vector<Token> tokens;
    Token current;
    bool apostrophe = false;
    size_t i = 0;
    while (i < text.size()) {
        size_t start = i;
        uint32_t cp = decode(text, i);
        char c = fold(cp);
        if (c == 1) continue;
        if (c == 0) {
            if (!current.letters.empty()) tokens.push_back(current);
            current = Token{};
            apostrophe = cp == '\'' || cp == 0x2019;
            continue;
        }
        if (current.letters.empty()) {
            current.begin = start;
            current.afterApostrophe = apostrophe;
        }
        current.letters.push_back(c);
        current.end = i;
    }
    if (!current.letters.empty()) tokens.push_back(current);
    return tokens;
}

// Shortens runs of one letter to at most `keep` (fuuuck -> fuck with 1, asss -> ass with 2).
std::string collapseRuns(const std::string& s, size_t keep) {
    std::string out;
    char previous = 0;
    size_t run = 0;
    for (char c : s) {
        run = c == previous ? run + 1 : 1;
        previous = c;
        if (run <= keep) out.push_back(c);
    }
    return out;
}

bool hasDouble(const std::string& s) {
    for (size_t i = 1; i < s.size(); ++i) {
        if (s[i] == s[i - 1]) return true;
    }
    return false;
}

std::string normalizeWord(const std::string& word) {
    std::string out;
    size_t i = 0;
    while (i < word.size()) {
        char c = fold(decode(word, i));
        if (c == 'I') c = 'i';
        if (c > 1) out.push_back(c);
    }
    return out;
}

bool endsWithEnding(const std::string& rest) {
    for (const char* ending : kEndings) {
        if (rest == ending) return true;
    }
    return false;
}

} // namespace

ProfanityFilter::ProfanityFilter() {
    for (const char* word : kAnywhere) words_[word] = true;
    for (const char* word : kWords) words_[word] = false;
}

void ProfanityFilter::addWord(std::string word, bool anywhere) {
    std::string normalized = normalizeWord(word);
    if (!normalized.empty()) words_[normalized] = anywhere;
}

void ProfanityFilter::removeWord(const std::string& word) { words_.erase(normalizeWord(word)); }

bool ProfanityFilter::matches(const std::string& folded, bool exactOnly) const {
    // Try 'I' as both i and l (sh1t, 1ick).
    for (char look : {'i', 'l'}) {
        std::string s = folded;
        for (char& c : s) {
            if (c == 'I') c = look;
        }
        const std::string single = collapseRuns(s, 1);
        const std::string pairs = collapseRuns(s, 2);
        if (allowed().count(single) > 0 || allowed().count(pairs) > 0) continue;
        for (const auto& [word, anywhere] : words_) {
            const std::string& key = hasDouble(word) ? pairs : single;
            if (key == word) return true;
            if (exactOnly) continue;
            if (anywhere) {
                if (key.find(word) != std::string::npos) return true;
            } else if (key.size() > word.size() && key.compare(0, word.size(), word) == 0 &&
                       endsWithEnding(key.substr(word.size()))) {
                return true;
            }
        }
    }
    return false;
}

ProfanityCheckResult ProfanityFilter::check(const std::string& text) const {
    ProfanityCheckResult result;
    result.censored = text;
    const std::vector<Token> tokens = tokenize(text);
    std::vector<bool> hit(tokens.size(), false);

    for (size_t t = 0; t < tokens.size(); ++t) hit[t] = matches(tokens[t].letters, false);

    // Words spelled across spaces or dots: runs of short pieces ("f u c k", "sh it") joined back up, longest
    // first. Only an exact word counts here, so "i am" in front of it isn't caught as well.
    constexpr size_t kShortPiece = 3;
    for (size_t first = 0; first < tokens.size(); ++first) {
        std::string joined;
        size_t longest = first;
        for (size_t last = first; last < tokens.size() && tokens[last].letters.size() <= kShortPiece; ++last) {
            if (last > first && tokens[last].afterApostrophe) break;
            joined += tokens[last].letters;
            if (last > first && matches(joined, true)) longest = last;
        }
        if (longest == first) continue;
        for (size_t k = first; k <= longest; ++k) hit[k] = true;
        first = longest;
    }

    for (size_t t = 0; t < tokens.size(); ++t) {
        if (!hit[t]) continue;
        result.containsProfanity = true;
        for (size_t b = tokens[t].begin; b < tokens[t].end; ++b) result.censored[b] = '*';
    }
    return result;
}

} // namespace engine::moderation
