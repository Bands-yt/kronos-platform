#pragma once

#include <array>
#include <cmath>
#include <cstddef>

namespace engine::core {

// The practical split scheme (Zhang et al.) cascade-split-depth
// computation, extracted out of Renderer::computeCascades() as a pure,
// Vulkan/GPU-free function -- purely so it's independently unit-testable
// (see tests/test_main.cpp's testCascadeSplitMath()) without needing a
// live Renderer/device. Renderer.cpp calls this directly rather than
// keeping a second, duplicate copy of the formula, so a test verifying
// this function is verifying what actually runs, not a reimplementation
// that could silently drift from it.
//
// Blends a purely logarithmic split (matches perspective's natural depth-
// precision falloff, but puts too much of the shadow budget far away for
// typical third-person camera distances) with a purely uniform split
// (opposite problem) via splitLambda -- 0.5 is Renderer's own standard,
// unremarkable starting point, not baked in here as a default so callers
// (and tests) can exercise the pure log/uniform extremes too.
template <size_t CascadeCount>
[[nodiscard]] std::array<float, CascadeCount> computeCascadeSplitDepths(float nearDist, float farDist,
                                                                          float splitLambda) {
    std::array<float, CascadeCount> splitDepths{};
    for (size_t i = 0; i < CascadeCount; ++i) {
        float p = static_cast<float>(i + 1) / static_cast<float>(CascadeCount);
        float logSplit = nearDist * std::pow(farDist / nearDist, p);
        float uniformSplit = nearDist + (farDist - nearDist) * p;
        splitDepths[i] = splitLambda * logSplit + (1.0f - splitLambda) * uniformSplit;
    }
    return splitDepths;
}

} // namespace engine::core
