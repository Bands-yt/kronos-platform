#pragma once

#include <cstdint>

#include "core/ECS.hpp"
#include "core/InstanceTree.hpp"

// Copies the server's Instance tree to its clients, like Roblox replication.
// The server sends the replicated containers (Workspace, ReplicatedStorage,
// Players, Lighting, the Starter folders, ...) once to each new client and
// then only what changed, 20 times a second. ServerStorage and
// ServerScriptService never leave the server. Messages travel through
// remotenet (attach it first).
namespace engine::core::replication {

inline constexpr double kSendRate = 20.0;
// A client starts its LocalScripts anyway if no copy arrives in this time.
inline constexpr double kClientWaitSeconds = 15.0;

void startServer(ECS& ecs);
// Holds LocalScripts until the first full copy has been applied.
void startClient(ECS& ecs);
void stop(ECS& ecs);
[[nodiscard]] bool active(ECS& ecs);

// Server: a joined connection id gets a full copy on the next tick.
void addClient(ECS& ecs, uint32_t netId);
void removeClient(ECS& ecs, uint32_t netId);

// Server: sends changes. Client: retries records whose parent hasn't arrived.
void tick(ECS& ecs, double dt);

// Client: the first full copy has been applied.
[[nodiscard]] bool snapshotApplied(ECS& ecs);
// The replication id of an instance (0 if it has none), and back.
[[nodiscard]] uint32_t idOf(ECS& ecs, InstanceRef ref);
[[nodiscard]] InstanceRef instanceFor(ECS& ecs, uint32_t id);

} // namespace engine::core::replication
