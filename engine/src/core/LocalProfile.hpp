#pragma once

#include <cstdint>
#include <string>

namespace engine::core {

// Kronos (Alpha Roadmap Phase 9, "Platform Services" -- "Account system
// (simple local profiles first)"): a real, minimal local identity -- no
// authentication, no server-side account, no password, matching the
// roadmap's own "simple local profiles first" scope exactly. A stable,
// locally-generated profileId (see generateProfileId()) plus a display
// name a creator can change any time.
struct LocalProfile {
    std::string displayName = "Player";
    uint64_t profileId = 0; // 0 = not yet assigned a real id -- see loadOrCreateProfile()
    int64_t createdAtUnixSeconds = 0;

    // Same hand-rolled "KEY value per line, END terminator" text format
    // every other save/load struct in core/ (ProjectFile, SceneFile,
    // Prefab) already uses.
    [[nodiscard]] bool saveToFile(const std::string& path) const;
    [[nodiscard]] bool loadFromFile(const std::string& path);
};

// Real, process-random 64-bit id -- not cryptographically secure (no
// need to be; this is a local-only alpha identity, not an auth token),
// just collision-resistant enough that independently-created profiles
// essentially never collide.
[[nodiscard]] uint64_t generateProfileId();

// Real, honest "get me a usable profile" entry point: loads `path` if it
// already exists; otherwise creates a fresh profile (a real
// generateProfileId(), the default display name, real
// std::chrono::system_clock "now" as its creation time -- same
// convention core::ProjectFile::touch() already uses), saves it to
// `path`, and returns it. The one real place this "load, or create if
// missing" logic lives, instead of every caller re-implementing it.
[[nodiscard]] LocalProfile loadOrCreateProfile(const std::string& path);

} // namespace engine::core
