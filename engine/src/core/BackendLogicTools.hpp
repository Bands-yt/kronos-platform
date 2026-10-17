#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// Logic for backend features, written and tested here without any server. Nothing in this file talks to a network or a database.
namespace engine::core::backend {

// ---- Servers and matchmaking (606 to 624)
inline int desiredServers(int current, double utilization, int minServers, int maxServers, double scaleUpAt = 0.8, double scaleDownAt = 0.3) {
    if (utilization > scaleUpAt) return std::min(current + 1, maxServers);
    if (utilization < scaleDownAt) return std::max(current - 1, minServers);
    return std::clamp(current, minServers, maxServers);
}

struct RegionInfo {
    std::string name;
    int pingMs;
    int freeSlots;
    bool up = true;
};

inline std::string pickRegion(const std::vector<RegionInfo>& regions, int neededSlots = 1) {
    const RegionInfo* best = nullptr;
    for (const auto& r : regions) {
        if (!r.up || r.freeSlots < neededSlots) continue;
        if (!best || r.pingMs < best->pingMs) best = &r;
    }
    return best ? best->name : std::string();
}

// Failover (619): same rule with the failed region marked down; returns empty when nothing is left.
inline std::string failoverRegion(std::vector<RegionInfo> regions, const std::string& failed) {
    for (auto& r : regions) {
        if (r.name == failed) r.up = false;
    }
    return pickRegion(regions);
}

// Parties (608): parties are put into matches of `size` players without splitting a party.
inline std::vector<std::vector<int>> groupParties(std::vector<std::pair<int, int>> parties, int size) { // (partyId, members)
    std::sort(parties.begin(), parties.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    std::vector<std::vector<int>> matches;
    std::vector<int> fill;
    for (const auto& [id, n] : parties) {
        if (n > size) continue;
        bool placed = false;
        for (std::size_t i = 0; i < matches.size(); ++i) {
            if (fill[i] + n <= size) {
                matches[i].push_back(id);
                fill[i] += n;
                placed = true;
                break;
            }
        }
        if (!placed) {
            matches.push_back({id});
            fill.push_back(n);
        }
    }
    return matches;
}

// Matching plugin (609): a scoring function decides who fits together; the best partner is picked.
struct Player {
    int id, skill, pingMs;
};
using ScoreFn = std::function<double(const Player&, const Player&)>;

inline double defaultScore(const Player& a, const Player& b) { return -std::abs(a.skill - b.skill) - 0.1 * std::abs(a.pingMs - b.pingMs); }

inline int bestPartner(const Player& p, const std::vector<Player>& pool, const ScoreFn& score) {
    int best = -1;
    double bestScore = -1e30;
    for (const auto& o : pool) {
        if (o.id == p.id) continue;
        const double s = score(p, o);
        if (s > bestScore) {
            bestScore = s;
            best = o.id;
        }
    }
    return best;
}

// Queue with a timeout (610, 622): wait estimate from recent service times.
class MatchQueue {
public:
    MatchQueue(std::size_t capacity, double timeoutSeconds) : cap_(capacity), timeout_(timeoutSeconds) {}
    bool join(int id, double now) {
        if (q_.size() >= cap_) return false;
        q_.push_back({id, now});
        return true;
    }
    std::vector<int> expire(double now) {
        std::vector<int> gone;
        while (!q_.empty() && now - q_.front().second > timeout_) {
            gone.push_back(q_.front().first);
            q_.pop_front();
        }
        return gone;
    }
    int pop() {
        if (q_.empty()) return -1;
        const int id = q_.front().first;
        q_.pop_front();
        return id;
    }
    void recordServed(double waitSeconds) { avgWait_ = avgWait_ < 0 ? waitSeconds : avgWait_ * 0.8 + waitSeconds * 0.2; }
    [[nodiscard]] int position(int id) const {
        for (std::size_t i = 0; i < q_.size(); ++i) {
            if (q_[i].first == id) return static_cast<int>(i) + 1;
        }
        return 0;
    }
    [[nodiscard]] double estimatedWait(int id) const { return avgWait_ < 0 ? -1.0 : avgWait_ * position(id); }
    [[nodiscard]] std::size_t size() const { return q_.size(); }

private:
    std::size_t cap_;
    double timeout_, avgWait_ = -1;
    std::deque<std::pair<int, double>> q_;
};

// Drain and rolling update (612).
enum class ServerState { Running, Draining, Stopped };

struct RollingStep {
    int server;
    std::string action;
};

inline std::vector<RollingStep> rollingUpdatePlan(const std::vector<std::pair<int, int>>& serversWithPlayers, int maxUnavailable) { // (id, players)
    auto sorted = serversWithPlayers;
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
    std::vector<RollingStep> out;
    int batch = 0;
    for (const auto& s : sorted) {
        out.push_back({s.first, "drain"});
        out.push_back({s.first, "replace"});
        if (++batch >= maxUnavailable) {
            out.push_back({-1, "wait until healthy"});
            batch = 0;
        }
    }
    return out;
}

// Restart budget (613): alert when the budget is spent.
inline std::string restartDecision(int restartsInWindow, int budget) { return restartsInWindow < budget ? "restart" : restartsInWindow == budget ? "restart and alert" : "stop and alert"; }

// Instance metrics (614) and cost (615).
struct InstanceMetrics {
    double cpuPercent, memoryMb;
    int players;
};

inline InstanceMetrics aggregateMetrics(const std::vector<InstanceMetrics>& all) {
    InstanceMetrics m{0, 0, 0};
    if (all.empty()) return m;
    for (const auto& i : all) {
        m.cpuPercent += i.cpuPercent;
        m.memoryMb += i.memoryMb;
        m.players += i.players;
    }
    m.cpuPercent /= static_cast<double>(all.size());
    return m;
}

inline double monthlyCostUsd(int servers, double hourlyRateUsd, double hoursPerDay = 24) { return servers * hourlyRateUsd * hoursPerDay * 30.0; }

// Load test simulator (621): arrivals and service in fixed steps; returns the average wait in steps.
inline double simulateQueue(int steps, int arrivalsPerStep, int servedPerStep) {
    std::deque<int> q;
    double total = 0;
    int served = 0;
    for (int t = 0; t < steps; ++t) {
        for (int i = 0; i < arrivalsPerStep; ++i) q.push_back(t);
        for (int i = 0; i < servedPerStep && !q.empty(); ++i) {
            total += t - q.front();
            q.pop_front();
            ++served;
        }
    }
    return served ? total / served : 0.0;
}

// Reserved private servers (623) and browser (624).
class Reservations {
public:
    bool reserve(const std::string& server, const std::string& owner, double fromTime, double toTime) {
        for (const auto& r : list_) {
            if (r.server == server && fromTime < r.to && toTime > r.from) return false;
        }
        list_.push_back({server, owner, fromTime, toTime});
        return true;
    }
    [[nodiscard]] std::string ownerAt(const std::string& server, double t) const {
        for (const auto& r : list_) {
            if (r.server == server && t >= r.from && t < r.to) return r.owner;
        }
        return {};
    }

private:
    struct R {
        std::string server, owner;
        double from, to;
    };
    std::vector<R> list_;
};

struct ListedServer {
    std::string name;
    int players, maxPlayers, pingMs;
    bool isPrivate;
};

inline std::vector<std::string> browseServers(std::vector<ListedServer> servers, const std::string& text, bool hideFull) {
    servers.erase(std::remove_if(servers.begin(), servers.end(), [&](const ListedServer& s) { return s.isPrivate || (hideFull && s.players >= s.maxPlayers) || (!text.empty() && s.name.find(text) == std::string::npos); }), servers.end());
    std::sort(servers.begin(), servers.end(), [](const ListedServer& a, const ListedServer& b) { return a.pingMs < b.pingMs; });
    std::vector<std::string> out;
    for (const auto& s : servers) out.push_back(s.name);
    return out;
}

// ---- SHA-256 and HMAC (642): needed to check webhook signatures.
inline std::array<uint8_t, 32> sha256(const std::vector<uint8_t>& data) {
    static const uint32_t k[64] = {0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::vector<uint8_t> msg = data;
    const uint64_t bits = static_cast<uint64_t>(data.size()) * 8;
    msg.push_back(0x80);
    while (msg.size() % 64 != 56) msg.push_back(0);
    for (int i = 7; i >= 0; --i) msg.push_back(static_cast<uint8_t>(bits >> (i * 8)));
    auto rotr = [](uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
    for (std::size_t off = 0; off < msg.size(); off += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) w[i] = (static_cast<uint32_t>(msg[off + static_cast<std::size_t>(i) * 4]) << 24) | (static_cast<uint32_t>(msg[off + static_cast<std::size_t>(i) * 4 + 1]) << 16) | (static_cast<uint32_t>(msg[off + static_cast<std::size_t>(i) * 4 + 2]) << 8) | msg[off + static_cast<std::size_t>(i) * 4 + 3];
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3), s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25), ch = (e & f) ^ (~e & g), t1 = hh + S1 + ch + k[i] + w[i];
            const uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22), maj = (a & b) ^ (a & c) ^ (b & c), t2 = S0 + maj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    std::array<uint8_t, 32> out{};
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 4; ++j) out[static_cast<std::size_t>(i * 4 + j)] = static_cast<uint8_t>(h[i] >> (24 - j * 8));
    return out;
}

inline std::string toHex(const std::array<uint8_t, 32>& d) {
    static const char* digits = "0123456789abcdef";
    std::string s;
    for (uint8_t b : d) {
        s += digits[b >> 4];
        s += digits[b & 15];
    }
    return s;
}

inline std::string sha256Hex(const std::string& text) { return toHex(sha256(std::vector<uint8_t>(text.begin(), text.end()))); }

inline std::array<uint8_t, 32> hmacSha256(const std::string& key, const std::string& message) {
    std::vector<uint8_t> k(key.begin(), key.end());
    if (k.size() > 64) {
        const auto d = sha256(k);
        k.assign(d.begin(), d.end());
    }
    k.resize(64, 0);
    std::vector<uint8_t> inner, outer;
    for (uint8_t b : k) {
        inner.push_back(b ^ 0x36);
        outer.push_back(b ^ 0x5c);
    }
    inner.insert(inner.end(), message.begin(), message.end());
    const auto ih = sha256(inner);
    outer.insert(outer.end(), ih.begin(), ih.end());
    return sha256(outer);
}

// Compares in time that does not depend on where the strings differ.
inline bool constantTimeEquals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    unsigned diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) diff |= static_cast<unsigned>(a[i] ^ b[i]);
    return diff == 0;
}

inline bool verifyWebhook(const std::string& secret, const std::string& body, const std::string& signatureHex) { return constantTimeEquals(toHex(hmacSha256(secret, body)), signatureHex); }

// ---- Backend API (626 to 650)
inline int apiVersionOf(const std::string& path) {
    if (path.rfind("/v", 0) != 0) return 0;
    return std::atoi(path.c_str() + 2);
}

struct Endpoint {
    std::string method, path, summary;
};

inline std::string openApiJson(const std::string& title, const std::string& version, const std::vector<Endpoint>& endpoints) {
    std::map<std::string, std::vector<const Endpoint*>> byPath;
    for (const auto& e : endpoints) byPath[e.path].push_back(&e);
    std::ostringstream o;
    o << "{\"openapi\":\"3.0.3\",\"info\":{\"title\":\"" << title << "\",\"version\":\"" << version << "\"},\"paths\":{";
    bool firstPath = true;
    for (const auto& [path, list] : byPath) {
        o << (firstPath ? "" : ",") << "\"" << path << "\":{";
        firstPath = false;
        for (std::size_t i = 0; i < list.size(); ++i) {
            std::string m = list[i]->method;
            for (char& c : m) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            o << (i ? "," : "") << "\"" << m << "\":{\"summary\":\"" << list[i]->summary << "\"}";
        }
        o << "}";
    }
    o << "}}";
    return o.str();
}

// Login lockout (628): the wait doubles after each failure past the free tries.
inline double lockoutSeconds(int failures, int freeTries = 3, double base = 5, double cap = 3600) {
    if (failures <= freeTries) return 0;
    return std::min(base * std::pow(2.0, failures - freeTries - 1), cap);
}

// Layered settings (629): game settings override environment settings which override defaults.
inline std::map<std::string, std::string> layerSettings(const std::vector<std::map<std::string, std::string>>& layersLowToHigh) {
    std::map<std::string, std::string> out;
    for (const auto& l : layersLowToHigh)
        for (const auto& kv : l) out[kv.first] = kv.second;
    return out;
}

// Staging and production must not share anything dangerous (630).
inline std::vector<std::string> environmentConflicts(const std::map<std::string, std::string>& staging, const std::map<std::string, std::string>& production) {
    std::vector<std::string> out;
    for (const char* k : {"database_url", "storage_bucket", "signing_key", "api_key"}) {
        auto s = staging.find(k), p = production.find(k);
        if (s != staging.end() && p != production.end() && s->second == p->second) out.push_back(std::string(k) + " is the same in staging and production");
    }
    return out;
}

// Resumable upload (631): remembers which byte ranges arrived.
class UploadTracker {
public:
    explicit UploadTracker(uint64_t total) : total_(total) {}
    void received(uint64_t from, uint64_t to) {
        if (from >= to) return;
        ranges_.push_back({from, std::min(to, total_)});
        std::sort(ranges_.begin(), ranges_.end());
        std::vector<std::pair<uint64_t, uint64_t>> merged;
        for (const auto& r : ranges_) {
            if (!merged.empty() && r.first <= merged.back().second) merged.back().second = std::max(merged.back().second, r.second);
            else merged.push_back(r);
        }
        ranges_ = merged;
    }
    [[nodiscard]] uint64_t resumeFrom() const { return !ranges_.empty() && ranges_.front().first == 0 ? ranges_.front().second : 0; }
    [[nodiscard]] bool complete() const { return ranges_.size() == 1 && ranges_[0].first == 0 && ranges_[0].second == total_; }

private:
    uint64_t total_;
    std::vector<std::pair<uint64_t, uint64_t>> ranges_;
};

// Release channels (632).
class Channels {
public:
    void publish(const std::string& channel, const std::string& version) { current_[channel] = version; }
    bool promote(const std::string& from, const std::string& to) {
        auto it = current_.find(from);
        if (it == current_.end()) return false;
        current_[to] = it->second;
        return true;
    }
    [[nodiscard]] std::string version(const std::string& channel) const { return current_.count(channel) ? current_.at(channel) : std::string(); }

private:
    std::map<std::string, std::string> current_;
};

// Rate-limit dashboard (633).
class RateCounters {
public:
    void hit(const std::string& key, bool blocked) {
        auto& c = counts_[key];
        ++c.first;
        if (blocked) ++c.second;
    }
    [[nodiscard]] std::vector<std::pair<std::string, double>> blockedShare() const {
        std::vector<std::pair<std::string, double>> out;
        for (const auto& [k, c] : counts_) out.push_back({k, c.first ? static_cast<double>(c.second) / c.first : 0.0});
        std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        return out;
    }

private:
    std::map<std::string, std::pair<int, int>> counts_;
};

// Correlation ids (634): the id is made once and passed along.
inline std::string newCorrelationId(uint64_t counter, const std::string& node) { return node + "-" + sha256Hex(node + std::to_string(counter)).substr(0, 12); }

// Migration plan (635, 636): every up has a down, numbers have no gaps.
struct Migration {
    int number;
    bool hasDown;
};

inline std::vector<std::string> migrationProblems(std::vector<Migration> list) {
    std::sort(list.begin(), list.end(), [](const Migration& a, const Migration& b) { return a.number < b.number; });
    std::vector<std::string> out;
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (i > 0 && list[i].number != list[i - 1].number + 1) out.push_back("gap before migration " + std::to_string(list[i].number));
        if (!list[i].hasDown) out.push_back("migration " + std::to_string(list[i].number) + " has no way back");
    }
    return out;
}

// Health and status (637, 645).
inline std::string overallStatus(const std::map<std::string, bool>& services) {
    int down = 0;
    for (const auto& kv : services) down += kv.second ? 0 : 1;
    return down == 0 ? "all systems working" : down == static_cast<int>(services.size()) ? "major outage" : "partial outage";
}

// Storage lifecycle (638).
struct StoredObject {
    std::string key;
    long long ageDays;
    bool referenced;
};

inline std::vector<std::string> objectsToDelete(const std::vector<StoredObject>& objects, long long unreferencedAfterDays) {
    std::vector<std::string> out;
    for (const auto& o : objects) {
        if (!o.referenced && o.ageDays > unreferencedAfterDays) out.push_back(o.key);
    }
    return out;
}

// Upload checks (639): size and type allow list.
inline std::string checkUpload(const std::string& declaredExt, const std::vector<uint8_t>& head, uint64_t size, uint64_t maxSize) {
    static const std::map<std::string, std::string> magic = {{"png", "\x89PNG"}, {"glb", "glTF"}, {"ogg", "OggS"}, {"zip", "PK"}};
    if (size > maxSize) return "file is too large";
    auto it = magic.find(declaredExt);
    if (it == magic.end()) return "this file type is not allowed";
    if (head.size() < it->second.size() || std::string(head.begin(), head.begin() + static_cast<std::ptrdiff_t>(it->second.size())) != it->second) return "file contents do not match the type";
    return {};
}

// API credentials (640, 641): only a hash is stored, scopes limit use, rotation keeps the old key valid for a while.
struct Credential {
    std::string hash;
    std::set<std::string> scopes;
    double expires;
};

inline Credential makeCredential(const std::string& secret, const std::set<std::string>& scopes, double expires) { return {sha256Hex(secret), scopes, expires}; }

inline bool credentialAllows(const Credential& c, const std::string& secret, const std::string& scope, double now) { return now < c.expires && c.scopes.count(scope) && constantTimeEquals(c.hash, sha256Hex(secret)); }

inline Credential rotated(const Credential& old, const std::string& newSecret, double now, double overlapSeconds, Credential& oldWithShortExpiry) {
    oldWithShortExpiry = old;
    oldWithShortExpiry.expires = std::min(old.expires, now + overlapSeconds);
    return {sha256Hex(newSecret), old.scopes, old.expires};
}

// Idempotency (643, 660): the same key returns the first result.
class IdempotencyStore {
public:
    std::string run(const std::string& key, const std::function<std::string()>& action) {
        auto it = results_.find(key);
        if (it != results_.end()) return it->second;
        return results_[key] = action();
    }
    [[nodiscard]] std::size_t size() const { return results_.size(); }

private:
    std::map<std::string, std::string> results_;
};

// Ownership transfer (648): the new owner must accept.
class OwnershipTransfer {
public:
    OwnershipTransfer(std::string owner) : owner_(std::move(owner)) {}
    bool offer(const std::string& from, const std::string& to) {
        if (from != owner_) return false;
        pending_ = to;
        return true;
    }
    bool accept(const std::string& who) {
        if (pending_.empty() || who != pending_) return false;
        owner_ = who;
        pending_.clear();
        return true;
    }
    [[nodiscard]] const std::string& owner() const { return owner_; }

private:
    std::string owner_, pending_;
};

// Export and delete (649, 650).
inline std::string exportUserData(const std::string& user, const std::map<std::string, std::string>& records) {
    std::string out = "user=" + user + "\n";
    for (const auto& kv : records) out += kv.first + "=" + kv.second + "\n";
    return out;
}

inline bool deletionConfirmed(const std::string& typed, const std::string& projectName) { return !projectName.empty() && typed == projectName; }

// ---- Persistence and economy (651 to 675)
class VersionedStore {
public:
    // Compare-and-set (652): only writes when the caller saw the latest version.
    bool compareAndSet(const std::string& key, int expectedVersion, const std::string& value) {
        auto& e = data_[key];
        if (e.version != expectedVersion) return false;
        e.value = value;
        ++e.version;
        return true;
    }
    [[nodiscard]] int version(const std::string& key) const { return data_.count(key) ? data_.at(key).version : 0; }
    [[nodiscard]] std::string value(const std::string& key) const { return data_.count(key) ? data_.at(key).value : std::string(); }
    [[nodiscard]] std::string snapshot() const { // backup (655)
        std::string s;
        for (const auto& [k, e] : data_) s += k + "\t" + std::to_string(e.version) + "\t" + e.value + "\n";
        return s;
    }
    void restore(const std::string& text) {
        data_.clear();
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line)) {
            const std::size_t a = line.find('\t'), b = line.find('\t', a + 1);
            if (a == std::string::npos || b == std::string::npos) continue;
            data_[line.substr(0, a)] = {line.substr(b + 1), std::atoi(line.c_str() + a + 1)};
        }
    }

private:
    struct E {
        std::string value;
        int version = 0;
    };
    std::map<std::string, E> data_;
};

// Per key limits (653): a bucket per key.
class KeyLimiter {
public:
    KeyLimiter(double perSecond, double burst) : rate_(perSecond), cap_(burst) {}
    bool allow(const std::string& key, double now) {
        auto& b = buckets_.try_emplace(key, Bucket{cap_, now}).first->second;
        b.tokens = std::min(cap_, b.tokens + (now - b.last) * rate_);
        b.last = now;
        if (b.tokens < 1) return false;
        b.tokens -= 1;
        return true;
    }
    [[nodiscard]] double retryAfter(const std::string& key) const {
        auto it = buckets_.find(key);
        return it == buckets_.end() || it->second.tokens >= 1 ? 0.0 : (1.0 - it->second.tokens) / rate_;
    }

private:
    struct Bucket {
        double tokens, last;
    };
    double rate_, cap_;
    std::map<std::string, Bucket> buckets_;
};

// Data migrations (654): saved data carries a version; steps run in order.
inline std::map<std::string, std::string> migrateData(std::map<std::string, std::string> data, const std::vector<std::function<void(std::map<std::string, std::string>&)>>& steps) {
    int v = data.count("_v") ? std::atoi(data["_v"].c_str()) : 0;
    for (std::size_t i = static_cast<std::size_t>(v); i < steps.size(); ++i) steps[i](data);
    data["_v"] = std::to_string(steps.size());
    return data;
}

// Receipt validation interface (657): the game never decides; a validator does, and a receipt only works once.
class ReceiptValidator {
public:
    virtual ~ReceiptValidator() = default;
    virtual bool valid(const std::string& receipt) = 0;
};

class SimpleReceiptValidator : public ReceiptValidator {
public:
    bool valid(const std::string& receipt) override { return receipt.rfind("rcpt_", 0) == 0 && seen_.insert(receipt).second; }

private:
    std::set<std::string> seen_;
};

// Ledger (658 to 663, 665, 671): every change is an entry; balances can be re-added from the entries.
struct LedgerEntry {
    std::string user, kind, item;
    long long amount;
    std::string id;
};

class Ledger {
public:
    bool add(const LedgerEntry& e) {
        if (!ids_.insert(e.id).second) return false; // duplicate id
        if (e.kind == "spend" && balance(e.user) < e.amount) { ids_.erase(e.id); return false; }
        entries_.push_back(e);
        return true;
    }
    [[nodiscard]] long long balance(const std::string& user) const {
        long long b = 0;
        for (const auto& e : entries_) {
            if (e.user != user) continue;
            if (e.kind == "grant" || e.kind == "refund") b += e.amount;
            else if (e.kind == "spend") b -= e.amount;
        }
        return b;
    }
    bool reverse(const std::string& id) {
        for (const auto& e : entries_) {
            if (e.id == id && e.kind == "spend") return add({e.user, "refund", e.item, e.amount, id + "-refund"});
        }
        return false;
    }
    [[nodiscard]] std::vector<LedgerEntry> forUser(const std::string& user) const {
        std::vector<LedgerEntry> out;
        for (const auto& e : entries_) {
            if (e.user == user) out.push_back(e);
        }
        return out;
    }
    [[nodiscard]] long long totalSpent() const {
        long long t = 0;
        for (const auto& e : entries_) t += e.kind == "spend" ? e.amount : 0;
        return t;
    }

private:
    std::vector<LedgerEntry> entries_;
    std::set<std::string> ids_;
};

// Inventory with unique ids (659) and ownership checks (664, 675).
class Inventory {
public:
    uint64_t grant(const std::string& user, const std::string& item) {
        items_[next_] = {user, item};
        return next_++;
    }
    [[nodiscard]] bool owns(const std::string& user, uint64_t uniqueId) const {
        auto it = items_.find(uniqueId);
        return it != items_.end() && it->second.first == user;
    }
    bool transfer(uint64_t id, const std::string& from, const std::string& to) {
        if (!owns(from, id)) return false;
        items_[id].first = to;
        return true;
    }
    [[nodiscard]] int count(const std::string& user, const std::string& item) const {
        int n = 0;
        for (const auto& kv : items_) n += kv.second.first == user && kv.second.second == item;
        return n;
    }

private:
    uint64_t next_ = 1;
    std::map<uint64_t, std::pair<std::string, std::string>> items_;
};

// Duplicate and fraud hints (663).
inline std::vector<std::string> suspiciousUsers(const std::vector<LedgerEntry>& entries, int maxGrantsPerUser) {
    std::map<std::string, int> grants;
    for (const auto& e : entries) grants[e.user] += e.kind == "grant";
    std::vector<std::string> out;
    for (const auto& kv : grants) {
        if (kv.second > maxGrantsPerUser) out.push_back(kv.first);
    }
    return out;
}

// Fees (665).
inline long long platformFee(long long priceCents, double percent, long long minimumCents) { return std::max(static_cast<long long>(std::llround(static_cast<double>(priceCents) * percent / 100.0)), std::min(minimumCents, priceCents)); }

// Payment adapter interface (666) and test mode (668).
class PaymentProvider {
public:
    virtual ~PaymentProvider() = default;
    virtual std::string charge(const std::string& token, long long cents) = 0; // returns a receipt or an empty string
};

class TestPaymentProvider : public PaymentProvider {
public:
    std::string charge(const std::string& token, long long cents) override {
        if (token == "tok_declined" || cents <= 0) return {};
        return "rcpt_test_" + std::to_string(++n_);
    }

private:
    int n_ = 0;
};

// Keeps card numbers out of the client (667): Luhn check on digit runs found in text.
inline bool luhnValid(const std::string& digits) {
    int sum = 0;
    bool dbl = false;
    for (std::size_t i = digits.size(); i-- > 0;) {
        int d = digits[i] - '0';
        if (dbl) {
            d *= 2;
            if (d > 9) d -= 9;
        }
        sum += d;
        dbl = !dbl;
    }
    return !digits.empty() && sum % 10 == 0;
}

inline bool containsCardNumber(const std::string& text) {
    std::string run;
    auto check = [&]() { return run.size() >= 13 && run.size() <= 19 && luhnValid(run); };
    for (char c : text) {
        if (std::isdigit(static_cast<unsigned char>(c))) run += c;
        else if (c != ' ' && c != '-') {
            if (check()) return true;
            run.clear();
        }
    }
    return check();
}

// Economy export (670) and analytics (671).
inline std::string ledgerCsv(const std::vector<LedgerEntry>& entries) {
    std::string out = "id,user,kind,item,amount\n";
    for (const auto& e : entries) out += e.id + "," + e.user + "," + e.kind + "," + e.item + "," + std::to_string(e.amount) + "\n";
    return out;
}

inline std::map<std::string, long long> purchasesByItem(const std::vector<LedgerEntry>& entries) {
    std::map<std::string, long long> out;
    for (const auto& e : entries) {
        if (e.kind == "spend") out[e.item] += e.amount;
    }
    return out;
}

// Gifts and limits (673, 674).
inline bool giftAllowed(bool senderIsChild, bool receiverAcceptsGifts, bool areFriends) { return receiverAcceptsGifts && (!senderIsChild || areFriends); }

inline bool withinSpendingLimit(long long spentThisMonth, long long priceCents, long long monthlyLimitCents) { return monthlyLimitCents <= 0 || spentThisMonth + priceCents <= monthlyLimitCents; }

} // namespace engine::core::backend
