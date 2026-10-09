#pragma once

#include <string>

#include <cstdint>

#include "safety/GeminiModerationClient.hpp"
#include "safety/TextClassifierStub.hpp"

namespace engine::moderation {

// Kronos ("Moderation Architecture v2", item H "ML Retraining Pipeline
// (Stub)"): the real, second half of the user's own named pair --
// reversed_decisions.log (studio::plugins::ModerationPanel, Phase 1)
// already captures real, confirmed false positives; this captures the
// real, raw (text, real heuristic classification) pairs a future real
// model would actually train on. Explicitly, plainly: there is NO
// training happening here, and nothing in this codebase reads this file
// back -- it's future-ready data collection only, the same honesty this
// whole safety:: layer already applies everywhere else (see
// safety::TextClassifierStub's own header comment). Real, bounded scope:
// only FLAGGED messages are appended, same "the overwhelming majority of
// routine traffic isn't worth persisting long-term" reasoning
// safety::TrustSafetyService::dispatchEscalation()'s own comment already
// gives for EscalationEventLog -- logging every clean message here would
// be enormous volume with near-zero real training value.
[[nodiscard]] std::string formatTrainingDataLine(const std::string& text, const safety::TextClassification& classification);

// Real, honest `false` if `path` couldn't be opened for append -- same
// "fail soft, say so" convention every other real disk write in this
// codebase follows. A real, honest no-op is the caller's job to apply
// when `classification.flagged` is false (see this file's own header
// comment on why only flagged messages are worth appending) -- this
// function itself doesn't gate on that, so a caller with a different
// real reason to log an unflagged sample still can.
bool appendTrainingDataSample(const std::string& path, const std::string& text,
                               const safety::TextClassification& classification);

// One chat message and every verdict on it, for reviewing or training a
// moderation model. Written as one JSON object per line.
struct ChatReviewSample {
    uint64_t timeMillis = 0;
    std::string text;
    std::string shown;
    bool profanity = false;
    safety::TextClassification local;
    bool geminiAsked = false;
    safety::ModerationVerdict gemini;
    // delivered, censored, blocked_local, blocked_gemini, gemini_timeout
    std::string outcome;
};

[[nodiscard]] std::string formatChatReviewLine(const ChatReviewSample& sample);
bool appendChatReviewSample(const std::string& path, const ChatReviewSample& sample);

} // namespace engine::moderation
