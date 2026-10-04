#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "brokenbones/RunRules.hpp"

namespace engine::brokenbones {

enum class ShopItem {
    Floats,
    BigBalloons,
    Bomb,
    Rocket,
    SpringShoes,
    BlastPower,
    BrittleBones,
    CashBonus,
    RocketThrust,
    RocketFuel,
    MoreBones,
    Altitude,
    GlacierMap,
    CanyonMap,
    VolcanoMap,
    GoldenBones,
    GlassSkeleton,
    NestEgg,
    MegaBlast,
    SkyHigh,
    HeadStart,
};
inline constexpr size_t kShopItemCount = 21;

struct ShopPage {
    const char* name;
    std::array<ShopItem, 9> items;
    size_t count;
};
inline constexpr size_t kShopPageCount = 4;
[[nodiscard]] const ShopPage& shopPage(size_t page);
inline constexpr int kNoPrerequisite = -1;

struct ShopItemInfo {
    const char* name;
    const char* description;
    int basePrice;
    int maxOwned;
    bool consumable;
    int unlockLevel;
    int prerequisite; // ShopItem index or kNoPrerequisite
    int rebirthsRequired = 0;
};

[[nodiscard]] const ShopItemInfo& shopItemInfo(ShopItem item);

enum class ContractKind { BreakBones, BreakHead, FallDistance, HitSpeed, Splashdown, Combo, BonesAndSplash };
inline constexpr int kContractKindCount = 7;
inline constexpr size_t kActiveContracts = 3;

struct Contract {
    ContractKind kind = ContractKind::BreakBones;
    int target = 1;
    int reward = 0;
};

struct PersonalBests {
    int bones = 0;
    int combo = 0;
    int payout = 0;
    float fall = 0.0f;
    float speed = 0.0f;
    float hit = 0.0f;
};

inline constexpr int kTutorialDone = 100;

struct GameSettings {
    float volume = 0.8f;
    float music = 0.6f;
    float sensitivity = 1.0f; // multiplies the controller's default mouse sensitivity
    bool slowMotion = true;
    bool cameraShake = true;
    bool tips = true;
};

struct LifetimeStats {
    long long cashEarned = 0;
    double playSeconds = 0.0;
    double metresFallen = 0.0;
    int bombs = 0;
    int rockets = 0;
    int superBoosts = 0;
    int splashdowns = 0;
    int injuries = 0;
};

struct RunRecord {
    int run = 0; // the run's number, unique per save
    int cash = 0;
    int bones = 0;
    float fall = 0.0f;
    float speed = 0.0f;
    int injuries = 0;
    int level = 1;
    MapTheme map = MapTheme::Coast;
};

enum class BoardStat { Cash, Bones, Fall, Speed };
inline constexpr size_t kBoardStatCount = 4;
inline constexpr size_t kBoardSize = 10;

// The player's best runs on each board, best first.
struct Leaderboard {
    std::array<std::vector<RunRecord>, kBoardStatCount> best;
};

struct Progress {
    int cash = 0;
    int level = 1;
    int bestLevel = 1;
    int totalBones = 0;
    int runs = 0;
    std::array<int, kShopItemCount> owned{};
    std::vector<Contract> contracts;
    PersonalBests bests;
    MapTheme map = MapTheme::Coast;
    int rebirths = 0;
    uint64_t achievements = 0; // bit i = Achievement i unlocked
    int tutorialStep = 0;
    GameSettings settings;
    LifetimeStats stats;
    uint32_t injuriesSeen = 0; // bit i = Injury i suffered at least once
    Leaderboard leaderboard;

    [[nodiscard]] int count(ShopItem item) const { return owned[static_cast<size_t>(item)]; }
    [[nodiscard]] bool has(ShopItem item) const { return count(item) > 0; }
};

enum class BuyResult { Bought, NotEnoughCash, MaxedOut, NeedsPrerequisite, Locked, NeedsRebirth };

[[nodiscard]] int shopPrice(const Progress& progress, ShopItem item);
BuyResult buyItem(Progress& progress, ShopItem item);
[[nodiscard]] std::string buyResultText(BuyResult result, ShopItem item);

[[nodiscard]] float floatsHeliumCapacity(const Progress& progress);
[[nodiscard]] float breakSpeedMultiplier(const Progress& progress);
[[nodiscard]] float blastPowerMultiplier(const Progress& progress);
[[nodiscard]] float cashBonusMultiplier(const Progress& progress);
// Level needed for the next rebirth: 20, then 40, 60, ...
[[nodiscard]] int rebirthLevelRequired(const Progress& progress);
[[nodiscard]] float rebirthMultiplier(const Progress& progress);
[[nodiscard]] bool canRebirth(const Progress& progress);
// Starts over at level 1 (later with HEAD START) with no cash or regular upgrades, keeping maps,
// rebirth upgrades, achievements, stats and settings.
bool rebirth(Progress& progress);
[[nodiscard]] bool isRebirthUpgrade(ShopItem item);
[[nodiscard]] float goldenBonesMultiplier(const Progress& progress);
[[nodiscard]] float rocketThrustMultiplier(const Progress& progress);
[[nodiscard]] float rocketBurnSeconds(const Progress& progress);
inline constexpr std::array<int, 8> kBoneTiers = {kBaseBoneCount, 25, 50, 100, 206, 300, 400, kMaxBoneCount};
[[nodiscard]] int skeletonBoneCount(const Progress& progress);
[[nodiscard]] float altitudeMultiplier(const Progress& progress);
[[nodiscard]] bool ownsMap(const Progress& progress, MapTheme theme);
// The next owned map after the current one, wrapping round to the coast.
[[nodiscard]] MapTheme nextOwnedMap(const Progress& progress);
// Cash for one break: a part's value spread over its bones, growing (sub-linearly) with skeleton size.
[[nodiscard]] int boneBreakCash(const BoneBreak& bone, int totalBones);
// Extra launch velocity for a flop, zero without spring shoes.
[[nodiscard]] glm::vec3 springLeapVelocity(const Progress& progress, glm::vec3 aim);

// Refilled at the start of every run and never topped up mid-run.
class FuelTank {
public:
    void refill(float capacity) { capacity_ = remaining_ = std::max(capacity, 0.0f); }
    // Returns the seconds of burn actually available out of the requested dt.
    float burn(float dt);
    [[nodiscard]] float remaining() const { return remaining_; }
    [[nodiscard]] float capacity() const { return capacity_; }
    [[nodiscard]] float fraction() const { return capacity_ > 0.0f ? remaining_ / capacity_ : 0.0f; }
    [[nodiscard]] bool empty() const { return remaining_ <= 0.0f; }

private:
    float capacity_ = 0.0f;
    float remaining_ = 0.0f;
};

struct RunPayout {
    bool abandoned = false;
    int bones = 0;
    int combo = 0;
    int hits = 0;
    int distance = 0;
    int bigHit = 0;
    int splash = 0;
    float levelMultiplier = 1.0f;
    float cashMultiplier = 1.0f;
    int contracts = 0;
    int injuries = 0;
    int total = 0;
};

[[nodiscard]] int boneCashValue(core::HumanoidRagdollPart part);
// A run ended by RunEndReason::None was abandoned and only pays half its bone cash.
[[nodiscard]] RunPayout computeRunPayout(const RunTracker& run, int level, RunEndReason reason,
                                         float cashMultiplier = 1.0f, int injuryCash = 0);

[[nodiscard]] Contract makeContract(std::mt19937& rng, int level, const std::vector<Contract>& avoid,
                                    int totalBones = kBaseBoneCount);
void refillContracts(Progress& progress, std::mt19937& rng);
[[nodiscard]] bool contractMet(const Contract& contract, const RunTracker& run, RunEndReason reason);
[[nodiscard]] std::string contractText(const Contract& contract);

// Records new bests and returns the label of each one beaten.
std::vector<std::string> updatePersonalBests(PersonalBests& bests, const RunTracker& run, int payout);

bool saveProgress(const Progress& progress, const std::string& path, std::string& outError);
// `outRefund` is cash returned for items that no longer exist (the old jetpack and fuel tanks).
bool loadProgress(Progress& progress, const std::string& path, std::string& outError, int* outRefund = nullptr);

} // namespace engine::brokenbones
