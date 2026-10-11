#include "migration/ProjectValidator.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <set>
#include <tuple>

#include <Luau/Compiler.h>

#include "core/Components.hpp"
#include "core/InstanceTree.hpp"
#include "core/KronosVersion.hpp"
#include "migration/LuauApiCompatibility.hpp"

namespace engine::migration {
namespace {

constexpr uint32_t kNoEntity = ~0u;

template <typename T>
std::vector<core::EntityId> sortedView(core::ECS& ecs) {
    std::vector<core::EntityId> out;
    for (core::EntityId e : ecs.view<T>()) out.push_back(e);
    std::sort(out.begin(), out.end(),
              [](core::EntityId a, core::EntityId b) { return entt::to_entity(a) < entt::to_entity(b); });
    return out;
}

core::EntityId noEntity() { return entt::null; }

uint32_t idOf(core::EntityId e) { return static_cast<uint32_t>(entt::to_integral(e)); }

std::string pathOf(core::ECS& ecs, core::EntityId e) {
    return core::instances::fullName(ecs, core::instances::refOf(ecs, e));
}

// Luau::compile errors look like ":12: message".
int compileErrorLine(const std::string& message) {
    if (message.empty() || message[0] != ':') return 0;
    int line = 0;
    size_t i = 1;
    while (i < message.size() && std::isdigit(static_cast<unsigned char>(message[i])) && i < 9) {
        line = line * 10 + (message[i++] - '0');
    }
    return i < message.size() && message[i] == ':' ? line : 0;
}

bool fileExists(const std::string& path, const std::vector<std::string>& roots) {
    std::error_code ec;
    const std::filesystem::path p(path);
    if (p.is_absolute()) return std::filesystem::exists(p, ec);
    for (const std::string& root : roots) {
        if (std::filesystem::exists(root.empty() ? p : std::filesystem::path(root) / p, ec)) return true;
    }
    return false;
}

bool isRobloxHosted(const std::string& text) {
    for (const char* prefix : {"rbxassetid://", "rbxasset://", "rbxthumb://", "http://", "https://"}) {
        if (text.rfind(prefix, 0) == 0) return true;
    }
    return false;
}

std::string q(const std::string& text) { return "\"" + doctorJsonEscape(text) + "\""; }

struct Builder {
    ValidationReport& report;
    void add(const std::string& checkId, core::EntityId entity, DoctorFinding f) {
        report.findings.push_back({std::move(f), entity == noEntity() ? kNoEntity : idOf(entity), checkId});
    }
    void check(const std::string& id, const std::string& title, CheckStatus status, const std::string& detail) {
        report.checks.push_back({id, title, status, detail});
    }
    size_t countFor(const std::string& checkId, DoctorCategory category) const {
        return std::count_if(report.findings.begin(), report.findings.end(), [&](const ProjectFinding& f) {
            return f.checkId == checkId && f.finding.category == category;
        });
    }
};

} // namespace

std::string ProjectFinding::stableId() const {
    return finding.code + "|" + finding.script + "|" + finding.subject;
}

const char* checkStatusName(CheckStatus status) {
    switch (status) {
        case CheckStatus::Pass: return "PASS";
        case CheckStatus::Fail: return "FAIL";
        case CheckStatus::Warning: return "WARNING";
        case CheckStatus::NotRun: return "NOT RUN";
        case CheckStatus::Unavailable: return "UNAVAILABLE";
        case CheckStatus::Inconclusive: return "INCONCLUSIVE";
    }
    return "?";
}

CheckStatus ValidationReport::overall() const {
    bool warning = false;
    for (const ProjectCheck& c : checks) {
        if (c.status == CheckStatus::Fail) return CheckStatus::Fail;
        warning = warning || c.status == CheckStatus::Warning || c.status == CheckStatus::Inconclusive;
    }
    return warning ? CheckStatus::Warning : CheckStatus::Pass;
}

const ProjectCheck* ValidationReport::check(const std::string& id) const {
    for (const ProjectCheck& c : checks) {
        if (c.id == id) return &c;
    }
    return nullptr;
}

bool isScriptErrorLine(const std::string& line) {
    return line.find("runtime error in ") != std::string::npos || line.find("compile error") != std::string::npos;
}

ValidationReport validateProject(core::ECS& ecs, const ValidationInput& input) {
    ValidationReport report;
    report.kronosVersion = core::kKronosVersion;
    Builder b{report};
    for (core::InstanceRef ref : core::instances::descendants(ecs, core::kGameInstance)) {
        if (ref < core::kCameraInstance) ++report.instances;
    }

    // Scripts compile.
    const std::vector<core::EntityId> scripts = sortedView<core::Script>(ecs);
    std::map<std::string, core::EntityId> scriptByPath;
    size_t compiled = 0;
    for (core::EntityId e : scripts) {
        const std::string& source = ecs.tryGetComponent<core::Script>(e)->source;
        const std::string path = pathOf(ecs, e);
        scriptByPath.emplace(path, e);
        if (source.empty()) continue;
        ++report.scripts;
        const std::string bytecode = Luau::compile(source);
        if (!bytecode.empty() && bytecode[0] == 0) {
            const std::string message = bytecode.substr(1);
            const int line = compileErrorLine(message);
            const std::string text = line > 0 ? message.substr(message.find(':', 1) + 1) : message;
            b.add("scripts-compile", e,
                  {DoctorCategory::Blocker, "compile-error", path, path, line, 1,
                   text.size() > 1 && text[0] == ' ' ? text.substr(1) : text,
                   "The script won't run at all until this is fixed.", "docs/LUA_API.md"});
        } else {
            ++compiled;
        }
    }
    const size_t compileFailures = report.scripts - compiled;
    b.check("scripts-compile", "Scripts compile",
            compileFailures ? CheckStatus::Fail : CheckStatus::Pass,
            report.scripts == 0 ? "No scripts in the scene."
                                : std::to_string(compiled) + "/" + std::to_string(report.scripts) + " scripts compile.");

    // Roblox APIs Kronos doesn't have.
    LuauApiCompatibility api;
    size_t missingApis = 0;
    for (core::EntityId e : scripts) {
        const std::string& source = ecs.tryGetComponent<core::Script>(e)->source;
        if (source.empty()) continue;
        const std::string path = pathOf(ecs, e);
        std::map<std::string, std::pair<size_t, ApiCompatibilityFinding>> uses;
        for (const ApiCompatibilityFinding& f : api.scan(source)) {
            if (f.status == ApiMappingStatus::Mapped) continue;
            auto& entry = uses[f.identifier];
            if (entry.first++ == 0) entry.second = f;
        }
        for (const auto& [identifier, entry] : uses) {
            const ApiCompatibilityFinding& f = entry.second;
            const bool missing = f.status == ApiMappingStatus::Unmapped;
            missingApis += missing ? 1 : 0;
            b.add("roblox-api", e,
                  {missing ? DoctorCategory::GameplayRisk : DoctorCategory::Info, missing ? "missing-api" : "shimmed-api",
                   identifier, path, f.line, entry.first,
                   missing ? identifier + " is not in Kronos yet." : identifier + " is rewritten to a Kronos call.",
                   missing ? "The script stops with an error when it reaches this line." : "It should work the same way.",
                   f.guidance});
        }
    }
    b.check("roblox-api", "Roblox APIs are available", missingApis ? CheckStatus::Warning : CheckStatus::Pass,
            missingApis ? std::to_string(missingApis) + " Roblox API(s) used that Kronos doesn't have yet."
                        : "Every Roblox API the scripts use is in Kronos (text scan).");

    // Object references.
    const std::vector<core::EntityId> infos = sortedView<core::InstanceInfo>(ecs);
    size_t weldCount = 0;
    for (core::EntityId e : infos) {
        const auto* info = ecs.tryGetComponent<core::InstanceInfo>(e);
        const std::string path = pathOf(ecs, e);
        const bool weld = info->className == "WeldConstraint";
        weldCount += weld ? 1 : 0;
        if (weld) {
            for (const char* name : {"Part0", "Part1"}) {
                const auto it = info->properties.find(name);
                if (it != info->properties.end() && it->second.type == core::InstanceValue::Type::Instance &&
                    it->second.ref != core::kNoInstance) {
                    continue;
                }
                b.add("references", e,
                      {DoctorCategory::GameplayRisk, "weld-incomplete", path + "." + name, {}, 0, 1,
                       std::string("WeldConstraint ") + name + " is empty.", "The weld holds nothing together.",
                       "Set Part0 and Part1 to the two parts to join."});
            }
        }
        for (const auto& [name, value] : info->properties) {
            if (value.type != core::InstanceValue::Type::Instance || value.ref == core::kNoInstance) continue;
            if (!core::instances::isAlive(ecs, value.ref)) {
                b.add("references", e,
                      {DoctorCategory::Blocker, "broken-reference", path + "." + name, {}, 0, 1,
                       name + " points to an object that no longer exists.",
                       "Scripts or joints that use it will get nil.", "Pick the object again in the Inspector."});
            }
        }
    }
    const size_t broken = b.countFor("references", DoctorCategory::Blocker);
    const size_t incomplete = b.countFor("references", DoctorCategory::GameplayRisk);
    b.check("references", "Object references point to real objects",
            broken ? CheckStatus::Fail : incomplete ? CheckStatus::Warning : CheckStatus::Pass,
            broken || incomplete ? std::to_string(broken) + " broken, " + std::to_string(incomplete) + " empty weld part(s)."
                                 : "No broken references.");

    // Asset files.
    size_t assetsChecked = 0;
    auto requireFile = [&](core::EntityId e, const std::string& what, const std::string& path) {
        ++assetsChecked;
        if (isRobloxHosted(path)) {
            b.add("assets", e,
                  {DoctorCategory::VisualAudio, "asset-unavailable", pathOf(ecs, e) + "." + what, {}, 0, 1,
                   path + " is a Roblox-hosted asset.", "Kronos never downloads Roblox assets, so it is missing.",
                   "Replace it with a local file."});
            return;
        }
        if (!path.empty() && fileExists(path, input.assetRoots)) return;
        b.add("assets", e,
              {DoctorCategory::Blocker, "missing-asset-file", pathOf(ecs, e) + "." + what, {}, 0, 1,
               path.empty() ? what + " has no file set." : "File not found: " + path,
               "It won't load when the game runs.", "Pick the file again, or put it back where it was."});
    };
    for (core::EntityId e : sortedView<core::AudioSource>(ecs)) {
        const std::string& path = ecs.tryGetComponent<core::AudioSource>(e)->path;
        if (!path.empty()) requireFile(e, "Sound", path);
    }
    for (core::EntityId e : sortedView<core::MeshSource>(ecs)) {
        const auto* mesh = ecs.tryGetComponent<core::MeshSource>(e);
        const bool fromFile = mesh->kind == core::MeshSourceKind::Obj || mesh->kind == core::MeshSourceKind::Gltf ||
                              mesh->kind == core::MeshSourceKind::Fbx;
        if (fromFile) requireFile(e, "Mesh", mesh->path);
    }
    for (core::EntityId e : sortedView<core::ColliderShape>(ecs)) {
        const auto* shape = ecs.tryGetComponent<core::ColliderShape>(e);
        if (shape->kind == core::ColliderShapeKind::Mesh) requireFile(e, "Collider", shape->path);
    }
    // Roblox asset ids left in imported properties, grouped per class and property.
    std::map<std::pair<std::string, std::string>, std::tuple<size_t, core::EntityId, std::string, std::string>> hosted;
    for (core::EntityId e : infos) {
        const auto* info = ecs.tryGetComponent<core::InstanceInfo>(e);
        for (const auto& [name, value] : info->properties) {
            if (value.type != core::InstanceValue::Type::String || !isRobloxHosted(value.text)) continue;
            auto& entry = hosted[{info->className, name}];
            if (std::get<0>(entry)++ == 0) entry = {1, e, pathOf(ecs, e), value.text};
        }
    }
    for (const auto& [key, entry] : hosted) {
        const auto& [count, entity, path, example] = entry;
        assetsChecked += count;
        b.add("assets", entity,
              {DoctorCategory::VisualAudio, "asset-unavailable", key.first + "." + key.second, {}, 0, count,
               std::to_string(count) + " " + key.first + "(s) use Roblox-hosted " + key.second + ", e.g. " + path +
                   " (" + example + ").",
               "Kronos never downloads Roblox assets, so these are missing.", "Replace them with local files."});
    }
    const size_t missingFiles = b.countFor("assets", DoctorCategory::Blocker);
    const size_t hostedCount = b.countFor("assets", DoctorCategory::VisualAudio);
    b.check("assets", "Asset files exist",
            missingFiles ? CheckStatus::Fail : hostedCount ? CheckStatus::Warning : CheckStatus::Pass,
            assetsChecked == 0 ? "The scene uses no asset files."
                               : std::to_string(missingFiles) + " missing file(s), " + std::to_string(hostedCount) +
                                     " Roblox-hosted asset group(s).");

    // Errors from the last Play.
    if (!input.lastPlayErrors) {
        b.check("last-play", "No script errors in the last Play", CheckStatus::NotRun,
                "Press Play, then validate again to include runtime errors.");
    } else {
        std::map<std::pair<std::string, std::string>, std::pair<size_t, int>> errors; // (script, message) -> count, line
        for (const std::string& line : *input.lastPlayErrors) {
            if (!isScriptErrorLine(line)) continue;
            std::string script;
            if (const size_t open = line.find("runtime error in \""); open != std::string::npos) {
                const size_t start = open + 18;
                script = line.substr(start, line.find('"', start) - start);
            }
            if (!script.empty() && script[0] == '=') script.erase(0, 1);
            auto& entry = errors[{script, line}];
            if (entry.first++ == 0) entry.second = script.empty() ? 0 : doctorLineFromError(line, script);
        }
        for (const auto& [key, entry] : errors) {
            const auto it = scriptByPath.find(key.first);
            b.add("last-play", it != scriptByPath.end() ? it->second : noEntity(),
                  {DoctorCategory::Blocker, "runtime-error", key.first.empty() ? "<unknown script>" : key.first,
                   key.first, entry.second, entry.first, key.second,
                   "The script stopped at this point during Play.", "docs/LUA_API.md"});
        }
        b.check("last-play", "No script errors in the last Play", errors.empty() ? CheckStatus::Pass : CheckStatus::Fail,
                errors.empty() ? "The last Play had no script errors."
                               : std::to_string(errors.size()) + " different script error(s) in the last Play.");
    }

    // Things the scene file can't keep yet.
    if (weldCount > 0) {
        b.add("saving", noEntity(),
              {DoctorCategory::GameplayRisk, "welds-not-saved", "WeldConstraint", {}, 0, weldCount,
               std::to_string(weldCount) + " WeldConstraint(s) are not saved in Kronos scenes yet.",
               "They are lost when the scene is saved and opened again.",
               "Keep the Roblox file and import it again, or anchor the parts."});
    }
    b.check("saving", "Everything in the scene can be saved", weldCount ? CheckStatus::Warning : CheckStatus::Pass,
            weldCount ? "WeldConstraints are not saved yet." : "Nothing known to be lost on save.");

    b.check("multiplayer", "Works with more than one player", CheckStatus::Unavailable,
            "Studio can't run a server with several players yet.");
    b.check("packaging", "Game packages and starts in the Player", CheckStatus::Unavailable,
            "Not checked here; publish and play it to test.");

    std::sort(report.findings.begin(), report.findings.end(), [](const ProjectFinding& a, const ProjectFinding& c) {
        const DoctorFinding& x = a.finding;
        const DoctorFinding& y = c.finding;
        return std::tie(x.category, x.code, x.script, x.subject, x.line, x.message) <
               std::tie(y.category, y.code, y.script, y.subject, y.line, y.message);
    });
    return report;
}

ValidationDiff compareValidations(const ValidationReport& previous, const ValidationReport& current) {
    std::set<std::string> before;
    std::set<std::string> after;
    for (const ProjectFinding& f : previous.findings) before.insert(f.stableId());
    for (const ProjectFinding& f : current.findings) after.insert(f.stableId());
    ValidationDiff diff;
    for (const std::string& id : after) {
        if (before.count(id)) {
            ++diff.unchanged;
        } else {
            diff.added.push_back(id);
        }
    }
    for (const std::string& id : before) {
        if (!after.count(id)) diff.resolved.push_back(id);
    }
    return diff;
}

std::string validationJson(const ValidationReport& report) {
    std::string out = "{\n  \"tool\": \"Kronos Validate Project\",\n  \"schema\": 1,\n";
    out += "  \"kronosVersion\": " + q(report.kronosVersion) + ",\n";
    out += "  \"overall\": " + q(checkStatusName(report.overall())) + ",\n";
    out += "  \"instances\": " + std::to_string(report.instances) + ",\n  \"scripts\": " +
           std::to_string(report.scripts) + ",\n  \"checks\": [";
    for (size_t i = 0; i < report.checks.size(); ++i) {
        const ProjectCheck& c = report.checks[i];
        out += (i ? ",\n    {" : "\n    {") + std::string("\"id\": ") + q(c.id) + ", \"title\": " + q(c.title) +
               ", \"status\": " + q(checkStatusName(c.status)) + ", \"detail\": " + q(c.detail) + "}";
    }
    out += report.checks.empty() ? "],\n  \"findings\": [" : "\n  ],\n  \"findings\": [";
    for (size_t i = 0; i < report.findings.size(); ++i) {
        const ProjectFinding& p = report.findings[i];
        const DoctorFinding& f = p.finding;
        out += i ? ",\n    {" : "\n    {";
        out += "\"id\": " + q(p.stableId()) + ", \"check\": " + q(p.checkId) + ", \"category\": " +
               q(doctorCategoryName(f.category)) + ", \"code\": " + q(f.code) + ", \"subject\": " + q(f.subject) +
               ", \"script\": " + (f.script.empty() ? "null" : q(f.script)) +
               ", \"line\": " + (f.line > 0 ? std::to_string(f.line) : "null") + ", \"count\": " +
               std::to_string(f.count) + ", \"message\": " + q(f.message) + ", \"why\": " + q(f.why) +
               ", \"help\": " + q(f.help) + "}";
    }
    out += report.findings.empty() ? "]\n}\n" : "\n  ]\n}\n";
    return out;
}

std::string validationMarkdown(const ValidationReport& report) {
    std::string out = "# Kronos Validate Project report\n\n";
    out += "Kronos " + report.kronosVersion + ". Overall: **" + checkStatusName(report.overall()) + "**. " +
           std::to_string(report.instances) + " objects, " + std::to_string(report.scripts) +
           " scripts. This report doesn't contain script source.\n\n";
    out += "| Check | Status | Detail |\n|---|---|---|\n";
    for (const ProjectCheck& c : report.checks) {
        out += "| " + doctorMarkdownCell(c.title) + " | " + checkStatusName(c.status) + " | " +
               doctorMarkdownCell(c.detail) + " |\n";
    }
    for (int c = 0; c <= 3; ++c) {
        const auto category = static_cast<DoctorCategory>(c);
        bool header = false;
        for (const ProjectFinding& p : report.findings) {
            const DoctorFinding& f = p.finding;
            if (f.category != category) continue;
            if (!header) {
                out += std::string("\n## ") + doctorCategoryName(category) + "\n\n";
                header = true;
            }
            out += "- `" + f.code + "` " + doctorMarkdownCell(f.subject);
            if (f.line > 0) out += " (line " + std::to_string(f.line) + ")";
            if (f.count > 1) out += " x" + std::to_string(f.count);
            out += ": " + doctorMarkdownCell(f.message) + " " + doctorMarkdownCell(f.why) + " Fix: " +
                   doctorMarkdownCell(f.help) + "\n";
        }
    }
    out += "\n## Not checked\n\n- Gameplay itself (only Play and behaviour checks test that).\n"
           "- Code that never ran during the last Play.\n- Multiplayer and packaging (see the checks above).\n";
    return out;
}

} // namespace engine::migration
