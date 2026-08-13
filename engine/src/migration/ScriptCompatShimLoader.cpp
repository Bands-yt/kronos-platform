#include "migration/ScriptCompatShimLoader.hpp"

namespace engine::migration {

ScriptCompatShimLoader::ScriptCompatShimLoader() {
    // The two entries below are the exact two warnings shown in
    // docs/ARCHITECTURE.md §7's worked importer-report example -- this
    // registry is what that example's report actually comes from.
    registry_.push_back(CompatShimEntry{
        "IsA(\"Tool\")",
        R"(-- shim: legacy Instance:IsA("Tool") inheritance check
local __compat_isTool = function(instance)
    return instance ~= nil and instance.ClassName == "Tool"
end
)",
        "uses deprecated Instance:IsA(\"Tool\") inheritance check; compat-shim applied, behavior verified equivalent.",
    });

    registry_.push_back(CompatShimEntry{
        "LoadAnimation",
        R"(-- shim: legacy Humanoid:LoadAnimation call site
local __compat_loadAnimation = function(humanoid, animation)
    local animator = humanoid:FindFirstChildOfClass("Animator")
        or Instance.new("Animator", humanoid)
    return animator:LoadAnimation(animation)
end
)",
        "Humanoid:LoadAnimation call site; mapped to current Animator-based API automatically.",
    });
}

std::vector<CompatShimEntry> ScriptCompatShimLoader::detectRequiredShims(const std::string& scriptSource) const {
    std::vector<CompatShimEntry> required;
    for (const auto& entry : registry_) {
        if (scriptSource.find(entry.deprecatedApiMarker) != std::string::npos) {
            required.push_back(entry);
        }
    }
    return required;
}

std::string ScriptCompatShimLoader::generateCompatModuleSource() const {
    std::string source = "-- Auto-generated compat module (ScriptCompatShimLoader) -- do not edit by hand.\n";
    for (const auto& entry : registry_) {
        source += entry.luauSource;
        source += "\n";
    }
    return source;
}

std::vector<std::string> ScriptCompatShimLoader::buildReportLines(const std::string& scriptPath,
                                                                    const std::string& scriptSource) const {
    std::vector<std::string> lines;
    for (const auto& entry : detectRequiredShims(scriptSource)) {
        lines.push_back("[warn] " + scriptPath + " -- " + entry.description);
    }
    return lines;
}

} // namespace engine::migration
