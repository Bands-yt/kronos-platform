#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <random>
#include <string>
#include <unordered_map>

namespace engine::net {

// One-time join tokens with an expiry. The server issues one per player; joining consumes it.
class JoinTokenStore {
public:
    using Clock = std::function<std::chrono::steady_clock::time_point()>;

    explicit JoinTokenStore(std::chrono::seconds lifetime = std::chrono::seconds(60),
                            Clock clock = [] { return std::chrono::steady_clock::now(); })
        : lifetime_(lifetime), clock_(std::move(clock)) {}

    std::string issue(const std::string& player) {
        prune();
        std::random_device rd;
        std::uniform_int_distribution<uint32_t> dist;
        static const char* kHex = "0123456789abcdef";
        std::string token;
        for (int i = 0; i < 4; ++i) {
            const uint32_t v = dist(rd);
            for (int shift = 28; shift >= 0; shift -= 4) token += kHex[(v >> shift) & 0xF];
        }
        tokens_[token] = {player, clock_() + lifetime_};
        return token;
    }

    // True once, only for the player it was issued to, and only before it expires.
    bool consume(const std::string& token, const std::string& player) {
        auto it = tokens_.find(token);
        if (it == tokens_.end()) return false;
        const bool ok = it->second.player == player && clock_() <= it->second.expires;
        tokens_.erase(it);
        return ok;
    }

    void revoke(const std::string& token) { tokens_.erase(token); }
    [[nodiscard]] std::size_t pending() const { return tokens_.size(); }

private:
    struct Entry {
        std::string player;
        std::chrono::steady_clock::time_point expires;
    };

    void prune() {
        const auto now = clock_();
        for (auto it = tokens_.begin(); it != tokens_.end();) {
            it = it->second.expires < now ? tokens_.erase(it) : std::next(it);
        }
    }

    std::chrono::seconds lifetime_;
    Clock clock_;
    std::unordered_map<std::string, Entry> tokens_;
};

} // namespace engine::net
