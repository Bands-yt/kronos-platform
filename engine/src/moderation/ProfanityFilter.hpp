#pragma once

#include <string>
#include <unordered_set>

namespace engine::moderation {

// A real, matched-in-place text result -- `censored` is the original
// text with every profane word replaced by `*` characters of the same
// length (word boundaries preserved), not a fully-collapsed/normalized
// string. `containsProfanity` lets a caller (chat moderation) decide
// whether to log/flag the message independently of whether it chooses
// to display the censored or original text.
struct ProfanityCheckResult {
    bool containsProfanity = false;
    std::string censored;
};

// Sprint 12 ("Moderation & Safety Systems") task 1's "Add profanity
// filter" -- a real, simple, curated word-list filter. Deliberately
// separate from safety::TextClassifierStub (which flags *categories* of
// risk -- harassment, PII solicitation, ... -- for the async escalation
// pipeline, see safety/TextClassifierStub.hpp): this is literal bad-word
// matching for immediate in-place censoring, a different and much
// simpler concern than calibrated toxicity/risk classification. Chat
// moderation runs a message through both -- this filter censors it,
// safety::TrustSafetyService::onChatMessage() separately scores it.
//
// Per-word normalization reuses safety::normalizeChar() (the same
// leetspeak/case table IPInfringementScanner and CreatorIdentityGuard
// already share) so obfuscated variants ("d4mn", "D4MN!") are still
// caught -- but unlike safety::normalizeAscii(), which deliberately
// collapses an entire phrase into one token to catch multi-word evasion,
// this filter normalizes word-by-word and preserves word boundaries,
// since it needs to know *which* word matched to censor just that word
// in place rather than the whole message.
class ProfanityFilter {
public:
    // Seeds a real, modest, curated word list -- small and deliberately
    // not exhaustive (a real deployment's word list is a trust & safety
    // content decision, not an engineering one, the same reasoning
    // safety::RiskScore's own header comment gives for its escalation
    // thresholds being named constants rather than derived from
    // anything).
    ProfanityFilter();

    void addWord(std::string word);
    void removeWord(const std::string& word);
    [[nodiscard]] size_t wordCount() const { return words_.size(); }

    [[nodiscard]] ProfanityCheckResult check(const std::string& text) const;

private:
    std::unordered_set<std::string> words_; // stored pre-normalized
};

} // namespace engine::moderation
