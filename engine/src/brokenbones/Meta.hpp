#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "brokenbones/Injuries.hpp"
#include "brokenbones/Shop.hpp"

namespace engine::brokenbones {

struct RunSummary {
    bool finished = false; // false for an abandoned run
    RunEndReason reason = RunEndReason::None;
    MapTheme map = MapTheme::Coast;
    int bones = 0;
    int totalBones = kBaseBoneCount;
    int bestCombo = 0;
    bool skull = false;
    float topSpeed = 0.0f;
    float fall = 0.0f;
    int bombs = 0;
    int rockets = 0;
    int superBoosts = 0;
    int misfires = 0;
    int injuries = 0;
    bool knockedOut = false;
};
// Gear counters are left at zero for the caller to fill in.
[[nodiscard]] RunSummary summarizeRun(const RunTracker& run, RunEndReason reason, MapTheme map);

enum class Achievement {
    FirstFall,
    BoneCollector,
    SkullCracker,
    ComboKing,
    ComboGod,
    MakeASplash,
    SpeedDemon,
    SoundBarrier,
    SkyHigh,
    Stratosphere,
    SuperBooster,
    Oops,
    Demolition,
    HundredBones,
    Shattered,
    FullSkeleton,
    Tourist,
    Climber,
    Summit,
    BigEarner,
    Veteran,
    Reborn,
    LavaDip,
    BoneZone,
    WalkingDisaster,
    LightsOut,
    FrequentFlyer,
};
inline constexpr size_t kAchievementCount = 27;

struct AchievementInfo {
    const char* name;
    const char* description;
    int reward;
};
[[nodiscard]] const AchievementInfo& achievementInfo(Achievement achievement);
[[nodiscard]] bool hasAchievement(const Progress& progress, Achievement achievement);
[[nodiscard]] int achievementsUnlocked(const Progress& progress);
// Unlocks everything newly earned and pays its reward. `run` is null for checks outside a run (after a purchase).
std::vector<Achievement> unlockAchievements(Progress& progress, const RunSummary* run);

[[nodiscard]] const char* boardStatName(BoardStat stat);
[[nodiscard]] float boardValue(const RunRecord& record, BoardStat stat);
[[nodiscard]] std::string formatBoardValue(BoardStat stat, float value);

struct BoardRow {
    std::string name;
    float value = 0.0f;
    bool player = false;
    RunRecord record; // the player's run, when `player`
};
// The player's best runs ranked against the built-in rivals, best first.
[[nodiscard]] std::vector<BoardRow> boardStandings(const Leaderboard& board, BoardStat stat,
                                                   size_t rows = kBoardSize);
// Records the run and returns each board where it now places in the top kBoardSize, with its 1-based rank.
std::vector<std::pair<BoardStat, int>> submitRun(Leaderboard& board, const RunRecord& record);

enum class TutorialEvent { StartedFalling, RunEnded, OpenedShop, Bought, ClosedShop };
// The tip for this step, or nullptr when there is nothing to show right now.
[[nodiscard]] const char* tutorialTip(int step, bool falling);
[[nodiscard]] int advanceTutorial(int step, TutorialEvent event);

} // namespace engine::brokenbones
