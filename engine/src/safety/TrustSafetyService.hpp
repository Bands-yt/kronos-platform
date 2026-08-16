#pragma once

#include <functional>
#include <string>
#include <vector>

#include "safety/AssetSafetyGuard.hpp"
#include "safety/BehavioralPatternAnalyzer.hpp"
#include "safety/CreatorIdentityGuard.hpp"
#include "safety/ImageClassifierStub.hpp"
#include "safety/IPInfringementScanner.hpp"
#include "safety/ModerationPipeline.hpp"
#include "safety/PolicyEngine.hpp"
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
        // Kronos ("Moderation Architecture v1", Phase 1): fired when
        // safety::PolicyEngine returns PolicyAction::Warn for a real
        // message -- the caller's own real "show the user a warning"
        // hook, same real, injected-not-implemented-here shape as every
        // other callback in this struct. `reason` is a source label,
        // same convention as onHumanReviewRequired/onLegalReportRequired.
        std::function<void(PlayerId, const char* reason)> onWarn;
        // Fired for every real escalation dispatch at Mute/Restrict/
        // HumanReview/LegalReport tier (never for the baseline `Log`
        // tier, which stays a real no-op here exactly as it always has
        // -- see dispatchEscalation()'s own comment on why: this is a
        // real audit trail of actual escalations, not a duplicate of
        // ChatLog's own per-message record). This class deliberately
        // has no moderation:: dependency of its own (see this class's
        // own header comment on why: "Trust & Safety Service... its own
        // service boundary"), so a real caller wires this straight to
        // moderation::EscalationEventLog::record() -- same "actions are
        // injected via Callbacks, not implemented here" shape as every
        // other callback in this struct.
        std::function<void(PlayerId, EscalationTier, const char* source)> onEscalationEvent;
    };

    void setCallbacks(Callbacks callbacks) { callbacks_ = std::move(callbacks); }

    // Synchronous send-path check (§10's <10ms budget). Real change
    // (Kronos "Moderation Architecture v1", Phase 1): the returned
    // TextClassification::blocked now genuinely means "do not deliver
    // this" for the real hard-block categories (safety::PolicyEngine) --
    // "flagging never blocks delivery by itself" is still true for
    // every *other* (ambiguous/borderline) category, only the specific
    // hard-block ones now stop delivery. The caller (net::NetworkSession's
    // real chat broadcast path) MUST check `blocked` and skip the
    // broadcast when true, same "caller must honor the flag" contract
    // onCreatorContentSubmission()'s IPInfringementResult::blocked
    // already establishes.
    //
    // `senderAgeGroup` defaults to AgeGroup::Unknown -- the real,
    // conservative default (see AgeGroup's own comment) for the real,
    // stated reason that no per-connected-remote-player age signal
    // exists in the network protocol yet (core::LocalProfile::ageGroup
    // is real but local-only today -- wiring a real per-player age
    // handshake into JoinRequest is a separate, later change). Every
    // existing call site keeps compiling and now gets the conservative,
    // safety-first default automatically, not silently unsafe leniency.
    TextClassification onChatMessage(PlayerId sender, const std::string& message,
                                      AgeGroup senderAgeGroup = AgeGroup::Unknown);

    // Kronos ("Moderation Architecture v2", item A "Image Moderation"):
    // `blocked` is the real, new contract this struct adds -- mirrors
    // TextClassification::blocked's exact same "caller MUST check and
    // reject" meaning, same convention IPInfringementResult::blocked
    // already established for onCreatorContentSubmission. Splitting
    // structural findings (AssetSafetyGuard) from content-keyword
    // findings (ImageClassifierStub) rather than merging them into one
    // opaque bool keeps each real, distinct signal source inspectable by
    // a caller that wants to show *why* something was blocked.
    struct ImageUploadResult {
        AssetSafetyReport structuralReport;
        ImageClassification classification;
        bool blocked = false;
    };

    // Runs the uploaded file through AssetSafetyGuard's magic-byte/
    // dimension/embedded-metadata checks AND ImageClassifierStub's real
    // filename-keyword heuristic (see that class's own header comment
    // for exactly what "real" means there), folding both into
    // `uploader`'s risk score, same escalation path as every other
    // signal source. `uploaderAgeGroup` defaults to AgeGroup::Unknown,
    // same real, conservative-by-default convention as onChatMessage's
    // own `senderAgeGroup` parameter. What this does NOT do, and still
    // needs (§10): PhotoDNA/Thorn Safer hash-matching against known CSAM
    // material, or a general vision classifier for *novel* harmful
    // content over actual decoded pixels -- both are content-
    // understanding systems entirely outside what either AssetSafetyGuard
    // (structural file-format checks) or ImageClassifierStub (filename
    // text only) can see. Those remain the real gap this call site still
    // needs, just no longer completely unimplemented.
    ImageUploadResult onImageUpload(PlayerId uploader, const std::string& assetPath,
                                     AgeGroup uploaderAgeGroup = AgeGroup::Unknown);

    // Kronos ("Moderation Architecture v2", item B "Behavioral Model
    // (Heuristic v1)"): the real async-signal caller for
    // BehavioralPatternAnalyzer -- see that class's own header comment
    // for exactly what it does and does not detect. `sender`'s own
    // recent DM history is handed in already-filtered-and-converted by
    // the real caller (net::NetworkSession's DM send handler, which owns
    // moderation::DirectMessageLog and converts its entries into
    // safety::DirectMessageSample) rather than this class reaching into
    // moderation:: itself -- same "no moderation:: dependency of its
    // own" rule this class's own header comment already states.
    EscalationTier onDirectMessagePattern(PlayerId sender, const std::vector<DirectMessageSample>& recentSamplesFromSender,
                                           double nowServerTimestampSeconds);

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

    // Kronos ("Moderation Architecture v1", Phase 1): real, read-only
    // pass-throughs to the real, already-computed risk pipeline --
    // ModerationPipeline::currentRisk()/currentTier() were already
    // public, just never surfaced in any UI (studio::plugins::
    // ModerationPanel's new risk-score/tier readout is the first real
    // caller). 0.0f/EscalationTier::Log for a player with no signals
    // yet, matching ModerationPipeline's own real "no entry = no risk"
    // default.
    [[nodiscard]] float currentRisk(PlayerId player) const { return pipeline_.currentRisk(player); }
    [[nodiscard]] EscalationTier currentTier(PlayerId player) const { return pipeline_.currentTier(player); }

private:
    void dispatchEscalation(PlayerId player, EscalationTier tier, const char* source);

    ModerationPipeline pipeline_;
    PolicyEngine policyEngine_;
    VoiceASRStub voiceAsr_;
    IPInfringementScanner ipScanner_;
    CreatorIdentityGuard identityGuard_;
    AssetSafetyGuard assetGuard_;
    ImageClassifierStub imageClassifier_;
    BehavioralPatternAnalyzer behaviorAnalyzer_;
    Callbacks callbacks_;
};

} // namespace engine::safety
