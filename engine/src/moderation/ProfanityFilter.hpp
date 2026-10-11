#pragma once

#include <string>
#include <unordered_map>

namespace engine::moderation {

// `censored` is the original text with each matched word replaced by '*' bytes of the same length.
struct ProfanityCheckResult {
    bool containsProfanity = false;
    std::string censored;
};

// Censors swear words and slurs in place, including the usual dodges: leetspeak (sh1t, @ss), look-alike
// letters (Cyrillic, accents, full-width), stretched letters (fuuuck), endings (fucking, shitty) and
// spelling across spaces or dots (f u c k, s.h.i.t). Separate from safety::TextClassifierStub, which flags
// risky phrases (grooming, threats, ...) for blocking and review.
class ProfanityFilter {
public:
    ProfanityFilter();

    // An `anywhere` word is also caught inside longer words (motherfucker); the others only alone or with
    // a common ending (crappy, damned), so "class" or "hello" stay clean.
    void addWord(std::string word, bool anywhere = false);
    void removeWord(const std::string& word);
    [[nodiscard]] size_t wordCount() const { return words_.size(); }

    [[nodiscard]] ProfanityCheckResult check(const std::string& text) const;

private:
    [[nodiscard]] bool matches(const std::string& folded, bool exactOnly) const;

    std::unordered_map<std::string, bool> words_; // normalized word -> anywhere
};

} // namespace engine::moderation
