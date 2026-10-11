#include "migration/BridgeDoctor.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <tuple>

#include "core/InstanceTree.hpp"
#include "core/KronosVersion.hpp"
#include "migration/InstanceHydrator.hpp"
#include "migration/LuauApiCompatibility.hpp"
#include "safety/IPInfringementScanner.hpp"

namespace engine::migration {
namespace {

constexpr const char* kBridgeDoc = "docs/ROBLOX_BRIDGE.md";

bool isScriptClass(const std::string& cls) {
    return cls == "Script" || cls == "LocalScript" || cls == "ModuleScript";
}

void visit(const std::vector<ImportedInstance>& nodes, const std::string& parentPath,
           const std::function<void(const ImportedInstance&, const std::string&)>& fn) {
    for (const ImportedInstance& node : nodes) {
        const std::string path = parentPath.empty() ? node.name : parentPath + "." + node.name;
        fn(node, path);
        visit(node.children, path, fn);
    }
}

// Classes that only change how a place looks or sounds.
bool isVisualClass(const std::string& cls) {
    static const std::set<std::string> kVisual = {
        "Decal", "Texture", "SurfaceAppearance", "SpecialMesh", "BlockMesh", "CylinderMesh", "ParticleEmitter",
        "Beam", "Trail", "Fire", "Smoke", "Sparkles", "Explosion", "Highlight", "SelectionBox", "Sky", "Atmosphere",
        "Clouds", "BloomEffect", "BlurEffect", "ColorCorrectionEffect", "DepthOfFieldEffect", "SunRaysEffect",
        "ImageLabel", "ImageButton", "ViewportFrame", "UIGradient", "UIStroke", "UICorner", "SoundGroup",
        "ReverbSoundEffect", "EchoSoundEffect", "EqualizerSoundEffect", "ChorusSoundEffect", "Animation"};
    return kVisual.count(cls) != 0;
}

bool hasScripts(const ImportedInstance& node) {
    if (isScriptClass(node.className)) return true;
    return std::any_of(node.children.begin(), node.children.end(), [](const ImportedInstance& c) { return hasScripts(c); });
}

// Roblox saves terrain voxels in SmoothGrid; an empty grid is a few bytes.
bool isEmptyTerrain(const ImportedInstance& node) {
    if (node.className != "Terrain") return false;
    const auto grid = node.properties.find("SmoothGrid");
    return grid == node.properties.end() || grid->second.size() <= 64;
}

// Raw property names the hydrator reads under another spelling.
bool isAliasProperty(const std::string& base) {
    // The last six are Roblox Studio bookkeeping saved on every instance.
    static const std::set<std::string> kRead = {"Name", "Source", "Tags", "Color3uint8", "size", "shape", "SoundId",
                                                "Capabilities", "DefinesCapabilities", "HistoryId",
                                                "SerializedOverrides", "SourceAssetId", "UniqueId"};
    return kRead.count(base) != 0;
}

bool classKnowsProperty(const std::string& cls, const std::string& base) {
    if (core::instances::findProperty(cls, base) != nullptr) return true;
    std::string lower = base;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
    const core::ClassDef* def = core::instances::findClass(cls);
    for (; def != nullptr; def = def->superclass.empty() ? nullptr : core::instances::findClass(def->superclass)) {
        for (const core::PropertyDef& property : def->properties) {
            std::string name = property.name;
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
            if (name == lower) return true;
        }
    }
    return false;
}

// The line in "runtime error in "X": ...X:12: message", only when the engine wrote one.
int lineFromError(const std::string& error, const std::string& script) {
    const std::string leaf = script.substr(script.rfind('.') == std::string::npos ? 0 : script.rfind('.') + 1);
    const size_t quote = error.find("\": ");
    if (quote == std::string::npos || leaf.empty()) return 0;
    // Luau writes either `Name:12:` or `[string "Path.Name"]:12:`.
    const std::string rest = error.substr(quote + 3);
    size_t i = std::string::npos;
    if (const size_t at = rest.find(leaf + "\"]:"); at != std::string::npos && at < 300) {
        i = at + leaf.size() + 3;
    } else if (const size_t plain = rest.find(leaf + ":"); plain != std::string::npos && plain < 64) {
        i = plain + leaf.size() + 1;
    }
    if (i == std::string::npos) return 0;
    int line = 0;
    size_t digits = 0;
    while (i < rest.size() && std::isdigit(static_cast<unsigned char>(rest[i])) && digits < 7) {
        line = line * 10 + (rest[i++] - '0');
        ++digits;
    }
    return digits > 0 && i < rest.size() && rest[i] == ':' ? line : 0;
}

std::string jsonEscape(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 2);
    for (unsigned char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                    out += buffer;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

std::string q(const std::string& text) { return "\"" + jsonEscape(text) + "\""; }

std::string mdCell(std::string text) {
    for (char& c : text) {
        if (c == '\n' || c == '\r') c = ' ';
    }
    std::string out;
    for (char c : text) {
        if (c == '|') out += '\\';
        out += c;
    }
    return out;
}

std::string ratio(size_t part, size_t whole) { return std::to_string(part) + "/" + std::to_string(whole); }

void sortFindings(std::vector<DoctorFinding>& findings) {
    std::sort(findings.begin(), findings.end(), [](const DoctorFinding& a, const DoctorFinding& b) {
        return std::tie(a.category, a.code, a.script, a.subject, a.line, a.message) <
               std::tie(b.category, b.code, b.script, b.subject, b.line, b.message);
    });
}

} // namespace

std::string doctorJsonEscape(const std::string& text) { return jsonEscape(text); }
std::string doctorMarkdownCell(const std::string& text) { return mdCell(text); }
int doctorLineFromError(const std::string& error, const std::string& script) { return lineFromError(error, script); }

const char* doctorCategoryName(DoctorCategory category) {
    switch (category) {
        case DoctorCategory::Blocker: return "blocker";
        case DoctorCategory::GameplayRisk: return "gameplay-risk";
        case DoctorCategory::VisualAudio: return "visual-audio";
        case DoctorCategory::Info: return "info";
    }
    return "info";
}

size_t DoctorPlace::countOf(DoctorCategory category) const {
    return static_cast<size_t>(std::count_if(findings.begin(), findings.end(),
                                             [&](const DoctorFinding& f) { return f.category == category; }));
}

std::string detectPlaceFormat(const std::string& bytes) {
    if (bytes.rfind("<roblox!", 0) == 0) return "rbxl/rbxm (binary)";
    size_t start = 0;
    while (start < bytes.size() && std::isspace(static_cast<unsigned char>(bytes[start]))) ++start;
    if (bytes.compare(start, 7, "<roblox") == 0) return "rbxlx (XML)";
    return "unknown";
}

const std::vector<std::string>& doctorChecksPerformed() {
    static const std::vector<std::string> kChecks = {
        "Import: the file parses and passes the import safety scan.",
        "Classes: every instance's class is checked against the classes Kronos imports.",
        "Properties: saved properties Kronos doesn't read are listed (they keep Kronos defaults).",
        "APIs: script source is scanned for Roblox APIs and checked against Kronos's API table.",
        "Assets: Roblox asset references (rbxassetid://, Roblox CDN) are listed; nothing is downloaded.",
        "Scripts: every Script, LocalScript and ModuleScript is compiled and started in a headless session with one "
        "player for 3 simulated seconds (6 more when behaviour checks run); compile and runtime errors are recorded.",
        "Behaviour: only when a .checks.luau file is given, its checks run in that session and each result is "
        "reported as passed or failed.",
    };
    return kChecks;
}

const std::vector<std::string>& doctorKnownLimits() {
    static const std::vector<std::string> kLimits = {
        "A place that imports and runs without errors is not proven to work: only behaviour checks test gameplay.",
        "Scripts run without physics, rendering or input; Touched only fires when a behaviour check calls touch().",
        "Server and client scripts share one session with one player; multiplayer behaviour is not tested.",
        "The API scan reads source text, so APIs reached indirectly (obj[name](), passed-in functions) are missed.",
        "Code that only runs later (after 3 seconds, on events nobody fires) is not executed, so its errors are "
        "not seen.",
        "Unread properties are listed by name; whether a game depends on them is not known.",
    };
    return kLimits;
}

std::vector<DoctorFinding> classifyFindings(const ImportReport& report, const CompatibilityScore& score) {
    std::vector<DoctorFinding> out;

    if (!report.parsed || report.blocked) {
        for (const ImportDiagnostic& d : report.diagnostics) {
            if (d.severity != ImportSeverity::Blocked) continue;
            out.push_back({DoctorCategory::Blocker, report.parsed ? "import-blocked" : "import-failed", d.subject, {},
                           0, 1, d.message, "Kronos can't bring this place in until this is fixed.",
                           "docs/TROUBLESHOOTING.md"});
        }
        if (out.empty()) {
            out.push_back({DoctorCategory::Blocker, "import-failed", "<file>", {}, 0, 1,
                           "The file could not be read as a Roblox place or model.",
                           "Nothing can be checked until the file imports.",
                           "Export the place again from Roblox Studio as .rbxlx or .rbxl."});
        }
        sortFindings(out);
        return out;
    }

    // Classes and properties.
    std::map<std::string, std::pair<size_t, std::string>> classes;                      // class -> count, example
    std::map<std::string, std::set<std::string>> unread;                                // class -> properties
    std::map<std::string, std::pair<size_t, std::string>> unreadUses;                   // class -> instances, example
    std::pair<size_t, std::string> attributes{0, {}};
    std::map<std::pair<std::string, std::string>, std::pair<size_t, int>> apis;         // (script, api) -> count, line
    std::map<std::string, std::string> apiHelp;
    std::map<std::string, ApiMappingStatus> apiStatus;
    LuauApiCompatibility api;
    // Roblox Studio saves dozens of built-in services in every place. Ones
    // Kronos doesn't have only matter if they hold something.
    std::set<std::string> quietRoots;
    std::vector<std::string> quietNames;
    for (const ImportedInstance& root : report.tree) {
        if (InstanceHydrator::isSupportedClass(root.className) || hasScripts(root)) continue;
        size_t descendants = 0;
        visit(root.children, "", [&](const ImportedInstance&, const std::string&) { ++descendants; });
        quietRoots.insert(root.name);
        quietNames.push_back(descendants == 0 ? root.className
                                              : root.className + " (" + std::to_string(descendants) + " settings objects)");
    }
    visit(report.tree, "", [&](const ImportedInstance& node, const std::string& path) {
        if (quietRoots.count(path.substr(0, path.find('.'))) != 0) return;
        if (isEmptyTerrain(node)) {
            quietNames.push_back("Terrain (empty)");
            return;
        }
        if (!InstanceHydrator::isSupportedClass(node.className)) {
            auto& entry = classes[node.className];
            if (entry.first++ == 0) entry.second = path;
            return;
        }
        bool anyUnread = false;
        for (const auto& [key, value] : node.properties) {
            if (key.rfind("@type.", 0) == 0) continue;
            const std::string base = key.substr(0, key.find('.'));
            if (base == "AttributesSerialize") {
                if (value.find_first_not_of(" \t\r\n") != std::string::npos && attributes.first++ == 0) {
                    attributes.second = path;
                }
                continue;
            }
            if (isAliasProperty(base) || classKnowsProperty(node.className, base)) continue;
            unread[node.className].insert(base);
            anyUnread = true;
        }
        if (anyUnread) {
            auto& entry = unreadUses[node.className];
            if (entry.first++ == 0) entry.second = path;
        }
        if (!isScriptClass(node.className)) return;
        const auto source = node.properties.find("Source");
        if (source == node.properties.end()) return;
        for (const ApiCompatibilityFinding& finding : api.scan(source->second)) {
            if (finding.status == ApiMappingStatus::Mapped) continue;
            auto& entry = apis[{path, finding.identifier}];
            if (entry.first++ == 0) entry.second = finding.line;
            apiHelp[finding.identifier] = finding.guidance;
            apiStatus[finding.identifier] = finding.status;
        }
    });

    for (const auto& [cls, entry] : classes) {
        const bool visual = isVisualClass(cls);
        out.push_back({visual ? DoctorCategory::VisualAudio : DoctorCategory::GameplayRisk, "unsupported-class", cls,
                       {}, 0, entry.first,
                       std::to_string(entry.first) + " " + cls + " instance(s), e.g. " + entry.second +
                           ", use a class Kronos doesn't import.",
                       visual ? "They are imported as plain placeholders, so the place looks different."
                              : "They are imported as plain placeholders without their behaviour, and scripts that "
                                "use their properties or methods will fail.",
                       std::string("Supported classes are listed in ") + kBridgeDoc + " (\"What works\")."});
    }
    if (!quietNames.empty()) {
        std::sort(quietNames.begin(), quietNames.end());
        std::string list;
        for (const std::string& name : quietNames) list += (list.empty() ? "" : ", ") + name;
        out.push_back({DoctorCategory::Info, "unused-roblox-services", "Services", {}, 0, quietNames.size(),
                       std::to_string(quietNames.size()) +
                           " built-in Roblox service(s) Kronos doesn't have are saved in this place, with no scripts inside: " + list +
                           ".",
                       "Roblox Studio saves these in every place. Kronos ignores their settings; scripts that call them "
                       "would show up above as a missing API or a script error.",
                       kBridgeDoc});
    }
    if (attributes.first > 0) {
        out.push_back({DoctorCategory::GameplayRisk, "attributes-not-imported", "Attributes", {}, 0, attributes.first,
                       std::to_string(attributes.first) + " instance(s), e.g. " + attributes.second +
                           ", have attributes saved in the file, which Kronos doesn't import yet.",
                       "GetAttribute returns nil for them until a script sets them.",
                       "Set the attributes from a script at startup."});
    }
    for (const auto& [cls, names] : unread) {
        std::string list;
        for (const std::string& name : names) list += (list.empty() ? "" : ", ") + name;
        const auto& uses = unreadUses[cls];
        out.push_back({DoctorCategory::Info, "unread-properties", cls, {}, 0, uses.first,
                       cls + " (" + std::to_string(uses.first) + " instance(s), e.g. " + uses.second +
                           ") has saved properties Kronos doesn't read: " + list + ".",
                       "They keep Kronos defaults; this matters only if the game relies on other values.",
                       kBridgeDoc});
    }
    for (const auto& [key, entry] : apis) {
        const bool shimmed = apiStatus[key.second] == ApiMappingStatus::Shimmed;
        out.push_back({shimmed ? DoctorCategory::Info : DoctorCategory::GameplayRisk,
                       shimmed ? "shimmed-api" : "missing-api", key.second, key.first, entry.second, entry.first,
                       (shimmed ? key.second + " is rewritten by the import shim (" : key.second +
                                      " is not in Kronos yet (") +
                           std::to_string(entry.first) + " use(s) in this script).",
                       shimmed ? "The script keeps working; the call is translated when it loads."
                               : "When this line runs it raises an error, which stops that script thread.",
                       apiHelp[key.second]});
    }

    // Assets and the safety scan.
    for (const ModerationFinding& finding : report.moderation.findings) {
        const bool asset = finding.code == ModerationReasonCode::BlockedProprietaryCdn ||
                           finding.code == ModerationReasonCode::BlockedQuarantinedHash;
        out.push_back({asset ? DoctorCategory::VisualAudio : DoctorCategory::Info,
                       asset ? "asset-unavailable" : "safety-flag", finding.subject, {}, 0, 1,
                       asset ? "References " + finding.reference + ", a Roblox-hosted asset." : finding.detail,
                       asset ? "Kronos doesn't download Roblox assets, so this shows a placeholder or stays silent."
                             : "The import safety scan flagged this name for review.",
                       asset ? "Replace it with a file you own or may use (docs/ROBLOX_BRIDGE.md)."
                             : "Rename it if it isn't yours to use."});
    }
    for (const ImportDiagnostic& d : report.diagnostics) {
        if (d.severity == ImportSeverity::Warning && d.message.find("has no Source property") != std::string::npos) {
            out.push_back({DoctorCategory::Info, "empty-script", d.subject, d.subject, 0, 1, d.message,
                           "The script does nothing in Kronos.", "Check the export included script sources."});
        }
    }

    // Script runs.
    for (const CompatScriptRun& run : score.scripts) {
        if (!run.ok) {
            const bool compile = run.error.rfind("compile error", 0) == 0;
            out.push_back({DoctorCategory::Blocker, compile ? "compile-error" : "runtime-error", run.path, run.path,
                           lineFromError(run.error, run.path), 1, run.error,
                           compile ? "The script can't load, so none of it runs."
                                   : "The script stopped with this error while starting, so the code after it "
                                     "didn't run.",
                           compile ? "Fix the syntax; Kronos uses Luau like Roblox." : kBridgeDoc});
        } else if (!run.started) {
            out.push_back({DoctorCategory::Info, "not-started", run.path, run.path, 0, 1,
                           run.className + " isn't started by Roblox's run rules in this location.",
                           "It was not tested; it may run in a real game if something requires or moves it.",
                           kBridgeDoc});
        }
    }
    for (const CompatBehaviourCheck& check : score.behaviour) {
        if (check.passed) continue;
        out.push_back({DoctorCategory::GameplayRisk, "behaviour-failed", check.name, {}, 0, 1,
                       "Behaviour check failed: " + check.name + (check.detail.empty() ? "" : " (" + check.detail + ")"),
                       "The game did something different from what this check expects.", kBridgeDoc});
    }
    sortFindings(out);
    return out;
}

DoctorPlace diagnosePlace(const std::string& fileName, const std::string& bytes, const std::string& checks) {
    DoctorPlace place;
    place.file = fileName;
    place.format = detectPlaceFormat(bytes);
    const safety::IPInfringementScanner scanner;
    ProjectImporter importer;
    const ImportReport report = importer.importDocument(bytes, scanner);
    place.parsed = report.parsed;
    place.blocked = report.blocked;
    if (report.parsed && !report.blocked) {
        place.score = scoreImport(report);
        runImportedScripts(report, place.score, checks);
        place.scriptsRan = true;
    }
    place.findings = classifyFindings(report, place.score);
    return place;
}

std::string doctorReportJson(const DoctorReport& report) {
    std::string out = "{\n  \"tool\": \"Kronos Bridge Doctor\",\n  \"schema\": 1,\n";
    out += "  \"kronosVersion\": " + q(report.kronosVersion.empty() ? core::kKronosVersion : report.kronosVersion) + ",\n";
    out += "  \"checksPerformed\": [";
    for (size_t i = 0; i < doctorChecksPerformed().size(); ++i) out += (i ? ", " : "") + q(doctorChecksPerformed()[i]);
    out += "],\n  \"knownLimits\": [";
    for (size_t i = 0; i < doctorKnownLimits().size(); ++i) out += (i ? ", " : "") + q(doctorKnownLimits()[i]);
    out += "],\n  \"places\": [";
    for (size_t p = 0; p < report.places.size(); ++p) {
        const DoctorPlace& place = report.places[p];
        const CompatibilityScore& s = place.score;
        out += p ? ",\n    {\n" : "\n    {\n";
        out += "      \"file\": " + q(place.file) + ",\n";
        out += "      \"format\": " + q(place.format) + ",\n";
        out += std::string("      \"imported\": ") + (place.parsed && !place.blocked ? "true" : "false") + ",\n";
        out += "      \"metrics\": {\n";
        out += "        \"classes\": {\"supported\": " + std::to_string(s.instancesMapped) +
               ", \"total\": " + std::to_string(s.instances) + "},\n";
        out += "        \"apiUses\": {\"supported\": " + std::to_string(s.apiSupported) +
               ", \"total\": " + std::to_string(s.apiUses) + "},\n";
        size_t notStarted = 0;
        for (const CompatScriptRun& run : s.scripts) notStarted += run.started ? 0 : 1;
        out += std::string("        \"scripts\": {\"ran\": ") + (place.scriptsRan ? "true" : "false") +
               ", \"withoutErrors\": " + std::to_string(s.scriptsOk) + ", \"started\": " +
               std::to_string(s.scriptsRun) + ", \"notStarted\": " + std::to_string(notStarted) + "},\n";
        size_t passed = 0;
        for (const CompatBehaviourCheck& check : s.behaviour) passed += check.passed ? 1 : 0;
        out += std::string("        \"behaviour\": {\"ran\": ") + (s.behaviourRan ? "true" : "false") +
               ", \"passed\": " + std::to_string(passed) + ", \"total\": " + std::to_string(s.behaviour.size()) +
               ", \"checks\": [";
        for (size_t i = 0; i < s.behaviour.size(); ++i) {
            const CompatBehaviourCheck& check = s.behaviour[i];
            out += (i ? ", " : "") + std::string("{\"name\": ") + q(check.name) + ", \"passed\": " +
                   (check.passed ? "true" : "false") + ", \"detail\": " + q(check.detail) + "}";
        }
        out += "]}\n      },\n";
        out += "      \"counts\": {";
        for (int c = 0; c <= 3; ++c) {
            const auto category = static_cast<DoctorCategory>(c);
            out += (c ? ", " : "") + q(doctorCategoryName(category)) + ": " + std::to_string(place.countOf(category));
        }
        out += "},\n      \"findings\": [";
        for (size_t i = 0; i < place.findings.size(); ++i) {
            const DoctorFinding& f = place.findings[i];
            out += i ? ",\n        {" : "\n        {";
            out += "\"category\": " + q(doctorCategoryName(f.category)) + ", \"code\": " + q(f.code) +
                   ", \"subject\": " + q(f.subject) + ", \"script\": " + (f.script.empty() ? "null" : q(f.script)) +
                   ", \"line\": " + (f.line > 0 ? std::to_string(f.line) : "null") + ", \"count\": " +
                   std::to_string(f.count) + ", \"message\": " + q(f.message) + ", \"why\": " + q(f.why) +
                   ", \"help\": " + q(f.help) + "}";
        }
        out += place.findings.empty() ? "]\n    }" : "\n      ]\n    }";
    }
    out += report.places.empty() ? "]\n}\n" : "\n  ]\n}\n";
    return out;
}

std::string doctorReportMarkdown(const DoctorReport& report) {
    std::string out = "# Kronos Bridge Doctor report\n\n";
    out += "Kronos " + (report.kronosVersion.empty() ? std::string(core::kKronosVersion) : report.kronosVersion) +
           ". This report doesn't contain script source.\n";
    for (const DoctorPlace& place : report.places) {
        const CompatibilityScore& s = place.score;
        out += "\n## " + mdCell(place.file) + "\n\nFormat: " + place.format + "\n\n";
        if (!place.parsed || place.blocked) {
            out += "**Not imported.**\n";
        } else {
            size_t passed = 0;
            for (const CompatBehaviourCheck& check : s.behaviour) passed += check.passed ? 1 : 0;
            size_t notStarted = 0;
            for (const CompatScriptRun& run : s.scripts) notStarted += run.started ? 0 : 1;
            out += "| Measure | Result |\n|---|---|\n";
            out += "| Classes Kronos imports | " + ratio(s.instancesMapped, s.instances) + " instances |\n";
            out += "| Roblox API uses Kronos has | " + ratio(s.apiSupported, s.apiUses) + " |\n";
            out += "| Scripts started without errors | " + ratio(s.scriptsOk, s.scriptsRun) +
                   (notStarted ? " (" + std::to_string(notStarted) + " not started)" : std::string()) + " |\n";
            out += "| Behaviour checks passed | " +
                   (s.behaviourRan ? ratio(passed, s.behaviour.size()) : std::string("not run (no checks file)")) +
                   " |\n";
            if (s.behaviourRan) {
                out += "\nBehaviour checks:\n";
                for (const CompatBehaviourCheck& check : s.behaviour) {
                    out += std::string("- ") + (check.passed ? "PASS " : "FAIL ") + mdCell(check.name) +
                           (check.detail.empty() ? "" : " (" + mdCell(check.detail) + ")") + "\n";
                }
            }
        }
        static const char* kTitles[] = {"Blockers", "Gameplay risks", "Visual and audio", "Information"};
        for (int c = 0; c <= 3; ++c) {
            const auto category = static_cast<DoctorCategory>(c);
            if (place.countOf(category) == 0) continue;
            out += std::string("\n### ") + kTitles[c] + " (" + std::to_string(place.countOf(category)) + ")\n\n";
            for (const DoctorFinding& f : place.findings) {
                if (f.category != category) continue;
                out += "- **" + mdCell(f.code) + "**";
                if (!f.script.empty()) {
                    out += " in `" + mdCell(f.script) + (f.line > 0 ? ":" + std::to_string(f.line) : std::string()) +
                           "`";
                }
                out += ": " + mdCell(f.message) + "\n  - Why: " + mdCell(f.why) + "\n";
                if (!f.help.empty()) out += "  - Help: " + mdCell(f.help) + "\n";
            }
        }
        if (place.findings.empty()) out += "\nNo findings.\n";
    }
    out += "\n## Checks performed\n\n";
    for (const std::string& line : doctorChecksPerformed()) out += "- " + line + "\n";
    out += "\n## Known limits\n\n";
    for (const std::string& line : doctorKnownLimits()) out += "- " + line + "\n";
    return out;
}

bool writeDoctorFile(const std::string& path, const std::string& text, std::string& error) {
    if (path.empty()) {
        error = "no output path given";
        return false;
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        error = "cannot open \"" + path + "\" for writing";
        return false;
    }
    file << text;
    file.flush();
    if (!file) {
        error = "could not write all of \"" + path + "\"";
        return false;
    }
    return true;
}

} // namespace engine::migration
