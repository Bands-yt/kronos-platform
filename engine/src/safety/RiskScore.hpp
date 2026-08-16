#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>

namespace engine::safety {

// A single signal's contribution to a user/conversation's risk score --
// one of the inputs feeding RiskScore::apply() below. `source` names which
// subsystem produced it (TextClassifier, ConversationGroomingModel,
// PhotoDNA, BehavioralAnomaly, ...) purely for logging/audit purposes
// (docs/ARCHITECTURE.md §10's data-governance requirement: anything
// moderation-related needs an audit trail).
struct RiskSignal {
    const char* source = "";
    float weight = 0.0f; // 0..1 contribution, source-specific meaning
};

enum class EscalationTier { Log, Mute, Restrict, HumanReview, LegalReport };

[[nodiscard]] inline const char* escalationTierName(EscalationTier tier) {
    switch (tier) {
        case EscalationTier::Log: return "Log";
        case EscalationTier::Mute: return "Mute";
        case EscalationTier::Restrict: return "Restrict";
        case EscalationTier::HumanReview: return "Human Review";
        case EscalationTier::LegalReport: return "Legal Report";
    }
    return "Unknown";
}

// The "rolling risk score (time-windowed)" node from docs/ARCHITECTURE.md
// §10's moderation flowchart: a single weighted score per user/
// conversation, decaying without reinforcement so one false positive
// doesn't create a permanent record -- the property called out explicitly
// in §15's risk table ("Grooming-model false positives/negatives carry
// real human cost").
class RiskScore {
public:
    explicit RiskScore(float halfLifeSeconds = 3600.0f) : halfLifeSeconds_(halfLifeSeconds) {}

    // Applies a new signal at the given time, first decaying the existing
    // score by how much time has passed since the last update (simple
    // exponential decay -- real tuning of the half-life and per-source
    // weighting is a trust & safety policy decision, not an engineering
    // one, hence the constructor parameter rather than a hardcoded curve).
    void apply(const RiskSignal& signal, std::chrono::steady_clock::time_point now) {
        decay(now);
        score_ = std::min(1.0f, score_ + signal.weight);
        lastUpdate_ = now;
    }

    [[nodiscard]] float value(std::chrono::steady_clock::time_point now) const {
        float elapsed = std::chrono::duration<float>(now - lastUpdate_).count();
        return decayedValue(elapsed);
    }

    // Escalation thresholds -- illustrative defaults matching the four
    // non-"Log" tiers in §10's flowchart. A real deployment tunes these
    // per category (grooming-adjacent signals likely warrant a lower
    // Restrict/HumanReview threshold than generic harassment); left as
    // named constants rather than derived from anything, since this
    // stub has no real classifier output to calibrate against yet.
    [[nodiscard]] EscalationTier tier(std::chrono::steady_clock::time_point now) const {
        float v = value(now);
        if (v >= 0.9f) return EscalationTier::LegalReport; // never automatic in practice -- see ModerationPipeline
        if (v >= 0.7f) return EscalationTier::HumanReview;
        if (v >= 0.5f) return EscalationTier::Restrict;
        if (v >= 0.25f) return EscalationTier::Mute;
        return EscalationTier::Log;
    }

private:
    void decay(std::chrono::steady_clock::time_point now) {
        float elapsed = std::chrono::duration<float>(now - lastUpdate_).count();
        score_ = decayedValue(elapsed);
    }

    [[nodiscard]] float decayedValue(float elapsedSeconds) const {
        if (elapsedSeconds <= 0.0f || halfLifeSeconds_ <= 0.0f) return score_;
        float halfLives = elapsedSeconds / halfLifeSeconds_;
        float factor = std::pow(0.5f, halfLives);
        return score_ * factor;
    }

    float score_ = 0.0f;
    float halfLifeSeconds_;
    std::chrono::steady_clock::time_point lastUpdate_{};
};

} // namespace engine::safety
