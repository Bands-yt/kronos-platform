#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "core/HumanoidRagdoll.hpp"

namespace engine::brokenbones {

enum class Injury {
    Concussion,
    KnockedOut,
    Whiplash,
    DislocatedShoulder,
    CompoundFracture,
    InternalBleeding,
    PuncturedLung,
    SpinalInjury,
};
inline constexpr size_t kInjuryCount = 8;

struct InjuryInfo {
    const char* name;
    const char* effect; // what it does to the player, shown when it happens
    int cash;
};
[[nodiscard]] const InjuryInfo& injuryInfo(Injury injury);

struct InjuryHit {
    Injury injury;
    core::HumanoidRagdollPart part;
};

// Injuries a single fall picks up on top of broken bones. Each pays once per run;
// some leave lasting effects (dazed, blacked out, bleeding, limp limbs) the game applies.
class InjuryTracker {
public:
    static constexpr float kConcussionSpeed = 15.0f;
    static constexpr float kKnockoutSpeed = 35.0f;
    static constexpr float kWhiplashSpeed = 18.0f; // head moving relative to the chest
    static constexpr float kDislocationSpeed = 14.0f;
    static constexpr float kCompoundFactor = 2.5f; // times the part's break speed
    static constexpr float kInternalBleedingSpeed = 25.0f;
    static constexpr float kPuncturedLungSpeed = 30.0f;
    static constexpr float kSpinalSpeed = 38.0f;
    static constexpr float kDazedSeconds = 8.0f;
    static constexpr float kKnockoutSeconds = 3.0f;
    static constexpr float kBleedCashPerSecond = 12.0f;
    static constexpr float kMaxBleedSeconds = 30.0f;
    static constexpr size_t kMaxBleeds = 3;

    void begin();
    // `breakSpeed` is the part's effective break threshold; `broke` whether this impact snapped bones.
    std::vector<InjuryHit> registerImpact(core::HumanoidRagdollPart part, float impactSpeed, float breakSpeed,
                                          bool broke);
    std::optional<InjuryHit> registerNeckSnap(float headSpeedRelativeToChest);
    // Bleeding only pays while `bleedingCounts` (the run is still going).
    void update(float dt, bool bleedingCounts = true);

    [[nodiscard]] bool has(Injury injury) const { return (mask_ & bit(injury)) != 0; }
    [[nodiscard]] uint32_t mask() const { return mask_; }
    [[nodiscard]] int count() const { return static_cast<int>(injuries_.size()); }
    [[nodiscard]] const std::vector<InjuryHit>& injuries() const { return injuries_; }
    [[nodiscard]] const std::vector<core::HumanoidRagdollPart>& bleeding() const { return bleeding_; }
    // 0..1 how dazed and how blacked out the player is right now.
    [[nodiscard]] float daze() const;
    [[nodiscard]] float blackout() const;
    [[nodiscard]] float bleedSeconds() const { return bleedSeconds_; }
    [[nodiscard]] int cash() const;

    static constexpr uint32_t bit(Injury injury) { return uint32_t{1} << static_cast<unsigned>(injury); }

private:
    bool add(Injury injury, core::HumanoidRagdollPart part, std::vector<InjuryHit>& out);

    uint32_t mask_ = 0;
    std::vector<InjuryHit> injuries_;
    std::vector<core::HumanoidRagdollPart> bleeding_;
    float dazedSeconds_ = 0.0f;
    float knockoutSeconds_ = 0.0f;
    float bleedSeconds_ = 0.0f;
};

} // namespace engine::brokenbones
