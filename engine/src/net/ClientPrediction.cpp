#include "net/ClientPrediction.hpp"

#include <algorithm>

namespace engine::net {

InputCommand ClientPrediction::recordAndPredict(InputCommand input) {
    input.sequence = nextSequence_++;

    buffer_.push_back(input);
    if (predictedApply_) {
        predictedApply_(input);
    }
    return input;
}

void ClientPrediction::reconcile(const EntityState& authoritative, uint32_t lastProcessedSequence) {
    if (setAuthoritativeState_) {
        setAuthoritativeState_(authoritative);
    }

    // Drop everything the server has already accounted for, then replay
    // the rest on top of the state we just set -- exactly the pseudocode's
    // "for input in buffer where input.seq > last_processed_seq: reconciled
    // = apply(input, reconciled)".
    buffer_.erase(
        std::remove_if(buffer_.begin(), buffer_.end(),
                        [&](const InputCommand& cmd) { return cmd.sequence <= lastProcessedSequence; }),
        buffer_.end());

    if (predictedApply_) {
        for (const auto& cmd : buffer_) {
            predictedApply_(cmd);
        }
    }
}

} // namespace engine::net
