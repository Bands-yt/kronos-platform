#include "anticheat/DeviceFingerprint.hpp"

#include <cstdio>
#include <functional>

namespace engine::anticheat {

std::string DeviceFingerprint::toHex() const {
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(hash));
    return std::string(buf);
}

DeviceFingerprint DeviceFingerprintCollector::compute() const {
    // std::hash is NOT a cryptographic hash and is not stable across
    // process runs on some standard library implementations (libstdc++'s
    // std::hash<std::string> is seeded per-process in some configurations)
    // -- fine for "is this probably the same machine as last session"
    // within one build/session, not a substitute for a real stable hash
    // (e.g. SHA-256 over a canonicalized component list) a production
    // implementation needs. Flagged rather than silently shipped as if
    // it were production-grade.
    std::hash<std::string> hasher;
    uint64_t combined = 0;
    for (const auto& component : components_) {
        uint64_t h = static_cast<uint64_t>(hasher(component));
        combined ^= h + 0x9e3779b97f4a7c15ULL + (combined << 6) + (combined >> 2); // boost::hash_combine-style mix
    }
    return DeviceFingerprint{combined};
}

} // namespace engine::anticheat
