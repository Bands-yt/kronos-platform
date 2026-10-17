#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace engine::net::tools {

// Snapshot interpolation (585): the client renders `delay` seconds in the past and blends between two snapshots.
struct Snapshot {
    double time = 0;
    float x = 0, y = 0, z = 0;
};

class InterpolationBuffer {
public:
    explicit InterpolationBuffer(double delaySeconds = 0.1) : delay_(delaySeconds) {}
    void push(const Snapshot& s) {
        if (!snaps_.empty() && s.time <= snaps_.back().time) return; // ignore old or duplicate
        snaps_.push_back(s);
        while (snaps_.size() > 32) snaps_.erase(snaps_.begin());
    }
    // Position to draw at wall-clock `now`; holds the newest snapshot when starved (no extrapolation).
    [[nodiscard]] bool sample(double now, Snapshot& out) const {
        if (snaps_.empty()) return false;
        const double t = now - delay_;
        if (t <= snaps_.front().time) {
            out = snaps_.front();
            return true;
        }
        for (std::size_t i = 0; i + 1 < snaps_.size(); ++i) {
            if (t >= snaps_[i].time && t <= snaps_[i + 1].time) {
                const double f = (t - snaps_[i].time) / (snaps_[i + 1].time - snaps_[i].time);
                out = {t, static_cast<float>(snaps_[i].x + (snaps_[i + 1].x - snaps_[i].x) * f),
                       static_cast<float>(snaps_[i].y + (snaps_[i + 1].y - snaps_[i].y) * f),
                       static_cast<float>(snaps_[i].z + (snaps_[i + 1].z - snaps_[i].z) * f)};
                return true;
            }
        }
        out = snaps_.back();
        return true;
    }

private:
    double delay_;
    std::vector<Snapshot> snaps_;
};

// Channels (587): reliable-ordered keeps a sequence number and drops or reorders late packets.
enum class Channel { Reliable, Unreliable, UnreliableSequenced };

class SequenceFilter {
public:
    // For UnreliableSequenced: accept only packets newer than the last one seen (wrap-around safe, 16-bit).
    bool accept(uint16_t seq) {
        if (!started_ || static_cast<int16_t>(seq - last_) > 0) {
            last_ = seq;
            started_ = true;
            return true;
        }
        return false;
    }

private:
    uint16_t last_ = 0;
    bool started_ = false;
};

// Token bucket (588, 839): `rate` tokens per second up to `burst`. Used for bandwidth budgets and chat/RPC rate limits.
class TokenBucket {
public:
    TokenBucket(double ratePerSecond, double burst) : rate_(ratePerSecond), burst_(burst), tokens_(burst) {}
    bool take(double cost, double nowSeconds) {
        tokens_ = std::min(burst_, tokens_ + (nowSeconds - last_) * rate_);
        last_ = nowSeconds;
        if (tokens_ < cost) return false;
        tokens_ -= cost;
        return true;
    }

private:
    double rate_, burst_, tokens_, last_ = 0;
};

// Congestion (590): additive increase, multiplicative decrease of the send rate in bytes per second.
class CongestionController {
public:
    explicit CongestionController(double start = 64000, double minRate = 8000, double maxRate = 2000000)
        : rate_(start), min_(minRate), max_(maxRate) {}
    void onAckRound() { rate_ = std::min(max_, rate_ + 8000); }
    void onLoss() { rate_ = std::max(min_, rate_ * 0.5); }
    [[nodiscard]] double rate() const { return rate_; }

private:
    double rate_, min_, max_;
};

// Clock sync (591): offset between client and server clocks from ping samples, trusting the one with the lowest round trip.
struct PingSample {
    double clientSend = 0, serverTime = 0, clientReceive = 0;
};

inline double estimateClockOffset(const std::vector<PingSample>& samples, double* bestRtt = nullptr) {
    double best = 1e18, offset = 0;
    for (const auto& s : samples) {
        const double rtt = s.clientReceive - s.clientSend;
        if (rtt < best && rtt >= 0) {
            best = rtt;
            offset = s.serverTime - (s.clientSend + rtt / 2.0);
        }
    }
    if (bestRtt) *bestRtt = best;
    return offset;
}

// Reconnect window (594): a dropped player may come back with their token inside `windowSeconds`.
class ReconnectTable {
public:
    explicit ReconnectTable(double windowSeconds = 30) : window_(windowSeconds) {}
    void onDisconnect(const std::string& token, double now) { dropped_[token] = now; }
    bool tryReconnect(const std::string& token, double now) {
        auto it = dropped_.find(token);
        if (it == dropped_.end()) return false;
        const bool ok = now - it->second <= window_;
        dropped_.erase(it);
        return ok;
    }
    int expire(double now) {
        int n = 0;
        for (auto it = dropped_.begin(); it != dropped_.end();) {
            if (now - it->second > window_) {
                it = dropped_.erase(it);
                ++n;
            } else ++it;
        }
        return n;
    }

private:
    double window_;
    std::map<std::string, double> dropped_;
};

// Replication schema (595): ordered field names and types; a client may join when it knows every field the server sends.
struct SchemaField {
    std::string name, type;
    bool operator==(const SchemaField& o) const { return name == o.name && type == o.type; }
};

struct SchemaCheck {
    bool compatible = true;
    std::vector<std::string> problems;
};

inline SchemaCheck checkSchemas(const std::vector<SchemaField>& server, const std::vector<SchemaField>& client) {
    SchemaCheck r;
    for (const auto& f : server) {
        auto it = std::find_if(client.begin(), client.end(), [&](const SchemaField& c) { return c.name == f.name; });
        if (it == client.end()) {
            r.compatible = false;
            r.problems.push_back("client doesn't know field '" + f.name + "'");
        } else if (it->type != f.type) {
            r.compatible = false;
            r.problems.push_back("field '" + f.name + "' is " + f.type + " on the server but " + it->type + " on the client");
        }
    }
    return r;
}

// Capture redaction (598): hides tokens and keys in a captured text line before it is saved or shared.
inline std::string redactCapture(std::string line) {
    for (const std::string key : {"token=", "key=", "password=", "authorization: bearer "}) {
        std::string lower = line;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
        std::size_t pos = 0;
        while ((pos = lower.find(key, pos)) != std::string::npos) {
            std::size_t start = pos + key.size(), end = start;
            while (end < line.size() && line[end] != '&' && line[end] != ' ' && line[end] != '"' && line[end] != ',') ++end;
            line.replace(start, end - start, "[redacted]");
            lower.replace(start, end - start, "[redacted]");
            pos = start + 10;
        }
    }
    return line;
}

// Desync detector (600): per-tick state hashes from two peers; returns the first tick where they differ, or -1.
inline long firstDesync(const std::vector<uint64_t>& a, const std::vector<uint64_t>& b) {
    const std::size_t n = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < n; ++i) {
        if (a[i] != b[i]) return static_cast<long>(i);
    }
    return -1;
}

// Version gate (563, 620): same major, and the client is not older than the server's minimum.
struct Version {
    int major = 0, minor = 0, patch = 0;
};

inline bool parseVersion(const std::string& text, Version& v) {
    int a = 0, b = 0, c = 0;
    if (std::sscanf(text.c_str(), "%d.%d.%d", &a, &b, &c) < 2) return false;
    v = {a, b, c};
    return true;
}

inline bool versionsCompatible(const Version& server, const Version& client) {
    return server.major == client.major && (client.minor > server.minor || (client.minor == server.minor && client.patch >= 0));
}

// Ownership transfer (593): a body is owned by the server or one player; a request is granted if the player is closest and nobody else holds it.
struct BodyOwner {
    int ownerPlayer = 0; // 0 = server
    double since = 0;
};

inline bool grantOwnership(BodyOwner& body, int player, double playerDistance, double closestOtherDistance, double now, double minHoldSeconds = 1.0) {
    if (body.ownerPlayer == player) return true;
    if (body.ownerPlayer != 0 && now - body.since < minHoldSeconds) return false;
    if (playerDistance > closestOtherDistance) return false;
    body = {player, now};
    return true;
}

// Fixed timestep (584): same number of ticks for the same elapsed time, however the frames are split.
class TickScheduler {
public:
    explicit TickScheduler(double tickSeconds) : tick_(tickSeconds) {}
    int advance(double frameSeconds) {
        acc_ += frameSeconds;
        int n = 0;
        while (acc_ + 1e-12 >= tick_) {
            acc_ -= tick_;
            ++n;
            ++total_;
        }
        return n;
    }
    [[nodiscard]] long total() const { return total_; }

private:
    double tick_, acc_ = 0;
    long total_ = 0;
};

// Health and readiness (605): the JSON a server would return from /healthz and /readyz.
struct ServerHealth {
    bool started = false, gameLoaded = false;
    int players = 0, maxPlayers = 0;
    double lastTickMs = 0;
};

inline std::string healthJson(const ServerHealth& h) {
    const bool ready = h.started && h.gameLoaded && h.players < h.maxPlayers;
    return std::string("{\"live\":") + (h.started ? "true" : "false") + ",\"ready\":" + (ready ? "true" : "false") + ",\"players\":" +
           std::to_string(h.players) + ",\"maxPlayers\":" + std::to_string(h.maxPlayers) + "}";
}

} // namespace engine::net::tools
