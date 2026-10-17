#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "core/AssetPipelineTools.hpp"

namespace engine::core::audionet {

// ---- Waveform editing (352, 353, 354)
inline std::vector<float> trimSamples(const std::vector<float>& s, std::size_t from, std::size_t to) {
    if (from >= s.size() || to <= from) return {};
    return {s.begin() + static_cast<std::ptrdiff_t>(from), s.begin() + static_cast<std::ptrdiff_t>(std::min(to, s.size()))};
}

inline void fadeIn(std::vector<float>& s, std::size_t samples) {
    for (std::size_t i = 0; i < samples && i < s.size(); ++i) s[i] *= static_cast<float>(i) / static_cast<float>(samples);
}

inline void fadeOut(std::vector<float>& s, std::size_t samples) {
    for (std::size_t i = 0; i < samples && i < s.size(); ++i) s[s.size() - 1 - i] *= static_cast<float>(i) / static_cast<float>(samples);
}

struct Region {
    std::size_t start, length;
};

inline std::pair<Region, Region> splitRegion(const Region& r, std::size_t at) {
    at = std::clamp(at, r.start, r.start + r.length);
    return {{r.start, at - r.start}, {at, r.start + r.length - at}};
}

inline float envelopeAt(const std::vector<std::pair<float, float>>& points, float t) {
    if (points.empty()) return 1;
    if (t <= points.front().first) return points.front().second;
    for (std::size_t i = 1; i < points.size(); ++i) {
        if (t <= points[i].first) {
            const float span = points[i].first - points[i - 1].first;
            return span <= 0 ? points[i].second : points[i - 1].second + (points[i].second - points[i - 1].second) * (t - points[i - 1].first) / span;
        }
    }
    return points.back().second;
}

// Tempo grid (357).
inline double beatsToSeconds(double beats, double bpm) { return beats * 60.0 / bpm; }

inline double snapToBeat(double seconds, double bpm, int subdivisions) {
    const double step = 60.0 / bpm / std::max(subdivisions, 1);
    return std::round(seconds / step) * step;
}

inline double barLengthSeconds(double bpm, int beatsPerBar) { return beatsToSeconds(beatsPerBar, bpm); }

// Pitch and time (360): resampling ratio for a pitch change in semitones (changes speed too).
inline double semitoneRatio(double semitones) { return std::pow(2.0, semitones / 12.0); }

inline std::vector<float> resampleLinear(const std::vector<float>& s, double ratio) {
    std::vector<float> out;
    if (s.empty() || ratio <= 0) return out;
    for (double pos = 0; pos < static_cast<double>(s.size() - 1); pos += ratio) {
        const std::size_t i = static_cast<std::size_t>(pos);
        const float f = static_cast<float>(pos - static_cast<double>(i));
        out.push_back(s[i] * (1 - f) + s[i + 1] * f);
    }
    return out;
}

// Noise gate (362) and sidechain ducking (365).
inline std::vector<float> noiseGate(std::vector<float> s, float threshold, std::size_t holdSamples) {
    std::size_t hold = 0;
    for (auto& x : s) {
        if (std::fabs(x) >= threshold) hold = holdSamples;
        else if (hold > 0) --hold;
        else x = 0;
    }
    return s;
}

inline float duckGain(float sidechainLevel, float threshold, float amount) { return sidechainLevel > threshold ? 1.0f - amount : 1.0f; }

// MIDI (371).
inline double midiToHz(int note) { return 440.0 * std::pow(2.0, (note - 69) / 12.0); }

struct MidiNote {
    double start, length;
    int pitch;
};

inline void quantizeNotes(std::vector<MidiNote>& notes, double grid) {
    for (auto& n : notes) n.start = std::round(n.start / grid) * grid;
}

// ---- Mixer (376, 377)
class BusGraph {
public:
    void add(const std::string& bus, const std::string& output, float gainDb) { buses_[bus] = {output, gainDb}; }
    [[nodiscard]] bool hasCycle() const {
        for (const auto& kv : buses_) {
            std::set<std::string> seen;
            std::string cur = kv.first;
            while (buses_.count(cur)) {
                if (!seen.insert(cur).second) return true;
                cur = buses_.at(cur).first;
            }
        }
        return false;
    }
    [[nodiscard]] float totalGainDb(const std::string& bus) const {
        float db = 0;
        std::string cur = bus;
        for (int guard = 0; guard < 32 && buses_.count(cur); ++guard) {
            db += buses_.at(cur).second;
            cur = buses_.at(cur).first;
        }
        return db;
    }

private:
    std::map<std::string, std::pair<std::string, float>> buses_;
};

inline float dbToGain(float db) { return std::pow(10.0f, db / 20.0f); }

inline float categoryGain(const std::map<std::string, float>& sliders, const std::string& category) {
    const float master = sliders.count("master") ? sliders.at("master") : 1.0f;
    return master * (sliders.count(category) ? sliders.at(category) : 1.0f);
}

// ---- 3D sound (378, 379, 380, 382, 387, 388)
inline float attenuation(const std::string& model, float distance, float minDist, float maxDist) {
    if (distance <= minDist) return 1;
    if (distance >= maxDist) return 0;
    if (model == "linear") return 1.0f - (distance - minDist) / (maxDist - minDist);
    if (model == "exponential") return std::pow(distance / minDist, -1.5f);
    return minDist / distance;
}

inline float coneGain(float angleDeg, float innerDeg, float outerDeg, float outerGain) {
    angleDeg = std::fabs(angleDeg);
    if (angleDeg <= innerDeg * 0.5f) return 1;
    if (angleDeg >= outerDeg * 0.5f) return outerGain;
    const float t = (angleDeg - innerDeg * 0.5f) / ((outerDeg - innerDeg) * 0.5f);
    return 1.0f + (outerGain - 1.0f) * t;
}

inline float occlusionCutoffHz(float obstruction) { return 22000.0f * std::pow(1.0f - std::clamp(obstruction, 0.0f, 0.95f), 3.0f) + 400.0f; }

inline float dopplerPitch(float sourceSpeedToward, float listenerSpeedToward, float soundSpeed = 343.0f) {
    return (soundSpeed + listenerSpeedToward) / std::max(soundSpeed - sourceSpeedToward, 1.0f);
}

// Interaural time difference with the Woodworth model (387, 388): seconds, positive when the sound is on the right.
inline float interauralTimeDifference(float azimuthDeg, float headRadius = 0.0875f) {
    const float a = std::clamp(azimuthDeg, -90.0f, 90.0f) * 0.0174532925f;
    return headRadius / 343.0f * (std::sin(a) + a);
}

// ---- Scripting (404, 407, 415, 417, 429, 432, 482, 497, 504, 507, 513, 518)
inline int activeParameter(const std::string& text, std::size_t cursor) {
    int depth = 0, commas = 0;
    for (std::size_t i = 0; i < cursor && i < text.size(); ++i) {
        if (text[i] == '(') { depth = 1; commas = 0; }
        else if (text[i] == ')') depth = 0;
        else if (text[i] == ',' && depth == 1) ++commas;
    }
    return depth == 1 ? commas : -1;
}

inline std::string renameSymbol(const std::string& source, const std::string& from, const std::string& to, int* count = nullptr) {
    auto isWord = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
    std::string out;
    int n = 0;
    for (std::size_t i = 0; i < source.size();) {
        if (source.compare(i, from.size(), from) == 0 && (i == 0 || !isWord(source[i - 1])) && (i + from.size() >= source.size() || !isWord(source[i + from.size()]))) {
            out += to;
            i += from.size();
            ++n;
        } else out += source[i++];
    }
    if (count) *count = n;
    return out;
}

struct BreakpointKey {
    std::string file, language;
    int line;
    bool operator<(const BreakpointKey& o) const { return std::tie(file, line, language) < std::tie(o.file, o.line, o.language); }
};

class Breakpoints {
public:
    void toggle(const BreakpointKey& k) { if (!set_.erase(k)) set_.insert(k); }
    [[nodiscard]] bool hit(const std::string& file, const std::string& language, int line) const { return set_.count({file, language, line}) > 0; }
    [[nodiscard]] std::size_t size() const { return set_.size(); }

private:
    std::set<BreakpointKey> set_;
};

inline std::vector<std::string> formatCallStack(const std::vector<std::pair<std::string, int>>& frames) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < frames.size(); ++i) out.push_back("#" + std::to_string(i) + " " + frames[i].first + ":" + std::to_string(frames[i].second));
    return out;
}

inline std::string typeStubs(const std::string& className, const std::vector<std::pair<std::string, std::string>>& props) {
    std::string out = "declare class " + className + "\n";
    for (const auto& [n, t] : props) out += "    " + n + ": " + t + "\n";
    return out + "end\n";
}

// Subscriptions that can be cancelled (432).
class Subscriptions {
public:
    using Handler = std::function<void(int)>;
    int subscribe(Handler h) {
        const int id = next_++;
        handlers_[id] = std::move(h);
        return id;
    }
    bool cancel(int id) { return handlers_.erase(id) > 0; }
    int fire(int value) {
        std::vector<Handler> copy;
        for (auto& kv : handlers_) copy.push_back(kv.second);
        for (auto& h : copy) h(value);
        return static_cast<int>(copy.size());
    }

private:
    int next_ = 1;
    std::map<int, Handler> handlers_;
};

// Bug report recorder (497).
class EventRecorder {
public:
    explicit EventRecorder(std::size_t cap = 200) : cap_(cap) {}
    void record(const std::string& line) {
        events_.push_back(line);
        if (events_.size() > cap_) events_.pop_front();
    }
    [[nodiscard]] std::string dump() const {
        std::string s;
        for (const auto& e : events_) s += e + "\n";
        return s;
    }
    [[nodiscard]] std::size_t size() const { return events_.size(); }

private:
    std::size_t cap_;
    std::deque<std::string> events_;
};

// Unknown properties survive a round trip (504).
struct PropertyBag {
    std::map<std::string, std::string> known, unknown;
};

inline PropertyBag splitProperties(const std::map<std::string, std::string>& all, const std::set<std::string>& knownNames) {
    PropertyBag b;
    for (const auto& [k, v] : all) (knownNames.count(k) ? b.known : b.unknown)[k] = v;
    return b;
}

inline std::map<std::string, std::string> mergeProperties(const PropertyBag& b) {
    auto out = b.unknown;
    for (const auto& kv : b.known) out[kv.first] = kv.second;
    return out;
}

// Constraint class mapping (513) and rewrite suggestions (518).
inline std::string mapConstraint(const std::string& robloxClass) {
    static const std::map<std::string, std::string> m = {{"WeldConstraint", "fixed joint"}, {"HingeConstraint", "hinge joint"}, {"BallSocketConstraint", "ball joint"}, {"PrismaticConstraint", "slider joint"}, {"RodConstraint", "distance joint"}, {"SpringConstraint", "spring joint"}};
    auto it = m.find(robloxClass);
    return it == m.end() ? std::string() : it->second;
}

inline std::string rewriteSuggestion(const std::string& api) {
    static const std::map<std::string, std::string> m = {{"PathfindingService", "Move toward the target with a simple raycast loop until pathfinding is supported."}, {"HttpService:GetAsync", "Remove web requests; Kronos games run offline."}, {"BindableFunction", "Use a BindableEvent plus a variable for the reply."}};
    auto it = m.find(api);
    return it == m.end() ? std::string() : it->second;
}

// Downloads (529, 557, 562).
inline std::string rangeHeader(uint64_t haveBytes) { return "bytes=" + std::to_string(haveBytes) + "-"; }

inline bool downloadComplete(const std::vector<uint8_t>& bytes, const std::string& expectedHex) { return assets::hex64(assets::contentHash(bytes)) == expectedHex; }

inline std::string dependencyManifest(const std::vector<std::pair<std::string, std::string>>& libs) {
    std::string out;
    for (const auto& [n, v] : libs) out += n + " " + v + "\n";
    return out;
}

// ---- Controller navigation (530, 531): picks the nearest item in a direction.
struct UiItem {
    int id;
    float x, y;
};

inline int navigateDirection(const std::vector<UiItem>& items, int from, const std::string& dir) {
    const UiItem* cur = nullptr;
    for (const auto& i : items) if (i.id == from) cur = &i;
    if (!cur) return items.empty() ? -1 : items.front().id;
    int best = from;
    float bestScore = 1e30f;
    for (const auto& i : items) {
        if (i.id == from) continue;
        const float dx = i.x - cur->x, dy = i.y - cur->y;
        float along = 0, across = 0;
        if (dir == "right") { along = dx; across = dy; }
        else if (dir == "left") { along = -dx; across = dy; }
        else if (dir == "down") { along = dy; across = dx; }
        else if (dir == "up") { along = -dy; across = dx; }
        if (along <= 0) continue;
        const float score = along + 2.0f * std::fabs(across);
        if (score < bestScore) { bestScore = score; best = i.id; }
    }
    return best;
}

struct GraphicsSettings {
    int shadowQuality, textureQuality, viewDistance;
    bool vsync;
};

inline GraphicsSettings graphicsPreset(const std::string& name) {
    if (name == "low") return {0, 0, 150, true};
    if (name == "medium") return {1, 1, 300, true};
    if (name == "high") return {2, 2, 600, true};
    return {1, 1, 300, true};
}

// ---- Networking (576 to 582, 586)
struct MoveInput {
    int seq;
    float dx, dz;
};

// The server applies an input but never lets a player move faster than the limit.
inline std::pair<float, float> applyMoveAuthoritative(std::pair<float, float> pos, const MoveInput& in, float maxStep) {
    float dx = in.dx, dz = in.dz;
    const float len = std::sqrt(dx * dx + dz * dz);
    if (len > maxStep && len > 0) {
        dx *= maxStep / len;
        dz *= maxStep / len;
    }
    return {pos.first + dx, pos.second + dz};
}

// Priority classes (577): bytes per tick split by weight; leftovers go to higher classes first.
inline std::map<std::string, std::size_t> allocateBandwidth(std::size_t budget, const std::vector<std::pair<std::string, int>>& classWeights, const std::map<std::string, std::size_t>& wanted) {
    int total = 0;
    for (const auto& c : classWeights) total += c.second;
    std::map<std::string, std::size_t> out;
    std::size_t left = budget;
    for (const auto& [name, w] : classWeights) {
        const std::size_t share = total > 0 ? budget * static_cast<std::size_t>(w) / static_cast<std::size_t>(total) : 0;
        const std::size_t want = wanted.count(name) ? wanted.at(name) : 0;
        out[name] = std::min(share, want);
        left -= out[name];
    }
    for (const auto& [name, w] : classWeights) {
        (void)w;
        const std::size_t want = wanted.count(name) ? wanted.at(name) : 0;
        const std::size_t extra = std::min(left, want - out[name]);
        out[name] += extra;
        left -= extra;
    }
    return out;
}

// Delta encoding (578): only fields that differ from the baseline are sent.
inline std::vector<std::pair<int, int32_t>> deltaEncode(const std::vector<int32_t>& baseline, const std::vector<int32_t>& current) {
    std::vector<std::pair<int, int32_t>> out;
    for (std::size_t i = 0; i < current.size(); ++i) {
        if (i >= baseline.size() || baseline[i] != current[i]) out.push_back({static_cast<int>(i), current[i]});
    }
    return out;
}

inline std::vector<int32_t> deltaApply(std::vector<int32_t> baseline, const std::vector<std::pair<int, int32_t>>& delta) {
    for (const auto& [i, v] : delta) {
        if (i >= 0) {
            if (static_cast<std::size_t>(i) >= baseline.size()) baseline.resize(static_cast<std::size_t>(i) + 1);
            baseline[static_cast<std::size_t>(i)] = v;
        }
    }
    return baseline;
}

// Interest management (579): entities in the player's cell and neighbours.
inline std::vector<int> relevantEntities(const std::vector<std::pair<int, std::pair<float, float>>>& entities, float px, float pz, float cellSize) {
    const int cx = static_cast<int>(std::floor(px / cellSize)), cz = static_cast<int>(std::floor(pz / cellSize));
    std::vector<int> out;
    for (const auto& [id, pos] : entities) {
        const int ex = static_cast<int>(std::floor(pos.first / cellSize)), ez = static_cast<int>(std::floor(pos.second / cellSize));
        if (std::abs(ex - cx) <= 1 && std::abs(ez - cz) <= 1) out.push_back(id);
    }
    return out;
}

// Prediction and reconciliation (581, 582).
inline std::pair<float, float> reconcile(std::pair<float, float> serverPos, int serverSeq, const std::vector<MoveInput>& pending, float maxStep) {
    auto pos = serverPos;
    for (const auto& in : pending) {
        if (in.seq > serverSeq) pos = applyMoveAuthoritative(pos, in, maxStep);
    }
    return pos;
}

struct ReconcileStats {
    int corrections = 0;
    float worstError = 0, totalError = 0;
};

inline void recordReconcile(ReconcileStats& s, std::pair<float, float> predicted, std::pair<float, float> corrected) {
    const float e = std::hypot(predicted.first - corrected.first, predicted.second - corrected.second);
    if (e > 0.001f) {
        ++s.corrections;
        s.totalError += e;
        s.worstError = std::max(s.worstError, e);
    }
}

// Lag compensation (586): positions are kept for a short time and looked up at the shooter's view time.
class PositionHistory {
public:
    void record(double time, float x, float z) {
        samples_.push_back({time, x, z});
        while (samples_.size() > 120) samples_.pop_front();
    }
    bool at(double time, float& x, float& z) const {
        if (samples_.empty() || time < samples_.front().t) return false;
        for (std::size_t i = 1; i < samples_.size(); ++i) {
            if (samples_[i].t >= time) {
                const double span = samples_[i].t - samples_[i - 1].t;
                const float f = span > 0 ? static_cast<float>((time - samples_[i - 1].t) / span) : 0.0f;
                x = samples_[i - 1].x + (samples_[i].x - samples_[i - 1].x) * f;
                z = samples_[i - 1].z + (samples_[i].z - samples_[i - 1].z) * f;
                return true;
            }
        }
        x = samples_.back().x;
        z = samples_.back().z;
        return true;
    }

private:
    struct S {
        double t;
        float x, z;
    };
    std::deque<S> samples_;
};

// Server launcher and supervisor (601, 602).
inline std::vector<std::string> serverCommand(const std::string& exe, const std::string& game, int port, int maxPlayers) {
    return {exe, "--server", "--headless", "--game", game, "--port", std::to_string(port), "--max-players", std::to_string(maxPlayers)};
}

class Supervisor {
public:
    Supervisor(int maxRestarts, double windowSeconds) : max_(maxRestarts), window_(windowSeconds) {}
    // Returns true when the server should be restarted.
    bool onExit(double now, bool wasClean) {
        if (wasClean) return false;
        crashes_.push_back(now);
        while (!crashes_.empty() && now - crashes_.front() > window_) crashes_.pop_front();
        return static_cast<int>(crashes_.size()) <= max_;
    }

private:
    int max_;
    double window_;
    std::deque<double> crashes_;
};

// Presence colours (678).
inline std::string presenceColour(int userIndex) {
    static const std::vector<std::string> palette = {"#e6194b", "#3cb44b", "#4363d8", "#f58231", "#911eb4", "#42d4f4", "#f032e6", "#bfef45"};
    return palette[static_cast<std::size_t>(std::abs(userIndex)) % palette.size()];
}

// Plugin manifest (701 to 707).
struct PluginManifest {
    std::string id, version;
    std::set<std::string> permissions;
};

inline std::vector<std::string> manifestIssues(const PluginManifest& m, const std::set<std::string>& knownPermissions) {
    std::vector<std::string> out;
    if (m.id.empty()) out.push_back("missing id");
    if (m.version.empty()) out.push_back("missing version");
    for (const auto& p : m.permissions) {
        if (!knownPermissions.count(p)) out.push_back("unknown permission " + p);
    }
    return out;
}

inline std::set<std::string> negotiate(const std::set<std::string>& hostHas, const std::set<std::string>& pluginWants, std::vector<std::string>* missing = nullptr) {
    std::set<std::string> ok;
    for (const auto& w : pluginWants) {
        if (hostHas.count(w)) ok.insert(w);
        else if (missing) missing->push_back(w);
    }
    return ok;
}

class Watchdog {
public:
    explicit Watchdog(double timeoutSeconds) : timeout_(timeoutSeconds) {}
    void heartbeat(double now) { last_ = now; }
    [[nodiscard]] bool healthy(double now) const { return now - last_ <= timeout_; }

private:
    double timeout_, last_ = 0;
};

// Automatic disable (707): too many crashes in a window.
inline bool shouldDisablePlugin(const std::vector<double>& crashTimes, double now, int maxCrashes, double windowSeconds) {
    int n = 0;
    for (double t : crashTimes) {
        if (now - t <= windowSeconds) ++n;
    }
    return n >= maxCrashes;
}

// ---- Safety and performance (832, 854, 857)
inline std::vector<std::pair<int, std::string>> findSecrets(const std::string& text) {
    static const std::vector<std::string> needles = {"-----BEGIN PRIVATE KEY", "-----BEGIN RSA PRIVATE KEY", "AKIA", "ghp_", "sk-", "password=", "api_key="};
    std::vector<std::pair<int, std::string>> out;
    std::istringstream in(text);
    std::string line;
    int n = 0;
    while (std::getline(in, line)) {
        ++n;
        for (const auto& s : needles) {
            if (line.find(s) != std::string::npos) out.push_back({n, s});
        }
    }
    return out;
}

class AllocationTags {
public:
    void add(const std::string& tag, std::size_t bytes) { bytes_[tag] += bytes; }
    [[nodiscard]] std::vector<std::pair<std::string, std::size_t>> top(std::size_t n) const {
        std::vector<std::pair<std::string, std::size_t>> v(bytes_.begin(), bytes_.end());
        std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        if (v.size() > n) v.resize(n);
        return v;
    }

private:
    std::map<std::string, std::size_t> bytes_;
};

// ---- Controls (876 to 879)
class KeyMap {
public:
    void bind(const std::string& action, const std::string& key) { map_[action] = key; }
    [[nodiscard]] std::string keyFor(const std::string& action) const { return map_.count(action) ? map_.at(action) : std::string(); }
    [[nodiscard]] std::vector<std::string> duplicates() const {
        std::map<std::string, int> n;
        for (const auto& kv : map_) ++n[kv.second];
        std::vector<std::string> out;
        for (const auto& kv : n) {
            if (kv.second > 1) out.push_back(kv.first);
        }
        return out;
    }
    [[nodiscard]] std::string serialize() const {
        std::string s;
        for (const auto& kv : map_) s += kv.first + "=" + kv.second + "\n";
        return s;
    }
    static KeyMap parse(const std::string& text) {
        KeyMap k;
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line)) {
            const std::size_t eq = line.find('=');
            if (eq != std::string::npos) k.map_[line.substr(0, eq)] = line.substr(eq + 1);
        }
        return k;
    }

private:
    std::map<std::string, std::string> map_;
};

inline float sensitivityPreset(const std::string& name) { return name == "low" ? 0.5f : name == "high" ? 2.0f : name == "very high" ? 3.0f : 1.0f; }

inline std::pair<float, float> applyLook(float dx, float dy, float sensitivity, bool invertX, bool invertY) { return {(invertX ? -dx : dx) * sensitivity, (invertY ? -dy : dy) * sensitivity}; }

// ---- Device tiers and budgets (901 to 904, 929)
inline std::string deviceTier(uint32_t vramMb, int cpuCores, bool discrete) {
    if (discrete && vramMb >= 8192 && cpuCores >= 8) return "high";
    if (vramMb >= 3072 && cpuCores >= 4) return "medium";
    return "low";
}

class DynamicResolution {
public:
    float update(float gpuMs, float targetMs) {
        if (gpuMs > targetMs * 1.1f) scale_ = std::max(0.5f, scale_ - 0.05f);
        else if (gpuMs < targetMs * 0.8f) scale_ = std::min(1.0f, scale_ + 0.025f);
        return scale_;
    }
    [[nodiscard]] float scale() const { return scale_; }

private:
    float scale_ = 1.0f;
};

// Mip levels to drop so textures fit a memory budget.
inline int mipsToDrop(uint64_t totalBytes, uint64_t budgetBytes) {
    int drop = 0;
    while (totalBytes > budgetBytes && drop < 6) {
        totalBytes /= 4;
        ++drop;
    }
    return drop;
}

struct LowEffects {
    bool shadows, ssao, bloom, motionBlur;
};

inline LowEffects reducedEffects(bool reducedShadows, bool reducedPostProcess) { return {!reducedShadows, !reducedPostProcess, !reducedPostProcess, !reducedPostProcess}; }

inline std::vector<std::string> overBundleBudget(const std::vector<std::pair<std::string, uint64_t>>& files, uint64_t budgetBytes) {
    std::vector<std::string> out;
    uint64_t total = 0;
    for (const auto& [n, b] : files) total += b;
    if (total > budgetBytes) {
        auto sorted = files;
        std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        for (std::size_t i = 0; i < sorted.size() && i < 3; ++i) out.push_back(sorted[i].first);
    }
    return out;
}

} // namespace engine::core::audionet
