#pragma once

#include <string>
#include <vector>

#include "core/LocalProfile.hpp"

namespace engine::net {
class LanSessionBrowser;
class NetworkSession;
} // namespace engine::net

namespace engine::social {

// Kronos ("Social Layer" -- "Friends + Presence + Messaging"): the real,
// pure (headlessly-testable) state-transition logic behind the Friends
// panel -- mirrors marketplace::submitRating()'s own exact shape
// ("free function takes core::LocalProfile& plus params, returns an
// outcome enum, mutates the profile in place"), not a new pattern.
//
// Honesty note, stated once here rather than repeated on every function:
// there is no account/server system anywhere in this codebase (see
// core::LocalProfile's own "no auth" scope), so nothing here reaches
// another machine. "Sending" a request real-appends to *this* profile's
// own pendingRequests; "accepting"/"declining" a request is a real, local
// action a player takes (either on their own outgoing request, standing
// in for what a real remote acceptance would look like, or -- if the
// same player is driving both sides of a manual UI demo -- literally
// playing the other side) -- this is exactly the spec's own stated scope
// ("local simulation only").
enum class FriendRequestOutcome { Sent, AlreadyFriends, AlreadyPending, CannotFriendSelf };
enum class FriendResolutionOutcome { Accepted, Declined, NotFound };

[[nodiscard]] FriendRequestOutcome sendFriendRequest(core::LocalProfile& profile, const std::string& friendId,
                                                       const std::string& displayName);
[[nodiscard]] FriendResolutionOutcome acceptFriendRequest(core::LocalProfile& profile, const std::string& friendId);
[[nodiscard]] FriendResolutionOutcome declineFriendRequest(core::LocalProfile& profile, const std::string& friendId);
// Real, honest no-op (returns false) if friendId isn't actually a friend.
bool removeFriend(core::LocalProfile& profile, const std::string& friendId);

// Real presence, derived only from real, currently-live data -- never a
// fabricated online/offline guess. `Offline` is the honest default any
// friend who can't be corroborated against either real source below
// gets.
enum class PresenceState { Offline, Online, InGame };

// Kronos ("Roblox-Style Presence" -- "In Game (with session info)"):
// real -- `detail` is populated only from real, already-available data
// (NetworkSession::sessionName()/DiscoveredSession::gameName), never a
// fabricated placeholder. Empty whenever `state == Offline`, or when the
// real underlying source (e.g. a session with a blank sessionName)
// simply has no detail to give.
//
// A real, deliberately NOT-yet-added fourth state, `InStudio`, is out of
// scope for this struct today: unlike a hosted game session, Studio
// currently broadcasts no real LAN announcement of its own for a peer's
// LanSessionBrowser to discover (net::LanSessionAnnouncer exists and is
// real, but StudioApp never constructs/ticks one) -- there is no real
// signal to derive "a friend is in Studio, editing project X" from yet.
// Adding the enum value without a real detection path would be exactly
// the kind of half-built feature this codebase avoids; the real fix is
// a Studio-side LanSessionAnnouncer broadcasting a real, new "editing"
// announcement kind, a genuinely separate follow-up.
struct FriendPresence {
    PresenceState state = PresenceState::Offline;
    std::string detail;
};

// `InGame`: `friendDisplayName` appears in `activeSession`'s own real
// roster (net::NetworkSession::clientKnownPlayers()) -- this profile is
// *currently connected to a real LAN session* alongside someone with
// that display name; `detail` is that real session's own
// `sessionName()`. `Online`: `friendDisplayName` matches a real
// `hostDisplayName` currently broadcasting in `browser`'s own
// discoveredSessions() -- someone with that name is hosting a real,
// live LAN session right now, whether or not this profile has joined
// it; `detail` is that real session's own `gameName` (what they're
// playing). Matching is by display name (the only real identity a
// DiscoveredSession/session roster carries) -- a real, stated
// limitation: two different people sharing a display name are
// indistinguishable here, same "no real identity verification" scope
// core::LocalProfile itself already states. Either pointer may be
// nullptr (not currently browsing / not currently connected); a null
// pointer is treated as "that source has nothing to say," never as a
// crash.
[[nodiscard]] FriendPresence computeFriendPresence(const std::string& friendDisplayName,
                                                     const net::LanSessionBrowser* browser,
                                                     const net::NetworkSession* activeSession);

// Real, local, capped message history -- see core::kMaxStoredFriendMessages.
void sendMessage(core::LocalProfile& profile, const std::string& friendId, const std::string& text, bool fromMe);
[[nodiscard]] std::vector<core::FriendMessage> messagesWithFriend(const core::LocalProfile& profile,
                                                                    const std::string& friendId);

// Real cap enforced by sendMessage() -- oldest messages (across every
// friend, not per-friend) are trimmed first, same "don't grow unbounded"
// reasoning moderation::ChatLog's own ring buffer already applies.
constexpr size_t kMaxStoredFriendMessages = 200;

} // namespace engine::social
