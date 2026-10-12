#include "core/ScriptHotReload.hpp"

#include "core/Components.hpp"
#include "core/ECS.hpp"
#include "core/PythonScripting.hpp"
#include "core/RobloxScripts.hpp"
#include "core/ScriptLanguage.hpp"
#include "core/Scripting.hpp"

namespace engine::core {

void tickScriptHotReload(ECS& ecs, Scripting& scripting, PythonScripting* python) {
    auto scriptView = ecs.view<Script>();
    for (auto entity : scriptView) {
        Script& script = scriptView.get<Script>(entity);
        if (robloxScripts::isRobloxScript(ecs, entity)) continue;
        if (script.autoRun && !script.source.empty() && script.source != script.loadedSource) {
            const bool wasPython = !script.loadedSource.empty() &&
                                   scriptLanguageOf(script.loadedSource) == ScriptLanguage::Python;
            if (script.scriptId != kInvalidScript) {
                if (wasPython) {
                    if (python != nullptr) python->unload(script.scriptId);
                } else {
                    scripting.unload(script.scriptId);
                }
            }
            const Name* name = ecs.tryGetComponent<Name>(entity);
            std::string chunkName = (name != nullptr && !name->value.empty()) ? name->value : "Script";
            if (scriptLanguageOf(script.source) == ScriptLanguage::Python) {
                script.scriptId = python != nullptr ? python->load(chunkName + ".py", script.source) : kInvalidScript;
            } else {
                script.scriptId = scripting.loadAndRun(chunkName, script.source, SecurityIdentity::UserScript,
                                                       static_cast<uint32_t>(entity));
            }
            script.loadedSource = script.source;
        }
    }
    robloxScripts::tick(ecs, scripting);
}

} // namespace engine::core
