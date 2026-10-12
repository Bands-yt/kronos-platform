#pragma once

#include "net/GameRules.hpp"
#include "net/NetworkSession.hpp"
#include "tntwars/TntWarsMatch.hpp"

#include <functional>

namespace engine::tntwars {

// TNT Wars' share of the network protocol: the server-side match and the
// class/fire/ultimate messages.
class TntWarsNetRules final : public net::GameRules {
public:
    // The session's TNT Wars rules; installs them on first use.
    static TntWarsNetRules& of(net::NetworkSession& session);

    [[nodiscard]] TntWarsMatch& match() { return match_; }

    // Client-side request senders (no-ops outside Client mode).
    void selectClass(net::NetworkSession& session, PlayerClassType classType);
    void fireWeapon(net::NetworkSession& session, glm::vec3 origin, glm::vec3 aimDirection);
    void triggerUltimate(net::NetworkSession& session);

    void setOnProjectileSpawned(std::function<void(const ProjectileState&)> callback) {
        onProjectileSpawned_ = std::move(callback);
    }
    void setOnUltimateTriggered(std::function<void(net::PlayerId, UltimateType)> callback) {
        onUltimateTriggered_ = std::move(callback);
    }

    void playerRegistered(net::PlayerId player) override { match_.registerPlayer(player); }
    void playerUnregistered(net::PlayerId player) override { match_.unregisterPlayer(player); }
    Outcome onClientMessage(uint8_t type, net::ByteReader& reader, net::PlayerId from, float clockSeconds,
                            net::ByteWriter& broadcast) override;
    Outcome onServerMessage(uint8_t type, net::ByteReader& reader) override;

private:
    TntWarsMatch match_;
    std::function<void(const ProjectileState&)> onProjectileSpawned_;
    std::function<void(net::PlayerId, UltimateType)> onUltimateTriggered_;
};

} // namespace engine::tntwars
