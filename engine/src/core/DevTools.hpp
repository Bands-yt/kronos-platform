#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace engine::core::devtools {

// Split panes and tabs (408): open files and which pane shows each.
class EditorTabs {
public:
    void open(const std::string& file, int pane) {
        auto it = std::find_if(tabs_.begin(), tabs_.end(), [&](const auto& t) { return t.first == file; });
        if (it == tabs_.end()) tabs_.push_back({file, pane});
        else it->second = pane;
    }
    void close(const std::string& file) { tabs_.erase(std::remove_if(tabs_.begin(), tabs_.end(), [&](const auto& t) { return t.first == file; }), tabs_.end()); }
    [[nodiscard]] std::vector<std::string> inPane(int pane) const {
        std::vector<std::string> out;
        for (const auto& t : tabs_) {
            if (t.second == pane) out.push_back(t.first);
        }
        return out;
    }

private:
    std::vector<std::pair<std::string, int>> tabs_;
};

// Type checking and autocomplete (412, 413): members of a class from a reflection table, filtered by prefix.
using ClassMembers = std::map<std::string, std::vector<std::string>>;

inline std::vector<std::string> completeMembers(const ClassMembers& classes, const std::map<std::string, std::string>& parentOf, const std::string& className, const std::string& prefix) {
    std::set<std::string> found;
    std::string cur = className;
    for (int guard = 0; guard < 16 && !cur.empty(); ++guard) {
        auto it = classes.find(cur);
        if (it != classes.end()) {
            for (const auto& m : it->second) {
                if (m.rfind(prefix, 0) == 0) found.insert(m);
            }
        }
        auto p = parentOf.find(cur);
        cur = p == parentOf.end() ? std::string() : p->second;
    }
    return {found.begin(), found.end()};
}

inline bool memberExists(const ClassMembers& classes, const std::map<std::string, std::string>& parentOf, const std::string& className, const std::string& member) {
    const auto all = completeMembers(classes, parentOf, className, "");
    return std::find(all.begin(), all.end(), member) != all.end();
}

// Debug console expressions (418): "a + b", "a * b" on numbers or names from a variable table.
inline bool evalExpression(const std::string& text, const std::map<std::string, double>& vars, double& result) {
    std::istringstream in(text);
    std::string lhs, op, rhs;
    auto value = [&](const std::string& s, double& v) {
        auto it = vars.find(s);
        if (it != vars.end()) return v = it->second, true;
        char* end = nullptr;
        v = std::strtod(s.c_str(), &end);
        return end != s.c_str() && *end == '\0';
    };
    if (!(in >> lhs)) return false;
    double a = 0;
    if (!value(lhs, a)) return false;
    if (!(in >> op)) return result = a, true;
    if (!(in >> rhs)) return false;
    double b = 0;
    if (!value(rhs, b)) return false;
    if (op == "+") result = a + b;
    else if (op == "-") result = a - b;
    else if (op == "*") result = a * b;
    else if (op == "/") {
        if (b == 0) return false;
        result = a / b;
    } else return false;
    return true;
}

// Script budgets (419, 435): per-script cpu and memory use against limits.
struct ScriptUse {
    std::string name;
    double cpuMs = 0;
    double memoryMb = 0;
};

inline std::vector<std::string> overBudget(const std::vector<ScriptUse>& use, double cpuLimitMs, double memoryLimitMb) {
    std::vector<std::string> out;
    for (const auto& u : use) {
        if (u.cpuMs > cpuLimitMs) out.push_back(u.name + " uses " + std::to_string(static_cast<int>(u.cpuMs)) + " ms of " + std::to_string(static_cast<int>(cpuLimitMs)) + " ms");
        if (u.memoryMb > memoryLimitMb) out.push_back(u.name + " uses " + std::to_string(static_cast<int>(u.memoryMb)) + " MB of " + std::to_string(static_cast<int>(memoryLimitMb)) + " MB");
    }
    return out;
}

// Hot reload diff (420): lines added and removed between two sources.
inline std::pair<int, int> lineDiffCounts(const std::string& before, const std::string& after) {
    auto split = [](const std::string& s) {
        std::multiset<std::string> m;
        std::istringstream in(s);
        std::string l;
        while (std::getline(in, l)) m.insert(l);
        return m;
    };
    auto a = split(before), b = split(after);
    int added = 0, removed = 0;
    for (auto it = b.begin(); it != b.end(); it = b.upper_bound(*it)) {
        const int diff = static_cast<int>(b.count(*it)) - static_cast<int>(a.count(*it));
        if (diff > 0) added += diff;
    }
    for (auto it = a.begin(); it != a.end(); it = a.upper_bound(*it)) {
        const int diff = static_cast<int>(a.count(*it)) - static_cast<int>(b.count(*it));
        if (diff > 0) removed += diff;
    }
    return {added, removed};
}

// Test runner (422, 442): tests are named callbacks that return an empty string on success.
struct TestCase {
    std::string name;
    std::function<std::string()> run;
};

struct TestSummary {
    int passed = 0, failed = 0;
    std::vector<std::string> failures;
};

inline TestSummary runTests(const std::vector<TestCase>& tests) {
    TestSummary s;
    for (const auto& t : tests) {
        std::string err;
        try {
            err = t.run();
        } catch (const std::exception& e) {
            err = e.what();
        }
        if (err.empty()) ++s.passed;
        else {
            ++s.failed;
            s.failures.push_back(t.name + ": " + err);
        }
    }
    return s;
}

// Script templates (423).
inline std::map<std::string, std::string> scriptTemplates(const std::string& lang) {
    if (lang == "python") return {{"Empty", "# python\n"}, {"Move a part", "# python\ndef on_update(dt):\n    world.set_position('Part', 0, 5, 0)\n"}};
    return {{"Empty", "-- script\n"}, {"Print hello", "print(\"Hello\")\n"}, {"Touch event", "script.Parent.Touched:Connect(function(hit)\n    print(hit.Name)\nend)\n"}};
}

// Roblox annotations (424): APIs that behave differently from Roblox get a note.
inline std::string compatibilityNote(const std::string& api) {
    static const std::map<std::string, std::string> notes = {
        {"HttpService:GetAsync", "Web requests always fail in Kronos."},
        {"DataStore", "Saved in a local folder, not in the cloud."},
        {"MarketplaceService", "Stub: purchases are not real."}};
    auto it = notes.find(api);
    return it == notes.end() ? std::string() : it->second;
}

// Python tracebacks (436): "File \"x.py\", line 12" to file and line.
inline bool parseTracebackLine(const std::string& text, std::string& file, int& line) {
    const std::size_t a = text.find("File \""), b = a == std::string::npos ? a : text.find('"', a + 6), c = b == std::string::npos ? b : text.find("line ", b);
    if (c == std::string::npos) return false;
    file = text.substr(a + 6, b - a - 6);
    line = std::atoi(text.c_str() + c + 5);
    return line > 0;
}

// Python formatter (441): trims trailing spaces, tabs to four spaces, one final newline.
inline std::string formatPython(const std::string& source) {
    std::string out, lineText;
    std::istringstream in(source);
    while (std::getline(in, lineText)) {
        std::string expanded;
        for (char c : lineText) expanded += c == '\t' ? "    " : std::string(1, c);
        while (!expanded.empty() && expanded.back() == ' ') expanded.pop_back();
        out += expanded + "\n";
    }
    while (out.size() > 1 && out[out.size() - 1] == '\n' && out[out.size() - 2] == '\n') out.pop_back();
    return out;
}

// Python type hints docs (443): "def f(a: int) -> str" to a markdown line.
inline std::string docFromSignature(const std::string& signature) {
    const std::size_t paren = signature.find('(');
    if (signature.rfind("def ", 0) != 0 || paren == std::string::npos) return {};
    const std::string name = signature.substr(4, paren - 4);
    const std::size_t arrow = signature.find("->");
    const std::string returns = arrow == std::string::npos ? "None" : signature.substr(arrow + 2);
    std::string r = returns;
    while (!r.empty() && (r.front() == ' ')) r.erase(r.begin());
    while (!r.empty() && (r.back() == ':' || r.back() == ' ')) r.pop_back();
    return "`" + name + signature.substr(paren, signature.rfind(')') - paren + 1) + "` returns " + r;
}

// Typed entity ids (459, 460): index plus generation; a stale id is detected.
struct EntityId {
    uint32_t index = 0, generation = 0;
    bool operator==(const EntityId& o) const { return index == o.index && generation == o.generation; }
};

class EntitySlots {
public:
    EntityId create() {
        if (!free_.empty()) {
            const uint32_t i = free_.back();
            free_.pop_back();
            alive_[i] = true;
            return {i, generation_[i]};
        }
        generation_.push_back(1);
        alive_.push_back(true);
        return {static_cast<uint32_t>(generation_.size() - 1), 1};
    }
    void destroy(EntityId id) {
        if (!valid(id)) return;
        alive_[id.index] = false;
        ++generation_[id.index];
        free_.push_back(id.index);
    }
    [[nodiscard]] bool valid(EntityId id) const { return id.index < generation_.size() && alive_[id.index] && generation_[id.index] == id.generation; }

private:
    std::vector<uint32_t> generation_, free_;
    std::vector<bool> alive_;
};

// Plugin ABI conformance (458): struct_size rules a plugin API struct must follow.
inline std::string checkApiStruct(std::size_t declaredSize, std::size_t expectedMinimum, uint32_t versionMajor, uint32_t hostMajor) {
    if (versionMajor != hostMajor) return "major version differs";
    if (declaredSize < expectedMinimum) return "struct is smaller than expected";
    return {};
}

// Logging macros for plugins (465): one line format.
inline std::string pluginLogLine(const std::string& plugin, const std::string& level, const std::string& message) { return "[" + plugin + "] " + level + ": " + message; }

// Language-neutral scene schema (468): required keys of a scene entity record.
inline std::vector<std::string> schemaProblems(const std::map<std::string, std::string>& record) {
    std::vector<std::string> out;
    for (const char* k : {"id", "name", "parent"}) {
        if (!record.count(k)) out.push_back(std::string("missing ") + k);
    }
    return out;
}

// Watch window (481).
class WatchList {
public:
    void add(const std::string& name) {
        if (std::find(names_.begin(), names_.end(), name) == names_.end()) names_.push_back(name);
    }
    [[nodiscard]] std::vector<std::string> render(const std::map<std::string, std::string>& values) const {
        std::vector<std::string> out;
        for (const auto& n : names_) {
            auto it = values.find(n);
            out.push_back(n + " = " + (it == values.end() ? "(not available)" : it->second));
        }
        return out;
    }

private:
    std::vector<std::string> names_;
};

// Frame stepping (483).
class FrameStepper {
public:
    void pause() { paused_ = true; }
    void resume() { paused_ = false; pendingSteps_ = 0; }
    void step(int frames = 1) { if (paused_) pendingSteps_ += frames; }
    // Returns true when the simulation should advance this frame.
    bool shouldAdvance() {
        if (!paused_) return true;
        if (pendingSteps_ > 0) { --pendingSteps_; return true; }
        return false;
    }

private:
    bool paused_ = false;
    int pendingSteps_ = 0;
};

// Contact visualisation (484): colour by impulse strength.
inline std::string contactColour(float impulse) { return impulse < 1 ? "green" : impulse < 10 ? "yellow" : "red"; }

// Replication visualisation (485): bytes per entity sorted, to show who costs the most.
inline std::vector<std::pair<std::string, std::size_t>> heaviestEntities(const std::map<std::string, std::size_t>& bytes, std::size_t n) {
    std::vector<std::pair<std::string, std::size_t>> out(bytes.begin(), bytes.end());
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    if (out.size() > n) out.resize(n);
    return out;
}

// Asset load timeline (487).
struct LoadSpan {
    std::string name;
    double startMs, endMs;
};

inline double loadTimelineTotal(const std::vector<LoadSpan>& spans) {
    if (spans.empty()) return 0;
    double lo = spans.front().startMs, hi = spans.front().endMs;
    for (const auto& s : spans) {
        lo = std::min(lo, s.startMs);
        hi = std::max(hi, s.endMs);
    }
    return hi - lo;
}

inline std::string slowestLoad(const std::vector<LoadSpan>& spans) {
    std::string best;
    double longest = -1;
    for (const auto& s : spans) {
        if (s.endMs - s.startMs > longest) {
            longest = s.endMs - s.startMs;
            best = s.name;
        }
    }
    return best;
}

// Shader error grouping (488).
inline std::map<std::string, std::vector<int>> groupShaderErrors(const std::vector<std::pair<std::string, int>>& fileLineErrors) {
    std::map<std::string, std::vector<int>> out;
    for (const auto& [file, line] : fileLineErrors) out[file].push_back(line);
    return out;
}

// Diagnostics bundle (490, 500): sections joined with headers, secrets removed.
inline std::string redactSecrets(std::string text) {
    for (const char* key : {"token=", "password=", "apikey=", "secret="}) {
        std::size_t pos = 0;
        const std::string k = key;
        while ((pos = text.find(k, pos)) != std::string::npos) {
            const std::size_t from = pos + k.size();
            std::size_t end = from;
            while (end < text.size() && !std::isspace(static_cast<unsigned char>(text[end])) && text[end] != '&') ++end;
            text.replace(from, end - from, "[removed]");
            pos = from + 9;
        }
    }
    return text;
}

inline std::string diagnosticsBundle(const std::vector<std::pair<std::string, std::string>>& sections) {
    std::string out;
    for (const auto& [title, body] : sections) out += "===== " + title + " =====\n" + redactSecrets(body) + "\n";
    return out;
}

// Project-wide log search (491), error panel links (492), notification queue (493).
struct LogLine {
    std::string source, level, text;
};

inline std::vector<LogLine> searchLogs(const std::vector<LogLine>& all, const std::string& text, const std::string& level) {
    std::vector<LogLine> out;
    for (const auto& l : all) {
        if (!level.empty() && l.level != level) continue;
        if (!text.empty() && l.text.find(text) == std::string::npos) continue;
        out.push_back(l);
    }
    return out;
}

inline bool parseSourceLink(const std::string& text, std::string& file, int& line) {
    const std::size_t colon = text.find(':');
    if (colon == std::string::npos || colon == 0) return false;
    const int n = std::atoi(text.c_str() + colon + 1);
    if (n <= 0) return false;
    file = text.substr(0, colon);
    line = n;
    return true;
}

class NotificationQueue {
public:
    explicit NotificationQueue(std::size_t cap = 5) : cap_(cap) {}
    void push(const std::string& text) {
        for (auto& n : items_) {
            if (n.first == text) { ++n.second; return; }
        }
        items_.push_back({text, 1});
        if (items_.size() > cap_) items_.pop_front();
    }
    [[nodiscard]] std::vector<std::string> lines() const {
        std::vector<std::string> out;
        for (const auto& n : items_) out.push_back(n.second > 1 ? n.first + " (x" + std::to_string(n.second) + ")" : n.first);
        return out;
    }

private:
    std::size_t cap_;
    std::deque<std::pair<std::string, int>> items_;
};

// Console command permissions (494).
inline bool commandAllowed(const std::string& command, bool isHost, bool devMode) {
    static const std::set<std::string> safe = {"help", "stats", "fps"};
    static const std::set<std::string> host = {"kick", "teleport", "setvalue"};
    if (safe.count(command)) return true;
    if (host.count(command)) return isHost && devMode;
    return false;
}

// Correlated logs (496): server and client lines merged by time, tagged.
struct TimedLog {
    double time;
    std::string side, text;
};

inline std::vector<TimedLog> mergeLogs(std::vector<TimedLog> server, const std::vector<TimedLog>& client) {
    server.insert(server.end(), client.begin(), client.end());
    std::stable_sort(server.begin(), server.end(), [](const TimedLog& a, const TimedLog& b) { return a.time < b.time; });
    return server;
}

// Debug-only overlays (498): off in release builds.
inline bool overlayEnabled(bool debugBuild, bool userRequested) { return debugBuild && userRequested; }

} // namespace engine::core::devtools
