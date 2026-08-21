#include "social/FriendsService.hpp"

#include <algorithm>
#include <chrono>

#include "net/LanSessionBrowser.hpp"
#include "net/NetworkSession.hpp"

namespace engine::social {

FriendRequestOutcome sendFriendRequest(core::LocalProfile& profile, const std::string& friendId,
                                        const std::string& displayName) {
    if (friendId == profile.creatorId) return FriendRequestOutcome::CannotFriendSelf;
    if (profile.isFriend(friendId)) return FriendRequestOutcome::AlreadyFriends;
    if (profile.hasPendingRequestTo(friendId)) return FriendRequestOutcome::AlreadyPending;
    profile.pendingRequests.push_back(core::FriendEntry{friendId, displayName});
    return FriendRequestOutcome::Sent;
}

FriendResolutionOutcome acceptFriendRequest(core::LocalProfile& profile, const std::string& friendId) {
    auto it = std::find_if(profile.pendingRequests.begin(), profile.pendingRequests.end(),
                            [&](const core::FriendEntry& f) { return f.friendId == friendId; });
    if (it == profile.pendingRequests.end()) return FriendResolutionOutcome::NotFound;
    if (!profile.isFriend(friendId)) profile.friends.push_back(*it);
    profile.pendingRequests.erase(it);
    return FriendResolutionOutcome::Accepted;
}

FriendResolutionOutcome declineFriendRequest(core::LocalProfile& profile, const std::string& friendId) {
    auto it = std::find_if(profile.pendingRequests.begin(), profile.pendingRequests.end(),
                            [&](const core::FriendEntry& f) { return f.friendId == friendId; });
    if (it == profile.pendingRequests.end()) return FriendResolutionOutcome::NotFound;
    profile.pendingRequests.erase(it);
    return FriendResolutionOutcome::Declined;
}

bool removeFriend(core::LocalProfile& profile, const std::string& friendId) {
    auto it = std::find_if(profile.friends.begin(), profile.friends.end(),
                            [&](const core::FriendEntry& f) { return f.friendId == friendId; });
    if (it == profile.friends.end()) return false;
    profile.friends.erase(it);
    return true;
}

FriendPresence computeFriendPresence(const std::string& friendDisplayName, const net::LanSessionBrowser* browser,
                                      const net::NetworkSession* activeSession) {
    if (activeSession != nullptr) {
        for (const auto& [playerId, name] : activeSession->clientKnownPlayers()) {
            if (name == friendDisplayName) return FriendPresence{PresenceState::InGame, activeSession->sessionName()};
        }
    }
    if (browser != nullptr) {
        for (const auto& session : browser->discoveredSessions()) {
            if (session.hostDisplayName == friendDisplayName) return FriendPresence{PresenceState::Online, session.gameName};
        }
    }
    return FriendPresence{PresenceState::Offline, std::string()};
}

void sendMessage(core::LocalProfile& profile, const std::string& friendId, const std::string& text, bool fromMe) {
    core::FriendMessage message;
    message.friendId = friendId;
    message.fromMe = fromMe;
    message.text = text;
    message.timestampUnixSeconds = std::chrono::duration_cast<std::chrono::seconds>(
                                        std::chrono::system_clock::now().time_since_epoch())
                                        .count();
    profile.friendMessages.push_back(std::move(message));
    if (profile.friendMessages.size() > kMaxStoredFriendMessages) {
        profile.friendMessages.erase(profile.friendMessages.begin(),
                                      profile.friendMessages.begin() +
                                          static_cast<long>(profile.friendMessages.size() - kMaxStoredFriendMessages));
    }
}

std::vector<core::FriendMessage> messagesWithFriend(const core::LocalProfile& profile, const std::string& friendId) {
    std::vector<core::FriendMessage> result;
    for (const auto& m : profile.friendMessages) {
        if (m.friendId == friendId) result.push_back(m);
    }
    return result;
}

} // namespace engine::social
