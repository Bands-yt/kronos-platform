#pragma once

#include <functional>
#include <string>

#include "safety/AssetSafetyGuard.hpp"
#include "safety/CreatorIdentityGuard.hpp"
#include "safety/IPInfringementScanner.hpp"
#include "safety/ModerationPipeline.hpp"
#include "safety/VoiceASRStub.hpp"

namespace engine::safety {

// The Trust & Safety Service interface from docs/ARCHITECTURE.md §10 --
// "its own service boundary, never embedded in the game server's hot
// path". In this skeleton it's a plain C++ class rather than a separate
// process/service with its own IPC boundary; that deployment topology
// (async queue, separate service) is an infrastructure decision layered
// on top of this interface, not a reason to change the interface itself --
// chat/voice/image call sites shouldn't need to know whether this runs
// in-process or over a queue to a real service.
//
// Escalation *actions* (actually muting a player, opening a human-review
// case, filing a legal report) are injected via Callbacks rather than
// implemented here, because none of Players/moderator-tooling/legal-
// reporting-integration exist yet in this codebase for this class to call
// directly.
class TrustSafetyService {
public:
    struct Callbacks {
        std::function<void(PlayerId)> onMute;
        std::function<void(PlayerId)> onRestrict;
        // `reason` is a source label (e.g. "TextClassifier", "PhotoDNA"),
        // not free text -- see docs/ARCHITECTURE.md §10's data-governance
        // note about minimizing what's retained around moderation events.
        std::function<void(PlayerId, const char* reason)> onHumanReviewRequired;
        std::function<void(PlayerId, const char* reason)> onLegalReportRequired;
    };

    void setCallbacks(Callbacks callbacks) { callbacks_ = std::move(callbacks); }

    // Synchronous send-path check (§10's <10ms budget). The message is
    // still delivered regardless of the classification result -- flagging
    // never blocks delivery by itself in this design, only the escalation
    // tiers act on the *account*, matching the flowchart's
    // "clear or flagged -> Deliver" branch.
    TextClassification onChatMessage(PlayerId sender, const std::string& message);

    // Runs the uploaded file through AssetSafetyGuard's magic-byte/
    // dimension/embedded-metadata checks and folds the result into
    // `uploader`'s risk score, same escalation path as every other
    // signal source. What this does NOT do, and still needs (§10):
    // PhotoDNA/Thorn Safer hash-matching against known CSAM material, or
    // a general vision classifier for *novel* harmful content -- both
    // are content-understanding systems entirely outside
    // AssetSafetyGuard's scope (structural file-format checks, not image
    // understanding). Those remain the real gap this call site still
    // needs, just no longer completely unimplemented.
    void onImageUpload(PlayerId uploader, const std::string& assetPath);

    // Feeds a voice chunk into VoiceASRStub (currently a no-op, see its
    // header) and, once that produces a real transcript, would run it
    // through the same text classifier path as chat -- not reachable yet
    // since transcribeRollingBuffer() always reports failure.
    void onVoiceChunk(PlayerId speaker, const AudioChunk& chunk);

    // Runs `text` (an asset/import name, a marketplace listing title, a
    // plugin publish name -- any creator-supplied label) through
    // IPInfringementScanner and folds the result into `creator`'s risk
    // score, same escalation path as every other signal source. Unlike
    // onChatMessage, the caller MUST check IPInfringementResult::blocked
    // and reject the submission itself when true -- this call flags/
    // escalates the account but does not (and cannot, from here) stop
    // whatever action `text` is attached to. `contextLabel` is a source
    // tag for the audit trail (e.g. "MigrationImport"), same convention
    // as dispatchEscalation's `source` parameter elsewhere in this class.
    IPInfringementResult onCreatorContentSubmission(PlayerId creator, const std::string& text, const char* contextLabel);

    // Runs `displayName` through CreatorIdentityGuard and folds the
    // result into `creator`'s risk score, same escalation path as every
    // other signal source. Structurally ready for whenever a real
    // Players/accounts system exists to call this on account creation
    // and on every display-name change -- no such system exists in this
    // codebase yet (PlayerId is still just an opaque integer typedef, see
    // RiskScore.hpp), so nothing calls this today. Kept here now, next to
    // onCreatorContentSubmission's identically-shaped seam, rather than
    // bolted on later as an afterthought once Players exists.
    IdentityCheckResult onCreatorDisplayNameChange(PlayerId creator, const std::string& displayName);

    // Sprint 12 ("Anti-Cheat Foundation"): folds a real anti-cheat signal
    // (e.g. anticheat::RollingEventCounter flagging repeated real
    // server-rejected movement inputs, or repeated real
    // core::EarnThrottle cap hits) into `player`'s risk score, same
    // escalation path as every other signal source in this class
    // (onImageUpload/onCreatorContentSubmission/onCreatorDisplayNameChange
    // all share this exact "applyAsyncSignal then dispatchEscalation"
    // shape) -- server-side anti-cheat detections are trust & safety
    // signals too, not a separate system with its own separate mute/
    // restrict logic. `source` is a fixed subsystem label for the audit
    // trail (e.g. "MovementRejection", "CurrencyAnomaly"), matching every
    // other `source`/`contextLabel` parameter in this class.
    EscalationTier onAntiCheatSignal(PlayerId player, const char* source, float weight);

private:
    void dispatchEscalation(PlayerId player, EscalationTier tier, const char* source);

    ModerationPipeline pipeline_;
    VoiceASRStub voiceAsr_;
    IPInfringementScanner ipScanner_;
    CreatorIdentityGuard identityGuard_;
    AssetSafetyGuard assetGuard_;
    Callbacks callbacks_;
};

} // namespace engine::safety
