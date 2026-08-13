#include "core/LocalProfile.hpp"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <random>

namespace engine::core {

uint64_t generateProfileId() {
    static std::mt19937_64 rng{std::random_device{}()};
    static std::uniform_int_distribution<uint64_t> dist(1, std::numeric_limits<uint64_t>::max());
    return dist(rng);
}

bool LocalProfile::saveToFile(const std::string& path) const {
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) return false;

    out << "PROFILE 1\n";
    out << "ID " << profileId << "\n";
    out << "CREATED " << createdAtUnixSeconds << "\n";
    // name is last on the line (never quoted -- loadFromFile reads it as
    // "everything after the key"), same trailing-string convention every
    // other name/label field in this codebase's own text formats uses.
    out << "NAME " << displayName << "\n";
    out << "END\n";
    return out.good();
}

bool LocalProfile::loadFromFile(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return false;

    std::string header;
    if (!std::getline(in, header) || header.rfind("PROFILE", 0) != 0) return false;

    LocalProfile loaded;
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("ID ", 0) == 0) {
            loaded.profileId = std::strtoull(line.substr(3).c_str(), nullptr, 10);
        } else if (line.rfind("CREATED ", 0) == 0) {
            loaded.createdAtUnixSeconds = std::strtoll(line.substr(8).c_str(), nullptr, 10);
        } else if (line.rfind("NAME ", 0) == 0) {
            loaded.displayName = line.substr(5);
        } else if (line == "END") {
            break;
        }
        // Any other/unrecognized line is skipped -- forward-compatible
        // with a future field addition, same convention as SceneFile/
        // AnimationClip.
    }

    *this = loaded;
    return true;
}

LocalProfile loadOrCreateProfile(const std::string& path) {
    LocalProfile profile;
    if (profile.loadFromFile(path)) return profile;

    profile = LocalProfile{};
    profile.profileId = generateProfileId();
    profile.createdAtUnixSeconds =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    // A real, honest best-effort save -- if it fails (unwritable
    // directory, disk full), the caller still gets a real, usable
    // in-memory profile for this session; there's nothing actionable to
    // do with the failure here beyond that.
    (void)profile.saveToFile(path);
    return profile;
}

} // namespace engine::core
