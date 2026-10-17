#pragma once

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace engine::core::collab {

// Three-way text merge by lines (684, 695): returns false and marks conflicts when both sides changed the same line.
inline bool mergeLines(const std::vector<std::string>& base, const std::vector<std::string>& mine, const std::vector<std::string>& theirs, std::vector<std::string>& out) {
    out.clear();
    bool clean = true;
    const std::size_t n = std::max({base.size(), mine.size(), theirs.size()});
    for (std::size_t i = 0; i < n; ++i) {
        const std::string b = i < base.size() ? base[i] : std::string(), m = i < mine.size() ? mine[i] : std::string(), t = i < theirs.size() ? theirs[i] : std::string();
        const bool hasM = i < mine.size(), hasT = i < theirs.size();
        if (m == t) {
            if (hasM) out.push_back(m);
        } else if (m == b) {
            if (hasT) out.push_back(t);
        } else if (t == b) {
            if (hasM) out.push_back(m);
        } else {
            clean = false;
            out.push_back("<<<<<<< mine");
            out.push_back(m);
            out.push_back("=======");
            out.push_back(t);
            out.push_back(">>>>>>> theirs");
        }
    }
    return clean;
}

// Locks with leases (685): a lock expires if not renewed.
class LeaseTable {
public:
    bool acquire(const std::string& what, const std::string& who, double now, double seconds) {
        auto it = leases_.find(what);
        if (it != leases_.end() && it->second.expires > now && it->second.owner != who) return false;
        leases_[what] = {who, now + seconds};
        return true;
    }
    void release(const std::string& what, const std::string& who) {
        auto it = leases_.find(what);
        if (it != leases_.end() && it->second.owner == who) leases_.erase(it);
    }
    [[nodiscard]] std::string holder(const std::string& what, double now) const {
        auto it = leases_.find(what);
        return it != leases_.end() && it->second.expires > now ? it->second.owner : std::string();
    }

private:
    struct Lease {
        std::string owner;
        double expires;
    };
    std::map<std::string, Lease> leases_;
};

// Roles (688).
enum class Role { Viewer, Editor, Owner };

inline bool roleCan(Role role, const std::string& action) {
    if (action == "view") return true;
    if (action == "edit" || action == "import") return role != Role::Viewer;
    if (action == "invite" || action == "delete-project") return role == Role::Owner;
    return false;
}

// Import status (690), attribution timeline (693), audit log (694).
struct SessionEvent {
    double time;
    std::string user, action;
};

class SessionLog {
public:
    void add(double t, const std::string& user, const std::string& action) { events_.push_back({t, user, action}); }
    [[nodiscard]] std::vector<SessionEvent> byUser(const std::string& user) const {
        std::vector<SessionEvent> out;
        for (const auto& e : events_) {
            if (e.user == user) out.push_back(e);
        }
        return out;
    }
    [[nodiscard]] std::map<std::string, int> counts() const {
        std::map<std::string, int> c;
        for (const auto& e : events_) ++c[e.user];
        return c;
    }
    [[nodiscard]] std::string text() const {
        std::string s;
        for (const auto& e : events_) s += std::to_string(static_cast<long long>(e.time)) + " " + e.user + " " + e.action + "\n";
        return s;
    }

private:
    std::vector<SessionEvent> events_;
};

inline std::string importStatusLine(const std::string& user, const std::string& file, int percent) { return user + " is importing " + file + " (" + std::to_string(percent) + "%)"; }

// Sync benchmark (697): bytes per second for a scene change rate.
inline double syncBytesPerSecond(int changedObjectsPerSecond, int bytesPerObject, int clients) { return static_cast<double>(changedObjectsPerSecond) * bytesPerObject * std::max(clients - 1, 0); }

// Convergence harness (698): last-writer-wins registers merged from several clients end equal.
struct Register {
    std::string value;
    double stamp = 0;
    std::string writer;
};

inline Register mergeRegister(const Register& a, const Register& b) {
    if (a.stamp != b.stamp) return a.stamp > b.stamp ? a : b;
    return a.writer >= b.writer ? a : b;
}

// Plugin telemetry (706).
class PluginMeter {
public:
    void add(const std::string& plugin, double cpuMs, double memoryMb) {
        auto& e = data_[plugin];
        e.cpuMs += cpuMs;
        e.memoryMb = std::max(e.memoryMb, memoryMb);
    }
    [[nodiscard]] std::string heaviest() const {
        std::string best;
        double most = -1;
        for (const auto& [k, v] : data_) {
            if (v.cpuMs > most) {
                most = v.cpuMs;
                best = k;
            }
        }
        return best;
    }

private:
    struct Entry {
        double cpuMs = 0, memoryMb = 0;
    };
    std::map<std::string, Entry> data_;
};

// Version ranges (708): "1.2.0-2.0.0" inclusive of the low end, exclusive of the high.
inline bool versionInRange(const std::string& range, int major, int minor, int patch) {
    const std::size_t dash = range.find('-');
    if (dash == std::string::npos) return false;
    int a1 = 0, a2 = 0, a3 = 0, b1 = 0, b2 = 0, b3 = 0;
    std::sscanf(range.substr(0, dash).c_str(), "%d.%d.%d", &a1, &a2, &a3);
    std::sscanf(range.substr(dash + 1).c_str(), "%d.%d.%d", &b1, &b2, &b3);
    const auto v = std::make_tuple(major, minor, patch);
    return v >= std::make_tuple(a1, a2, a3) && v < std::make_tuple(b1, b2, b3);
}

// Extension registries (711 to 718): one template for commands, menu items, editors, gizmos, importers, exporters, render passes, languages.
template <typename T>
class Registry {
public:
    bool add(const std::string& id, T item) {
        if (items_.count(id)) return false;
        items_[id] = std::move(item);
        order_.push_back(id);
        return true;
    }
    bool remove(const std::string& id) {
        if (!items_.erase(id)) return false;
        order_.erase(std::remove(order_.begin(), order_.end(), id), order_.end());
        return true;
    }
    [[nodiscard]] const T* find(const std::string& id) const {
        auto it = items_.find(id);
        return it == items_.end() ? nullptr : &it->second;
    }
    [[nodiscard]] const std::vector<std::string>& ids() const { return order_; }
    [[nodiscard]] std::size_t size() const { return items_.size(); }

private:
    std::map<std::string, T> items_;
    std::vector<std::string> order_;
};

// Menu extension points (712): "Tools/My Plugin/Run" creates the path.
inline std::vector<std::string> menuPath(const std::string& path) {
    std::vector<std::string> parts;
    std::istringstream in(path);
    std::string p;
    while (std::getline(in, p, '/')) {
        if (!p.empty()) parts.push_back(p);
    }
    return parts;
}

// Importer by extension (715, 716).
inline std::string handlerForExtension(const std::map<std::string, std::string>& byExt, const std::string& file) {
    const std::size_t dot = file.find_last_of('.');
    if (dot == std::string::npos) return {};
    std::string ext = file.substr(dot + 1);
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    auto it = byExt.find(ext);
    return it == byExt.end() ? std::string() : it->second;
}

// Render pass ordering (717): passes sorted by priority, ties keep registration order.
struct PassEntry {
    std::string name;
    int priority;
};

inline std::vector<std::string> orderPasses(std::vector<PassEntry> passes) {
    std::stable_sort(passes.begin(), passes.end(), [](const PassEntry& a, const PassEntry& b) { return a.priority < b.priority; });
    std::vector<std::string> out;
    for (const auto& p : passes) out.push_back(p.name);
    return out;
}

// Project validators from plugins (719): each returns messages; a throwing validator becomes one message.
inline std::vector<std::string> runValidators(const std::vector<std::pair<std::string, std::function<std::vector<std::string>()>>>& validators) {
    std::vector<std::string> out;
    for (const auto& [name, fn] : validators) {
        try {
            for (const auto& m : fn()) out.push_back(name + ": " + m);
        } catch (const std::exception& e) {
            out.push_back(name + " failed: " + e.what());
        }
    }
    return out;
}

} // namespace engine::core::collab
