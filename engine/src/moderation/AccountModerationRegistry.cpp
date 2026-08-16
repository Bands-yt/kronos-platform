#include "moderation/AccountModerationRegistry.hpp"

#include <fstream>
#include <sstream>

namespace engine::moderation {

AccountModerationRecord& AccountModerationRegistry::recordFor(uint64_t profileId) {
    auto it = records_.find(profileId);
    if (it == records_.end()) {
        AccountModerationRecord record;
        record.profileId = profileId;
        it = records_.emplace(profileId, record).first;
    }
    return it->second;
}

void AccountModerationRegistry::ban(uint64_t profileId, const std::string& reason) {
    AccountModerationRecord& record = recordFor(profileId);
    record.banned = true;
    record.reason = reason;
}

void AccountModerationRegistry::unban(uint64_t profileId) {
    auto it = records_.find(profileId);
    if (it != records_.end()) it->second.banned = false;
}

bool AccountModerationRegistry::isBanned(uint64_t profileId) const {
    auto it = records_.find(profileId);
    return it != records_.end() && it->second.banned;
}

void AccountModerationRegistry::mute(uint64_t profileId, const std::string& reason) {
    AccountModerationRecord& record = recordFor(profileId);
    record.muted = true;
    record.reason = reason;
}

void AccountModerationRegistry::unmute(uint64_t profileId) {
    auto it = records_.find(profileId);
    if (it != records_.end()) it->second.muted = false;
}

bool AccountModerationRegistry::isMuted(uint64_t profileId) const {
    auto it = records_.find(profileId);
    return it != records_.end() && it->second.muted;
}

bool AccountModerationRegistry::saveToFile(const std::string& path) const {
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) return false;

    out << "ACCOUNTMODERATION 1\n";
    for (const auto& [profileId, record] : records_) {
        // reason is last on the line (trailing-string convention).
        out << "RECORD " << profileId << ' ' << (record.banned ? 1 : 0) << ' ' << (record.muted ? 1 : 0) << ' '
            << record.reason << "\n";
    }
    out << "END\n";
    return out.good();
}

bool AccountModerationRegistry::loadFromFile(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return false;

    std::string header;
    if (!std::getline(in, header) || header.rfind("ACCOUNTMODERATION", 0) != 0) return false;

    std::unordered_map<uint64_t, AccountModerationRecord> loaded;
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("RECORD ", 0) == 0) {
            std::istringstream iss(line.substr(7));
            AccountModerationRecord record;
            int bannedInt = 0;
            int mutedInt = 0;
            iss >> record.profileId >> bannedInt >> mutedInt;
            record.banned = bannedInt != 0;
            record.muted = mutedInt != 0;
            std::string rest;
            std::getline(iss, rest);
            if (!rest.empty() && rest.front() == ' ') rest.erase(rest.begin());
            record.reason = rest;
            loaded[record.profileId] = std::move(record);
        } else if (line == "END") {
            break;
        }
    }

    records_ = std::move(loaded);
    return true;
}

} // namespace engine::moderation
