#pragma once

#include <vector>

#include "core/LocalProfile.hpp"
#include "net/LanSessionBrowser.hpp"

namespace engine::net {

// Kronos ("Session Browser Polish v2" -- "Sorting"): real, pure
// (headlessly-testable) sort/filter logic extracted from
// runtime::RuntimeShell::drawSessionBrowserPanel(), same "pure logic
// lives in net::/core::, ImGui just calls it" split this codebase
// already follows throughout (see core::QualityScore/
// core::HiddenGemsSelector for the exact same precedent).
enum class SessionSortOrder { MostActive, NewlyCreated, Alphabetical };

// Sorts a copy of `sessions` and returns it -- the input is never
// mutated. MostActive: highest currentPlayerCount first (ties broken by
// lowest real ping, then name). NewlyCreated: highest real
// sessionStartUnixSeconds first (the session that began most recently).
// Alphabetical: sessionName, case-insensitive, ascending.
[[nodiscard]] std::vector<DiscoveredSession> sortDiscoveredSessions(std::vector<DiscoveredSession> sessions,
                                                                      SessionSortOrder order);

// Kronos ("Session Browser Polish v2" -- "Filters: Friends' sessions"):
// real -- matches by real display name (the only real identity a
// DiscoveredSession carries; see social::computeFriendPresence()'s own
// comment for why this same matching convention is used there too).
// `friendsOnly = false` returns every session unfiltered. A real,
// honest, stated gap: this protocol has no real "private session"
// concept at all (every LAN-broadcast session is, by definition,
// discoverable by anyone listening -- there's no invite-only/password
// broadcast mode anywhere in LanDiscoveryProtocol), so there is no
// "Public"/"Private" filter here -- building one would mean a fabricated
// flag that doesn't actually gate anything real.
[[nodiscard]] std::vector<DiscoveredSession> filterDiscoveredSessionsToFriends(
    const std::vector<DiscoveredSession>& sessions, const std::vector<core::FriendEntry>& friends);

} // namespace engine::net
