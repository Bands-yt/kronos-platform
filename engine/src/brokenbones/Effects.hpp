#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "core/ECS.hpp"
#include "core/ParticleSystem.hpp"

namespace engine::brokenbones {

// One-shot particle presets. Counts and speeds scale with how hard the hit was.
[[nodiscard]] core::ParticleEmitterSettings impactDust(glm::vec3 rockColor, float impactSpeed);
[[nodiscard]] core::ParticleEmitterSettings boneChips(int bones);
[[nodiscard]] core::ParticleEmitterSettings bloodBurst(float impactSpeed);
[[nodiscard]] core::ParticleEmitterSettings splashColumn(float entrySpeed, bool lava);
[[nodiscard]] core::ParticleEmitterSettings splashRing(float entrySpeed, bool lava);
[[nodiscard]] core::ParticleEmitterSettings splashMist(float entrySpeed, bool lava);
[[nodiscard]] core::ParticleEmitterSettings explosionSmoke(float power);
// Continuous presets for emitters that follow the body.
[[nodiscard]] core::ParticleEmitterSettings bloodDrip();
[[nodiscard]] core::ParticleEmitterSettings rocketSmoke();

// Owns short-lived burst emitters and deletes each entity once its particles have died.
class EffectsPool {
public:
    static constexpr size_t kMaxLive = 48;

    void attach(core::ECS& ecs) { ecs_ = &ecs; }
    void burst(glm::vec3 at, const core::ParticleEmitterSettings& settings);
    void update(float dt);
    void clear();
    [[nodiscard]] size_t live() const { return live_.size(); }

private:
    struct Live {
        core::EntityId entity;
        float secondsLeft;
    };
    core::ECS* ecs_ = nullptr;
    std::vector<Live> live_;
};

} // namespace engine::brokenbones
