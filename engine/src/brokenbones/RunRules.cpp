#include "brokenbones/RunRules.hpp"

#include <algorithm>

namespace engine::brokenbones {

using core::HumanoidRagdollPart;

const char* boneName(HumanoidRagdollPart part) {
    switch (part) {
        case HumanoidRagdollPart::Pelvis: return "PELVIS";
        case HumanoidRagdollPart::Abdomen: return "SPINE";
        case HumanoidRagdollPart::Chest: return "RIBS";
        case HumanoidRagdollPart::Head: return "SKULL";
        case HumanoidRagdollPart::UpperArmL: return "LEFT HUMERUS";
        case HumanoidRagdollPart::LowerArmL: return "LEFT FOREARM";
        case HumanoidRagdollPart::UpperArmR: return "RIGHT HUMERUS";
        case HumanoidRagdollPart::LowerArmR: return "RIGHT FOREARM";
        case HumanoidRagdollPart::UpperLegL: return "LEFT FEMUR";
        case HumanoidRagdollPart::LowerLegL: return "LEFT TIBIA";
        case HumanoidRagdollPart::UpperLegR: return "RIGHT FEMUR";
        case HumanoidRagdollPart::LowerLegR: return "RIGHT TIBIA";
    }
    return "BONE";
}

float boneBreakSpeed(HumanoidRagdollPart part) {
    switch (part) {
        case HumanoidRagdollPart::Head: return 9.0f;
        case HumanoidRagdollPart::Pelvis:
        case HumanoidRagdollPart::Abdomen:
        case HumanoidRagdollPart::Chest: return 12.0f;
        case HumanoidRagdollPart::UpperArmL:
        case HumanoidRagdollPart::UpperArmR:
        case HumanoidRagdollPart::UpperLegL:
        case HumanoidRagdollPart::UpperLegR: return 11.0f;
        case HumanoidRagdollPart::LowerArmL:
        case HumanoidRagdollPart::LowerArmR:
        case HumanoidRagdollPart::LowerLegL:
        case HumanoidRagdollPart::LowerLegR: return 10.0f;
    }
    return 10.0f;
}

int bonesNeededForLevel(int level) {
    level = std::max(level, 1);
    if (level <= 8) return 4 + level;
    int beyond = level - 8;
    return std::min(12 + 3 * beyond * beyond, kMaxBoneCount);
}

BoneCounts distributeBones(int total) {
    // Pelvis, abdomen, chest, head, then arms and legs (upper, lower) left and right; sums to 206.
    constexpr std::array<int, core::kHumanoidRagdollPartCount> kWeights = {4, 5, 48, 29, 1, 29, 1, 29, 2, 28, 2, 28};
    constexpr int kWeightSum = 206;
    total = std::clamp(total, kBaseBoneCount, kMaxBoneCount);
    BoneCounts counts;
    counts.fill(1);
    int extra = total - kBaseBoneCount;
    std::array<float, core::kHumanoidRagdollPartCount> remainder{};
    int given = 0;
    for (size_t p = 0; p < counts.size(); ++p) {
        float share = static_cast<float>(extra) * static_cast<float>(kWeights[p]) / kWeightSum;
        int whole = static_cast<int>(share);
        counts[p] += whole;
        given += whole;
        remainder[p] = share - static_cast<float>(whole);
    }
    for (; given < extra; ++given) {
        size_t best = static_cast<size_t>(std::max_element(remainder.begin(), remainder.end()) - remainder.begin());
        ++counts[best];
        remainder[best] = -1.0f;
    }
    return counts;
}

int bonesSnappedByImpact(int partBones, int remaining, float impactSpeed, float breakSpeed) {
    if (remaining <= 0 || impactSpeed < breakSpeed || breakSpeed <= 0.0f) return 0;
    float overshoot = impactSpeed / breakSpeed - 1.0f;
    int snapped = 1 + static_cast<int>(overshoot * static_cast<float>(partBones) * 0.3f);
    return std::min(snapped, remaining);
}

const char* runEndReasonText(RunEndReason reason) {
    switch (reason) {
        case RunEndReason::None: return "";
        case RunEndReason::CameToRest: return "CAME TO REST";
        case RunEndReason::Splashdown: return "SPLASHDOWN";
        case RunEndReason::OutOfBounds: return "OUT OF BOUNDS";
        case RunEndReason::TimeLimit: return "TIME'S UP";
        case RunEndReason::EndedEarly: return "ENDED EARLY";
    }
    return "";
}

void RunTracker::begin(float startHeight, float breakSpeedMultiplier, int totalBones) {
    *this = RunTracker{};
    bones_ = distributeBones(totalBones);
    totalBones_ = std::clamp(totalBones, kBaseBoneCount, kMaxBoneCount);
    breakSpeedMultiplier_ = breakSpeedMultiplier;
    startHeight_ = startHeight;
    lowestHeight_ = startHeight;
}

std::optional<BoneBreak> RunTracker::registerImpact(HumanoidRagdollPart part, float impactSpeed) {
    hardestImpact_ = std::max(hardestImpact_, impactSpeed);
    if (impactSpeed >= kCountedHitSpeed) ++hits_;
    size_t index = static_cast<size_t>(part);
    int snapped = bonesSnappedByImpact(bones_[index], bones_[index] - broken_[index], impactSpeed,
                                       boneBreakSpeed(part) * breakSpeedMultiplier_);
    if (snapped == 0) return std::nullopt;
    broken_[index] += snapped;
    bonesBroken_ += snapped;
    int combo = liveCombo() + 1;
    bestCombo_ = std::max(bestCombo_, combo);
    lastBreakTime_ = elapsed_;
    breaks_.push_back(BoneBreak{part, impactSpeed, combo, snapped, bones_[index]});
    return breaks_.back();
}

RunEndReason RunTracker::update(float dt, glm::vec3 pelvisPosition, float maxPartSpeed, const CliffLayout& layout) {
    elapsed_ += dt;
    topSpeed_ = std::max(topSpeed_, maxPartSpeed);
    lowestHeight_ = std::min(lowestHeight_, pelvisPosition.y);

    if (layout.inLagoon(pelvisPosition) && pelvisPosition.y < layout.waterY + 0.2f) return RunEndReason::Splashdown;
    if (pelvisPosition.y < layout.lagoonFloorY - 30.0f) return RunEndReason::OutOfBounds;

    stillSeconds_ = maxPartSpeed < kStillSpeed ? stillSeconds_ + dt : 0.0f;
    if (stillSeconds_ >= kStillSecondsToEnd) return RunEndReason::CameToRest;
    if (elapsed_ >= kTimeLimitSeconds) return RunEndReason::TimeLimit;
    return RunEndReason::None;
}

} // namespace engine::brokenbones
