#include "core/RobloxPlayers.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/Components.hpp"
#include "core/Hierarchy.hpp"
#include "core/InstanceSignals.hpp"
#include "core/PlayerAvatar.hpp"

namespace engine::core::players {
namespace {

struct PendingJoin {
    std::string name;
    int64_t userId = 0;
    EntityId rootPart = kNullEntity;
    bool local = false;
};

struct PlayersState {
    std::vector<PendingJoin> pending;
    glm::vec3 fallbackSpawn{0.0f, 3.0f, -6.0f};
};

constexpr float kMoveToTimeout = 8.0f;

PlayersState& stateOf(ECS& ecs) { return ecs.raw().ctx().emplace<PlayersState>(); }

bool valid(ECS& ecs, EntityId e) { return e != kNullEntity && ecs.raw().valid(e); }

InstanceValue get(ECS& ecs, InstanceRef ref, const char* property) {
    InstanceValue value;
    const PropertyDef* def = instances::findProperty(instances::className(ecs, ref), property);
    if (def != nullptr) (void)instances::getProperty(ecs, ref, *def, value);
    return value;
}

void set(ECS& ecs, InstanceRef ref, const char* property, const InstanceValue& value) {
    const PropertyDef* def = instances::findProperty(instances::className(ecs, ref), property);
    if (def != nullptr) instances::setProperty(ecs, ref, *def, value);
}

void fire(ECS& ecs, InstanceRef ref, const char* event, std::vector<SignalArg> args = {}) {
    if (SignalHub* hub = signals::findHub(ecs)) hub->fire(ref, event, std::move(args));
}

SignalArg instanceArg(InstanceRef ref) { return SignalArg::of(InstanceValue::ofInstance(ref)); }

InstanceRef playersService(ECS& ecs) {
    std::string error;
    return instances::getService(ecs, "Players", error);
}

InstanceRef childOfClass(ECS& ecs, InstanceRef parent, const char* cls) {
    for (InstanceRef child : instances::children(ecs, parent)) {
        if (instances::className(ecs, child) == cls) return child;
    }
    return kNoInstance;
}

HumanoidState& humanoidState(ECS& ecs, EntityId humanoid) {
    return ecs.raw().get_or_emplace<HumanoidState>(humanoid);
}

int stateValue(const std::string& name) {
    static const std::pair<const char*, int> kStates[] = {
        {"FallingDown", 0}, {"Ragdoll", 1}, {"GettingUp", 2}, {"Jumping", 3}, {"Swimming", 4}, {"Freefall", 5},
        {"Flying", 6}, {"Landed", 7}, {"Running", 8}, {"RunningNoPhysics", 10}, {"StrafingNoPhysics", 11},
        {"Climbing", 12}, {"Seated", 13}, {"PlatformStanding", 14}, {"Dead", 15}, {"Physics", 16}, {"None", 18}};
    for (const auto& [item, value] : kStates) {
        if (name == item) return value;
    }
    return -1;
}

SignalArg stateArg(const std::string& name) {
    return SignalArg::of(InstanceValue::ofEnum("HumanoidStateType", name, stateValue(name)));
}

void setState(ECS& ecs, EntityId humanoid, const std::string& next) {
    HumanoidState& s = humanoidState(ecs, humanoid);
    if (s.state == next || (s.dead && next != "Dead")) return;
    const std::string previous = s.state;
    s.state = next;
    fire(ecs, instances::refOf(ecs, humanoid), "StateChanged", {stateArg(previous), stateArg(next)});
}

void finishMoveTo(ECS& ecs, EntityId humanoid, bool reached) {
    HumanoidState& s = humanoidState(ecs, humanoid);
    if (!s.hasMoveTo) return;
    s.hasMoveTo = false;
    s.moveToPart = kNoInstance;
    fire(ecs, instances::refOf(ecs, humanoid), "MoveToFinished", {SignalArg::of(InstanceValue::ofBool(reached))});
}

std::string formatValue(const InstanceValue& v) {
    switch (v.type) {
        case InstanceValue::Type::Number: {
            char buffer[64];
            if (std::abs(v.number - std::round(v.number)) < 1e-9 && std::abs(v.number) < 1e15) {
                std::snprintf(buffer, sizeof(buffer), "%.0f", v.number);
            } else {
                std::snprintf(buffer, sizeof(buffer), "%.2f", v.number);
            }
            return buffer;
        }
        case InstanceValue::Type::String: return v.text;
        case InstanceValue::Type::Bool: return v.boolean ? "true" : "false";
        default: return "-";
    }
}

// Places a fresh character Model around `rootPart` and tells scripts.
void buildCharacter(ECS& ecs, InstanceRef player, EntityId rootPart) {
    const InstanceRef root = instances::refOf(ecs, rootPart);
    const InstanceRef model = instances::createUnchecked(ecs, "Model");
    instances::setName(ecs, model, instances::name(ecs, player));
    std::string error;
    (void)instances::setParent(ecs, model, kWorkspaceInstance, error);

    auto& info = ecs.raw().get_or_emplace<InstanceInfo>(rootPart);
    info.className = "Part";
    info.properties["Anchored"] = InstanceValue::ofBool(false);
    info.properties["CanCollide"] = InstanceValue::ofBool(true);
    instances::setName(ecs, root, "HumanoidRootPart");
    if (auto* r = ecs.tryGetComponent<Renderable>(rootPart)) r->baseColor.a = 0.0f; // Transparency 1, like Roblox
    if (!instances::setParent(ecs, root, model, error)) {
        std::fprintf(stderr, "players: couldn't place %s in its character: %s\n",
                     instances::name(ecs, player).c_str(), error.c_str());
    }

    const InstanceRef humanoid = instances::createUnchecked(ecs, "Humanoid");
    (void)instances::setParent(ecs, humanoid, model, error);
    set(ecs, humanoid, "RootPart", InstanceValue::ofInstance(root));
    set(ecs, humanoid, "DisplayName", get(ecs, player, "DisplayName"));
    humanoidState(ecs, instances::entityOf(ecs, humanoid));
    set(ecs, model, "PrimaryPart", InstanceValue::ofInstance(root));

    set(ecs, player, "Character", InstanceValue::ofInstance(model));
    fire(ecs, player, "CharacterAdded", {instanceArg(model)});
}

glm::vec3 spawnPositionFor(ECS& ecs, InstanceRef player) {
    const InstanceValue respawn = get(ecs, player, "RespawnLocation");
    if (respawn.type == InstanceValue::Type::Instance) {
        const EntityId spawn = instances::entityOf(ecs, respawn.ref);
        if (valid(ecs, spawn) && instances::isInWorld(ecs, spawn)) return spawnPositionOn(ecs, spawn);
    }
    return findPlayerSpawnPosition(ecs, stateOf(ecs).fallbackSpawn);
}

} // namespace

InstanceRef join(ECS& ecs, const std::string& name, int64_t userId, EntityId rootPart, bool local) {
    const InstanceRef service = playersService(ecs);
    const InstanceRef player = instances::createUnchecked(ecs, "Player");
    const EntityId playerEntity = instances::entityOf(ecs, player);
    instances::setName(ecs, player, name);
    set(ecs, player, "UserId", InstanceValue::ofNumber(static_cast<double>(userId)));
    set(ecs, player, "DisplayName", InstanceValue::ofString(name));
    auto& state = ecs.raw().get_or_emplace<PlayerState>(playerEntity);
    state.rootPart = rootPart;
    state.local = local;

    std::string error;
    for (const char* folder : {"Backpack", "PlayerGui"}) {
        const InstanceRef child = instances::createUnchecked(ecs, folder);
        (void)instances::setParent(ecs, child, player, error);
    }
    (void)instances::setParent(ecs, player, service, error);
    if (local) set(ecs, service, "LocalPlayer", InstanceValue::ofInstance(player));

    fire(ecs, service, "PlayerAdded", {instanceArg(player)});
    // PlayerAdded handlers usually connect CharacterAdded, so they run first.
    signals::flush(ecs);
    if (valid(ecs, rootPart) && instances::isAlive(ecs, player)) buildCharacter(ecs, player, rootPart);
    return player;
}

void requestJoin(ECS& ecs, const std::string& name, int64_t userId, EntityId rootPart, bool local) {
    stateOf(ecs).pending.push_back(PendingJoin{name, userId, rootPart, local});
}

void leave(ECS& ecs, InstanceRef player) {
    const EntityId playerEntity = instances::entityOf(ecs, player);
    if (!valid(ecs, playerEntity)) return;
    const InstanceRef service = playersService(ecs);
    fire(ecs, service, "PlayerRemoving", {instanceArg(player)});
    const InstanceValue character = get(ecs, player, "Character");
    if (character.type == InstanceValue::Type::Instance) fire(ecs, player, "CharacterRemoving", {instanceArg(character.ref)});
    signals::flush(ecs);

    if (const auto* state = ecs.tryGetComponent<PlayerState>(playerEntity); state != nullptr && valid(ecs, state->rootPart)) {
        hierarchy::unparent(ecs, state->rootPart); // the host owns the capsule
    }
    if (character.type == InstanceValue::Type::Instance) instances::destroy(ecs, character.ref);
    if (get(ecs, service, "LocalPlayer").ref == player) set(ecs, service, "LocalPlayer", InstanceValue{});
    instances::destroy(ecs, player);
}

void loadCharacter(ECS& ecs, InstanceRef player) {
    const EntityId playerEntity = instances::entityOf(ecs, player);
    auto* state = valid(ecs, playerEntity) ? ecs.tryGetComponent<PlayerState>(playerEntity) : nullptr;
    if (state == nullptr) return;
    state->loadRequested = false;
    state->deadSeconds = 0.0f;
    if (!valid(ecs, state->rootPart)) return;

    const InstanceValue old = get(ecs, player, "Character");
    const bool hadCharacter = old.type == InstanceValue::Type::Instance && instances::isAlive(ecs, old.ref);
    if (hadCharacter) {
        fire(ecs, player, "CharacterRemoving", {instanceArg(old.ref)});
        signals::flush(ecs);
    }
    const EntityId root = state->rootPart;
    buildCharacter(ecs, player, root);
    if (hadCharacter) instances::destroy(ecs, old.ref);

    const glm::quat rotation = instances::worldPose(ecs, root).rotation;
    instances::teleport(ecs, root, spawnPositionFor(ecs, player), rotation, true);
}

void requestLoadCharacter(ECS& ecs, InstanceRef player) {
    const EntityId playerEntity = instances::entityOf(ecs, player);
    if (auto* state = valid(ecs, playerEntity) ? ecs.tryGetComponent<PlayerState>(playerEntity) : nullptr) {
        state->loadRequested = true;
    }
}

void tick(ECS& ecs, float dt) {
    PlayersState& shared = stateOf(ecs);
    if (!shared.pending.empty()) {
        std::vector<PendingJoin> pending;
        pending.swap(shared.pending);
        for (const PendingJoin& p : pending) (void)join(ecs, p.name, p.userId, p.rootPart, p.local);
    }

    const InstanceRef service = instances::findService(ecs, "Players");
    if (service != kNoInstance) {
        const double respawnTime = get(ecs, service, "RespawnTime").number;
        const bool autoLoads = get(ecs, service, "CharacterAutoLoads").boolean;
        const double fallenHeight = get(ecs, kWorkspaceInstance, "FallenPartsDestroyHeight").number;
        for (InstanceRef player : list(ecs)) {
            auto* state = ecs.tryGetComponent<PlayerState>(instances::entityOf(ecs, player));
            if (state == nullptr) continue;
            if (state->loadRequested) {
                loadCharacter(ecs, player);
                continue;
            }
            const EntityId humanoid = humanoidFor(ecs, state->rootPart);
            if (humanoid == kNullEntity) continue;
            if (humanoidState(ecs, humanoid).dead) {
                state->deadSeconds += dt;
                if (autoLoads && state->deadSeconds >= respawnTime) loadCharacter(ecs, player);
            } else if (instances::worldPose(ecs, state->rootPart).position.y < fallenHeight) {
                set(ecs, instances::refOf(ecs, humanoid), "Health", InstanceValue::ofNumber(0.0));
            }
        }
    }

    for (auto [entity, s] : ecs.raw().view<HumanoidState>().each()) {
        if (!s.hasMoveTo) continue;
        s.moveToElapsed += dt;
        if (s.moveToPart != kNoInstance) {
            const EntityId part = instances::entityOf(ecs, s.moveToPart);
            if (valid(ecs, part)) s.moveToTarget = instances::worldPose(ecs, part).position;
        }
        if (s.moveToElapsed >= kMoveToTimeout) finishMoveTo(ecs, entity, false);
    }
}

void reset(ECS& ecs) { ecs.raw().ctx().erase<PlayersState>(); }

void setFallbackSpawn(ECS& ecs, glm::vec3 position) { stateOf(ecs).fallbackSpawn = position; }

InstanceRef localPlayer(ECS& ecs) {
    const InstanceRef service = instances::findService(ecs, "Players");
    if (service == kNoInstance) return kNoInstance;
    const InstanceValue value = get(ecs, service, "LocalPlayer");
    return value.type == InstanceValue::Type::Instance ? value.ref : kNoInstance;
}

std::vector<InstanceRef> list(ECS& ecs) {
    std::vector<InstanceRef> out;
    const InstanceRef service = instances::findService(ecs, "Players");
    if (service == kNoInstance) return out;
    for (InstanceRef child : instances::children(ecs, service)) {
        if (instances::className(ecs, child) == "Player") out.push_back(child);
    }
    return out;
}

InstanceRef playerFromCharacter(ECS& ecs, InstanceRef character) {
    if (character == kNoInstance) return kNoInstance;
    for (InstanceRef player : list(ecs)) {
        const InstanceValue value = get(ecs, player, "Character");
        if (value.type == InstanceValue::Type::Instance && value.ref == character) return player;
    }
    return kNoInstance;
}

InstanceRef playerByUserId(ECS& ecs, int64_t userId) {
    for (InstanceRef player : list(ecs)) {
        if (static_cast<int64_t>(get(ecs, player, "UserId").number) == userId) return player;
    }
    return kNoInstance;
}

EntityId humanoidFor(ECS& ecs, EntityId rootPart) {
    if (!valid(ecs, rootPart)) return kNullEntity;
    const InstanceRef model = instances::parent(ecs, instances::refOf(ecs, rootPart));
    if (model == kNoInstance || model == kWorkspaceInstance || model == kGameInstance) return kNullEntity;
    return instances::entityOf(ecs, childOfClass(ecs, model, "Humanoid"));
}

HumanoidControl control(ECS& ecs, EntityId rootPart) {
    HumanoidControl out;
    const EntityId humanoid = humanoidFor(ecs, rootPart);
    if (humanoid == kNullEntity) return out;
    const InstanceRef ref = instances::refOf(ecs, humanoid);
    const HumanoidState& s = humanoidState(ecs, humanoid);
    out.found = true;
    out.dead = s.dead;
    out.walkScale = static_cast<float>(std::max(0.0, get(ecs, ref, "WalkSpeed").number) / kDefaultWalkSpeed);
    if (get(ecs, ref, "UseJumpPower").boolean) {
        out.jumpScale = static_cast<float>(std::max(0.0, get(ecs, ref, "JumpPower").number) / kDefaultJumpPower);
    } else {
        out.jumpScale = static_cast<float>(std::sqrt(std::max(0.0, get(ecs, ref, "JumpHeight").number) / kDefaultJumpHeight));
    }
    out.autoRotate = get(ecs, ref, "AutoRotate").boolean;
    if (get(ecs, ref, "Jump").boolean) {
        out.jump = true;
        set(ecs, ref, "Jump", InstanceValue::ofBool(false));
    }
    out.hasMoveTo = s.hasMoveTo;
    out.moveToTarget = s.moveToTarget;
    out.scriptedMove = s.scriptedMove;
    out.scriptedMoveRelativeToCamera = s.scriptedMoveRelativeToCamera;
    if (out.dead) {
        out.jump = false;
        out.hasMoveTo = false;
        out.scriptedMove = glm::vec3(0.0f);
    }
    return out;
}

void reportMotion(ECS& ecs, EntityId rootPart, const HumanoidMotion& motion) {
    const EntityId humanoid = humanoidFor(ecs, rootPart);
    if (humanoid == kNullEntity) return;
    const InstanceRef ref = instances::refOf(ecs, humanoid);
    HumanoidState& s = humanoidState(ecs, humanoid);
    if (s.dead) return;

    const glm::vec3 direction = glm::length(motion.moveDirection) > 1e-4f ? glm::normalize(motion.moveDirection)
                                                                           : glm::vec3(0.0f);
    if (glm::length(direction - get(ecs, ref, "MoveDirection").vec) > 1e-3f) {
        set(ecs, ref, "MoveDirection", InstanceValue::ofVector3(direction));
    }
    if (motion.reachedMoveTo) finishMoveTo(ecs, humanoid, true);

    if (motion.jumped) {
        setState(ecs, humanoid, "Jumping");
        fire(ecs, ref, "Jumping", {SignalArg::of(InstanceValue::ofBool(true))});
        return;
    }
    if (!motion.grounded) {
        if (s.state != "Freefall" && s.state != "Jumping") {
            setState(ecs, humanoid, "Freefall");
            fire(ecs, ref, "FreeFalling", {SignalArg::of(InstanceValue::ofBool(true))});
        } else if (s.state == "Jumping") {
            setState(ecs, humanoid, "Freefall");
        }
        return;
    }
    if (s.state == "Freefall" || s.state == "Jumping") {
        fire(ecs, ref, "FreeFalling", {SignalArg::of(InstanceValue::ofBool(false))});
        setState(ecs, humanoid, "Landed");
        setState(ecs, humanoid, "Running");
    }
    const bool moving = motion.speed > 0.1f;
    if (std::abs(motion.speed - s.lastSpeed) > 0.5f || moving != (s.lastSpeed > 0.1f)) {
        s.lastSpeed = moving ? motion.speed : 0.0f;
        fire(ecs, ref, "Running", {SignalArg::of(InstanceValue::ofNumber(s.lastSpeed))});
    }
}

void takeDamage(ECS& ecs, EntityId humanoid, double amount) {
    const InstanceRef ref = instances::refOf(ecs, humanoid);
    set(ecs, ref, "Health", InstanceValue::ofNumber(get(ecs, ref, "Health").number - amount));
}

void moveTo(ECS& ecs, EntityId humanoid, glm::vec3 target, InstanceRef part) {
    HumanoidState& s = humanoidState(ecs, humanoid);
    if (s.hasMoveTo) finishMoveTo(ecs, humanoid, false);
    s.hasMoveTo = true;
    s.moveToTarget = target;
    s.moveToPart = part;
    s.moveToElapsed = 0.0f;
    set(ecs, instances::refOf(ecs, humanoid), "WalkToPoint", InstanceValue::ofVector3(target));
}

void move(ECS& ecs, EntityId humanoid, glm::vec3 direction, bool relativeToCamera) {
    HumanoidState& s = humanoidState(ecs, humanoid);
    direction.y = 0.0f;
    s.scriptedMove = glm::length(direction) > 1e-4f ? glm::normalize(direction) : glm::vec3(0.0f);
    s.scriptedMoveRelativeToCamera = relativeToCamera;
}

std::string state(ECS& ecs, EntityId humanoid) { return humanoidState(ecs, humanoid).state; }

void changeState(ECS& ecs, EntityId humanoid, const std::string& next) {
    if (stateValue(next) < 0) return;
    const InstanceRef ref = instances::refOf(ecs, humanoid);
    if (next == "Dead") {
        set(ecs, ref, "Health", InstanceValue::ofNumber(0.0));
        return;
    }
    if (next == "Jumping") {
        set(ecs, ref, "Jump", InstanceValue::ofBool(true));
        return;
    }
    setState(ecs, humanoid, next);
}

void onHealthSet(ECS& ecs, EntityId humanoid, const InstanceValue& value) {
    auto* info = ecs.tryGetComponent<InstanceInfo>(humanoid);
    if (info == nullptr) return;
    const InstanceRef ref = instances::refOf(ecs, humanoid);
    const double maxHealth = get(ecs, ref, "MaxHealth").number;
    const double health = std::clamp(std::isnan(value.number) ? 0.0 : value.number, 0.0, std::max(0.0, maxHealth));
    info->properties["Health"] = InstanceValue::ofNumber(health);

    HumanoidState& s = humanoidState(ecs, humanoid);
    if (health != s.lastHealth) {
        s.lastHealth = health;
        fire(ecs, ref, "HealthChanged", {SignalArg::of(InstanceValue::ofNumber(health))});
    }
    if (health <= 0.0 && !s.dead) {
        setState(ecs, humanoid, "Dead");
        s.dead = true;
        s.hasMoveTo = false;
        s.scriptedMove = glm::vec3(0.0f);
        set(ecs, ref, "MoveDirection", InstanceValue::ofVector3(glm::vec3(0.0f)));
        fire(ecs, ref, "Died");
    }
}

void onMaxHealthSet(ECS& ecs, EntityId humanoid, const InstanceValue& value) {
    const InstanceRef ref = instances::refOf(ecs, humanoid);
    if (get(ecs, ref, "Health").number > value.number) set(ecs, ref, "Health", InstanceValue::ofNumber(value.number));
}

Leaderboard leaderboard(ECS& ecs) {
    Leaderboard board;
    std::vector<std::pair<InstanceRef, InstanceRef>> withStats;
    for (InstanceRef player : list(ecs)) {
        InstanceRef stats = kNoInstance;
        for (InstanceRef child : instances::children(ecs, player)) {
            if (instances::name(ecs, child) == "leaderstats") {
                stats = child;
                break;
            }
        }
        withStats.emplace_back(player, stats);
        if (stats == kNoInstance) continue;
        for (InstanceRef value : instances::children(ecs, stats)) {
            if (!instances::classIsA(instances::className(ecs, value), "ValueBase")) continue;
            const std::string column = instances::name(ecs, value);
            if (std::find(board.columns.begin(), board.columns.end(), column) == board.columns.end()) {
                board.columns.push_back(column);
            }
        }
    }
    if (board.columns.empty()) return board;

    std::vector<double> sortKeys;
    for (const auto& [player, stats] : withStats) {
        Leaderboard::Row row;
        row.name = instances::name(ecs, player);
        if (const auto* state = ecs.tryGetComponent<PlayerState>(instances::entityOf(ecs, player))) row.local = state->local;
        double key = -1e300;
        for (const std::string& column : board.columns) {
            std::string text = "-";
            if (stats != kNoInstance) {
                for (InstanceRef value : instances::children(ecs, stats)) {
                    if (instances::name(ecs, value) != column) continue;
                    const InstanceValue v = get(ecs, value, "Value");
                    text = formatValue(v);
                    if (column == board.columns.front() && v.type == InstanceValue::Type::Number) key = v.number;
                    break;
                }
            }
            row.values.push_back(text);
        }
        board.rows.push_back(std::move(row));
        sortKeys.push_back(key);
    }
    std::vector<size_t> order(board.rows.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return sortKeys[a] > sortKeys[b]; });
    std::vector<Leaderboard::Row> sorted;
    for (size_t i : order) sorted.push_back(std::move(board.rows[i]));
    board.rows = std::move(sorted);
    return board;
}

} // namespace engine::core::players
