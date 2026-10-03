#pragma once

#include <array>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

#include "brokenbones/CliffGenerator.hpp"
#include "core/HumanoidRagdoll.hpp"

namespace engine::brokenbones {

[[nodiscard]] const char* boneName(core::HumanoidRagdollPart part);
// Closing speed (m/s) at which an impact on this part breaks its bone.
[[nodiscard]] float boneBreakSpeed(core::HumanoidRagdollPart part);
// Bones a run must break to unlock the next, taller level.
[[nodiscard]] int bonesNeededForLevel(int level);

inline constexpr int kBaseBoneCount = static_cast<int>(core::kHumanoidRagdollPartCount);
inline constexpr int kMaxBoneCount = 500;
using BoneCounts = std::array<int, core::kHumanoidRagdollPartCount>;
// Splits `total` bones over the body parts roughly like a real skeleton (hands and feet hold the most).
[[nodiscard]] BoneCounts distributeBones(int total);
// How many of a part's `remaining` bones one impact snaps; harder hits shatter more.
[[nodiscard]] int bonesSnappedByImpact(int partBones, int remaining, float impactSpeed, float breakSpeed);

// None means the player abandoned the run; EndedEarly means they skipped the wait after a real fall.
enum class RunEndReason { None, CameToRest, Splashdown, OutOfBounds, TimeLimit, EndedEarly };
[[nodiscard]] const char* runEndReasonText(RunEndReason reason);

struct BoneBreak {
    core::HumanoidRagdollPart part;
    float impactSpeed = 0.0f;
    int combo = 1; // position in a chain of breaks less than kComboWindowSeconds apart
    int count = 1; // bones snapped by this impact
    int partBones = 1;
};

// Everything a single fall tracks, independent of rendering.
class RunTracker {
public:
    static constexpr float kStillSpeed = 0.8f;
    static constexpr float kStillSecondsToEnd = 3.0f;
    static constexpr float kTimeLimitSeconds = 120.0f;
    static constexpr float kCountedHitSpeed = 4.0f;
    static constexpr float kComboWindowSeconds = 1.5f;

    void begin(float startHeight, float breakSpeedMultiplier = 1.0f, int totalBones = kBaseBoneCount);

    // Returns the break if this impact snapped bones that were still intact.
    std::optional<BoneBreak> registerImpact(core::HumanoidRagdollPart part, float impactSpeed);

    // `maxPartSpeed` is the fastest ragdoll body this frame.
    RunEndReason update(float dt, glm::vec3 pelvisPosition, float maxPartSpeed, const CliffLayout& layout);

    [[nodiscard]] bool isBroken(core::HumanoidRagdollPart part) const { return brokenInPart(part) > 0; }
    [[nodiscard]] int brokenInPart(core::HumanoidRagdollPart part) const { return broken_[static_cast<size_t>(part)]; }
    [[nodiscard]] int bonesInPart(core::HumanoidRagdollPart part) const { return bones_[static_cast<size_t>(part)]; }
    [[nodiscard]] int totalBones() const { return totalBones_; }
    [[nodiscard]] int bonesBroken() const { return bonesBroken_; }
    [[nodiscard]] int hits() const { return hits_; }
    [[nodiscard]] float hardestImpact() const { return hardestImpact_; }
    [[nodiscard]] float topSpeed() const { return topSpeed_; }
    [[nodiscard]] float distanceFallen() const { return startHeight_ - lowestHeight_; }
    [[nodiscard]] float stillSeconds() const { return stillSeconds_; }
    [[nodiscard]] float elapsedSeconds() const { return elapsed_; }
    [[nodiscard]] const std::vector<BoneBreak>& breaks() const { return breaks_; }
    [[nodiscard]] int bestCombo() const { return bestCombo_; }
    // The running chain, or 0 once the window has lapsed.
    [[nodiscard]] int liveCombo() const {
        return !breaks_.empty() && elapsed_ - lastBreakTime_ <= kComboWindowSeconds ? breaks_.back().combo : 0;
    }

private:
    BoneCounts broken_{};
    BoneCounts bones_ = distributeBones(kBaseBoneCount);
    int totalBones_ = kBaseBoneCount;
    int bonesBroken_ = 0;
    int hits_ = 0;
    float hardestImpact_ = 0.0f;
    float topSpeed_ = 0.0f;
    float startHeight_ = 0.0f;
    float breakSpeedMultiplier_ = 1.0f;
    float lowestHeight_ = 0.0f;
    float stillSeconds_ = 0.0f;
    float elapsed_ = 0.0f;
    float lastBreakTime_ = -1e6f;
    int bestCombo_ = 0;
    std::vector<BoneBreak> breaks_;
};

} // namespace engine::brokenbones
