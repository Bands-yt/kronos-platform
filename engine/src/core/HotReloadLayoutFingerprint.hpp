#pragma once

#include <cstdint>

#include "core/ECS.hpp"
#include "core/Renderer.hpp"

namespace engine::core {

inline constexpr unsigned long long kHotReloadLayoutFingerprint = [] {
    unsigned long long hash = 14695981039346656037ull;
    for (unsigned long long value : {sizeof(Renderer), alignof(Renderer), sizeof(ECS), alignof(ECS)}) {
        hash = (hash ^ value) * 1099511628211ull;
    }
    return hash;
}();

} // namespace engine::core
