#pragma once

#include <unordered_map>
#include <unordered_set>

#include "net/NetTypes.hpp"

namespace engine::moderation {

// Sprint 12 ("Moderation & Safety Systems") task 1's "Add mute/block
// system" -- two real, deliberately distinct per-player relations, both
// entirely the affected player's own choice (unlike server-side
// moderation mutes, see NetworkSession's serverMutedPlayers_, which are
// an escalation *action*, not a personal preference):
//
//   mute()  -- "I don't want to hear from this player in chat, for now."
//              Cheap, reversible, chat-scoped only.
//   block() -- the same real chat-silencing effect as mute(), but kept
//              as a separate, real relation so future systems (trading,
//              direct messaging, party invites -- none of which exist
//              yet) have a real block list to consult beyond chat, not
//              just a stronger synonym for mute.
//
// Both are one-directional (recipient's own choice) and per-pair, keyed
// by net::PlayerId now that a real player-id concept exists (Sprint 11).
class MuteBlockRegistry {
public:
    void mute(net::PlayerId muter, net::PlayerId target);
    void unmute(net::PlayerId muter, net::PlayerId target);
    [[nodiscard]] bool isMuted(net::PlayerId muter, net::PlayerId target) const;

    void block(net::PlayerId blocker, net::PlayerId target);
    void unblock(net::PlayerId blocker, net::PlayerId target);
    [[nodiscard]] bool isBlocked(net::PlayerId blocker, net::PlayerId target) const;

    // Real, combined delivery check chat broadcast uses per-recipient:
    // false if `recipient` has muted or blocked `sender`.
    [[nodiscard]] bool shouldDeliver(net::PlayerId recipient, net::PlayerId sender) const;

    // Real cleanup on disconnect -- removes `player` as both a muter/
    // blocker (their own preferences no longer matter) and as a target
    // (so a reused PlayerId, impossible today since NetworkSession's
    // nextPlayerId_ is monotonic per session, but real hygiene
    // regardless, never silently carries a stale mute/block forward).
    void removePlayer(net::PlayerId player);

private:
    std::unordered_map<net::PlayerId, std::unordered_set<net::PlayerId>> muted_;
    std::unordered_map<net::PlayerId, std::unordered_set<net::PlayerId>> blocked_;
};

} // namespace engine::moderation
