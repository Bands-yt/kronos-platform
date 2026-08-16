#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>

#include "core/LocalProfile.hpp"
#include "safety/RiskScore.hpp"
#include "safety/TextClassifierStub.hpp"

namespace engine::safety {

using PlayerId = uint32_t;

// The escalation pipeline from docs/ARCHITECTURE.md §10's flowchart,
// minus the actual mute/restrict/report *actions* (those belong to
// TrustSafetyService, which owns the callbacks into systems this class
// doesn't know about -- Players, legal-reporting integration, etc). This
// class owns exactly the decision: given a new signal for a player, what
// tier does their updated risk score land in.
class ModerationPipeline {
public:
    struct ChatResult {
        TextClassification classification;
        EscalationTier tier;
    };

    // Runs the text classifier, folds the result into `player`'s RiskScore
    // (creating one on first contact), and returns both the raw
    // classification (the caller may want it independent of the
    // account-level tier -- e.g. a client-side warning) and the resulting
    // tier. This is the synchronous send-path step from §10 -- the
    // classifier itself is budgeted at <10ms in the real design; this
    // stub's classifier is a keyword heuristic (see TextClassifierStub.hpp)
    // so that budget isn't meaningfully exercisable yet, but the call
    // shape -- one synchronous classification per message, before any
    // async work -- is real.
    // Kronos ("Moderation Architecture v2", "Minor Mode Enforcement"):
    // `ageGroup` defaults to Unknown (the real, conservative default,
    // see core::AgeGroup's own comment) so every pre-v2 call site keeps
    // compiling and behaving identically -- a real, known Minor (or
    // Unknown) sender's flagged signal contributes at
    // kMinorModeRiskMultiplier (see .cpp) instead of the plain per-
    // category weight, so the exact same repeated pattern escalates
    // through the tiers *faster* for a real (or possibly) minor account
    // -- the real, concrete meaning of "stricter thresholds" here,
    // without duplicating RiskScore's own tier boundaries per age group.
    ChatResult processChatMessage(PlayerId player, const std::string& message, core::AgeGroup ageGroup = core::AgeGroup::Unknown);

    // Async-path signal folding -- called once a conversation-level
    // grooming model, image hash-match, or behavioral anomaly detector
    // (all currently unimplemented, per §10/§12's structural-only scope)
    // produces a result. Takes a raw weight rather than a typed result
    // from any specific detector, since none of those detectors exist yet
    // to define a shape for this to depend on.
    EscalationTier applyAsyncSignal(PlayerId player, const char* source, float weight);

    [[nodiscard]] float currentRisk(PlayerId player) const;

    // Kronos ("Moderation Architecture v1", Phase 1): the real tier
    // currentRisk()'s own value would map to right now -- added for
    // studio::plugins::ModerationPanel's real risk-score/tier dashboard
    // readout (previously nothing surfaced this in any UI even though
    // it was already computed here).
    [[nodiscard]] EscalationTier currentTier(PlayerId player) const;

private:
    [[nodiscard]] RiskScore& scoreFor(PlayerId player);

    std::unordered_map<PlayerId, RiskScore> scores_;
    TextClassifierStub textClassifier_;
};

} // namespace engine::safety
