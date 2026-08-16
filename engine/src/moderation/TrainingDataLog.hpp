#pragma once

#include <string>

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

} // namespace engine::moderation
