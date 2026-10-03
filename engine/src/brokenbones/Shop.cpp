#include "brokenbones/Shop.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>

#include <nlohmann/json.hpp>

namespace engine::brokenbones {

using core::HumanoidRagdollPart;

namespace {

constexpr std::array<ShopItemInfo, kShopItemCount> kItems = {{
    {"FLOATS", "Hold SHIFT to float and glide", 400, 1, false, 1, kNoPrerequisite},
    {"BIG BALLOONS", "+2.5 s of helium per run", 350, 3, false, 2, static_cast<int>(ShopItem::Floats)},
    {"BOMB", "Press X to blast yourself off the cliff", 90, 10, true, 1, kNoPrerequisite},
    {"ROCKET", "Press Q: nosedive head-first, explode on impact", 150, 5, true, 2, kNoPrerequisite},
    {"SPRING SHOES", "[F] becomes a huge leap off the edge", 300, 2, false, 1, kNoPrerequisite},
    {"BLAST POWER", "+25% bomb and rocket blast", 500, 3, false, 3, kNoPrerequisite},
    {"BRITTLE BONES", "Bones break 10% easier", 600, 3, false, 3, kNoPrerequisite},
    {"CASH BONUS", "+15% cash from every run", 800, 3, false, 4, kNoPrerequisite},
    {"ROCKET THRUST", "Rocket pushes 30% harder", 350, 3, false, 2, kNoPrerequisite},
    {"ROCKET FUEL", "+0.6 s of rocket burn", 400, 3, false, 3, kNoPrerequisite},
    {"MORE BONES", "Grow a bigger skeleton: more to break, more cash", 250, 7, false, 1, kNoPrerequisite},
    {"ALTITUDE", "Every cliff is 25% taller", 600, 4, false, 2, kNoPrerequisite},
    {"GLACIER MAP", "Icy overhangs, pays x1.25  [M] switch map", 1500, 1, false, 3, kNoPrerequisite},
    {"CANYON MAP", "Packed with beams and rocks, pays x1.5", 3000, 1, false, 5, kNoPrerequisite},
    {"VOLCANO MAP", "Basalt and a lava pool, pays x2", 6000, 1, false, 7, kNoPrerequisite},
}};

constexpr std::array<ShopPage, kShopPageCount> kPages = {{
    {"GEAR",
     {ShopItem::Floats, ShopItem::BigBalloons, ShopItem::Bomb, ShopItem::Rocket, ShopItem::RocketThrust,
      ShopItem::RocketFuel, ShopItem::SpringShoes},
     7},
    {"BODY & CASH", {ShopItem::MoreBones, ShopItem::BrittleBones, ShopItem::BlastPower, ShopItem::CashBonus}, 4},
    {"MAPS", {ShopItem::Altitude, ShopItem::GlacierMap, ShopItem::CanyonMap, ShopItem::VolcanoMap}, 4},
}};

constexpr const char* kItemKeys[kShopItemCount] = {"floats",      "bigBalloons", "bomb",         "rocket",
                                                   "springShoes", "blastPower",  "brittleBones", "cashBonus",
                                                   "rocketThrust", "rocketFuel",  "moreBones",    "altitude",
                                                   "glacierMap",   "canyonMap",   "volcanoMap"};

glm::vec3 flatten(glm::vec3 aim) {
    glm::vec3 flat(aim.x, 0.0f, aim.z);
    float length = glm::length(flat);
    return length > 1e-4f ? flat / length : glm::vec3(0.0f, 0.0f, 1.0f);
}

int roundTo(float value, int step) { return std::max(step, static_cast<int>(std::round(value / step)) * step); }

} // namespace

const ShopItemInfo& shopItemInfo(ShopItem item) { return kItems[static_cast<size_t>(item)]; }

const ShopPage& shopPage(size_t page) { return kPages[std::min(page, kShopPageCount - 1)]; }

int shopPrice(const Progress& progress, ShopItem item) {
    const ShopItemInfo& info = shopItemInfo(item);
    if (info.consumable) return info.basePrice;
    if (item == ShopItem::MoreBones) return info.basePrice << std::min(progress.count(item), 16);
    return info.basePrice * (1 + progress.count(item));
}

BuyResult buyItem(Progress& progress, ShopItem item) {
    const ShopItemInfo& info = shopItemInfo(item);
    if (progress.bestLevel < info.unlockLevel) return BuyResult::Locked;
    if (progress.count(item) >= info.maxOwned) return BuyResult::MaxedOut;
    if (info.prerequisite != kNoPrerequisite && !progress.has(static_cast<ShopItem>(info.prerequisite))) {
        return BuyResult::NeedsPrerequisite;
    }
    int price = shopPrice(progress, item);
    if (progress.cash < price) return BuyResult::NotEnoughCash;
    progress.cash -= price;
    ++progress.owned[static_cast<size_t>(item)];
    return BuyResult::Bought;
}

std::string buyResultText(BuyResult result, ShopItem item) {
    const ShopItemInfo& info = shopItemInfo(item);
    switch (result) {
        case BuyResult::Bought: return "BOUGHT";
        case BuyResult::NotEnoughCash: return "NOT ENOUGH CASH";
        case BuyResult::MaxedOut: return "MAXED OUT";
        case BuyResult::NeedsPrerequisite:
            return std::string("BUY ") + kItems[static_cast<size_t>(info.prerequisite)].name + " FIRST";
        case BuyResult::Locked: return "UNLOCKS AT LEVEL " + std::to_string(info.unlockLevel);
    }
    return "";
}

float floatsHeliumCapacity(const Progress& progress) {
    if (!progress.has(ShopItem::Floats)) return 0.0f;
    return 5.0f + 2.5f * static_cast<float>(progress.count(ShopItem::BigBalloons));
}

float breakSpeedMultiplier(const Progress& progress) {
    return 1.0f - 0.1f * static_cast<float>(progress.count(ShopItem::BrittleBones));
}

float blastPowerMultiplier(const Progress& progress) {
    return 1.0f + 0.25f * static_cast<float>(progress.count(ShopItem::BlastPower));
}

float cashBonusMultiplier(const Progress& progress) {
    return 1.0f + 0.15f * static_cast<float>(progress.count(ShopItem::CashBonus));
}

float rocketThrustMultiplier(const Progress& progress) {
    return 1.0f + 0.3f * static_cast<float>(progress.count(ShopItem::RocketThrust));
}

float rebirthMultiplier(const Progress& progress) { return 1.0f + 0.5f * static_cast<float>(std::max(progress.rebirths, 0)); }

bool canRebirth(const Progress& progress) { return progress.level >= kRebirthLevel; }

bool rebirth(Progress& progress) {
    if (!canRebirth(progress)) return false;
    Progress next;
    next.rebirths = progress.rebirths + 1;
    next.totalBones = progress.totalBones;
    next.runs = progress.runs;
    next.bests = progress.bests;
    next.achievements = progress.achievements;
    next.tutorialStep = progress.tutorialStep;
    next.settings = progress.settings;
    next.stats = progress.stats;
    next.injuriesSeen = progress.injuriesSeen;
    next.leaderboard = progress.leaderboard;
    for (ShopItem keep : {ShopItem::GlacierMap, ShopItem::CanyonMap, ShopItem::VolcanoMap}) {
        next.owned[static_cast<size_t>(keep)] = progress.count(keep);
    }
    next.map = ownsMap(next, progress.map) ? progress.map : MapTheme::Coast;
    progress = next;
    return true;
}

int skeletonBoneCount(const Progress& progress) {
    return kBoneTiers[static_cast<size_t>(std::clamp(progress.count(ShopItem::MoreBones), 0, 7))];
}

float altitudeMultiplier(const Progress& progress) {
    return 1.0f + 0.25f * static_cast<float>(progress.count(ShopItem::Altitude));
}

bool ownsMap(const Progress& progress, MapTheme theme) {
    switch (theme) {
        case MapTheme::Coast: return true;
        case MapTheme::Glacier: return progress.has(ShopItem::GlacierMap);
        case MapTheme::Canyon: return progress.has(ShopItem::CanyonMap);
        case MapTheme::Volcano: return progress.has(ShopItem::VolcanoMap);
    }
    return false;
}

MapTheme nextOwnedMap(const Progress& progress) {
    size_t current = static_cast<size_t>(progress.map);
    for (size_t step = 1; step <= kMapThemeCount; ++step) {
        auto candidate = static_cast<MapTheme>((current + step) % kMapThemeCount);
        if (ownsMap(progress, candidate)) return candidate;
    }
    return MapTheme::Coast;
}

float rocketBurnSeconds(const Progress& progress) {
    return 1.8f + 0.6f * static_cast<float>(progress.count(ShopItem::RocketFuel));
}

glm::vec3 springLeapVelocity(const Progress& progress, glm::vec3 aim) {
    int shoes = progress.count(ShopItem::SpringShoes);
    if (shoes == 0) return glm::vec3(0.0f);
    float s = static_cast<float>(shoes);
    return flatten(aim) * (9.0f + 5.0f * s) + glm::vec3(0.0f, 7.0f + 2.0f * s, 0.0f);
}

float FuelTank::burn(float dt) {
    float used = std::clamp(dt, 0.0f, remaining_);
    remaining_ -= used;
    return used;
}

int boneCashValue(HumanoidRagdollPart part) {
    switch (part) {
        case HumanoidRagdollPart::Head: return 30;
        case HumanoidRagdollPart::Abdomen: return 25;
        case HumanoidRagdollPart::Pelvis:
        case HumanoidRagdollPart::Chest: return 20;
        case HumanoidRagdollPart::UpperLegL:
        case HumanoidRagdollPart::UpperLegR: return 15;
        default: return 10;
    }
}

int boneBreakCash(const BoneBreak& bone, int totalBones) {
    float growth = std::sqrt(static_cast<float>(std::max(totalBones, kBaseBoneCount)) / kBaseBoneCount);
    float share = static_cast<float>(bone.count) / static_cast<float>(std::max(bone.partBones, 1));
    int cash = static_cast<int>(std::round(static_cast<float>(boneCashValue(bone.part)) * share * growth));
    return std::max(cash, bone.count);
}

RunPayout computeRunPayout(const RunTracker& run, int level, RunEndReason reason, float cashMultiplier,
                           int injuryCash) {
    RunPayout payout;
    payout.abandoned = reason == RunEndReason::None;
    for (const BoneBreak& bone : run.breaks()) {
        int value = boneBreakCash(bone, run.totalBones());
        payout.bones += value;
        payout.combo += static_cast<int>(std::round(static_cast<float>(value) * 0.25f * static_cast<float>(bone.combo - 1)));
    }
    payout.levelMultiplier = 1.0f + 0.15f * static_cast<float>(std::max(level, 1) - 1);
    if (payout.abandoned) {
        payout.bones /= 2;
        payout.combo = 0;
        payout.total = static_cast<int>(std::round(static_cast<float>(payout.bones) * payout.levelMultiplier));
        return payout;
    }
    payout.hits = run.hits();
    payout.distance = static_cast<int>(std::max(0.0f, run.distanceFallen()) * 0.2f);
    payout.bigHit = static_cast<int>(std::max(0.0f, run.hardestImpact() - 25.0f));
    payout.splash = reason == RunEndReason::Splashdown ? 50 : 0;
    payout.cashMultiplier = cashMultiplier;
    payout.injuries = std::max(injuryCash, 0);
    int subtotal = payout.bones + payout.combo + payout.hits + payout.distance + payout.bigHit + payout.splash +
                   payout.injuries;
    payout.total =
        static_cast<int>(std::round(static_cast<float>(subtotal) * payout.levelMultiplier * payout.cashMultiplier));
    return payout;
}

Contract makeContract(std::mt19937& rng, int level, const std::vector<Contract>& avoid, int totalBones) {
    level = std::max(level, 1);
    std::uniform_int_distribution<int> kindPick(0, kContractKindCount - 1);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    Contract contract;
    for (int attempt = 0; attempt < 20; ++attempt) {
        contract.kind = static_cast<ContractKind>(kindPick(rng));
        bool duplicate = std::any_of(avoid.begin(), avoid.end(),
                                     [&](const Contract& other) { return other.kind == contract.kind; });
        if (!duplicate) break;
    }
    float r = unit(rng);
    int maxBones = std::max(totalBones, kBaseBoneCount) - 2;
    float boneScale = std::sqrt(static_cast<float>(std::max(totalBones, kBaseBoneCount)) / kBaseBoneCount);
    switch (contract.kind) {
        case ContractKind::BreakBones: {
            int base = 3 + level / 2 + static_cast<int>(r * 3.0f);
            contract.target = std::min(maxBones, static_cast<int>(std::round(static_cast<float>(base) * boneScale)));
            contract.reward = 35 * base;
            break;
        }
        case ContractKind::BreakHead:
            contract.target = 1;
            contract.reward = 120;
            break;
        case ContractKind::FallDistance:
            contract.target = roundTo(cliffHeightForLevel(level) * (0.6f + 0.3f * r), 10);
            contract.reward = contract.target;
            break;
        case ContractKind::HitSpeed:
            contract.target = 20 + 3 * std::min(level, 10) + static_cast<int>(r * 8.0f);
            contract.reward = 6 * contract.target;
            break;
        case ContractKind::Splashdown:
            contract.target = 1;
            contract.reward = 150;
            break;
        case ContractKind::Combo:
            contract.target = 2 + std::min(4, level / 3) + static_cast<int>(r * 2.0f);
            contract.reward = 90 * contract.target;
            break;
        case ContractKind::BonesAndSplash: {
            int base = 2 + level / 3 + static_cast<int>(r * 2.0f);
            contract.target = std::min(maxBones, static_cast<int>(std::round(static_cast<float>(base) * boneScale)));
            contract.reward = 100 + 60 * base;
            break;
        }
    }
    contract.reward = roundTo(static_cast<float>(contract.reward) * (1.0f + 0.1f * static_cast<float>(level - 1)), 10);
    return contract;
}

void refillContracts(Progress& progress, std::mt19937& rng) {
    while (progress.contracts.size() < kActiveContracts) {
        progress.contracts.push_back(makeContract(rng, progress.level, progress.contracts, skeletonBoneCount(progress)));
    }
}

bool contractMet(const Contract& contract, const RunTracker& run, RunEndReason reason) {
    if (reason == RunEndReason::None) return false;
    bool splash = reason == RunEndReason::Splashdown;
    switch (contract.kind) {
        case ContractKind::BreakBones: return run.bonesBroken() >= contract.target;
        case ContractKind::BreakHead: return run.isBroken(HumanoidRagdollPart::Head);
        case ContractKind::FallDistance: return run.distanceFallen() >= static_cast<float>(contract.target);
        case ContractKind::HitSpeed: return run.hardestImpact() >= static_cast<float>(contract.target);
        case ContractKind::Splashdown: return splash;
        case ContractKind::Combo: return run.bestCombo() >= contract.target;
        case ContractKind::BonesAndSplash: return splash && run.bonesBroken() >= contract.target;
    }
    return false;
}

std::string contractText(const Contract& contract) {
    char text[96];
    switch (contract.kind) {
        case ContractKind::BreakBones: std::snprintf(text, sizeof(text), "Break %d bones in one run", contract.target); break;
        case ContractKind::BreakHead: std::snprintf(text, sizeof(text), "Crack your skull"); break;
        case ContractKind::FallDistance: std::snprintf(text, sizeof(text), "Fall %d m", contract.target); break;
        case ContractKind::HitSpeed: std::snprintf(text, sizeof(text), "Hit something at %d m/s", contract.target); break;
        case ContractKind::Splashdown: std::snprintf(text, sizeof(text), "Land in the lagoon"); break;
        case ContractKind::Combo: std::snprintf(text, sizeof(text), "Chain a x%d break combo", contract.target); break;
        case ContractKind::BonesAndSplash:
            std::snprintf(text, sizeof(text), "Break %d bones, then land in the lagoon", contract.target);
            break;
    }
    return text;
}

std::vector<std::string> updatePersonalBests(PersonalBests& bests, const RunTracker& run, int payout) {
    std::vector<std::string> beaten;
    auto check = [&](auto& best, auto value, const char* label) {
        if (value > best) {
            if (best > 0) beaten.emplace_back(label);
            best = value;
        }
    };
    check(bests.bones, run.bonesBroken(), "MOST BONES");
    check(bests.combo, run.bestCombo(), "BIGGEST COMBO");
    check(bests.payout, payout, "BIGGEST PAYDAY");
    check(bests.fall, run.distanceFallen(), "LONGEST FALL");
    check(bests.speed, run.topSpeed(), "TOP SPEED");
    check(bests.hit, run.hardestImpact(), "HARDEST HIT");
    return beaten;
}

bool saveProgress(const Progress& progress, const std::string& path, std::string& outError) {
    nlohmann::json json;
    json["version"] = 2;
    json["cash"] = progress.cash;
    json["level"] = progress.level;
    json["bestLevel"] = progress.bestLevel;
    json["totalBones"] = progress.totalBones;
    json["runs"] = progress.runs;
    json["map"] = static_cast<int>(progress.map);
    json["rebirths"] = progress.rebirths;
    json["achievements"] = progress.achievements;
    json["tutorialStep"] = progress.tutorialStep;
    const GameSettings& settings = progress.settings;
    json["injuriesSeen"] = progress.injuriesSeen;
    json["settings"] = {{"volume", settings.volume},       {"music", settings.music}, {"sensitivity", settings.sensitivity},
                        {"slowMotion", settings.slowMotion}, {"cameraShake", settings.cameraShake},
                        {"tips", settings.tips}};
    const LifetimeStats& stats = progress.stats;
    json["stats"] = {{"cashEarned", stats.cashEarned}, {"playSeconds", stats.playSeconds},
                     {"metresFallen", stats.metresFallen}, {"bombs", stats.bombs},
                     {"rockets", stats.rockets}, {"superBoosts", stats.superBoosts},
                     {"splashdowns", stats.splashdowns}, {"injuries", stats.injuries}};
    static constexpr const char* kBoardKeys[kBoardStatCount] = {"cash", "bones", "fall", "speed"};
    for (size_t b = 0; b < kBoardStatCount; ++b) {
        nlohmann::json& board = json["leaderboard"][kBoardKeys[b]];
        board = nlohmann::json::array();
        for (const RunRecord& r : progress.leaderboard.best[b]) {
            board.push_back({{"run", r.run},           {"cash", r.cash},   {"bones", r.bones},
                             {"fall", r.fall},         {"speed", r.speed}, {"injuries", r.injuries},
                             {"level", r.level},       {"map", static_cast<int>(r.map)}});
        }
    }
    for (size_t i = 0; i < kShopItemCount; ++i) json["owned"][kItemKeys[i]] = progress.owned[i];
    json["contracts"] = nlohmann::json::array();
    for (const Contract& contract : progress.contracts) {
        json["contracts"].push_back(
            {{"kind", static_cast<int>(contract.kind)}, {"target", contract.target}, {"reward", contract.reward}});
    }
    json["bests"] = {{"bones", progress.bests.bones}, {"combo", progress.bests.combo},
                     {"payout", progress.bests.payout}, {"fall", progress.bests.fall},
                     {"speed", progress.bests.speed}, {"hit", progress.bests.hit}};

    std::string tmpPath = path + ".tmp";
    {
        std::ofstream out(tmpPath, std::ios::trunc);
        if (!out) {
            outError = "cannot write " + tmpPath;
            return false;
        }
        out << json.dump(2);
        if (!out) {
            outError = "write failed for " + tmpPath;
            return false;
        }
    }
    if (std::rename(tmpPath.c_str(), path.c_str()) != 0) {
        outError = "cannot replace " + path;
        return false;
    }
    return true;
}

bool loadProgress(Progress& progress, const std::string& path, std::string& outError, int* outRefund) {
    if (outRefund != nullptr) *outRefund = 0;
    std::ifstream in(path);
    if (!in) {
        outError = "no save at " + path;
        return false;
    }
    nlohmann::json json = nlohmann::json::parse(in, nullptr, false);
    if (json.is_discarded() || !json.is_object()) {
        outError = "malformed save " + path;
        return false;
    }
    Progress loaded;
    int refund = 0;
    try {
        loaded.cash = std::max(0, json.value("cash", 0));
        loaded.level = std::max(1, json.value("level", 1));
        loaded.bestLevel = std::max(loaded.level, json.value("bestLevel", 1));
        loaded.totalBones = std::max(0, json.value("totalBones", 0));
        loaded.runs = std::max(0, json.value("runs", 0));
        loaded.map = static_cast<MapTheme>(std::clamp(json.value("map", 0), 0, static_cast<int>(kMapThemeCount) - 1));
        loaded.rebirths = std::max(0, json.value("rebirths", 0));
        loaded.achievements = json.value("achievements", static_cast<uint64_t>(0));
        // Saves from before the tutorial existed belong to players who already know the ropes.
        loaded.tutorialStep = std::max(0, json.value("tutorialStep", loaded.runs > 0 ? kTutorialDone : 0));
        if (json.contains("settings") && json["settings"].is_object()) {
            const nlohmann::json& in = json["settings"];
            GameSettings& out = loaded.settings;
            out.volume = std::clamp(in.value("volume", out.volume), 0.0f, 1.0f);
            out.music = std::clamp(in.value("music", out.music), 0.0f, 1.0f);
            out.sensitivity = std::clamp(in.value("sensitivity", out.sensitivity), 0.2f, 3.0f);
            out.slowMotion = in.value("slowMotion", out.slowMotion);
            out.cameraShake = in.value("cameraShake", out.cameraShake);
            out.tips = in.value("tips", out.tips);
        }
        if (json.contains("stats") && json["stats"].is_object()) {
            const nlohmann::json& in = json["stats"];
            LifetimeStats& out = loaded.stats;
            out.cashEarned = std::max(0LL, in.value("cashEarned", 0LL));
            out.playSeconds = std::max(0.0, in.value("playSeconds", 0.0));
            out.metresFallen = std::max(0.0, in.value("metresFallen", 0.0));
            out.bombs = std::max(0, in.value("bombs", 0));
            out.rockets = std::max(0, in.value("rockets", 0));
            out.superBoosts = std::max(0, in.value("superBoosts", 0));
            out.splashdowns = std::max(0, in.value("splashdowns", 0));
            out.injuries = std::max(0, in.value("injuries", 0));
        }
        loaded.injuriesSeen = json.value("injuriesSeen", 0u);
        static constexpr const char* kBoardKeys[kBoardStatCount] = {"cash", "bones", "fall", "speed"};
        if (json.contains("leaderboard") && json["leaderboard"].is_object()) {
            for (size_t b = 0; b < kBoardStatCount; ++b) {
                const nlohmann::json& board = json["leaderboard"].value(kBoardKeys[b], nlohmann::json::array());
                if (!board.is_array()) continue;
                for (const nlohmann::json& e : board) {
                    if (!e.is_object() || loaded.leaderboard.best[b].size() >= kBoardSize) continue;
                    RunRecord r;
                    r.run = std::max(0, e.value("run", 0));
                    r.cash = std::max(0, e.value("cash", 0));
                    r.bones = std::max(0, e.value("bones", 0));
                    r.fall = std::max(0.0f, e.value("fall", 0.0f));
                    r.speed = std::max(0.0f, e.value("speed", 0.0f));
                    r.injuries = std::max(0, e.value("injuries", 0));
                    r.level = std::max(1, e.value("level", 1));
                    r.map = static_cast<MapTheme>(std::clamp(e.value("map", 0), 0, static_cast<int>(kMapThemeCount) - 1));
                    loaded.leaderboard.best[b].push_back(r);
                }
            }
        }
        if (json.contains("owned") && json["owned"].is_object()) {
            const nlohmann::json& owned = json["owned"];
            for (size_t i = 0; i < kShopItemCount; ++i) {
                loaded.owned[i] = std::clamp(owned.value(kItemKeys[i], 0), 0, kItems[i].maxOwned);
            }
            if (json.value("version", 1) < 2) {
                if (owned.value("jetpack", 0) > 0) refund += 300;
                int tanks = std::clamp(owned.value("fuelTank", 0), 0, 3);
                for (int t = 0; t < tanks; ++t) refund += 200 * (1 + t);
            }
        }
        if (json.contains("contracts") && json["contracts"].is_array()) {
            for (const nlohmann::json& entry : json["contracts"]) {
                if (loaded.contracts.size() >= kActiveContracts || !entry.is_object()) break;
                int kind = entry.value("kind", -1);
                if (kind < 0 || kind >= kContractKindCount) continue;
                Contract contract;
                contract.kind = static_cast<ContractKind>(kind);
                contract.target = std::max(1, entry.value("target", 1));
                contract.reward = std::max(0, entry.value("reward", 0));
                loaded.contracts.push_back(contract);
            }
        }
        if (json.contains("bests") && json["bests"].is_object()) {
            const nlohmann::json& bests = json["bests"];
            loaded.bests.bones = std::max(0, bests.value("bones", 0));
            loaded.bests.combo = std::max(0, bests.value("combo", 0));
            loaded.bests.payout = std::max(0, bests.value("payout", 0));
            loaded.bests.fall = std::max(0.0f, bests.value("fall", 0.0f));
            loaded.bests.speed = std::max(0.0f, bests.value("speed", 0.0f));
            loaded.bests.hit = std::max(0.0f, bests.value("hit", 0.0f));
        }
    } catch (const nlohmann::json::exception& e) {
        outError = std::string("malformed save ") + path + ": " + e.what();
        return false;
    }
    loaded.cash += refund;
    if (!ownsMap(loaded, loaded.map)) loaded.map = MapTheme::Coast;
    progress = loaded;
    if (outRefund != nullptr) *outRefund = refund;
    return true;
}

} // namespace engine::brokenbones
