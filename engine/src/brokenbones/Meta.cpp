#include "brokenbones/Meta.hpp"

#include <algorithm>
#include <array>
#include <bit>

namespace engine::brokenbones {

namespace {

constexpr std::array<AchievementInfo, kAchievementCount> kAchievements = {{
    {"FIRST FALL", "Finish a run", 50},
    {"BONE COLLECTOR", "Break 10 bones in one run", 150},
    {"SKULL CRACKER", "Crack your skull", 100},
    {"COMBO KING", "Chain a x5 combo", 200},
    {"COMBO GOD", "Chain a x10 combo", 600},
    {"MAKE A SPLASH", "Land in the water", 100},
    {"SPEED DEMON", "Hit 100 m/s", 300},
    {"SOUND BARRIER", "Hit 250 m/s", 1500},
    {"SKY HIGH", "Fall 1000 m in one run", 500},
    {"STRATOSPHERE", "Fall 2000 m in one run", 2000},
    {"SUPER BOOSTER", "Land a rocket super boost", 250},
    {"OOPS", "Misfire a rocket", 100},
    {"DEMOLITION", "Set off 5 bombs in one run", 400},
    {"HUNDRED BONES", "Break 100 bones in one run", 1000},
    {"SHATTERED", "Break every bone in your body", 800},
    {"FULL SKELETON", "Grow all 500 bones", 5000},
    {"TOURIST", "Unlock every map", 3000},
    {"CLIMBER", "Reach level 10", 1000},
    {"SUMMIT", "Reach level 20", 5000},
    {"BIG EARNER", "Earn $100,000 in total", 5000},
    {"VETERAN", "Finish 100 runs", 2000},
    {"REBORN", "Rebirth once", 1000},
    {"LAVA DIP", "Land in the volcano's lava", 500},
    {"BONE ZONE", "Break 10,000 bones in total", 3000},
    {"WALKING DISASTER", "Suffer 5 injuries in one run", 1000},
    {"LIGHTS OUT", "Get knocked out", 300},
    {"FREQUENT FLYER", "Suffer every kind of injury", 2500},
}};

struct Rival {
    const char* name;
    std::array<float, kBoardStatCount> scores; // cash, bones, fall, speed
};
constexpr std::array<Rival, 8> kRivals = {{
    {"THE HUMAN PINATA", {40000.0f, 420.0f, 2600.0f, 290.0f}},
    {"CRASH TEST CARL", {15000.0f, 210.0f, 1600.0f, 190.0f}},
    {"RAGDOLL RITA", {6000.0f, 110.0f, 950.0f, 130.0f}},
    {"BONEHEAD BOB", {2500.0f, 55.0f, 520.0f, 90.0f}},
    {"WOBBLY WENDY", {1200.0f, 28.0f, 300.0f, 64.0f}},
    {"CLUMSY KEV", {600.0f, 14.0f, 160.0f, 45.0f}},
    {"STUBBED TOE STEVE", {250.0f, 8.0f, 85.0f, 28.0f}},
    {"PAPERCUT PAUL", {90.0f, 3.0f, 40.0f, 15.0f}},
}};

bool earned(Achievement achievement, const Progress& progress, const RunSummary* run) {
    bool finished = run != nullptr && run->finished;
    switch (achievement) {
        case Achievement::FirstFall: return finished;
        case Achievement::BoneCollector: return finished && run->bones >= 10;
        case Achievement::SkullCracker: return finished && run->skull;
        case Achievement::ComboKing: return finished && run->bestCombo >= 5;
        case Achievement::ComboGod: return finished && run->bestCombo >= 10;
        case Achievement::MakeASplash: return finished && run->reason == RunEndReason::Splashdown;
        case Achievement::SpeedDemon: return finished && run->topSpeed >= 100.0f;
        case Achievement::SoundBarrier: return finished && run->topSpeed >= 250.0f;
        case Achievement::SkyHigh: return finished && run->fall >= 1000.0f;
        case Achievement::Stratosphere: return finished && run->fall >= 2000.0f;
        case Achievement::SuperBooster: return finished && run->superBoosts > 0;
        case Achievement::Oops: return finished && run->misfires > 0;
        case Achievement::Demolition: return finished && run->bombs >= 5;
        case Achievement::HundredBones: return finished && run->bones >= 100;
        case Achievement::Shattered: return finished && run->bones >= run->totalBones;
        case Achievement::FullSkeleton: return skeletonBoneCount(progress) >= kMaxBoneCount;
        case Achievement::Tourist:
            return ownsMap(progress, MapTheme::Glacier) && ownsMap(progress, MapTheme::Canyon) &&
                   ownsMap(progress, MapTheme::Volcano);
        case Achievement::Climber: return progress.bestLevel >= 10;
        case Achievement::Summit: return progress.bestLevel >= 20;
        case Achievement::BigEarner: return progress.stats.cashEarned >= 100000;
        case Achievement::Veteran: return progress.runs >= 100;
        case Achievement::Reborn: return progress.rebirths >= 1;
        case Achievement::LavaDip:
            return finished && run->reason == RunEndReason::Splashdown && run->map == MapTheme::Volcano;
        case Achievement::BoneZone: return progress.totalBones >= 10000;
        case Achievement::WalkingDisaster: return finished && run->injuries >= 5;
        case Achievement::LightsOut: return finished && run->knockedOut;
        case Achievement::FrequentFlyer:
            return std::popcount(progress.injuriesSeen & ((uint32_t{1} << kInjuryCount) - 1)) ==
                   static_cast<int>(kInjuryCount);
    }
    return false;
}

constexpr uint64_t bit(Achievement achievement) { return uint64_t{1} << static_cast<unsigned>(achievement); }

} // namespace

RunSummary summarizeRun(const RunTracker& run, RunEndReason reason, MapTheme map) {
    RunSummary summary;
    summary.finished = reason != RunEndReason::None;
    summary.reason = reason;
    summary.map = map;
    summary.bones = run.bonesBroken();
    summary.totalBones = run.totalBones();
    summary.bestCombo = run.bestCombo();
    summary.skull = run.isBroken(core::HumanoidRagdollPart::Head);
    summary.topSpeed = run.topSpeed();
    summary.fall = run.distanceFallen();
    return summary;
}

const AchievementInfo& achievementInfo(Achievement achievement) {
    return kAchievements[static_cast<size_t>(achievement)];
}

bool hasAchievement(const Progress& progress, Achievement achievement) {
    return (progress.achievements & bit(achievement)) != 0;
}

int achievementsUnlocked(const Progress& progress) {
    uint64_t all = (uint64_t{1} << kAchievementCount) - 1;
    return std::popcount(progress.achievements & all);
}

std::vector<Achievement> unlockAchievements(Progress& progress, const RunSummary* run) {
    std::vector<Achievement> unlocked;
    for (size_t i = 0; i < kAchievementCount; ++i) {
        auto achievement = static_cast<Achievement>(i);
        if (hasAchievement(progress, achievement) || !earned(achievement, progress, run)) continue;
        progress.achievements |= bit(achievement);
        progress.cash += achievementInfo(achievement).reward;
        unlocked.push_back(achievement);
    }
    return unlocked;
}

const char* boardStatName(BoardStat stat) {
    switch (stat) {
        case BoardStat::Cash: return "BIGGEST PAYDAY";
        case BoardStat::Bones: return "MOST BONES";
        case BoardStat::Fall: return "LONGEST FALL";
        case BoardStat::Speed: return "TOP SPEED";
    }
    return "";
}

float boardValue(const RunRecord& record, BoardStat stat) {
    switch (stat) {
        case BoardStat::Cash: return static_cast<float>(record.cash);
        case BoardStat::Bones: return static_cast<float>(record.bones);
        case BoardStat::Fall: return record.fall;
        case BoardStat::Speed: return record.speed;
    }
    return 0.0f;
}

std::string formatBoardValue(BoardStat stat, float value) {
    int whole = static_cast<int>(value);
    switch (stat) {
        case BoardStat::Cash: return "$" + std::to_string(whole);
        case BoardStat::Bones: return std::to_string(whole) + " bones";
        case BoardStat::Fall: return std::to_string(whole) + " m";
        case BoardStat::Speed: return std::to_string(whole) + " m/s";
    }
    return std::to_string(whole);
}

std::vector<BoardRow> boardStandings(const Leaderboard& board, BoardStat stat, size_t rows) {
    std::vector<BoardRow> standings;
    for (const Rival& rival : kRivals) {
        standings.push_back({rival.name, rival.scores[static_cast<size_t>(stat)], false, {}});
    }
    for (const RunRecord& record : board.best[static_cast<size_t>(stat)]) {
        standings.push_back({"YOU  (run " + std::to_string(record.run) + ")", boardValue(record, stat), true, record});
    }
    // Ties go to the player.
    std::stable_sort(standings.begin(), standings.end(), [](const BoardRow& a, const BoardRow& b) {
        return a.value > b.value || (a.value == b.value && a.player && !b.player);
    });
    if (standings.size() > rows) standings.resize(rows);
    return standings;
}

std::vector<std::pair<BoardStat, int>> submitRun(Leaderboard& board, const RunRecord& record) {
    std::vector<std::pair<BoardStat, int>> placed;
    for (size_t b = 0; b < kBoardStatCount; ++b) {
        auto stat = static_cast<BoardStat>(b);
        if (boardValue(record, stat) <= 0.0f) continue;
        std::vector<RunRecord>& best = board.best[b];
        best.push_back(record);
        std::stable_sort(best.begin(), best.end(), [stat](const RunRecord& x, const RunRecord& y) {
            return boardValue(x, stat) > boardValue(y, stat);
        });
        if (best.size() > kBoardSize) best.resize(kBoardSize);
        std::vector<BoardRow> standings = boardStandings(board, stat);
        for (size_t i = 0; i < standings.size(); ++i) {
            if (standings[i].player && standings[i].record.run == record.run) {
                placed.emplace_back(stat, static_cast<int>(i) + 1);
                break;
            }
        }
    }
    return placed;
}

const char* tutorialTip(int step, bool falling) {
    switch (step) {
        case 0: return falling ? nullptr : "Walk off the end of the diving board  [WASD]  -  or press [F] to flop";
        case 1: return falling ? "Smash into things! Hard hits snap bones and pay cash" : nullptr;
        case 2: return falling ? nullptr : "Nice! Spend your cash in the shop  [B]";
        case 3: return falling ? nullptr : "Buy with [1-9], flip pages with [LEFT/RIGHT], close with [B]";
        case 4: return falling ? nullptr : "Break enough bones in one run to unlock a taller cliff. [ESC] for the menu";
        default: return nullptr;
    }
}

int advanceTutorial(int step, TutorialEvent event) {
    switch (step) {
        case 0: return event == TutorialEvent::StartedFalling ? 1 : step;
        case 1: return event == TutorialEvent::RunEnded ? 2 : step;
        case 2: return event == TutorialEvent::OpenedShop ? 3 : step;
        case 3: return event == TutorialEvent::Bought || event == TutorialEvent::ClosedShop ? 4 : step;
        case 4: return event == TutorialEvent::StartedFalling ? kTutorialDone : step;
        default: return step;
    }
}

} // namespace engine::brokenbones
