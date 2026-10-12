#include "migration/CompatLab.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <set>

#include <lua.h>
#include <lualib.h>
#include <nlohmann/json.hpp>

#include "core/Camera.hpp"
#include "core/CharacterController.hpp"
#include "core/Components.hpp"
#include "core/InstanceSignals.hpp"
#include "core/InstanceTree.hpp"
#include "core/KronosVersion.hpp"
#include "core/PartBodies.hpp"
#include "core/Physics.hpp"
#include "core/PlayerAvatar.hpp"
#include "core/RobloxGui.hpp"
#include "core/RobloxPlayers.hpp"
#include "core/RobloxScripts.hpp"
#include "core/ScriptInstanceApi.hpp"
#include "core/Scripting.hpp"
#include "core/UnionGeometry.hpp"
#include "migration/BridgeDoctor.hpp"
#include "migration/InstanceHydrator.hpp"
#include "platform_adapters/UnifiedInput.hpp"
#include "safety/IPInfringementScanner.hpp"

namespace engine::migration {
namespace {

using Json = nlohmann::ordered_json;

constexpr const char* kChecksChunk = "LabChecks";
constexpr const char* kCheckPrefix = "@@kronos-lab-check@@";

int labTouch(lua_State* L) {
    auto* ecs = static_cast<core::ECS*>(lua_tolightuserdata(L, lua_upvalueindex(1)));
    const core::EntityId a = core::instances::entityOf(*ecs, core::toSignalArg(L, 1).value.ref);
    const core::EntityId b = core::instances::entityOf(*ecs, core::toSignalArg(L, 2).value.ref);
    if (a == core::kNullEntity || b == core::kNullEntity) luaL_error(L, "touch expects two parts");
    core::signals::touch(*ecs, a, b, true);
    core::signals::touch(*ecs, a, b, false);
    return 0;
}

// click(target, player): a ClickDetector (or the part holding one) or a GUI button.
int labClick(lua_State* L) {
    auto* ecs = static_cast<core::ECS*>(lua_tolightuserdata(L, lua_upvalueindex(1)));
    const core::InstanceRef target = core::toSignalArg(L, 1).value.ref;
    core::InstanceRef player = lua_gettop(L) >= 2 ? core::toSignalArg(L, 2).value.ref : core::kNoInstance;
    core::SignalHub* hub = core::signals::findHub(*ecs);
    if (hub == nullptr || target == core::kNoInstance || !core::instances::isAlive(*ecs, target)) {
        luaL_error(L, "click expects a part with a ClickDetector or a GUI button");
    }
    const std::string cls = core::instances::className(*ecs, target);
    if (core::instances::classIsA(cls, "GuiButton")) {
        hub->fire(target, "MouseButton1Click");
        hub->fire(target, "Activated");
        return 0;
    }
    core::InstanceRef detector = cls == "ClickDetector" ? target : core::kNoInstance;
    for (core::InstanceRef child : core::instances::children(*ecs, target)) {
        if (detector == core::kNoInstance && core::instances::className(*ecs, child) == "ClickDetector") detector = child;
    }
    if (detector == core::kNoInstance) luaL_error(L, "click: %s has no ClickDetector", cls.c_str());
    if (player == core::kNoInstance) {
        const auto all = core::players::list(*ecs);
        if (!all.empty()) player = all.front();
    }
    hub->fire(detector, "MouseClick", {core::SignalArg::of(core::InstanceValue::ofInstance(player))});
    return 0;
}

double numberProperty(core::ECS& ecs, core::InstanceRef ref, const char* name) {
    const core::PropertyDef* def = core::instances::findProperty(core::instances::className(ecs, ref), name);
    core::InstanceValue value;
    return def != nullptr && core::instances::getProperty(ecs, ref, *def, value) ? value.number : 0.0;
}

size_t instanceCount(core::ECS& ecs) { return ecs.raw().view<core::InstanceInfo>().size(); }

struct TestPlayer {
    std::string name;
    std::unique_ptr<core::CharacterController> controller;
    platform_adapters::UnifiedInput input;
    core::Camera camera;
    core::InstanceRef player = core::kNoInstance;
    bool wasDead = false;
    bool wasBelow = false;
    LabPlayer stats;
};

struct ParsedError {
    std::string chunk;
    int line = 0;
    std::string message;
};

// Reads `runtime error in "Chunk": ...` and, from resumed threads,
// `runtime error in task.wait: [string "Chunk"]:4: ...`.
ParsedError parseScriptError(const std::string& text) {
    ParsedError out;
    std::string rest = text;
    if (const size_t in = text.find(" in \""); in != std::string::npos && in < 20) {
        const size_t close = text.find('"', in + 5);
        if (close != std::string::npos) {
            out.chunk = text.substr(in + 5, close - in - 5);
            rest = text.compare(close + 1, 2, ": ") == 0 ? text.substr(close + 3) : text.substr(close + 1);
        }
    } else if (const size_t colon = text.find(": "); colon != std::string::npos) {
        rest = text.substr(colon + 2);
    }
    // Luau's own location prefix: [string "Chunk"]:12: or Chunk:12:
    std::string where;
    size_t after = std::string::npos;
    if (rest.rfind("[string \"", 0) == 0) {
        const size_t end = rest.find("\"]:");
        if (end != std::string::npos) {
            where = rest.substr(9, end - 9);
            after = end + 3;
        }
    } else if (!out.chunk.empty() && rest.rfind(out.chunk + ":", 0) == 0) {
        where = out.chunk;
        after = out.chunk.size() + 1;
    }
    if (after != std::string::npos) {
        size_t i = after;
        int line = 0;
        while (i < rest.size() && i - after < 7 && std::isdigit(static_cast<unsigned char>(rest[i]))) line = line * 10 + (rest[i++] - '0');
        if (i > after && i < rest.size() && rest[i] == ':') {
            out.line = line;
            if (out.chunk.empty()) out.chunk = where;
            rest = rest.substr(i + 1);
            if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
        }
    }
    out.message = rest;
    return out;
}

// Copies made for a player count as the script they came from.
std::string originalScript(const std::string& path, const std::vector<std::string>& playerNames) {
    for (const std::string& player : playerNames) {
        const std::pair<std::string, std::string> copies[] = {
            {"Players." + player + ".PlayerGui.", "StarterGui."},
            {"Players." + player + ".PlayerScripts.", "StarterPlayer.StarterPlayerScripts."},
            {"Players." + player + ".Backpack.", "StarterPack."},
            {"Workspace." + player + ".", "StarterPlayer.StarterCharacterScripts."},
        };
        for (const auto& [copy, from] : copies) {
            if (path.rfind(copy, 0) == 0) return from + path.substr(copy.size());
        }
    }
    return path;
}

} // namespace

size_t LabResult::behaviourPassed() const {
    return static_cast<size_t>(std::count_if(behaviour.begin(), behaviour.end(), [](const auto& c) { return c.passed; }));
}

LabResult runLab(const ImportReport& report, const LabOptions& options, const std::string& checks) {
    LabResult result;
    core::ECS ecs;
    (void)InstanceHydrator{}.hydrate(report.tree, ecs, HydrationMeshes{});
    result.instancesStart = instanceCount(ecs);

    core::Physics physics;
    if (!physics.initialize(1)) return result;
    physics.setTouchRecording(true);
    core::partbodies::setPhysics(ecs, &physics);

    core::Scripting scripting;
    scripting.setBindingsHook([&ecs](lua_State* L) {
        core::registerInstanceApi(L, ecs);
        lua_pushlightuserdata(L, &ecs);
        lua_pushcclosure(L, &labTouch, "__lab_touch", 1);
        lua_setglobal(L, "__lab_touch");
        lua_pushlightuserdata(L, &ecs);
        lua_pushcclosure(L, &labClick, "__lab_click", 1);
        lua_setglobal(L, "__lab_click");
    });
    if (!scripting.initialize()) return result;

    double now = 0.0;
    std::vector<std::string> playerNames;
    for (int i = 0; i < options.players; ++i) playerNames.push_back("Player" + std::to_string(i + 1));
    std::map<std::pair<std::string, std::string>, size_t> errorIndex;
    std::vector<std::string> checkLines;
    scripting.setOutputCallback([&](const std::string& line) {
        if (const size_t at = line.find(kCheckPrefix); at != std::string::npos) {
            checkLines.push_back(line.substr(at + std::string(kCheckPrefix).size()));
            return;
        }
        if (line.rfind("runtime error", 0) != 0 && line.rfind("compile error", 0) != 0) return;
        const ParsedError parsed = parseScriptError(line);
        const std::string script = parsed.chunk == kChecksChunk ? parsed.chunk : originalScript(parsed.chunk, playerNames);
        const std::string message = (line.rfind("compile", 0) == 0 ? "compile error: " : "") + parsed.message;
        const auto key = std::make_pair(script, message);
        if (const auto found = errorIndex.find(key); found != errorIndex.end()) {
            ++result.errors[found->second].count;
            return;
        }
        errorIndex[key] = result.errors.size();
        result.errors.push_back({now, script, parsed.line, message, 1});
    });

    const auto frameStart = [&](float dt) {
        core::robloxScripts::tick(ecs, scripting);
        core::signals::renderStepped(ecs, dt);
        core::signals::flush(ecs);
        core::signals::stepped(ecs, dt);
        scripting.tick(dt);
        core::players::tick(ecs, dt);
        core::signals::flush(ecs);
    };

    std::vector<TestPlayer> testPlayers(static_cast<size_t>(std::max(0, options.players)));
    const float dt = std::max(options.dt, 1.0f / 240.0f);
    frameStart(dt); // server scripts start before anyone joins
    const glm::vec3 spawn = core::findPlayerSpawnPosition(ecs, glm::vec3(0.0f, 5.0f, 0.0f));
    for (size_t i = 0; i < testPlayers.size(); ++i) {
        TestPlayer& p = testPlayers[i];
        p.name = playerNames[i];
        p.stats.name = p.name;
        p.controller = std::make_unique<core::CharacterController>();
        const core::EntityId root =
            p.controller->spawn(ecs, physics, spawn + glm::vec3(0.0f, 0.0f, 3.0f * static_cast<float>(i)));
        p.player = core::players::join(ecs, p.name, static_cast<int64_t>(i + 1), root, i == 0);
    }

    const double fallenHeight = numberProperty(ecs, core::kWorkspaceInstance, "FallenPartsDestroyHeight");
    const int frames = static_cast<int>(options.seconds / dt + 0.5);
    const int checksFrame = static_cast<int>(options.checksAfter / dt + 0.5);
    for (int frame = 0; frame < frames; ++frame) {
        if (frame == checksFrame && !checks.empty()) {
            result.behaviourRan = true;
            const std::string prelude = std::string(R"LUAU(
local touch = __lab_touch
local click = __lab_click
local players = game:GetService("Players"):GetPlayers()
local player = players[1]
local character = player and player.Character
local hit = character and (character:FindFirstChild("HumanoidRootPart") or character:FindFirstChildWhichIsA("BasePart"))
local seconds = )LUAU") + std::to_string(options.seconds) + R"LUAU(
local function check(name, ok, detail)
	print(")LUAU" + kCheckPrefix + R"LUAU(" .. (ok and "1" or "0") .. tostring(name) .. "\t" .. tostring(detail or ""))
end
)LUAU";
            scripting.loadAndRun(kChecksChunk, prelude + checks);
        }

        const auto started = std::chrono::steady_clock::now();
        frameStart(dt);
        const double scriptMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        result.slowestFrameMs = std::max(result.slowestFrameMs, scriptMs);

        for (TestPlayer& p : testPlayers) {
            p.controller->tick(dt, ecs, physics, p.input, p.camera);
        }
        core::partbodies::sync(ecs, physics, false);
        physics.step(dt, ecs);
        for (const auto& touch : physics.drainTouchEvents()) {
            if (touch.began) ++result.touches;
            core::signals::touch(ecs, touch.a, touch.b, touch.began);
        }
        core::signals::heartbeat(ecs, dt);
        core::gui::update(ecs, glm::vec2(1280.0f, 720.0f), {});
        core::signals::flush(ecs);
        now += dt;

        for (TestPlayer& p : testPlayers) {
            const core::EntityId root = p.controller->entity();
            const core::EntityId humanoid = core::players::humanoidFor(ecs, root);
            const bool dead = humanoid != core::kNullEntity &&
                              numberProperty(ecs, core::instances::refOf(ecs, humanoid), "Health") <= 0.0;
            if (dead && !p.wasDead) ++p.stats.deaths;
            p.wasDead = dead;
            const bool below = core::instances::worldPose(ecs, root).position.y < fallenHeight;
            if (below && !p.wasBelow) ++p.stats.fellOut;
            p.wasBelow = below;
        }
    }

    for (TestPlayer& p : testPlayers) {
        p.stats.character = core::players::humanoidFor(ecs, p.controller->entity()) != core::kNullEntity;
        result.players.push_back(p.stats);
    }
    std::set<core::EntityId> characterParts;
    for (const TestPlayer& p : testPlayers) characterParts.insert(p.controller->entity());
    for (core::EntityId e : ecs.raw().view<core::InstanceInfo>()) {
        const auto& info = ecs.raw().get<core::InstanceInfo>(e);
        if (characterParts.count(e) != 0 || ecs.tryGetComponent<core::unions::UnionPiece>(e) != nullptr ||
            !core::instances::classIsA(info.className, "BasePart")) {
            continue;
        }
        const auto anchored = info.properties.find("Anchored");
        if (anchored != info.properties.end() && anchored->second.boolean) continue;
        const core::InstanceRef ref = core::instances::refOf(ecs, e);
        if (core::players::playerFromCharacter(ecs, core::instances::parent(ecs, ref)) != core::kNoInstance) continue;
        if (core::instances::worldPose(ecs, e).position.y < fallenHeight) {
            result.fallenParts.push_back(core::instances::fullName(ecs, ref));
        }
    }
    std::sort(result.fallenParts.begin(), result.fallenParts.end());
    result.missingImages = core::gui::missingImages(ecs, options.imageRoots);

    for (const std::string& line : checkLines) {
        const size_t tab = line.find('\t');
        CompatBehaviourCheck check;
        check.passed = !line.empty() && line[0] == '1';
        check.name = line.substr(1, tab == std::string::npos ? std::string::npos : tab - 1);
        if (tab != std::string::npos) check.detail = line.substr(tab + 1);
        result.behaviour.push_back(std::move(check));
    }
    for (const LabError& error : result.errors) {
        if (error.script == kChecksChunk) result.behaviour.push_back({"checks file ran to the end", false, error.message});
    }
    result.errors.erase(std::remove_if(result.errors.begin(), result.errors.end(),
                                       [](const LabError& e) { return e.script == kChecksChunk; }),
                        result.errors.end());

    for (TestPlayer& p : testPlayers) {
        if (core::instances::isAlive(ecs, p.player)) core::players::leave(ecs, p.player);
    }
    core::signals::flush(ecs);
    result.instancesEnd = instanceCount(ecs);
    core::partbodies::detachAll(ecs, physics);
    scripting.shutdown();
    result.seconds = now;
    result.ran = true;
    return result;
}

LabPlace labPlace(const std::string& fileName, const std::string& bytes, const LabOptions& options,
                  const std::string& checks) {
    LabPlace place;
    place.file = fileName;
    const safety::IPInfringementScanner scanner;
    const ImportReport report = ProjectImporter{}.importDocument(bytes, scanner);
    place.imported = report.parsed && !report.blocked;
    if (!place.imported) return place;
    CompatibilityScore score = scoreImport(report);
    runImportedScripts(report, score);
    place.score = score.overallPercent();
    place.classesSupported = score.instancesMapped;
    place.classesTotal = score.instances;
    place.result = runLab(report, options, checks);
    return place;
}

namespace {

Json placeJson(const LabPlace& place) {
    const LabResult& r = place.result;
    Json out;
    out["file"] = place.file;
    out["imported"] = place.imported;
    out["crashed"] = place.crashed;
    if (place.crashed) out["crash"] = place.crash;
    out["score"] = std::round(place.score * 10.0) / 10.0;
    out["classes"] = {{"supported", place.classesSupported}, {"total", place.classesTotal}};
    out["ran"] = r.ran;
    out["seconds"] = std::round(r.seconds * 100.0) / 100.0;
    Json errors = Json::array();
    for (const LabError& e : r.errors) {
        errors.push_back({{"time", std::round(e.time * 100.0) / 100.0},
                          {"script", e.script},
                          {"line", e.line},
                          {"message", e.message},
                          {"count", e.count}});
    }
    out["errors"] = std::move(errors);
    Json players = Json::array();
    for (const LabPlayer& p : r.players) {
        players.push_back({{"name", p.name}, {"character", p.character}, {"deaths", p.deaths}, {"fellOut", p.fellOut}});
    }
    out["players"] = std::move(players);
    out["fallenParts"] = r.fallenParts;
    out["missingImages"] = r.missingImages;
    out["touches"] = r.touches;
    out["instances"] = {{"start", r.instancesStart}, {"end", r.instancesEnd}};
    out["slowestScriptFrameMs"] = std::round(r.slowestFrameMs * 100.0) / 100.0;
    Json checks = Json::array();
    for (const CompatBehaviourCheck& c : r.behaviour) {
        checks.push_back({{"name", c.name}, {"passed", c.passed}, {"detail", c.detail}});
    }
    out["behaviour"] = {{"ran", r.behaviourRan}, {"checks", std::move(checks)}};
    return out;
}

std::string cell(const std::string& text) { return doctorMarkdownCell(text); }

} // namespace

std::string labReportJson(const std::vector<LabPlace>& places, const LabOptions& options) {
    Json out;
    out["tool"] = "Kronos Compatibility Lab";
    out["schema"] = 1;
    out["kronosVersion"] = core::kKronosVersion;
    out["options"] = {{"seconds", options.seconds}, {"players", options.players}, {"dt", options.dt}};
    Json list = Json::array();
    for (const LabPlace& place : places) list.push_back(placeJson(place));
    out["places"] = std::move(list);
    return out.dump(2) + "\n";
}

bool readLabReport(const std::string& json, std::vector<LabPlace>& places, std::string& error) {
    const Json doc = Json::parse(json, nullptr, false);
    if (doc.is_discarded() || !doc.is_object() || !doc.contains("places") || !doc["places"].is_array()) {
        error = "not a Compatibility Lab report";
        return false;
    }
    try {
        for (const Json& p : doc["places"]) {
            LabPlace place;
            place.file = p.value("file", "");
            place.imported = p.value("imported", false);
            place.crashed = p.value("crashed", false);
            place.crash = p.value("crash", "");
            place.score = p.value("score", 0.0);
            if (p.contains("classes")) {
                place.classesSupported = p["classes"].value("supported", size_t{0});
                place.classesTotal = p["classes"].value("total", size_t{0});
            }
            LabResult& r = place.result;
            r.ran = p.value("ran", false);
            r.seconds = p.value("seconds", 0.0);
            for (const Json& e : p.value("errors", Json::array())) {
                r.errors.push_back({e.value("time", 0.0), e.value("script", ""), e.value("line", 0),
                                    e.value("message", ""), e.value("count", size_t{1})});
            }
            for (const Json& pl : p.value("players", Json::array())) {
                r.players.push_back({pl.value("name", ""), pl.value("character", false), pl.value("deaths", 0),
                                     pl.value("fellOut", 0)});
            }
            r.fallenParts = p.value("fallenParts", std::vector<std::string>{});
            r.missingImages = p.value("missingImages", std::vector<std::string>{});
            r.touches = p.value("touches", size_t{0});
            if (p.contains("instances")) {
                r.instancesStart = p["instances"].value("start", size_t{0});
                r.instancesEnd = p["instances"].value("end", size_t{0});
            }
            r.slowestFrameMs = p.value("slowestScriptFrameMs", 0.0);
            if (p.contains("behaviour")) {
                r.behaviourRan = p["behaviour"].value("ran", false);
                for (const Json& c : p["behaviour"].value("checks", Json::array())) {
                    r.behaviour.push_back({c.value("name", ""), c.value("passed", false), c.value("detail", "")});
                }
            }
            places.push_back(std::move(place));
        }
    } catch (const std::exception& e) {
        error = std::string("bad report: ") + e.what();
        return false;
    }
    return true;
}

std::string labReportMarkdown(const std::vector<LabPlace>& places, const LabOptions& options) {
    char header[256];
    std::snprintf(header, sizeof(header),
                  "# Compatibility Lab\n\nKronos %s. Each place played for %.0f game seconds with %d players, "
                  "physics on.\n\n",
                  core::kKronosVersion, options.seconds, options.players);
    std::string out = header;
    out += "| Place | Score | Ran | Errors | Checks | Deaths | Fell out | Parts fell |\n";
    out += "|---|---|---|---|---|---|---|---|\n";
    for (const LabPlace& place : places) {
        const LabResult& r = place.result;
        int deaths = 0;
        int fell = 0;
        for (const LabPlayer& p : r.players) {
            deaths += p.deaths;
            fell += p.fellOut;
        }
        char row[512];
        std::snprintf(row, sizeof(row), "| %s | %.0f%% | %s | %zu | %s | %d | %d | %zu |\n", cell(place.file).c_str(),
                      place.score,
                      place.crashed ? ("crashed (" + place.crash + ")").c_str() : !place.imported ? "not imported"
                                                                                                   : "yes",
                      r.errors.size(),
                      r.behaviourRan ? (std::to_string(r.behaviourPassed()) + "/" + std::to_string(r.behaviour.size())).c_str()
                                     : "none",
                      deaths, fell, r.fallenParts.size());
        out += row;
    }
    for (const LabPlace& place : places) {
        const LabResult& r = place.result;
        if (r.errors.empty() && r.fallenParts.empty() && r.missingImages.empty() &&
            r.behaviourPassed() == r.behaviour.size()) {
            continue;
        }
        out += "\n## " + place.file + "\n\n";
        for (const LabError& e : r.errors) {
            char line[64];
            std::snprintf(line, sizeof(line), "%.1f s", e.time);
            out += "- **Error** at " + std::string(line) + " in `" + e.script + "`" +
                   (e.line > 0 ? " line " + std::to_string(e.line) : "") +
                   (e.count > 1 ? " (" + std::to_string(e.count) + " times)" : "") + ": " + cell(e.message) + "\n";
        }
        for (const CompatBehaviourCheck& c : r.behaviour) {
            if (!c.passed) out += "- **Check failed**: " + cell(c.name) + (c.detail.empty() ? "" : " (" + cell(c.detail) + ")") + "\n";
        }
        if (!r.fallenParts.empty()) {
            out += "- **" + std::to_string(r.fallenParts.size()) +
                   " unanchored parts fell out of the world** (missing Anchored or welds?): ";
            for (size_t i = 0; i < r.fallenParts.size() && i < 10; ++i) out += (i ? ", `" : "`") + r.fallenParts[i] + "`";
            out += r.fallenParts.size() > 10 ? ", ...\n" : "\n";
        }
        if (!r.missingImages.empty()) {
            out += "- **" + std::to_string(r.missingImages.size()) +
                   " GUI images not found** (put them in `assets/` or list them in `asset_map.json`): ";
            for (size_t i = 0; i < r.missingImages.size() && i < 10; ++i) out += (i ? ", `" : "`") + r.missingImages[i] + "`";
            out += r.missingImages.size() > 10 ? ", ...\n" : "\n";
        }
    }
    out += "\nLimits: players stand still unless a checks file moves them; nothing is drawn (GUI is laid out); "
           "server and clients share one session.\n";
    return out;
}

std::vector<LabChange> compareLab(const std::vector<LabPlace>& baseline, const std::vector<LabPlace>& now) {
    std::vector<LabChange> changes;
    auto add = [&](const std::string& file, bool worse, std::string what) { changes.push_back({file, worse, std::move(what)}); };
    for (const LabPlace& place : now) {
        const auto before = std::find_if(baseline.begin(), baseline.end(), [&](const LabPlace& b) { return b.file == place.file; });
        if (before == baseline.end()) {
            add(place.file, false, "new place");
            continue;
        }
        if (place.crashed && !before->crashed) add(place.file, true, "crashes now (" + place.crash + ")");
        if (!place.crashed && before->crashed) add(place.file, false, "no longer crashes");
        if (place.crashed || before->crashed) continue;
        char text[128];
        if (place.score < before->score - 0.05) {
            std::snprintf(text, sizeof(text), "score fell from %.1f%% to %.1f%%", before->score, place.score);
            add(place.file, true, text);
        } else if (place.score > before->score + 0.05) {
            std::snprintf(text, sizeof(text), "score rose from %.1f%% to %.1f%%", before->score, place.score);
            add(place.file, false, text);
        }
        std::map<std::string, bool> oldChecks;
        for (const auto& c : before->result.behaviour) oldChecks[c.name] = c.passed;
        for (const auto& c : place.result.behaviour) {
            const auto old = oldChecks.find(c.name);
            if (old == oldChecks.end()) continue;
            if (old->second && !c.passed) add(place.file, true, "check now fails: " + c.name);
            if (!old->second && c.passed) add(place.file, false, "check now passes: " + c.name);
        }
        std::set<std::pair<std::string, std::string>> oldErrors;
        std::set<std::pair<std::string, std::string>> newErrors;
        for (const LabError& e : before->result.errors) oldErrors.insert({e.script, e.message});
        for (const LabError& e : place.result.errors) newErrors.insert({e.script, e.message});
        for (const auto& [script, message] : newErrors) {
            if (oldErrors.count({script, message}) == 0) add(place.file, true, "new error in " + script + ": " + message);
        }
        for (const auto& [script, message] : oldErrors) {
            if (newErrors.count({script, message}) == 0) add(place.file, false, "error gone in " + script + ": " + message);
        }
        if (place.result.fallenParts.size() > before->result.fallenParts.size()) {
            add(place.file, true, std::to_string(place.result.fallenParts.size()) + " parts fall out of the world (was " +
                                      std::to_string(before->result.fallenParts.size()) + ")");
        } else if (place.result.fallenParts.size() < before->result.fallenParts.size()) {
            add(place.file, false, "fewer parts fall out of the world (" + std::to_string(place.result.fallenParts.size()) +
                                       ", was " + std::to_string(before->result.fallenParts.size()) + ")");
        }
    }
    for (const LabPlace& old : baseline) {
        if (std::none_of(now.begin(), now.end(), [&](const LabPlace& p) { return p.file == old.file; })) {
            add(old.file, false, "not in this run");
        }
    }
    std::stable_sort(changes.begin(), changes.end(), [](const LabChange& a, const LabChange& b) { return a.worse > b.worse; });
    return changes;
}

} // namespace engine::migration
