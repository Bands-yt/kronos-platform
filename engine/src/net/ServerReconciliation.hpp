#pragma once

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

#include "net/NetTypes.hpp"

namespace engine::net {

// Server-side half of docs/ARCHITECTURE.md §4.2's loop -- literally:
//
//   Server, each sim tick:
//     for input in received_inputs (ordered by seq):
//         validate(input)               // reject out-of-range / impossible
//         apply(input) to authoritative state
//     snapshot = { state, last_processed_seq }
//     broadcast(snapshot)
//
// Also where §4.2's Lua security boundary rule lives at the transport
// level: "the server never trusts a client-sent remote payload for
// anything security-relevant" -- validate() is a mandatory, separate step
// from apply(), not a suggestion the caller can skip, because this class
// won't call apply() on an input that fails it.
class ServerReconciliation {
public:
    using ValidateFn = std::function<bool(PlayerId, const InputCommand&)>;
    using ApplyFn = std::function<void(PlayerId, const InputCommand&)>;

    // Interest-managed replication (§8.5's StreamingEnabled-equivalent) --
    // returns whatever entity states `recipient` should see this
    // snapshot. Defaults to "everything" if unset, matching streaming
    // being off by default so a ported project's replication behavior
    // doesn't silently change on import (§4.2's closing rule).
    using GatherStateFn = std::function<std::vector<EntityState>(PlayerId recipient)>;

    void setValidate(ValidateFn fn) { validate_ = std::move(fn); }
    void setApply(ApplyFn fn) { apply_ = std::move(fn); }
    void setGatherState(GatherStateFn fn) { gatherState_ = std::move(fn); }

    // Called from ENetTransport::Callbacks::onPacketReceived once the
    // payload is deserialized into an InputCommand -- queues it for the
    // next tick() rather than applying immediately, so all of one tick's
    // inputs are processed in a single, seq-ordered batch.
    void queueInput(PlayerId player, const InputCommand& input);

    void registerPlayer(PlayerId player);
    void unregisterPlayer(PlayerId player);

    // Drains every player's queued inputs in sequence order, running
    // validate() then apply() on each -- the pseudocode's inner for-loop --
    // and advances that player's lastProcessedSequence(). Does not itself
    // broadcast; call buildSnapshot() per recipient afterward and send it
    // over ENetTransport, since serialization/send is a transport concern
    // this class deliberately doesn't own.
    void tick(uint32_t serverTick);

    // baselineTick is left at 0 (full snapshot) -- delta-compression
    // against a per-client acknowledged baseline is real work (bit-packing,
    // an ack protocol) that belongs to the wire-format layer described in
    // docs/ARCHITECTURE.md §3's replication-format stack entry, not
    // duplicated here.
    [[nodiscard]] DeltaSnapshot buildSnapshot(PlayerId recipient) const;

    [[nodiscard]] uint32_t lastProcessedSequence(PlayerId player) const;

private:
    struct PlayerQueue {
        std::vector<InputCommand> pending;
        uint32_t lastProcessedSequence = 0;
    };

    std::unordered_map<PlayerId, PlayerQueue> players_;
    ValidateFn validate_;
    ApplyFn apply_;
    GatherStateFn gatherState_;
    uint32_t currentTick_ = 0;
};

} // namespace engine::net
