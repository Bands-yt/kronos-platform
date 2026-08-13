#include "net/ServerReconciliation.hpp"

#include <algorithm>
#include <cstdio>

namespace engine::net {

void ServerReconciliation::registerPlayer(PlayerId player) {
    players_.emplace(player, PlayerQueue{});
}

void ServerReconciliation::unregisterPlayer(PlayerId player) {
    players_.erase(player);
}

void ServerReconciliation::queueInput(PlayerId player, const InputCommand& input) {
    auto it = players_.find(player);
    if (it == players_.end()) return; // not a registered/connected player -- drop silently
    it->second.pending.push_back(input);
}

void ServerReconciliation::tick(uint32_t serverTick) {
    currentTick_ = serverTick;

    for (auto& [player, queue] : players_) {
        // "ordered by seq" -- a client's packets can arrive out of order
        // over UDP even with ENet's reliable channel retry semantics for
        // packets on *different* channels, so this is not a no-op.
        std::sort(queue.pending.begin(), queue.pending.end(),
                  [](const InputCommand& a, const InputCommand& b) { return a.sequence < b.sequence; });

        for (const auto& input : queue.pending) {
            if (validate_ && !validate_(player, input)) {
                // Reject and move on -- does NOT disconnect or flag the
                // player here. Repeated validation failures are an
                // anti-cheat behavioral signal (docs/ARCHITECTURE.md §11),
                // not this class's concern to score.
                std::fprintf(stderr, "ServerReconciliation: rejected input seq=%u from player=%u\n",
                             input.sequence, player);
                continue;
            }
            if (apply_) {
                apply_(player, input);
            }
            queue.lastProcessedSequence = input.sequence;
        }
        queue.pending.clear();
    }
}

DeltaSnapshot ServerReconciliation::buildSnapshot(PlayerId recipient) const {
    DeltaSnapshot snapshot;
    snapshot.tick = currentTick_;
    snapshot.baselineTick = 0; // TODO(wire format): full snapshot until delta-compression exists, see header note

    auto it = players_.find(recipient);
    snapshot.lastProcessedInputSequence = (it != players_.end()) ? it->second.lastProcessedSequence : 0;

    if (gatherState_) {
        snapshot.entities = gatherState_(recipient);
    }
    return snapshot;
}

uint32_t ServerReconciliation::lastProcessedSequence(PlayerId player) const {
    auto it = players_.find(player);
    return it != players_.end() ? it->second.lastProcessedSequence : 0;
}

} // namespace engine::net
