#include "moderation/MuteBlockRegistry.hpp"

namespace engine::moderation {

void MuteBlockRegistry::mute(net::PlayerId muter, net::PlayerId target) { muted_[muter].insert(target); }

void MuteBlockRegistry::unmute(net::PlayerId muter, net::PlayerId target) {
    auto it = muted_.find(muter);
    if (it != muted_.end()) it->second.erase(target);
}

bool MuteBlockRegistry::isMuted(net::PlayerId muter, net::PlayerId target) const {
    auto it = muted_.find(muter);
    return it != muted_.end() && it->second.count(target) > 0;
}

void MuteBlockRegistry::block(net::PlayerId blocker, net::PlayerId target) { blocked_[blocker].insert(target); }

void MuteBlockRegistry::unblock(net::PlayerId blocker, net::PlayerId target) {
    auto it = blocked_.find(blocker);
    if (it != blocked_.end()) it->second.erase(target);
}

bool MuteBlockRegistry::isBlocked(net::PlayerId blocker, net::PlayerId target) const {
    auto it = blocked_.find(blocker);
    return it != blocked_.end() && it->second.count(target) > 0;
}

bool MuteBlockRegistry::shouldDeliver(net::PlayerId recipient, net::PlayerId sender) const {
    if (recipient == sender) return true; // real, honest: never silence a player's own echo of their own message
    return !isMuted(recipient, sender) && !isBlocked(recipient, sender);
}

void MuteBlockRegistry::removePlayer(net::PlayerId player) {
    muted_.erase(player);
    blocked_.erase(player);
    for (auto& [muter, targets] : muted_) targets.erase(player);
    for (auto& [blocker, targets] : blocked_) targets.erase(player);
}

} // namespace engine::moderation
