#pragma once

#include "core/LocalProfile.hpp"
#include "safety/ImageClassifierStub.hpp"
#include "safety/TextClassifierStub.hpp"

namespace engine::safety {

// Kronos ("Moderation Architecture v2"): AgeGroup moved to core::
// LocalProfile.hpp (Phase 1 originally defined it here) -- it's
// fundamentally an account/profile attribute, not a policy-engine-
// specific type. Real alias kept so every existing safety:: call site
// (and this header's own doc comments below) keeps reading naturally.
using AgeGroup = core::AgeGroup;

// What PolicyEngine::decide() below actually recommends the caller do
// with one classified message. `Block` is the real, new behavior this
// phase adds -- see PolicyEngine.hpp's own class comment on why "flag
// but always deliver" (TrustSafetyService::onChatMessage's original
// behavior) was wrong for the user's explicit hard-block list.
enum class PolicyAction { Allow, Warn, QueueForReview, Block };

struct PolicyDecision {
    PolicyAction action = PolicyAction::Allow;
    // A fixed, source-label-shaped string (e.g. "Grooming", "SexualContent+Minor")
    // for the real audit trail -- same "source labels, not free text"
    // convention TrustSafetyService's own `source`/`contextLabel`
    // parameters already establish, not user-authored content.
    const char* reason = "";
};

// The real, new "hard-block vs soft-block/warn vs queue-for-review"
// policy layer the user's spec asks for -- consulted from
// TrustSafetyService::onChatMessage() before a message is delivered,
// closing the real gap that class's own original header comment stated
// plainly: "flagging never blocks delivery by itself." That's still
// correct for ambiguous/borderline categories (Harassment/PiiSolicitation/
// OffPlatformRedirect/Spam below); it was wrong for the user's explicit
// hard-block categories (sexual content involving minors, explicit
// grooming, extremist propaganda), which this class now genuinely blocks
// before delivery, not just after-the-fact.
//
// Real, honest, single-message-and-single-party scope: `ageGroup` is the
// *sender's own* self-declared age (the only identity `onChatMessage`'s
// existing call shape carries synchronously) -- this is a real, stated
// limitation, not a full conversation-pair analysis (which would need
// the recipient's own AgeGroup threaded through too, a larger, separate
// change to the chat send path this phase doesn't make).
class PolicyEngine {
public:
    [[nodiscard]] PolicyDecision decide(const TextClassification& classification, AgeGroup ageGroup) const;

    // Kronos ("Moderation Architecture v2", item A "Image Moderation"):
    // same real hard-block/warn split as the text overload above, applied
    // to ImageClassifierStub's real, honest, filename-only categories --
    // see that class's own header comment for exactly what "real" means
    // here (a keyword match on the filename, not visual content
    // understanding). Gore and ExtremistSymbols always block, the same
    // "always block regardless of age" treatment Hate gets in the text
    // overload -- no ambiguous/artistic-context carve-out this heuristic
    // could honestly make that distinction for. Nudity and Sexualization
    // block for a non-Adult uploader (Minor Mode Enforcement's "no unsafe
    // uploads" half) and warn-only for a self-declared Adult, mirroring
    // SexualContent's own age-gated treatment in the text overload.
    [[nodiscard]] PolicyDecision decide(const ImageClassification& classification, AgeGroup ageGroup) const;
};

} // namespace engine::safety
