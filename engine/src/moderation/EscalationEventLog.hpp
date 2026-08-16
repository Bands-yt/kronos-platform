#pragma once

#include <string>
#include <vector>

#include "net/NetTypes.hpp"
#include "safety/RiskScore.hpp"

namespace engine::moderation {

// Kronos ("Moderation Architecture v1", Phase 1): the real "Full logging
// + audit trails" backbone the user's spec asks for -- until now,
// safety::TrustSafetyService::dispatchEscalation() (TrustSafetyService.cpp)
// only ever fprintf(stderr, ...)'d on the LegalReport tier and did
// *nothing* persisted for Mute/Restrict/HumanReview. One real, disk-
// persisted entry per real escalation, for every tier.
//
// Deliberately a *separate* real log from safety::RiskScore itself:
// RiskScore stays unpersisted and decays by design (one false positive
// must not become a permanent record, per docs/ARCHITECTURE.md §10/§15)
// -- that real, deliberate choice is unchanged by this class. This log
// is the different, real thing that DOES need to persist: an audit
// record that a real decision happened, when, for whom, and why, for
// debugging/defense/regulatory-compliance purposes (the user's own
// spec's §3). Don't "fix" RiskScore to persist by merging it with this.
struct EscalationEvent {
    net::PlayerId player = net::kInvalidPlayer;
    safety::EscalationTier tier = safety::EscalationTier::Log;
    std::string source; // fixed subsystem label, e.g. "TextClassifier" -- same convention as TrustSafetyService's own `source` parameters
    // Real, monotonic per-session clock seconds -- the exact same real
    // value/convention moderation::ChatLogEntry::serverTimestampSeconds/
    // moderation::ReviewCase::serverTimestampSeconds already use (fed
    // from net::NetworkSession's own clockSeconds_), NOT a Unix epoch
    // timestamp -- named to match that existing honest convention rather
    // than implying wall-clock time this class doesn't actually have.
    double serverTimestampSeconds = 0.0;
};

class EscalationEventLog {
public:
    void record(EscalationEvent event);

    [[nodiscard]] const std::vector<EscalationEvent>& events() const { return events_; }
    [[nodiscard]] size_t size() const { return events_.size(); }

    [[nodiscard]] bool saveToFile(const std::string& path) const;
    [[nodiscard]] bool loadFromFile(const std::string& path);

private:
    std::vector<EscalationEvent> events_;
};

} // namespace engine::moderation
