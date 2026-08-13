#include "safety/ModerationPipeline.hpp"

namespace engine::safety {

namespace {
// Per-category weight for a single flagged message. Illustrative, not
// policy -- see RiskScore.hpp's note on threshold tuning being a trust &
// safety decision, not an engineering one.
constexpr float kTextFlagWeight = 0.15f;
}

RiskScore& ModerationPipeline::scoreFor(PlayerId player) {
    auto it = scores_.find(player);
    if (it == scores_.end()) {
        it = scores_.emplace(player, RiskScore{}).first;
    }
    return it->second;
}

ModerationPipeline::ChatResult ModerationPipeline::processChatMessage(PlayerId player, const std::string& message) {
    auto now = std::chrono::steady_clock::now();
    TextClassification result = textClassifier_.classify(message);

    RiskScore& score = scoreFor(player);
    if (result.flagged) {
        score.apply(RiskSignal{"TextClassifier", kTextFlagWeight * result.categories.size()}, now);
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

} // namespace engine::safety
