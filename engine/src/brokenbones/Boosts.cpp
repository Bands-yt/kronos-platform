#include "brokenbones/Boosts.hpp"

#include <algorithm>
#include <cmath>

namespace engine::brokenbones {

using core::HumanoidRagdollPart;

namespace {

glm::vec3 flatten(glm::vec3 aim) {
    glm::vec3 flat(aim.x, 0.0f, aim.z);
    float length = glm::length(flat);
    return length > 1e-4f ? flat / length : glm::vec3(0.0f, 0.0f, 1.0f);
}

constexpr int partIndex(HumanoidRagdollPart part) { return static_cast<int>(part); }

} // namespace

glm::vec3 bombLaunchVelocity(glm::vec3 aim) {
    return flatten(aim) * BoostController::kBombLaunchForward + glm::vec3(0.0f, BoostController::kBombLaunchUp, 0.0f);
}

glm::vec3 rocketDiveDirection(glm::vec3 aim) { return glm::normalize(flatten(aim) * 0.45f + glm::vec3(0.0f, -1.0f, 0.0f)); }

void BoostController::beginRun(const Progress& progress) { helium_.refill(floatsHeliumCapacity(progress)); }

BoostState BoostController::update(float dt, const BoostInput& input, core::Physics& physics,
                                   core::Physics::RagdollHandle ragdoll) {
    BoostState state;
    if (!physics.ragdollExists(ragdoll) || dt <= 0.0f || !input.floats) return state;
    float burn = helium_.burn(dt);
    if (burn > 0.0f) {
        float fallSpeed = std::max(0.0f, -physics.ragdollPartVelocity(ragdoll, 0).y);
        glm::vec3 deltaV(0.0f, (kFloatLift + kFloatDrag * fallSpeed) * burn, 0.0f);
        deltaV += flatten(input.aim) * (kFloatGlide * burn);
        physics.addRagdollVelocity(ragdoll, deltaV);
        state.floatsLifting = true;
    }
    return state;
}

bool RocketController::update(float dt, glm::vec3 aim, core::Physics& physics, core::Physics::RagdollHandle ragdoll) {
    if (!live_ || dt <= 0.0f || !physics.ragdollExists(ragdoll)) return false;
    float burn = std::max(0.0f, std::min(age_ + dt, tuning_.burnSeconds) - std::max(age_, 0.0f));
    age_ += dt;
    if (burn > 0.0f) {
        glm::vec3 dive = rocketDiveDirection(aim);
        physics.addRagdollVelocity(ragdoll, dive * (tuning_.thrust * burn));

        std::vector<glm::mat4> parts;
        if (physics.getRagdollPartTransforms(ragdoll, parts) && parts.size() == core::kHumanoidRagdollPartCount) {
            glm::vec3 head(parts[partIndex(HumanoidRagdollPart::Head)][3]);
            glm::vec3 pelvis(parts[partIndex(HumanoidRagdollPart::Pelvis)][3]);
            glm::vec3 spine = glm::normalize(head - pelvis);
            glm::vec3 error = dive - spine * glm::dot(spine, dive);
            // Head pointing straight away from the dive: any sideways nudge starts the flip.
            if (glm::dot(spine, dive) < -0.9f && glm::length(error) < 0.3f) error = flatten(aim);
            glm::vec3 relative = physics.ragdollPartVelocity(ragdoll, partIndex(HumanoidRagdollPart::Head)) -
                                 physics.ragdollPartVelocity(ragdoll, partIndex(HumanoidRagdollPart::Pelvis));
            relative -= spine * glm::dot(spine, relative);
            glm::vec3 impulse = (error * kSteerStiffness - relative * kSteerDamping) * (0.5f * burn);
            physics.addRagdollImpulse(ragdoll, partIndex(HumanoidRagdollPart::Head), impulse);
            physics.addRagdollImpulse(ragdoll, partIndex(HumanoidRagdollPart::Chest), impulse);
            physics.addRagdollImpulse(ragdoll, partIndex(HumanoidRagdollPart::LowerLegL), -impulse);
            physics.addRagdollImpulse(ragdoll, partIndex(HumanoidRagdollPart::LowerLegR), -impulse);
        }
    }
    return age_ >= fuseSeconds();
}

RocketTuning rocketTuning(const Progress& progress, bool super) {
    RocketTuning tuning{RocketController::kThrust * rocketThrustMultiplier(progress), rocketBurnSeconds(progress)};
    if (super) {
        tuning.thrust *= RocketController::kSuperThrust;
        tuning.burnSeconds *= RocketController::kSuperBurn;
    }
    return tuning;
}

void RocketTimingGame::start(std::mt19937& rng, int level) {
    float width = std::max(0.16f, 0.28f - 0.01f * static_cast<float>(std::max(level, 1) - 1));
    float centre = std::uniform_real_distribution<float>(0.5f, 0.85f)(rng);
    centre = std::min(centre, 1.0f - 0.5f * width);
    greenMin_ = centre - 0.5f * width;
    greenMax_ = centre + 0.5f * width;
    elapsed_ = 0.0f;
    active_ = true;
}

TimingResult RocketTimingGame::update(float dt) {
    if (!active_) return TimingResult::Pending;
    elapsed_ += std::max(dt, 0.0f);
    if (elapsed_ < kTimeoutSeconds) return TimingResult::Pending;
    active_ = false;
    return TimingResult::TimedOut;
}

TimingResult RocketTimingGame::lock() {
    if (!active_) return TimingResult::Pending;
    active_ = false;
    float m = marker();
    return m >= greenMin_ && m <= greenMax_ ? TimingResult::Hit : TimingResult::Miss;
}

float RocketTimingGame::marker() const {
    float phase = std::fmod(elapsed_ / kSweepSeconds, 2.0f);
    return phase <= 1.0f ? phase : 2.0f - phase;
}

glm::vec3 airDragDeltaV(glm::vec3 velocity, float dt) {
    constexpr float kDrag = 9.81f / (kTerminalSpeed * kTerminalSpeed);
    float speed = glm::length(velocity);
    return -velocity * std::min(1.0f, kDrag * speed * std::max(dt, 0.0f));
}

std::vector<BoneBreak> applyBlast(core::Physics& physics, core::Physics::RagdollHandle ragdoll, RunTracker& run,
                                  glm::vec3 centre, float blastSpeed, glm::vec3 launchVelocity, std::mt19937& rng,
                                  float absorb) {
    std::vector<BoneBreak> breaks;
    std::vector<glm::mat4> parts;
    if (!physics.getRagdollPartTransforms(ragdoll, parts)) return breaks;
    for (size_t p = 0; p < parts.size() && p < core::kHumanoidRagdollPartCount; ++p) {
        float distance = glm::distance(glm::vec3(parts[p][3]), centre);
        float speed = blastSpeed * std::clamp(1.25f - distance / 1.6f, 0.35f, 1.0f);
        auto broken = run.registerImpact(static_cast<HumanoidRagdollPart>(p), speed);
        if (!broken) continue;
        physics.setRagdollJointLimp(ragdoll, static_cast<int>(p));
        breaks.push_back(*broken);
    }
    physics.addRagdollVelocity(ragdoll, launchVelocity - physics.ragdollPartVelocity(ragdoll, 0) * absorb);
    std::uniform_real_distribution<float> tumble(-1.0f, 1.0f);
    glm::vec3 spin(tumble(rng) * 10.0f, 7.0f, tumble(rng) * 10.0f);
    physics.addRagdollImpulse(ragdoll, partIndex(HumanoidRagdollPart::Head), spin);
    return breaks;
}

} // namespace engine::brokenbones
