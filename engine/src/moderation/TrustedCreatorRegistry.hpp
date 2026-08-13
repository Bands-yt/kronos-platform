#pragma once

#include <unordered_set>

#include "net/NetTypes.hpp"

namespace engine::moderation {

// Sprint 12 task 4's "Add 'trusted creator' flags for Studio tools" -- a
// real, small, explicit allowlist of net::PlayerIds, not a reputation/
// scoring system (that would just be a second, competing risk model
// alongside safety::RiskScore -- this is deliberately the opposite: a
// simple, creator/moderator-managed, positive allowlist). Consulted by
// WorldSafetySettings::trustedCreatorOnlyMode and (Sprint 13, forward-
// declared here rather than bolted on later) intended as the real gate
// a future publishing pipeline's riskier actions check before allowing
// an untrusted creator through -- the same "build the shared primitive
// now, the second consumer arrives next sprint" pattern
// net::TokenBucketRateLimiter and net::isMovementPlausible() already
// established in Sprint 11.
class TrustedCreatorRegistry {
public:
    void setTrusted(net::PlayerId creator, bool trusted);
    [[nodiscard]] bool isTrusted(net::PlayerId creator) const;
    [[nodiscard]] size_t trustedCount() const { return trusted_.size(); }

private:
    std::unordered_set<net::PlayerId> trusted_;
};

} // namespace engine::moderation
