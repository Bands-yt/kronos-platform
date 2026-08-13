#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <variant>

#include "net/NetTypes.hpp"

namespace engine::net {

class ByteWriter;
class ByteReader;

// C++-side enforcement primitive behind the Luau-facing RemoteEvent API
// sketched in docs/ARCHITECTURE.md §6:
//
//   local RemoteEvent = ReplicatedStorage.PurchaseItem
//   RemoteEvent:SetInboundSchema({ itemId = "number", quantity = "number" })
//   RemoteEvent.RateLimit = 5 -- max calls/sec per player, enforced by the engine
//
// This class *is* "enforced by the engine": schema + rate-limit checks run
// here, in C++, before a client-fired payload ever reaches a script's
// handler -- a script cannot accidentally skip its own validation because
// there is no code path that lets it. The Luau binding that calls
// SetInboundSchema/handleFromClient doesn't exist yet (that's the
// Instance/DataModel translation layer, same TODO as everywhere else this
// document references §6); this is the engine-side half that binding will
// attach to.
//
// Both schema and rate limit are opt-in per docs/ARCHITECTURE.md Principle
// 2: an event with neither set behaves exactly like a ported Roblox
// RemoteEvent always has -- the engine trusts the script to validate its
// own payloads, nothing new is required for existing scripts to keep working.
class RemoteEvent {
public:
    enum class FieldType { Number, String, Boolean };
    using FieldValue = std::variant<double, std::string, bool>;
    using Payload = std::unordered_map<std::string, FieldValue>;
    using ServerHandler = std::function<void(PlayerId sender, const Payload& payload)>;

    explicit RemoteEvent(std::string name) : name_(std::move(name)) {}

    void setInboundSchema(std::unordered_map<std::string, FieldType> schema) { schema_ = std::move(schema); }
    void setRateLimit(float maxCallsPerSecondPerPlayer) { maxCallsPerSecond_ = maxCallsPerSecondPerPlayer; }
    void setServerHandler(ServerHandler handler) { handler_ = std::move(handler); }

    // Entry point from the network receive path once a client-fired
    // payload has been deserialized. Returns false without invoking the
    // handler if the schema or rate-limit check fails -- the caller
    // should treat repeated rejections as an anti-cheat signal (§11), not
    // just silently drop them.
    bool handleFromClient(PlayerId sender, const Payload& payload);

    [[nodiscard]] const std::string& name() const { return name_; }

private:
    [[nodiscard]] bool validateSchema(const Payload& payload) const;
    [[nodiscard]] bool checkRateLimit(PlayerId sender);

    std::string name_;
    std::unordered_map<std::string, FieldType> schema_; // empty = no schema enforced
    float maxCallsPerSecond_ = 0.0f;                     // 0 = unlimited (default, opt-in)
    ServerHandler handler_;

    struct RateState {
        double tokens = 0.0;
        std::chrono::steady_clock::time_point lastRefill{};
    };
    std::unordered_map<PlayerId, RateState> rateState_;
};

// Kronos (Alpha Roadmap Phase 4, "Networking Upgrade"): a real field
// count above this is silently truncated, not a parse error -- matches
// ByteWriter::writeString()'s own "truncated at 255" precedent for
// bounding oversized-but-not-fatal input.
inline constexpr uint8_t kMaxRemoteEventFields = 32;

// Real wire (de)serialization for RemoteEvent::Payload -- shared by
// net::NetworkSession's client->server "fire" and server->client
// "broadcast" wire messages and by core::ScriptNetworkApi's Luau binding,
// so every layer that moves a Payload over the wire uses the same real
// format instead of hand-rolling its own copy.
void serializeRemoteEventPayload(const RemoteEvent::Payload& payload, ByteWriter& writer);
// Real, bounds-checked -- caller MUST check reader.hasError() afterward,
// same contract every other deserializeX() in Serialization.hpp already
// carries. Stops early (returning whatever fields were already decoded)
// on an unrecognized field-type tag, since there's no way to know how
// many bytes to skip for a tag this build doesn't understand -- a real,
// honest partial result rather than guessing and risking desync.
[[nodiscard]] RemoteEvent::Payload deserializeRemoteEventPayload(ByteReader& reader);

} // namespace engine::net
