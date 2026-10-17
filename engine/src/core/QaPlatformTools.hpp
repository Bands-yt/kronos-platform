#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "core/AssetPipelineTools.hpp"

namespace engine::core::qa {

// Clean exit (805): what an exit status means.
inline std::string classifyExit(int status, bool killedBySignal) {
    if (killedBySignal) return "crashed";
    return status == 0 ? "clean exit" : "exited with code " + std::to_string(status);
}

// Golden images (808).
inline bool matchesGolden(const std::vector<uint8_t>& golden, const std::vector<uint8_t>& actual, double maxRmse) {
    if (golden.size() != actual.size() || golden.empty()) return false;
    double sum = 0;
    for (std::size_t i = 0; i < golden.size(); ++i) {
        const double d = static_cast<double>(golden[i]) - actual[i];
        sum += d * d;
    }
    return std::sqrt(sum / golden.size()) <= maxRmse;
}

// Physics scenarios (809): analytic free fall for checking the solver.
inline double freeFallHeight(double startY, double seconds, double gravity = 9.81) { return startY - 0.5 * gravity * seconds * seconds; }

inline double fallTime(double height, double gravity = 9.81) { return std::sqrt(2.0 * height / gravity); }

// Network simulation (810): a lossy link with a fixed delay and a repeatable drop pattern.
struct LinkResult {
    int sent = 0, delivered = 0;
    double averageDelayMs = 0;
};

inline LinkResult simulateLink(int packets, double lossPercent, double delayMs, double jitterMs, uint32_t seed) {
    uint32_t x = seed ? seed : 7;
    auto rnd = [&]() {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        return static_cast<double>(x % 10000) / 10000.0;
    };
    LinkResult r;
    double total = 0;
    for (int i = 0; i < packets; ++i) {
        ++r.sent;
        if (rnd() * 100.0 < lossPercent) continue;
        ++r.delivered;
        total += delayMs + (rnd() - 0.5) * 2.0 * jitterMs;
    }
    r.averageDelayMs = r.delivered ? total / r.delivered : 0;
    return r;
}

// Save and reopen (813): which keys differ between two saves.
inline std::vector<std::string> identityDiff(const std::map<std::string, std::string>& before, const std::map<std::string, std::string>& after) {
    std::vector<std::string> out;
    for (const auto& [k, v] : before) {
        auto it = after.find(k);
        if (it == after.end()) out.push_back(k + " lost");
        else if (it->second != v) out.push_back(k + " changed");
    }
    for (const auto& kv : after) {
        if (!before.count(kv.first)) out.push_back(kv.first + " appeared");
    }
    return out;
}

// Leak counter (817).
class AllocationCounter {
public:
    void onAlloc() { ++live_; ++total_; }
    void onFree() { if (live_ > 0) --live_; }
    [[nodiscard]] int live() const { return live_; }
    [[nodiscard]] int total() const { return total_; }

private:
    int live_ = 0, total_ = 0;
};

// Soak test (821): memory growth per hour from samples (hours, MB) by least squares.
inline double growthPerHour(const std::vector<std::pair<double, double>>& samples) {
    if (samples.size() < 2) return 0;
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (const auto& [x, y] : samples) {
        sx += x;
        sy += y;
        sxx += x * x;
        sxy += x * y;
    }
    const double n = static_cast<double>(samples.size()), den = n * sxx - sx * sx;
    return den == 0 ? 0 : (n * sxy - sx * sy) / den;
}

inline bool soakLooksLeaky(const std::vector<std::pair<double, double>>& samples, double maxMbPerHour) { return growthPerHour(samples) > maxMbPerHour; }

// Packaged game launch (822).
inline std::vector<std::string> launchArgs(const std::string& exe, const std::string& game, bool headless) {
    std::vector<std::string> a = {exe, "--game", game};
    if (headless) a.push_back("--headless");
    return a;
}

// TLS errors (831).
inline std::string tlsMessage(const std::string& code) {
    if (code == "expired") return "The server's certificate has expired. Check your PC's date, or try again later.";
    if (code == "untrusted") return "This server's certificate is not trusted. Kronos did not connect.";
    if (code == "hostname") return "The certificate belongs to a different server. Kronos did not connect.";
    return "A secure connection could not be made.";
}

// Authorisation per game (837).
inline bool canManageGame(const std::string& userId, const std::string& ownerId, const std::set<std::string>& editors, const std::string& action) {
    if (userId.empty()) return false;
    if (userId == ownerId) return true;
    return action == "edit" && editors.count(userId) > 0;
}

// Moderation audit trail (841): append-only, each entry carries the hash of the one before it.
class AuditTrail {
public:
    void add(const std::string& actor, const std::string& action) {
        const std::string prev = entries_.empty() ? std::string("0") : entries_.back().hash;
        entries_.push_back({actor, action, prev, hashOf(prev + actor + action)});
    }
    [[nodiscard]] bool intact() const {
        std::string prev = "0";
        for (const auto& e : entries_) {
            if (e.prevHash != prev || e.hash != hashOf(prev + e.actor + e.action)) return false;
            prev = e.hash;
        }
        return true;
    }
    void tamper(std::size_t i, const std::string& newAction) { if (i < entries_.size()) entries_[i].action = newAction; }
    [[nodiscard]] std::size_t size() const { return entries_.size(); }

private:
    struct Entry {
        std::string actor, action, prevHash, hash;
    };
    static std::string hashOf(const std::string& s) { return assets::hex64(assets::contentHash(std::vector<uint8_t>(s.begin(), s.end()))); }
    std::vector<Entry> entries_;
};

// Voice meter (859): peak and a 0..1 level from samples.
inline float peakLevel(const std::vector<float>& samples) {
    float p = 0;
    for (float s : samples) p = std::max(p, std::fabs(s));
    return std::min(p, 1.0f);
}

// Keyboard navigation (883) and focus order (899).
inline int nextFocus(int current, int count, bool backwards) {
    if (count <= 0) return -1;
    if (current < 0) return backwards ? count - 1 : 0;
    return backwards ? (current + count - 1) % count : (current + 1) % count;
}

struct FocusItem {
    std::string id;
    float x, y;
};

inline bool focusOrderFollowsLayout(const std::vector<FocusItem>& inFocusOrder) {
    for (std::size_t i = 1; i < inFocusOrder.size(); ++i) {
        const auto& a = inFocusOrder[i - 1];
        const auto& b = inFocusOrder[i];
        if (b.y + 1.0f < a.y) return false;
        if (std::fabs(b.y - a.y) <= 1.0f && b.x < a.x) return false;
    }
    return true;
}

// Screen reader labels (884).
struct Control {
    std::string id, kind, accessibleName;
};

inline std::vector<std::string> missingAccessibleNames(const std::vector<Control>& controls) {
    std::vector<std::string> out;
    for (const auto& c : controls) {
        if ((c.kind == "button" || c.kind == "slider" || c.kind == "checkbox" || c.kind == "textbox") && c.accessibleName.empty()) out.push_back(c.id);
    }
    return out;
}

// Touch controls (906): a virtual stick from a drag, deadzone and clamp.
inline std::pair<float, float> virtualStick(float dx, float dy, float radius, float deadzone) {
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < deadzone * radius || radius <= 0) return {0, 0};
    const float k = std::min(len, radius) / radius / len;
    return {dx * k, dy * k};
}

// Suspend and resume (913).
inline std::vector<std::string> resumeSteps(double secondsAway, bool graphicsLost) {
    std::vector<std::string> s = {"restore window"};
    if (graphicsLost) s.push_back("recreate graphics objects");
    if (secondsAway > 30) s.push_back("reconnect to server");
    s.push_back("resume audio");
    return s;
}

// Small download profile (915).
inline std::vector<std::string> excludeForSmallDownload(const std::vector<std::string>& files) {
    std::vector<std::string> keep;
    for (const auto& f : files) {
        if (f.rfind("docs/", 0) == 0 || f.rfind("samples/", 0) == 0 || f.find(".pdb") != std::string::npos || f.find("4k") != std::string::npos) continue;
        keep.push_back(f);
    }
    return keep;
}

// Reconnect (918): waits grow and stop at a ceiling.
inline std::vector<double> backoffSchedule(int attempts, double first, double ceiling) {
    std::vector<double> out;
    double w = first;
    for (int i = 0; i < attempts; ++i) {
        out.push_back(std::min(w, ceiling));
        w *= 2;
    }
    return out;
}

// Device report (920) and shader fallback (922).
struct GpuCaps {
    bool vulkan13 = false, rayTracing = false, timestamps = false;
    uint32_t memoryMb = 0;
};

inline std::string deviceVerdict(const GpuCaps& g) {
    if (!g.vulkan13) return "not supported: needs Vulkan 1.3";
    if (g.memoryMb < 2048) return "runs with low settings";
    return g.rayTracing ? "great: all features" : "good";
}

inline std::string pickShaderVariant(const GpuCaps& g) { return !g.vulkan13 ? "none" : g.memoryMb < 2048 ? "simple" : "full"; }

// Web notes (930, 933, 934, 935).
inline bool webApiAllowed(const std::string& api) {
    static const std::set<std::string> ok = {"audio", "gamepad", "storage", "fullscreen", "pointerlock"};
    return ok.count(api) > 0;
}

inline bool audioNeedsUserGesture(bool contextSuspended, bool userInteracted) { return contextSuspended && !userInteracted; }

inline std::string pointerLockHint(bool locked, bool escapePressed) { return locked && !escapePressed ? "Press Esc to release the mouse" : "Click to capture the mouse"; }

inline std::vector<std::string> contextLossSteps() { return {"stop rendering", "free GPU objects", "wait for context restore", "rebuild GPU objects", "resume"}; }

// Save paths (931, 941): one portable form with forward slashes and Unix line endings.
inline std::string portablePath(std::string p) {
    std::replace(p.begin(), p.end(), '\\', '/');
    return p;
}

inline std::string normalizeSave(const std::string& text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n') continue;
            out += '\n';
        } else out += text[i];
    }
    return out;
}

// Platform audit (938): lines that include a platform-specific header outside guarded code.
inline std::vector<std::pair<int, std::string>> platformIncludes(const std::string& source) {
    static const std::vector<std::string> heads = {"<windows.h>", "<unistd.h>", "<sys/", "<pthread.h>", "<dlfcn.h>"};
    std::vector<std::pair<int, std::string>> out;
    std::istringstream in(source);
    std::string line;
    int n = 0;
    while (std::getline(in, line)) {
        ++n;
        if (line.find("#include") == std::string::npos) continue;
        for (const auto& h : heads) {
            if (line.find(h) != std::string::npos) out.push_back({n, h});
        }
    }
    return out;
}

// Input adapters (940).
inline std::string actionFor(const std::string& device, const std::string& input) {
    static const std::map<std::string, std::string> m = {{"key:space", "jump"}, {"pad:a", "jump"}, {"touch:button1", "jump"}, {"key:w", "forward"}, {"pad:stick-up", "forward"}, {"touch:stick-up", "forward"}};
    auto it = m.find(device + ":" + input);
    return it == m.end() ? std::string() : it->second;
}

// Project format conformance (944).
inline std::vector<std::string> manifestProblems(const std::map<std::string, std::string>& manifest) {
    std::vector<std::string> out;
    for (const char* k : {"name", "version", "entry"}) {
        if (!manifest.count(k) || manifest.at(k).empty()) out.push_back(std::string("missing ") + k);
    }
    return out;
}

// Feature flags per platform (947).
class FeatureFlags {
public:
    void set(const std::string& flag, bool on, const std::string& platform = "") { flags_[platform + "|" + flag] = on; }
    [[nodiscard]] bool enabled(const std::string& flag, const std::string& platform) const {
        auto p = flags_.find(platform + "|" + flag);
        if (p != flags_.end()) return p->second;
        auto g = flags_.find("|" + flag);
        return g != flags_.end() && g->second;
    }

private:
    std::map<std::string, bool> flags_;
};

// Reflection API docs (766) and tutorial versions (768).
struct DocProperty {
    std::string name, type, description;
};

inline std::string apiReference(const std::string& className, const std::vector<DocProperty>& props) {
    std::string out = "## " + className + "\n\n| Property | Type | Description |\n|---|---|---|\n";
    for (const auto& p : props) out += "| " + p.name + " | " + p.type + " | " + p.description + " |\n";
    return out;
}

inline bool tutorialMatchesRelease(const std::string& tutorialVersion, const std::string& release) {
    auto major_minor = [](const std::string& v) {
        int a = 0, b = 0;
        std::sscanf(v.c_str()[0] == 'v' ? v.c_str() + 1 : v.c_str(), "%d.%d", &a, &b);
        return std::make_pair(a, b);
    };
    return major_minor(tutorialVersion) == major_minor(release);
}

inline std::vector<std::string> sampleProblems(const std::set<std::string>& files) {
    std::vector<std::string> out;
    if (!files.count("project.json")) out.push_back("no project.json");
    bool hasScript = false;
    for (const auto& f : files) hasScript = hasScript || f.rfind("Scripts/", 0) == 0;
    if (!hasScript) out.push_back("no scripts");
    return out;
}

// Licence wording (979): finds claims that call the whole product open source.
inline std::vector<std::pair<int, std::string>> licenceClaims(const std::string& text) {
    std::vector<std::pair<int, std::string>> out;
    std::istringstream in(text);
    std::string line;
    int n = 0;
    while (std::getline(in, line)) {
        ++n;
        std::string low = line;
        for (char& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (low.find("open source") != std::string::npos || low.find("open-source") != std::string::npos) {
            if (low.find("plugin") == std::string::npos) out.push_back({n, line});
        }
    }
    return out;
}

// Data flow diagram (981).
inline std::string dataFlowMermaid(const std::vector<std::pair<std::string, std::string>>& edges) {
    std::string out = "flowchart LR\n";
    for (const auto& [a, b] : edges) out += "  " + a + " --> " + b + "\n";
    return out;
}

// Measurable roadmap (988): items with no number or date in them.
inline std::vector<std::string> unmeasurableItems(const std::vector<std::string>& items) {
    std::vector<std::string> out;
    for (const auto& i : items) {
        if (std::none_of(i.begin(), i.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })) out.push_back(i);
    }
    return out;
}

} // namespace engine::core::qa
