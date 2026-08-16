#include "safety/BehavioralPatternAnalyzer.hpp"

#include <algorithm>
#include <unordered_set>

#include "safety/TextClassifierStub.hpp"

namespace engine::safety {

namespace {

// Illustrative, tunable constants -- real thresholds for a real trust &
// safety heuristic are a content-policy decision, not an engineering
// one (same disclaimer TextClassifierStub's own word lists carry). The
// window is 5 real minutes: long enough to catch a real burst, short
// enough that this stays a live "what's this account doing right now"
// signal rather than a lifetime tally (RiskScore's own decay already
// owns "did this fade over time" -- this class doesn't duplicate that).
constexpr double kRecentWindowSeconds = 300.0;
constexpr size_t kRapidMessageCountThreshold = 10;
constexpr size_t kDistinctMinorRecipientThreshold = 3;
constexpr size_t kHarassmentCountThreshold = 3;
constexpr size_t kOffPlatformCountThreshold = 3;

bool hasCategory(const TextClassification& c, TextRiskCategory category) {
    return std::find(c.categories.begin(), c.categories.end(), category) != c.categories.end();
}

} // namespace

BehaviorSignal BehavioralPatternAnalyzer::analyze(const std::vector<DirectMessageSample>& recentSamplesFromOneSender,
                                                    double nowServerTimestampSeconds) const {
    std::vector<const DirectMessageSample*> windowed;
    for (const DirectMessageSample& sample : recentSamplesFromOneSender) {
        if (nowServerTimestampSeconds - sample.serverTimestampSeconds <= kRecentWindowSeconds) {
            windowed.push_back(&sample);
        }
    }

    std::unordered_set<uint32_t> distinctMinorRecipients;
    size_t harassmentCount = 0;
    size_t offPlatformCount = 0;
    TextClassifierStub textClassifier;

    for (const DirectMessageSample* sample : windowed) {
        if (sample->recipientAgeGroup != core::AgeGroup::Adult) {
            distinctMinorRecipients.insert(sample->recipient);
        }
        TextClassification classification = textClassifier.classify(sample->text);
        if (hasCategory(classification, TextRiskCategory::Harassment)) ++harassmentCount;
        if (hasCategory(classification, TextRiskCategory::OffPlatformRedirect)) ++offPlatformCount;
    }

    // Ordered by severity -- the first real match wins, same "most severe
    // pattern determines the action" convention safety::PolicyEngine::
    // decide() already establishes for text/image categories.
    if (distinctMinorRecipients.size() >= kDistinctMinorRecipientThreshold) {
        return BehaviorSignal{BehaviorRiskTier::High, "RepeatedContactWithMinors"};
    }
    if (windowed.size() >= kRapidMessageCountThreshold) {
        return BehaviorSignal{BehaviorRiskTier::Medium, "RapidDmEscalation"};
    }
    if (harassmentCount >= kHarassmentCountThreshold) {
        return BehaviorSignal{BehaviorRiskTier::Medium, "HarassmentPattern"};
    }
    if (offPlatformCount >= kOffPlatformCountThreshold) {
        return BehaviorSignal{BehaviorRiskTier::Low, "RepeatedOffPlatformRedirectAttempts"};
    }

    return BehaviorSignal{BehaviorRiskTier::None, ""};
}

} // namespace engine::safety
