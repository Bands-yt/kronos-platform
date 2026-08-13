#pragma once

#include <cstdint>

#include "net/NetTypes.hpp"

namespace engine::net {

// Sprint 11 ("Networking Foundation"): the real missing link between
// core::ECS::EntityId (an entt::entity -- a local-process handle with no
// meaning across a network boundary, see ECS.hpp) and
// net::EntityState::networkId (a bare uint32_t, previously free-floating
// with nothing on the ECS side populating or consuming it). Every entity
// that should exist identically on both server and client carries one of
// these; the server assigns real, unique, monotonically-increasing ids
// via allocateNetworkId() (never reused within a process's lifetime, so
// a client can never confuse a just-despawned entity's old id with a
// newly-spawned one that happens to reuse an EntityId slot), and a
// client attaches the same component with whatever id it received from
// the server -- this struct is the one place server and client agree on
// what a given networkId actually identifies.
struct NetworkIdentity {
    uint32_t networkId = 0;
    PlayerId ownerId = kInvalidPlayer; // kInvalidPlayer = server-owned (props, ore nodes, world state, not a specific player's avatar)
    // True only on the one client instance whose local player this
    // entity's input belongs to -- gates whether net::ClientPrediction's
    // predicted-apply runs for this entity at all (every *other* entity,
    // including other players' avatars, is purely snapshot-driven +
    // interpolated on a given client, never locally predicted).
    bool isLocallyControlled = false;
};

// Real, monotonic, process-lifetime counter -- the server-side allocator
// for NetworkIdentity::networkId. Deliberately free-standing (not a
// method on any class) since id allocation needs to be a single, global
// sequence for the whole server process regardless of which system is
// spawning an entity (a player join, a dynamically-spawned ore drop, a
// Studio-authored prop) -- the same "one real call site, can't drift"
// reasoning core::createOreNode()/core::spawnWorldProp() already
// established for their own domains.
[[nodiscard]] uint32_t allocateNetworkId();

} // namespace engine::net
