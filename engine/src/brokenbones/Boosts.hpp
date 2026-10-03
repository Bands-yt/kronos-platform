#pragma once

#include <algorithm>
#include <random>
#include <vector>

#include <glm/glm.hpp>

#include "brokenbones/Shop.hpp"
#include "core/Physics.hpp"

namespace engine::brokenbones {

struct BoostInput {
    bool floats = false;
    glm::vec3 aim{0.0f, 0.0f, 1.0f};
};

struct BoostState {
    bool floatsLifting = false;
};

// Floats are applied as uniform velocity changes to every ragdoll part so lift never spins the body.
class BoostController {
public:
    static constexpr float kFloatLift = 9.0f;
    static constexpr float kFloatDrag = 1.6f;
    static constexpr float kFloatGlide = 4.0f;
    static constexpr float kBombLaunchForward = 26.0f;
    static constexpr float kBombLaunchUp = 13.0f;

    void beginRun(const Progress& progress);
    BoostState update(float dt, const BoostInput& input, core::Physics& physics, core::Physics::RagdollHandle ragdoll);

    [[nodiscard]] const FuelTank& helium() const { return helium_; }

private:
    FuelTank helium_;
};

struct RocketTuning {
    float thrust = 40.0f;
    float burnSeconds = 1.8f;
};

// Pushes the body along a steep dive and twists it so the head leads, then
// blows up on the first hard impact (or when the fuse runs out).
class RocketController {
public:
    static constexpr float kBurnSeconds = 1.8f;
    static constexpr float kThrust = 40.0f;
    static constexpr float kSteerStiffness = 700.0f;
    static constexpr float kSteerDamping = 90.0f;
    static constexpr float kArmSeconds = 0.25f;
    static constexpr float kFuseAfterBurn = 2.5f;
    static constexpr float kBlastSpeed = 24.0f;
    static constexpr float kBlastLaunch = 15.0f;
    static constexpr float kBlastAbsorb = 0.85f;
    static constexpr float kSuperThrust = 2.0f;
    static constexpr float kSuperBurn = 1.4f;

    void ignite(float delaySeconds = 0.0f, RocketTuning tuning = {}) {
        live_ = true;
        age_ = -std::max(delaySeconds, 0.0f);
        tuning_ = tuning;
    }
    void reset() { live_ = false; }
    [[nodiscard]] bool live() const { return live_; }
    [[nodiscard]] bool burning() const { return live_ && age_ >= 0.0f && age_ < tuning_.burnSeconds; }
    [[nodiscard]] float burnFraction() const {
        return live_ ? glm::clamp(1.0f - age_ / tuning_.burnSeconds, 0.0f, 1.0f) : 0.0f;
    }
    [[nodiscard]] float fuseSeconds() const { return tuning_.burnSeconds + kFuseAfterBurn; }
    [[nodiscard]] const RocketTuning& tuning() const { return tuning_; }

    // Returns true when the fuse runs out and the rocket should detonate in mid-air.
    bool update(float dt, glm::vec3 aim, core::Physics& physics, core::Physics::RagdollHandle ragdoll);
    // Whether a hard impact now should detonate the rocket.
    [[nodiscard]] bool armed() const { return live_ && age_ >= kArmSeconds; }

private:
    bool live_ = false;
    float age_ = 0.0f;
    RocketTuning tuning_;
};

[[nodiscard]] RocketTuning rocketTuning(const Progress& progress, bool super);

enum class TimingResult { Pending, Hit, Miss, TimedOut };

// A marker sweeps back and forth across a bar; locking it inside the green zone
// earns a super boost, anywhere else sets the rocket off.
class RocketTimingGame {
public:
    static constexpr float kSweepSeconds = 1.3f;
    static constexpr float kTimeoutSeconds = 4.5f;

    void start(std::mt19937& rng, int level);
    void cancel() { active_ = false; }
    TimingResult update(float dt);
    TimingResult lock();

    [[nodiscard]] bool active() const { return active_; }
    [[nodiscard]] float marker() const;
    [[nodiscard]] float greenMin() const { return greenMin_; }
    [[nodiscard]] float greenMax() const { return greenMax_; }
    [[nodiscard]] float timeLeft() const { return std::max(0.0f, kTimeoutSeconds - elapsed_); }

private:
    bool active_ = false;
    float elapsed_ = 0.0f;
    float greenMin_ = 0.0f;
    float greenMax_ = 0.0f;
};

inline constexpr float kTerminalSpeed = 65.0f;
// Quadratic air drag as a velocity change, so free fall levels off near kTerminalSpeed.
[[nodiscard]] glm::vec3 airDragDeltaV(glm::vec3 velocity, float dt);

[[nodiscard]] glm::vec3 bombLaunchVelocity(glm::vec3 aim);
[[nodiscard]] glm::vec3 rocketDiveDirection(glm::vec3 aim);

// Breaks bones near `centre` (full `blastSpeed` within ~0.4 m, fading with distance),
// makes broken joints limp and throws the body. `absorb` is the share of the
// body's current velocity the blast cancels before adding `launchVelocity`.
std::vector<BoneBreak> applyBlast(core::Physics& physics, core::Physics::RagdollHandle ragdoll, RunTracker& run,
                                  glm::vec3 centre, float blastSpeed, glm::vec3 launchVelocity, std::mt19937& rng,
                                  float absorb = 0.0f);

} // namespace engine::brokenbones
