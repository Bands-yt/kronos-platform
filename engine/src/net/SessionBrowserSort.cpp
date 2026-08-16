#include "net/SessionBrowserSort.hpp"

#include <algorithm>
#include <cctype>

namespace engine::net {

namespace {
std::string toLowerCopy(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}
} // namespace

std::vector<DiscoveredSession> sortDiscoveredSessions(std::vector<DiscoveredSession> sessions, SessionSortOrder order) {
    switch (order) {
        case SessionSortOrder::MostActive:
            std::stable_sort(sessions.begin(), sessions.end(), [](const DiscoveredSession& a, const DiscoveredSession& b) {
                if (a.currentPlayerCount != b.currentPlayerCount) return a.currentPlayerCount > b.currentPlayerCount;
                if (a.pingMs != b.pingMs) return a.pingMs < b.pingMs;
                return toLowerCopy(a.sessionName) < toLowerCopy(b.sessionName);
            });
            break;
        case SessionSortOrder::NewlyCreated:
            std::stable_sort(sessions.begin(), sessions.end(), [](const DiscoveredSession& a, const DiscoveredSession& b) {
                return a.sessionStartUnixSeconds > b.sessionStartUnixSeconds;
            });
            break;
        case SessionSortOrder::Alphabetical:
            std::stable_sort(sessions.begin(), sessions.end(), [](const DiscoveredSession& a, const DiscoveredSession& b) {
                return toLowerCopy(a.sessionName) < toLowerCopy(b.sessionName);
            });
            break;
    }
    return sessions;
}

std::vector<DiscoveredSession> filterDiscoveredSessionsToFriends(const std::vector<DiscoveredSession>& sessions,
                                                                   const std::vector<core::FriendEntry>& friends) {
    std::vector<DiscoveredSession> result;
    for (const auto& session : sessions) {
        bool isFriend = std::any_of(friends.begin(), friends.end(), [&](const core::FriendEntry& f) {
            return f.displayName == session.hostDisplayName;
        });
        if (isFriend) result.push_back(session);
    }
    return result;
}

} // namespace engine::net
