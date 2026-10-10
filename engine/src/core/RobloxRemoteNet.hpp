#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/ECS.hpp"
#include "core/InstanceSignals.hpp"
#include "core/InstanceTree.hpp"

// RemoteEvent and RemoteFunction calls between a server and its clients over
// a real connection. The host supplies a send function and passes received
// bytes to receive(); this file turns them into OnServerEvent, OnClientEvent
// and the Invoke callbacks. Both sides load the same place, so a remote is
// found by its path (names, plus an index among same-named siblings).
namespace engine::core::remotenet {

enum class Side { Server, Client };

// Server: target 0 means every client. Client: target is ignored (the server).
using SendFn = std::function<void(uint32_t target, std::vector<uint8_t> bytes, bool reliable)>;

inline constexpr uint32_t kEveryone = 0;
inline constexpr size_t kMaxPayloadBytes = 64 * 1024;
// Roblox drops UnreliableRemoteEvent payloads over 900 bytes.
inline constexpr size_t kMaxUnreliableBytes = 900;
// Per client on the server: a burst of this many messages, refilled per second.
inline constexpr double kServerReceiveBurst = 240.0;
inline constexpr double kServerReceivePerSecond = 120.0;

void attach(ECS& ecs, Side side, SendFn send);
// Pending invokes fail with "connection lost".
void detach(ECS& ecs);
[[nodiscard]] bool attached(ECS& ecs);
[[nodiscard]] bool isServer(ECS& ecs);

// The Player instance for a connection id (and back).
void addPlayer(ECS& ecs, uint32_t netId, InstanceRef player);
// Invokes waiting on that player fail.
void removePlayer(ECS& ecs, uint32_t netId);
[[nodiscard]] InstanceRef playerFor(ECS& ecs, uint32_t netId);
[[nodiscard]] uint32_t netIdFor(ECS& ecs, InstanceRef player);

// Messages whose first byte is at least kFirstRawKind belong to another
// module (replication). Only server-to-client ones are accepted.
inline constexpr uint8_t kFirstRawKind = 16;
using RawReceiver = std::function<void(uint32_t sender, const uint8_t* data, size_t size)>;
void setRawReceiver(ECS& ecs, RawReceiver receiver);
// Runs before each script message the server sends (replication flushes).
void setBeforeServerSend(ECS& ecs, std::function<void()> hook);
void sendRaw(ECS& ecs, uint32_t target, std::vector<uint8_t> bytes, bool reliable);

// sender: the client's connection id on the server, 0 on a client.
void receive(ECS& ecs, uint32_t sender, const uint8_t* data, size_t size);
// Sends finished invoke results and refills rate budgets.
void tick(ECS& ecs, double dt);

// Called by the script API. False with `error` set when the call can't go out.
bool fireServer(ECS& ecs, InstanceRef remote, const std::vector<SignalArg>& args, std::string& error);
bool fireClient(ECS& ecs, InstanceRef remote, InstanceRef player, const std::vector<SignalArg>& args,
                std::string& error);
bool fireAllClients(ECS& ecs, InstanceRef remote, const std::vector<SignalArg>& args, std::string& error);
// A SignalHub invoke id to wait on, or 0 with `error` set.
uint64_t invokeServer(ECS& ecs, InstanceRef remote, const std::vector<SignalArg>& args, std::string& error);
uint64_t invokeClient(ECS& ecs, InstanceRef remote, InstanceRef player, const std::vector<SignalArg>& args,
                      std::string& error);

// Instance values that can't be found on the other side arrive as nil.
std::vector<uint8_t> encodeArgs(ECS& ecs, const std::vector<SignalArg>& args);
bool decodeArgs(ECS& ecs, const uint8_t* data, size_t size, std::vector<SignalArg>& out);

} // namespace engine::core::remotenet
