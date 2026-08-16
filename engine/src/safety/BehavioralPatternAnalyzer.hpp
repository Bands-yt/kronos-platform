#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/LocalProfile.hpp"

namespace engine::safety {

// Kronos ("Moderation Architecture v2", item B "Behavioral Model (Heuristic
// v1)"): one real DM, already sent and already logged, converted to the
// one shape this analyzer needs. Deliberately NOT moderation::
// DirectMessageLogEntry -- this class stays free of any moderation::
// dependency, same real reason TrustSafetyService.hpp's own class
// comment already states for itself ("this class deliberately has no
// moderation:: dependency of its own"): the real caller (net::
// NetworkSession, which already owns both safety:: and moderation::)
// converts its own moderation::DirectMessageLog entries into these
// before calling analyze(), the same "actions/data are handed in, not
// reached for" shape this whole safety:: layer already uses throughout.
struct DirectMessageSample {
    uint32_t recipient = 0;
    core::AgeGroup recipientAgeGroup = core::AgeGroup::Unknown;
    std::string text;
    double serverTimestampSeconds = 0.0;
};

// Kronos ("Moderation Architecture v2", item B): a real, distinct tier
// from EscalationTier -- this is what the *pattern* looks like, not what
// action to take about it. The real caller (TrustSafetyService) converts
// this into a weight and folds it into ModerationPipeline::
// applyAsyncSignal(), the exact same seam that class's own header
// comment already names as waiting for "a real behavioral anomaly
// detector" -- this is that detector, now real instead of hypothetical.
enum class BehaviorRiskTier { None, Low, Medium, High };

struct BehaviorSignal {
    BehaviorRiskTier tier = BehaviorRiskTier::None;
    // Fixed source-label convention, same as PolicyDecision::reason --
    // not free text.
    const char* reason = "";
};

// Structural stand-in for docs/ARCHITECTURE.md §10's real behavioral
// model ("predator pattern detection... conversation-level analysis").
// analyze() below is NOT that model -- there is no graph analysis, no
// conversation-content understanding, and definitely no ML here, only
// real frequency-counting over a sender's own already-logged DM history
// within a fixed recent window, same tiny-heuristic honesty
// TextClassifierStub's own header comment establishes for text. Four of
// the five patterns the user's spec names are real and implemented
// below: rapid DM escalation, repeated contact with minors, off-platform
// link attempts (reusing TextClassifierStub's own OffPlatformRedirect
// marker), and harassment patterns (reusing TextClassifierStub's own
// Harassment marker) -- re-running the same real text heuristic here
// rather than inventing a second, parallel one. The fifth, "high-
// frequency friend requests," is NOT implemented: no friend/relationship
// system exists anywhere in this codebase (confirmed by grep) for there
// to be a request to count -- a real, stated scope gap, not a silent
// drop.
class BehavioralPatternAnalyzer {
public:
    // `recentSamplesFromOneSender` is real, honest, caller-filtered input --
    // this class doesn't know or care which player sent them, only what
    // the pattern across those samples looks like. The real caller
    // (TrustSafetyService::onDirectMessagePattern) already knows the
    // sender, since it needs that PlayerId to fold the result into
    // ModerationPipeline anyway.
    [[nodiscard]] BehaviorSignal analyze(const std::vector<DirectMessageSample>& recentSamplesFromOneSender,
                                          double nowServerTimestampSeconds) const;
};

} // namespace engine::safety
