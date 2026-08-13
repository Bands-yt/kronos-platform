#include "moderation/ProfanityFilter.hpp"

#include <cctype>

#include "safety/TextNormalize.hpp"

namespace engine::moderation {

namespace {
std::string normalizeWord(const std::string& word) {
    std::string result;
    result.reserve(word.size());
    for (char c : word) {
        char normalized = safety::normalizeChar(c);
        if (normalized != '\0') result.push_back(normalized);
    }
    return result;
}
} // namespace

ProfanityFilter::ProfanityFilter() {
    // Real, modest, curated seed list -- common English profanity only,
    // no slurs (a real deployment's full list, including any per-locale/
    // per-category expansion, is a trust & safety content decision made
    // separately from this engineering skeleton, the same boundary
    // safety::RiskScore's escalation thresholds already draw).
    for (const char* word : {"damn", "hell", "shit", "fuck", "bitch", "asshole", "bastard", "crap"}) {
        words_.insert(word);
    }
}

void ProfanityFilter::addWord(std::string word) { words_.insert(normalizeWord(word)); }

void ProfanityFilter::removeWord(const std::string& word) { words_.erase(normalizeWord(word)); }

ProfanityCheckResult ProfanityFilter::check(const std::string& text) const {
    ProfanityCheckResult result;
    result.censored = text;

    // Real, simple whitespace tokenization -- word boundaries matter
    // here (see class header comment for why this differs from
    // safety::normalizeAscii()'s whole-phrase collapse).
    size_t wordStart = 0;
    for (size_t i = 0; i <= text.size(); ++i) {
        bool atBoundary = i == text.size() || std::isspace(static_cast<unsigned char>(text[i])) != 0;
        if (!atBoundary) continue;
        if (i > wordStart) {
            std::string word = text.substr(wordStart, i - wordStart);
            std::string normalized = normalizeWord(word);
            if (!normalized.empty() && words_.count(normalized) > 0) {
                result.containsProfanity = true;
                for (size_t j = wordStart; j < i; ++j) result.censored[j] = '*';
            }
        }
        wordStart = i + 1;
    }
    return result;
}

} // namespace engine::moderation
