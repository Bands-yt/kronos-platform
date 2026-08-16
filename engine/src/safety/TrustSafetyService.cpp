#include "safety/TrustSafetyService.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>

namespace engine::safety {

void TrustSafetyService::dispatchEscalation(PlayerId player, EscalationTier tier, const char* source) {
    switch (tier) {
        case EscalationTier::Log:
            // No action -- logging happens implicitly via RiskScore's own
            // bookkeeping; nothing to dispatch. Kronos ("Moderation
            // Architecture v1", Phase 1): deliberately still no
            // onEscalationEvent() call here either -- every processed
            // message reaches dispatchEscalation() and the overwhelming
            // majority land on this baseline tier, so logging all of them
            // would just duplicate ChatLog's own per-message record, not
            // add a real audit signal. The real audit log is for actual
            // escalations (below), not routine traffic.
            break;
        case EscalationTier::Mute:
            if (callbacks_.onMute) callbacks_.onMute(player);
            if (callbacks_.onEscalationEvent) callbacks_.onEscalationEvent(player, tier, source);
            break;
        case EscalationTier::Restrict:
            if (callbacks_.onRestrict) callbacks_.onRestrict(player);
            if (callbacks_.onEscalationEvent) callbacks_.onEscalationEvent(player, tier, source);
            break;
        case EscalationTier::HumanReview:
            if (callbacks_.onHumanReviewRequired) callbacks_.onHumanReviewRequired(player, source);
            if (callbacks_.onEscalationEvent) callbacks_.onEscalationEvent(player, tier, source);
            break;
        case EscalationTier::LegalReport:
            // Per docs/ARCHITECTURE.md §10: "mandatory legal reporting for
            // confirmed CSAM/grooming" -- confirmed. A risk score alone
            // reaching this tier is a signal to open human review, not
            // grounds to auto-file a report; this stub still forwards to
            // onLegalReportRequired so the callback wiring exists, but a
            // real implementation must gate this behind human
            // confirmation first (§15's risk table is explicit that this
            // system "needs a human review layer for anything above 'log
            // only' ... never full automation at the report tier").
            if (callbacks_.onHumanReviewRequired) callbacks_.onHumanReviewRequired(player, source);
            if (callbacks_.onLegalReportRequired) {
                std::fprintf(stderr,
                             "TrustSafetyService: risk score reached LegalReport tier for player=%u -- "
                             "routing to human review first, NOT auto-filing (see dispatchEscalation comment).\n",
                             player);
            }
            if (callbacks_.onEscalationEvent) callbacks_.onEscalationEvent(player, tier, source);
            break;
    }
}

TextClassification TrustSafetyService::onChatMessage(PlayerId sender, const std::string& message,
                                                       AgeGroup senderAgeGroup) {
    ModerationPipeline::ChatResult result = pipeline_.processChatMessage(sender, message, senderAgeGroup);
    dispatchEscalation(sender, result.tier, "TextClassifier");

    // Kronos ("Moderation Architecture v1", Phase 1): the real hard-
    // block/warn decision -- see PolicyEngine.hpp's own class comment
    // and TextClassification::blocked's own comment for what this
    // actually changes about the real chat delivery path.
    PolicyDecision decision = policyEngine_.decide(result.classification, senderAgeGroup);
    result.classification.blocked = decision.action == PolicyAction::Block;
    if (decision.action == PolicyAction::Warn && callbacks_.onWarn) {
        callbacks_.onWarn(sender, decision.reason);
    }
    if (decision.action == PolicyAction::QueueForReview && callbacks_.onHumanReviewRequired) {
        callbacks_.onHumanReviewRequired(sender, decision.reason);
    }

    return result.classification;
}

TrustSafetyService::ImageUploadResult TrustSafetyService::onImageUpload(PlayerId uploader, const std::string& assetPath,
                                                                          AgeGroup uploaderAgeGroup) {
    ImageUploadResult result;

    std::ifstream file(assetPath, std::ios::binary);
    if (!file) {
        std::fprintf(stderr, "TrustSafetyService: onImageUpload(%u, \"%s\") -- could not open file for scanning.\n",
                     uploader, assetPath.c_str());
        return result;
    }
    std::vector<uint8_t> fileBytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    size_t dotPos = assetPath.find_last_of('.');
    std::string extension = dotPos != std::string::npos ? assetPath.substr(dotPos + 1) : "";
    // Kronos ("Moderation Architecture v2", item A): filename only, not
    // the full assetPath -- ImageClassifierStub's real signal is the
    // uploader-chosen name, and a full path could spuriously match on
    // unrelated parent-directory text.
    size_t slashPos = assetPath.find_last_of("/\\");
    std::string filename = slashPos != std::string::npos ? assetPath.substr(slashPos + 1) : assetPath;

    result.structuralReport = assetGuard_.scanImageAsset(fileBytes, extension, ipScanner_);
    result.classification = imageClassifier_.classify(filename);

    if (result.structuralReport.anyFlagged()) {
        // Same two-tier weighting every other signal source in this class
        // uses: a blocked finding (format spoofing, a decompression-bomb-
        // sized declared dimension, a hard-block IP term smuggled into
        // metadata) pushes hard; a flag-only finding contributes less so
        // one ambiguous upload doesn't escalate an account by itself.
        float weight = result.structuralReport.anyBlocked() ? 1.0f : 0.3f;
        EscalationTier tier = pipeline_.applyAsyncSignal(uploader, "AssetSafetyGuard", weight);
        dispatchEscalation(uploader, tier, "AssetSafetyGuard");
    }

    if (result.classification.flagged) {
        PolicyDecision decision = policyEngine_.decide(result.classification, uploaderAgeGroup);
        result.blocked = decision.action == PolicyAction::Block;
        float weight = result.blocked ? 1.0f : 0.3f;
        EscalationTier tier = pipeline_.applyAsyncSignal(uploader, "ImageClassifier", weight);
        dispatchEscalation(uploader, tier, "ImageClassifier");
        if (decision.action == PolicyAction::Warn && callbacks_.onWarn) {
            callbacks_.onWarn(uploader, decision.reason);
        }
        if (decision.action == PolicyAction::QueueForReview && callbacks_.onHumanReviewRequired) {
            callbacks_.onHumanReviewRequired(uploader, decision.reason);
        }
    }

    result.blocked = result.blocked || result.structuralReport.anyBlocked();
    return result;
}

EscalationTier TrustSafetyService::onDirectMessagePattern(PlayerId sender,
                                                            const std::vector<DirectMessageSample>& recentSamplesFromSender,
                                                            double nowServerTimestampSeconds) {
    BehaviorSignal signal = behaviorAnalyzer_.analyze(recentSamplesFromSender, nowServerTimestampSeconds);
    if (signal.tier == BehaviorRiskTier::None) return pipeline_.currentTier(sender);

    // Same two-tier-weighting spirit every other signal source in this
    // class uses, extended to BehaviorRiskTier's own three real non-None
    // levels rather than collapsing them into one flat weight -- a
    // repeated-minor-contact pattern is a categorically more urgent
    // signal than a rapid-messaging burst, and the weight fed into
    // RiskScore should reflect that real difference.
    float weight = 0.0f;
    switch (signal.tier) {
        case BehaviorRiskTier::None: weight = 0.0f; break;
        case BehaviorRiskTier::Low: weight = 0.2f; break;
        case BehaviorRiskTier::Medium: weight = 0.4f; break;
        case BehaviorRiskTier::High: weight = 0.8f; break;
    }

    EscalationTier tier = pipeline_.applyAsyncSignal(sender, "BehavioralPatternAnalyzer", weight);
    dispatchEscalation(sender, tier, signal.reason);
    return tier;
}

void TrustSafetyService::onVoiceChunk(PlayerId speaker, const AudioChunk& chunk) {
    voiceAsr_.pushAudio(chunk);
    TranscriptionResult transcription = voiceAsr_.transcribeRollingBuffer();
    if (!transcription.succeeded) return; // always false today -- see VoiceASRStub.hpp

    ModerationPipeline::ChatResult result = pipeline_.processChatMessage(speaker, transcription.text);
    dispatchEscalation(speaker, result.tier, "VoiceASR");
}

IPInfringementResult TrustSafetyService::onCreatorContentSubmission(PlayerId creator, const std::string& text,
                                                                      const char* contextLabel) {
    IPInfringementResult scanResult = ipScanner_.scan(text);
    if (!scanResult.flagged) return scanResult;

    float maxConfidence = 0.0f;
    for (const auto& match : scanResult.matches) {
        maxConfidence = std::max(maxConfidence, match.confidence);
    }
    // Hard-block matches (Roblox's own trademarks) push the account's risk
    // score hard and immediately; review-tier matches (well-known original
    // IP titles, which could be an honest title collision -- see
    // IPInfringementScanner.hpp) contribute a much smaller signal so one
    // ambiguous hit doesn't escalate an account by itself. Only a repeated
    // pattern does -- the same "rolling, decaying risk score" property
    // RiskScore.hpp documents for every other signal source in this
    // pipeline.
    float weight = scanResult.blocked ? 1.0f : maxConfidence * 0.4f;
    EscalationTier tier = pipeline_.applyAsyncSignal(creator, "IPInfringementScanner", weight);
    dispatchEscalation(creator, tier, contextLabel);
    return scanResult;
}

IdentityCheckResult TrustSafetyService::onCreatorDisplayNameChange(PlayerId creator, const std::string& displayName) {
    IdentityCheckResult result = identityGuard_.checkDisplayName(displayName);
    if (!result.flagged) return result;

    // Same two-tier weighting as onCreatorContentSubmission: a blocked
    // result (brand + authority claim, the compound signal) pushes hard;
    // a flag-only result (either signal alone) contributes a much
    // smaller amount so one ambiguous name doesn't escalate an account by
    // itself -- only a repeated pattern does.
    float weight = result.blocked ? 1.0f : 0.3f;
    EscalationTier tier = pipeline_.applyAsyncSignal(creator, "CreatorIdentityGuard", weight);
    dispatchEscalation(creator, tier, result.reason.c_str());
    return result;
}

EscalationTier TrustSafetyService::onAntiCheatSignal(PlayerId player, const char* source, float weight) {
    EscalationTier tier = pipeline_.applyAsyncSignal(player, source, weight);
    dispatchEscalation(player, tier, source);
    return tier;
}

} // namespace engine::safety
