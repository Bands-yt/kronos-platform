#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "net/NetTypes.hpp"

namespace engine::net {

// Client-side half of docs/ARCHITECTURE.md §4.2's reconciliation loop,
// implemented literally from that section's pseudocode:
//
//   Client, each local tick:
//     input = sample(); input.seq = next_seq++
//     buffer.push(input); apply(input)     // predicted, immediate local response
//     send(input) to server
//
//   Client, on snapshot received:
//     reconciled = snapshot.state
//     for input in buffer where input.seq > snapshot.last_processed_seq:
//         reconciled = apply(input, reconciled)   // replay unacknowledged inputs
//     buffer.drop(seq <= snapshot.last_processed_seq)
//
// This class owns the input ring buffer and sequence counter; it does not
// own what "apply" means for a given entity (movement/physics integration
// is gameplay- and Physics-specific), so both apply steps are injected as
// callbacks rather than hardcoded here.
class ClientPrediction {
public:
    using ApplyFn = std::function<void(const InputCommand&)>;
    using SetAuthoritativeStateFn = std::function<void(const EntityState&)>;

    // `predictedApply` runs immediately in recordAndPredict() (the local,
    // no-latency response) and again during reconcile() when replaying
    // unacknowledged inputs on top of the server's corrected state -- it
    // must be the same function both times, or prediction and
    // reconciliation would disagree about what an input does.
    void setPredictedApply(ApplyFn predictedApply) { predictedApply_ = std::move(predictedApply); }
    void setAuthoritativeState(SetAuthoritativeStateFn setState) { setAuthoritativeState_ = std::move(setState); }

    // Builds the next InputCommand (assigning the next sequence number),
    // applies it locally via predictedApply, buffers it for later
    // reconciliation, and returns it so the caller can send it to the
    // server over ENetTransport. This is "sample() + apply(input)" from
    // the pseudocode; "send(input) to server" is deliberately left to the
    // caller, matching ENetTransport's send() not knowing about
    // InputCommand's layout (see ENetTransport.hpp).
    InputCommand recordAndPredict(InputCommand input);

    // "on snapshot received" from the pseudocode: sets local state to the
    // server's authoritative snapshot, then replays every buffered input
    // newer than the snapshot's last-processed sequence on top of it, and
    // drops everything older from the buffer.
    void reconcile(const EntityState& authoritative, uint32_t lastProcessedSequence);

    [[nodiscard]] size_t pendingInputCount() const { return buffer_.size(); }

private:
    std::vector<InputCommand> buffer_;
    uint32_t nextSequence_ = 1;
    ApplyFn predictedApply_;
    SetAuthoritativeStateFn setAuthoritativeState_;
};

} // namespace engine::net
