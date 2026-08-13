#include "net/RemoteEntityInterpolation.hpp"

#include <algorithm>

#include <glm/gtc/quaternion.hpp>

namespace engine::net {

namespace {
EntityState extrapolate(const RemoteEntitySnapshot& snapshot, float targetTimeSeconds) {
    float dt = std::clamp(targetTimeSeconds - snapshot.receivedAtSeconds, 0.0f,
                           RemoteEntityInterpolator::kMaxExtrapolationSeconds);
    EntityState result = snapshot.state;
    result.position += snapshot.state.velocity * dt; // real dead-reckoning along the last known velocity
    return result;
}
} // namespace

void RemoteEntityInterpolator::pushSnapshot(const EntityState& state, float receivedAtSeconds) {
    if (hasNewer_ && receivedAtSeconds < newer_.receivedAtSeconds) {
        // A real out-of-order arrival -- honestly dropped, not inserted
        // out of chronological order (see class header comment on why
        // the buffer's ordering invariant matters).
        return;
    }
    older_ = newer_;
    hasOlder_ = hasNewer_;
    newer_.state = state;
    newer_.receivedAtSeconds = receivedAtSeconds;
    hasNewer_ = true;
}

EntityState RemoteEntityInterpolator::sample(float renderTimeSeconds) const {
    if (!hasNewer_) return EntityState{}; // real, honest "nothing received yet" -- identity state, not garbage

    float targetTime = renderTimeSeconds - kInterpolationDelaySeconds;

    if (!hasOlder_) {
        // Only one snapshot ever received -- nothing to interpolate
        // between yet, so dead-reckon from the one we have.
        return extrapolate(newer_, targetTime);
    }

    if (targetTime <= older_.receivedAtSeconds) {
        // Target predates even the older buffered snapshot -- clamp to
        // it rather than extrapolating backward in time.
        return older_.state;
    }

    if (targetTime >= newer_.receivedAtSeconds) {
        return extrapolate(newer_, targetTime);
    }

    // Real interpolation, bracketed between two real received states.
    float span = newer_.receivedAtSeconds - older_.receivedAtSeconds;
    float t = span > 0.0f ? (targetTime - older_.receivedAtSeconds) / span : 1.0f;

    EntityState result;
    result.networkId = newer_.state.networkId;
    result.position = glm::mix(older_.state.position, newer_.state.position, t);
    result.rotation = glm::slerp(older_.state.rotation, newer_.state.rotation, t);
    result.velocity = glm::mix(older_.state.velocity, newer_.state.velocity, t);
    return result;
}

} // namespace engine::net
