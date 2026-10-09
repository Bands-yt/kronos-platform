#include "migration/CompatibilityScore.hpp"

#include <algorithm>
#include <cstdio>
#include <functional>

#include <Luau/Compiler.h>

#include "core/Components.hpp"
#include "core/InstanceSignals.hpp"
#include "core/InstanceTree.hpp"
#include "core/RobloxPlayers.hpp"
#include "core/ScriptInstanceApi.hpp"
#include "core/Scripting.hpp"
#include "migration/InstanceHydrator.hpp"

namespace engine::migration {
namespace {

double percent(size_t part, size_t whole) {
    return whole == 0 ? 100.0 : 100.0 * static_cast<double>(part) / static_cast<double>(whole);
}

bool isScriptClass(const std::string& className) {
    return className == "Script" || className == "LocalScript" || className == "ModuleScript";
}

void visit(const std::vector<ImportedInstance>& nodes, const std::string& parentPath,
           const std::function<void(const ImportedInstance&, const std::string&)>& fn) {
    for (const ImportedInstance& node : nodes) {
        const std::string path = parentPath.empty() ? node.name : parentPath + "." + node.name;
        fn(node, path);
        visit(node.children, path, fn);
    }
}

std::string sourceOf(const ImportedInstance& node) {
    const auto it = node.properties.find("Source");
    return it == node.properties.end() ? std::string() : it->second;
}

} // namespace

double CompatibilityScore::instancePercent() const { return percent(instancesMapped, instances); }
double CompatibilityScore::apiPercent() const { return percent(apiSupported, apiUses); }
double CompatibilityScore::scriptPercent() const { return percent(scriptsOk, scriptsRun); }
double CompatibilityScore::overallPercent() const {
    return (instancePercent() + apiPercent() + scriptPercent()) / 3.0;
}

std::string CompatibilityScore::summary() const {
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer),
                  "%.0f%% overall -- instances %zu/%zu (%.0f%%), API uses %zu/%zu (%.0f%%), scripts %zu/%zu run "
                  "(%.0f%%)",
                  overallPercent(), instancesMapped, instances, instancePercent(), apiSupported, apiUses,
                  apiPercent(), scriptsOk, scriptsRun, scriptPercent());
    return buffer;
}

CompatibilityScore scoreImport(const ImportReport& report) {
    CompatibilityScore score;
    LuauApiCompatibility api;
    visit(report.tree, "", [&](const ImportedInstance& node, const std::string&) {
        ++score.instances;
        if (InstanceHydrator::isSupportedClass(node.className)) {
            ++score.instancesMapped;
        } else {
            ++score.unmappedClasses[node.className];
        }
        if (!isScriptClass(node.className)) return;
        for (const ApiCompatibilityFinding& finding : api.scan(sourceOf(node))) {
            ++score.apiUses;
            if (finding.status == ApiMappingStatus::Unmapped) {
                ++score.missingApis[finding.identifier];
            } else {
                ++score.apiSupported;
            }
        }
    });
    return score;
}

void runImportedScripts(const ImportReport& report, CompatibilityScore& score) {
    // A headless copy of the place, so scripts can find their parts.
    core::ECS ecs;
    (void)InstanceHydrator{}.hydrate(report.tree, ecs, HydrationMeshes{});

    core::Scripting scripting;
    scripting.setBindingsHook([&ecs](lua_State* L) { core::registerInstanceApi(L, ecs); });
    if (!scripting.initialize()) return;

    std::string lastError;
    std::vector<std::string> laterErrors; // from handlers and tasks, after every script has loaded
    bool loading = true;
    scripting.setOutputCallback([&](const std::string& line) {
        if (line.rfind("runtime error", 0) != 0 && line.rfind("compile error", 0) != 0) return;
        if (loading) {
            lastError = line;
        } else {
            laterErrors.push_back(line);
        }
    });

    std::vector<core::EntityId> scriptEntities;
    for (core::EntityId e : ecs.view<core::Script>()) scriptEntities.push_back(e);
    std::sort(scriptEntities.begin(), scriptEntities.end(),
              [](core::EntityId a, core::EntityId b) { return entt::to_entity(a) < entt::to_entity(b); });

    for (core::EntityId e : scriptEntities) {
        if (!ecs.raw().valid(e)) continue; // a script that ran earlier destroyed it
        const std::string source = ecs.tryGetComponent<core::Script>(e)->source;
        if (source.empty()) continue;
        const core::InstanceRef ref = core::instances::refOf(ecs, e);
        const std::string className = core::instances::className(ecs, ref);
        const std::string path = core::instances::fullName(ecs, ref);

        CompatScriptRun run{path, className, true, {}};
        if (className == "ModuleScript") {
            const std::string bytecode = Luau::compile(source);
            if (!bytecode.empty() && bytecode[0] == 0) {
                run.ok = false;
                run.error = "compile error: " + bytecode.substr(1);
            }
        } else {
            lastError.clear();
            const core::ScriptId id = scripting.loadAndRun(path, source, core::SecurityIdentity::UserScript,
                                                           static_cast<uint32_t>(entt::to_integral(e)));
            if (id == core::kInvalidScript || !lastError.empty()) {
                run.ok = false;
                run.error = lastError.empty() ? "failed to load" : lastError;
            }
        }
        score.scripts.push_back(std::move(run));
    }

    // A test player joins (with a character), plays for a moment and
    // leaves, so PlayerAdded/CharacterAdded/PlayerRemoving handlers and
    // loops run too. Their errors count against the script they came from.
    loading = false;
    std::string error;
    const core::InstanceRef root = core::instances::create(ecs, "Part", error);
    (void)core::instances::setParent(ecs, root, core::kWorkspaceInstance, error);
    const core::InstanceRef player =
        core::players::join(ecs, "Player1", 1, core::instances::entityOf(ecs, root), true);
    for (int frame = 0; frame < 30; ++frame) {
        scripting.tick(0.1f);
        core::players::tick(ecs, 0.1f);
        core::signals::flush(ecs);
    }
    core::players::leave(ecs, player);
    for (const std::string& line : laterErrors) {
        const size_t open = line.find('"');
        const size_t close = open == std::string::npos ? open : line.find('"', open + 1);
        if (close == std::string::npos) continue;
        const std::string path = line.substr(open + 1, close - open - 1);
        for (CompatScriptRun& run : score.scripts) {
            if (run.path == path && run.ok) {
                run.ok = false;
                run.error = line;
            }
        }
    }
    for (const CompatScriptRun& run : score.scripts) {
        ++score.scriptsRun;
        if (run.ok) ++score.scriptsOk;
    }
}

} // namespace engine::migration
