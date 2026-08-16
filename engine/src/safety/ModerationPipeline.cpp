#include "safety/ModerationPipeline.hpp"

namespace engine::safety {

namespace {
// Per-category weight for a single flagged message. Illustrative, not
// policy -- see RiskScore.hpp's note on threshold tuning being a trust &
// safety decision, not an engineering one.
constexpr float kTextFlagWeight = 0.15f;
// Kronos ("Moderation Architecture v2", "Minor Mode Enforcement"): a
// real, illustrative multiplier -- the same repeated flagged pattern
// escalates a real (or possibly) minor account through the tiers
// faster. Same "a trust & safety policy decision, not an engineering
// one" framing as kTextFlagWeight above -- a real tuning knob, not a
// derived value.
constexpr float kMinorModeRiskMultiplier = 1.5f;
}

RiskScore& ModerationPipeline::scoreFor(PlayerId player) {
    auto it = scores_.find(player);
    if (it == scores_.end()) {
        it = scores_.emplace(player, RiskScore{}).first;
    }
    return it->second;
}

ModerationPipeline::ChatResult ModerationPipeline::processChatMessage(PlayerId player, const std::string& message,
                                                                       core::AgeGroup ageGroup) {
    auto now = std::chrono::steady_clock::now();
    TextClassification result = textClassifier_.classify(message);

    RiskScore& score = scoreFor(player);
    if (result.flagged) {
        float weight = kTextFlagWeight * static_cast<float>(result.categories.size());
        if (ageGroup != core::AgeGroup::Adult) weight *= kMinorModeRiskMultiplier;
        score.apply(RiskSignal{"TextClassifier", weight}, now);
    }
    return ChatResult{result, score.tier(now)};
}

EscalationTier ModerationPipeline::applyAsyncSignal(PlayerId player, const char* source, float weight) {
    auto now = std::chrono::steady_clock::now();
    RiskScore& score = scoreFor(player);
    score.apply(RiskSignal{source, weight}, now);
    return score.tier(now);
}

float ModerationPipeline::currentRisk(PlayerId player) const {
    auto it = scores_.find(player);
    if (it == scores_.end()) return 0.0f;
    return it->second.value(std::chrono::steady_clock::now());
}

EscalationTier ModerationPipeline::currentTier(PlayerId player) const {
    auto it = scores_.find(player);
    if (it == scores_.end()) return EscalationTier::Log;
    return it->second.tier(std::chrono::steady_clock::now());
}

} // namespace engine::safety
