#include "core/RobloxScripts.hpp"

#include <string>
#include <unordered_map>
#include <vector>

#include "core/Components.hpp"
#include "core/InstanceSignals.hpp"
#include "core/InstanceTree.hpp"
#include "core/RobloxPlayers.hpp"

namespace engine::core::robloxScripts {
namespace {

struct Running {
    ScriptId id = kInvalidScript;
    uint64_t generation = 0;
    std::string source;
};

struct RunningScripts {
    std::unordered_map<EntityId, Running> byEntity;
    std::vector<std::string> started;
};

RunningScripts& runningOf(ECS& ecs) {
    auto& ctx = ecs.raw().ctx();
    if (!ctx.contains<RunningScripts>()) ctx.emplace<RunningScripts>();
    return ctx.get<RunningScripts>();
}

bool valid(ECS& ecs, EntityId e) { return e != kNullEntity && ecs.raw().valid(e); }

bool disabled(ECS& ecs, EntityId e) {
    const auto* info = ecs.tryGetComponent<InstanceInfo>(e);
    if (info == nullptr) return false;
    const auto it = info->properties.find("Disabled");
    return it != info->properties.end() && it->second.boolean;
}

bool under(ECS& ecs, InstanceRef ref, InstanceRef ancestor) {
    return ancestor != kNoInstance && instances::isDescendantOf(ecs, ref, ancestor);
}

InstanceRef childOfClass(ECS& ecs, InstanceRef parent, const char* className) {
    if (parent == kNoInstance) return kNoInstance;
    for (InstanceRef child : instances::children(ecs, parent)) {
        if (instances::className(ecs, child) == className) return child;
    }
    return kNoInstance;
}

InstanceRef characterOf(ECS& ecs, InstanceRef player) {
    InstanceValue value;
    const PropertyDef* property = instances::findProperty("Player", "Character");
    if (player == kNoInstance || property == nullptr || !instances::getProperty(ecs, player, *property, value)) {
        return kNoInstance;
    }
    return value.type == InstanceValue::Type::Instance ? value.ref : kNoInstance;
}

} // namespace

bool isRobloxScript(ECS& ecs, EntityId entity) {
    const auto* info = ecs.tryGetComponent<InstanceInfo>(entity);
    if (info == nullptr || (info->className != "Script" && info->className != "LocalScript")) return false;
    return info->properties.count("Disabled") != 0;
}

std::optional<RunContext> startContext(ECS& ecs, EntityId entity) {
    if (!isRobloxScript(ecs, entity) || disabled(ecs, entity)) return std::nullopt;
    const InstanceRef ref = instances::refOf(ecs, entity);
    const RunServiceState& sides = signals::runService(ecs);
    if (instances::className(ecs, ref) == "Script") {
        if (!sides.server) return std::nullopt;
        if (under(ecs, ref, kWorkspaceInstance) || under(ecs, ref, instances::findService(ecs, "ServerScriptService"))) {
            return RunContext::Server;
        }
        for (InstanceRef player : players::list(ecs)) {
            if (under(ecs, ref, childOfClass(ecs, player, "Backpack"))) return RunContext::Server;
        }
        return std::nullopt;
    }
    if (!sides.client) return std::nullopt;
    const InstanceRef local = players::localPlayer(ecs);
    if (under(ecs, ref, local) || under(ecs, ref, characterOf(ecs, local)) ||
        under(ecs, ref, instances::findService(ecs, "ReplicatedFirst"))) {
        return RunContext::Client;
    }
    return std::nullopt;
}

void tick(ECS& ecs, Scripting& scripting) {
    RunningScripts& running = runningOf(ecs);
    for (auto it = running.byEntity.begin(); it != running.byEntity.end();) {
        const EntityId e = it->first;
        Running& run = it->second;
        const auto* script = valid(ecs, e) ? ecs.tryGetComponent<Script>(e) : nullptr;
        const bool stale = run.generation != scripting.generation();
        const bool stop = stale || script == nullptr || disabled(ecs, e) || script->source != run.source;
        if (!stop) {
            ++it;
            continue;
        }
        if (!stale && run.id != kInvalidScript) scripting.unload(run.id);
        it = running.byEntity.erase(it);
    }

    std::vector<EntityId> candidates;
    for (EntityId e : ecs.view<Script>()) {
        if (running.byEntity.count(e) == 0 && isRobloxScript(ecs, e)) candidates.push_back(e);
    }
    for (EntityId e : candidates) {
        if (!valid(ecs, e)) continue;
        const auto* script = ecs.tryGetComponent<Script>(e);
        const std::optional<RunContext> context = startContext(ecs, e);
        if (script == nullptr || script->source.empty() || !context) continue;
        if (*context == RunContext::Client && signals::runService(ecs).awaitingReplication) continue;
        const std::string source = script->source;
        const std::string chunkName = instances::fullName(ecs, instances::refOf(ecs, e));
        // Recorded first: a script can destroy or edit itself while it starts.
        running.byEntity[e] = Running{kInvalidScript, scripting.generation(), source};
        running.started.push_back(chunkName);
        const ScriptId id =
            scripting.loadAndRun(chunkName, source, SecurityIdentity::UserScript, static_cast<uint32_t>(e), *context);
        if (auto found = running.byEntity.find(e); found != running.byEntity.end()) found->second.id = id;
        else if (id != kInvalidScript) scripting.unload(id);
    }
}

std::vector<std::string> startedNames(ECS& ecs) { return runningOf(ecs).started; }

void reset(ECS& ecs) { ecs.raw().ctx().erase<RunningScripts>(); }

} // namespace engine::core::robloxScripts
