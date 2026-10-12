#include "tntwars/TntWarsNetRules.hpp"

namespace engine::tntwars {

namespace {
constexpr uint8_t kSelectClass = 8;
constexpr uint8_t kFireWeapon = 9;
constexpr uint8_t kProjectileSpawned = 10;
constexpr uint8_t kTriggerUltimate = 11;
constexpr uint8_t kUltimateTriggered = 12;
} // namespace

TntWarsNetRules& TntWarsNetRules::of(net::NetworkSession& session) {
    if (auto* existing = dynamic_cast<TntWarsNetRules*>(session.gameRules())) return *existing;
    auto rules = std::make_unique<TntWarsNetRules>();
    TntWarsNetRules& ref = *rules;
    session.setGameRules(std::move(rules));
    return ref;
}

void TntWarsNetRules::selectClass(net::NetworkSession& session, PlayerClassType classType) {
    net::ByteWriter writer;
    writer.writeU8(kSelectClass);
    writer.writeU8(static_cast<uint8_t>(classType));
    session.sendGameMessage(writer);
}

void TntWarsNetRules::fireWeapon(net::NetworkSession& session, glm::vec3 origin, glm::vec3 aimDirection) {
    net::ByteWriter writer;
    writer.writeU8(kFireWeapon);
    writer.writeVec3(origin);
    writer.writeVec3(aimDirection);
    session.sendGameMessage(writer);
}

void TntWarsNetRules::triggerUltimate(net::NetworkSession& session) {
    net::ByteWriter writer;
    writer.writeU8(kTriggerUltimate);
    session.sendGameMessage(writer);
}

net::GameRules::Outcome TntWarsNetRules::onClientMessage(uint8_t type, net::ByteReader& reader, net::PlayerId from,
                                                         float clockSeconds, net::ByteWriter& broadcast) {
    if (type == kSelectClass) {
        auto classType = static_cast<PlayerClassType>(reader.readU8());
        if (reader.hasError()) return Outcome::Rejected;
        return match_.selectClass(from, classType) ? Outcome::Handled : Outcome::Rejected;
    }
    if (type == kFireWeapon) {
        glm::vec3 origin = reader.readVec3();
        glm::vec3 aimDirection = reader.readVec3();
        if (reader.hasError()) return Outcome::Rejected;
        TntWarsMatch::FireResult result = match_.fireWeapon(from, origin, aimDirection, clockSeconds);
        if (!result.accepted) return Outcome::Rejected;
        broadcast.writeU8(kProjectileSpawned);
        broadcast.writeU8(static_cast<uint8_t>(result.projectile.type));
        broadcast.writeU32(result.projectile.owner);
        broadcast.writeVec3(result.projectile.position);
        broadcast.writeVec3(result.projectile.velocity);
        broadcast.writeFloat(result.projectile.damage);
        return Outcome::Handled;
    }
    if (type == kTriggerUltimate) {
        TntWarsMatch::UltimateResult result = match_.triggerUltimate(from, clockSeconds);
        if (!result.accepted) return Outcome::Rejected;
        broadcast.writeU8(kUltimateTriggered);
        broadcast.writeU32(from);
        broadcast.writeU8(static_cast<uint8_t>(result.type));
        return Outcome::Handled;
    }
    return Outcome::Ignored;
}

net::GameRules::Outcome TntWarsNetRules::onServerMessage(uint8_t type, net::ByteReader& reader) {
    if (type == kProjectileSpawned) {
        ProjectileState projectile;
        projectile.type = static_cast<ProjectileType>(reader.readU8());
        projectile.owner = reader.readU32();
        projectile.position = reader.readVec3();
        projectile.velocity = reader.readVec3();
        projectile.damage = reader.readFloat();
        if (reader.hasError()) return Outcome::Rejected;
        if (onProjectileSpawned_) onProjectileSpawned_(projectile);
        return Outcome::Handled;
    }
    if (type == kUltimateTriggered) {
        net::PlayerId player = reader.readU32();
        auto ultimateType = static_cast<UltimateType>(reader.readU8());
        if (reader.hasError()) return Outcome::Rejected;
        if (onUltimateTriggered_) onUltimateTriggered_(player, ultimateType);
        return Outcome::Handled;
    }
    return Outcome::Ignored;
}

} // namespace engine::tntwars
