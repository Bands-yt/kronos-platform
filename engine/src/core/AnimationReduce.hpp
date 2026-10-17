#pragma once

#include <cmath>
#include <vector>

namespace engine::core {

struct FloatKey {
    float time = 0.0f;
    float value = 0.0f;
};

// Drops keys that a straight line between their neighbours already reproduces within `tolerance`.
// First and last keys always stay. Works on one channel; run it per channel.
inline std::vector<FloatKey> reduceKeys(const std::vector<FloatKey>& keys, float tolerance) {
    if (keys.size() < 3) return keys;
    std::vector<FloatKey> out{keys.front()};
    for (std::size_t i = 1; i + 1 < keys.size(); ++i) {
        const FloatKey& a = out.back();
        const FloatKey& c = keys[i + 1];
        const float span = c.time - a.time;
        const float t = span > 1e-9f ? (keys[i].time - a.time) / span : 0.0f;
        const float predicted = a.value + (c.value - a.value) * t;
        if (std::fabs(predicted - keys[i].value) > tolerance) out.push_back(keys[i]);
    }
    out.push_back(keys.back());
    return out;
}

} // namespace engine::core
