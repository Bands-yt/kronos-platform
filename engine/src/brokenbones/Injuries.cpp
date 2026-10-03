#include "brokenbones/Injuries.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace engine::brokenbones {

using core::HumanoidRagdollPart;

namespace {

constexpr std::array<InjuryInfo, kInjuryCount> kInjuries = {{
    {"CONCUSSION", "SEEING STARS", 120},
    {"KNOCKED OUT", "LIGHTS OUT", 500},
    {"WHIPLASH", "NECK SNAPPED BACK", 150},
    {"DISLOCATED SHOULDER", "ARM POPPED OUT", 120},
    {"COMPOUND FRACTURE", "BONE THROUGH SKIN - BLEEDING", 300},
    {"INTERNAL BLEEDING", "THAT'S NOT GOOD", 250},
    {"PUNCTURED LUNG", "RIB WENT IN", 350},
    {"SPINAL INJURY", "LEGS STOPPED WORKING", 600},
}};

bool isUpperArm(HumanoidRagdollPart part) {
    return part == HumanoidRagdollPart::UpperArmL || part == HumanoidRagdollPart::UpperArmR;
}

} // namespace

const InjuryInfo& injuryInfo(Injury injury) { return kInjuries[static_cast<size_t>(injury)]; }

void InjuryTracker::begin() { *this = InjuryTracker{}; }

bool InjuryTracker::add(Injury injury, HumanoidRagdollPart part, std::vector<InjuryHit>& out) {
    if (has(injury)) return false;
    mask_ |= bit(injury);
    injuries_.push_back({injury, part});
    out.push_back({injury, part});
    return true;
}

std::vector<InjuryHit> InjuryTracker::registerImpact(HumanoidRagdollPart part, float impactSpeed, float breakSpeed,
                                                     bool broke) {
    std::vector<InjuryHit> out;
    if (part == HumanoidRagdollPart::Head && impactSpeed >= kConcussionSpeed) {
        add(Injury::Concussion, part, out);
        dazedSeconds_ = kDazedSeconds;
    }
    if (part == HumanoidRagdollPart::Head && impactSpeed >= kKnockoutSpeed && add(Injury::KnockedOut, part, out)) {
        knockoutSeconds_ = kKnockoutSeconds;
    }
    if (isUpperArm(part) && impactSpeed >= kDislocationSpeed) add(Injury::DislocatedShoulder, part, out);
    if (broke && impactSpeed >= kCompoundFactor * breakSpeed) {
        add(Injury::CompoundFracture, part, out);
        bool alreadyBleeding = std::find(bleeding_.begin(), bleeding_.end(), part) != bleeding_.end();
        if (!alreadyBleeding && bleeding_.size() < kMaxBleeds) bleeding_.push_back(part);
    }
    if (part == HumanoidRagdollPart::Abdomen && impactSpeed >= kInternalBleedingSpeed) {
        add(Injury::InternalBleeding, part, out);
    }
    if (part == HumanoidRagdollPart::Chest && broke && impactSpeed >= kPuncturedLungSpeed) {
        add(Injury::PuncturedLung, part, out);
    }
    if (part == HumanoidRagdollPart::Pelvis && impactSpeed >= kSpinalSpeed) add(Injury::SpinalInjury, part, out);
    return out;
}

std::optional<InjuryHit> InjuryTracker::registerNeckSnap(float headSpeedRelativeToChest) {
    std::vector<InjuryHit> out;
    if (headSpeedRelativeToChest < kWhiplashSpeed || !add(Injury::Whiplash, HumanoidRagdollPart::Head, out)) {
        return std::nullopt;
    }
    return out.front();
}

void InjuryTracker::update(float dt, bool bleedingCounts) {
    dt = std::max(dt, 0.0f);
    dazedSeconds_ = std::max(0.0f, dazedSeconds_ - dt);
    knockoutSeconds_ = std::max(0.0f, knockoutSeconds_ - dt);
    if (bleedingCounts && !bleeding_.empty()) bleedSeconds_ = std::min(kMaxBleedSeconds, bleedSeconds_ + dt);
}

float InjuryTracker::daze() const { return std::clamp(dazedSeconds_ / 2.0f, 0.0f, 1.0f); }

float InjuryTracker::blackout() const {
    // Snap to black, hold, then come round over the last second.
    return std::clamp(knockoutSeconds_, 0.0f, 1.0f);
}

int InjuryTracker::cash() const {
    int total = static_cast<int>(std::round(bleedSeconds_ * kBleedCashPerSecond));
    for (const InjuryHit& hit : injuries_) total += injuryInfo(hit.injury).cash;
    return total;
}

} // namespace engine::brokenbones
