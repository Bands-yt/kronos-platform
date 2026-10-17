#pragma once

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace engine::migration {

struct AggregatedWarning {
    std::string message;
    int count = 0;
    std::vector<std::string> examples; // first few places it happened
};

// Groups repeated import warnings ("Part 'Wall12': mesh not supported") by their text
// with the digits removed, so 400 near-identical lines become one line with a count.
class ImportWarningAggregator {
public:
    void add(const std::string& message, const std::string& where = {}) {
        const std::string key = normalize(message);
        auto it = groups_.find(key);
        if (it == groups_.end()) {
            order_.push_back(key);
            it = groups_.emplace(key, AggregatedWarning{key, 0, {}}).first;
        }
        ++it->second.count;
        if (!where.empty() && it->second.examples.size() < kMaxExamples) it->second.examples.push_back(where);
    }

    [[nodiscard]] std::vector<AggregatedWarning> result() const {
        std::vector<AggregatedWarning> out;
        for (const auto& key : order_) out.push_back(groups_.at(key));
        std::stable_sort(out.begin(), out.end(),
                         [](const AggregatedWarning& a, const AggregatedWarning& b) { return a.count > b.count; });
        return out;
    }

    [[nodiscard]] int total() const {
        int n = 0;
        for (const auto& g : groups_) n += g.second.count;
        return n;
    }

    static std::string normalize(const std::string& message) {
        std::string out;
        bool lastDigit = false;
        for (char c : message) {
            const bool digit = c >= '0' && c <= '9';
            if (digit) {
                if (!lastDigit) out += '#';
            } else {
                out += c;
            }
            lastDigit = digit;
        }
        return out;
    }

private:
    static constexpr std::size_t kMaxExamples = 3;
    std::map<std::string, AggregatedWarning> groups_;
    std::vector<std::string> order_;
};

} // namespace engine::migration
