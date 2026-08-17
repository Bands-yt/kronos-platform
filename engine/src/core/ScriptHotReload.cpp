#include "core/ScriptHotReload.hpp"

#include "core/Components.hpp"
#include "core/ECS.hpp"
#include "core/Scripting.hpp"

namespace engine::core {

void tickScriptHotReload(ECS& ecs, Scripting& scripting) {
    auto scriptView = ecs.view<Script>();
    for (auto entity : scriptView) {
        Script& script = scriptView.get<Script>(entity);
        if (script.autoRun && !script.source.empty() && script.source != script.loadedSource) {
            if (script.scriptId != kInvalidScript) scripting.unload(script.scriptId);
            const Name* name = ecs.tryGetComponent<Name>(entity);
            std::string chunkName = (name != nullptr && !name->value.empty()) ? name->value : "Script";
            script.scriptId = scripting.loadAndRun(chunkName, script.source);
            script.loadedSource = script.source;
        }
    }
}

} // namespace engine::core
