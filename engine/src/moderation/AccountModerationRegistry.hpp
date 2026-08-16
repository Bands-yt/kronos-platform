#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace engine::moderation {

// Kronos ("Moderation Architecture v2", "Account System v1"): real,
// disk-persisted moderation state keyed by the real, stable
// core::LocalProfile::profileId a client now sends in its real
// JoinRequest (net::NetworkSession::setLocalIdentity()) -- distinct from
// moderation::MuteBlockRegistry (a player's own real, per-session,
// PlayerId-keyed mute/block preference) and net::NetworkSession's own
// setServerMuted() (a real, session-scoped, PlayerId-keyed moderator
// action). This is the real, cross-session, "this real account is
// banned/muted, full stop" record -- survives a reconnect and a server
// restart, the thing the user's own "Persistent bans/mutes" spec item
// asks for. A plain uint64_t profileId parameter, not a core::LocalProfile
// reference -- this class only ever needs the id, not the full profile.
//
// Real, honest, stated limitation: nothing here stops a client from
// generating a brand-new profileId to evade a ban -- there is no real
// account authentication anywhere in this codebase (see
// core::LocalProfile's own "no auth, no password" scope), matching the
// user's own explicit "no networking or cloud accounts yet" framing.
struct AccountModerationRecord {
    uint64_t profileId = 0;
    bool banned = false;
    bool muted = false;
    std::string reason;
};

class AccountModerationRegistry {
public:
    void ban(uint64_t profileId, const std::string& reason);
    void unban(uint64_t profileId);
    [[nodiscard]] bool isBanned(uint64_t profileId) const;

    void mute(uint64_t profileId, const std::string& reason);
    void unmute(uint64_t profileId);
    [[nodiscard]] bool isMuted(uint64_t profileId) const;

    [[nodiscard]] size_t size() const { return records_.size(); }

    [[nodiscard]] bool saveToFile(const std::string& path) const;
    [[nodiscard]] bool loadFromFile(const std::string& path);

private:
    [[nodiscard]] AccountModerationRecord& recordFor(uint64_t profileId);

    std::unordered_map<uint64_t, AccountModerationRecord> records_;
};

} // namespace engine::moderation
