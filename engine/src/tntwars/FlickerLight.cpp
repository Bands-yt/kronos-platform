#include "tntwars/FlickerLight.hpp"

#include <cmath>

#include "core/Components.hpp"

namespace engine::tntwars {

float flickerIntensityFactor(float phase, float totalTimeSeconds) {
    float t = totalTimeSeconds + phase;
    float steady = 0.85f + 0.15f * std::sin(t * 2.3f);

    float window = std::floor(t * 2.0f);
    float hash = std::sin(window * 78.233f + phase * 12.9898f) * 43758.5453f;
    hash -= std::floor(hash); // real, fractional part -- a real, deterministic 0..1 pseudo-random value per window
    float dropout = hash < 0.06f ? 0.15f : 1.0f;

    return steady * dropout;
}

void tickFlickerLights(core::ECS& ecs, float totalTimeSeconds) {
    auto view = ecs.view<FlickerLight, core::Renderable>();
    for (auto entity : view) {
        auto& light = view.get<FlickerLight>(entity);
        auto& renderable = view.get<core::Renderable>(entity);
        renderable.emissiveColor = light.baseColor;
        renderable.emissiveIntensity = light.baseIntensity * flickerIntensityFactor(light.phase, totalTimeSeconds);
    }
}

} // namespace engine::tntwars
