#pragma once

#include <string>

namespace engine::core {

// Kronos ("In-App Auto-Updater"): the real, parsed form of a version
// string like "0.2.0-alpha" or a real git tag like "v0.2.0-alpha" (the
// leading "v" is accepted and ignored -- GitHub tags carry it, this
// codebase's own kKronosVersion doesn't).
//
// `prerelease` is everything after the first '-' ("alpha", "beta.2",
// ...), empty for a real final release. Semantic Versioning's own rule
// applies in compareVersions(): a real prerelease sorts BEFORE the
// otherwise-identical final release (0.2.0-alpha < 0.2.0), so shipping
// a real 0.2.0 to someone on 0.2.0-alpha correctly reads as an update.
struct SemanticVersion {
    int major = 0;
    int minor = 0;
    int patch = 0;
    std::string prerelease;
    // False when the string genuinely couldn't be parsed as a version at
    // all -- callers must check this rather than silently treating an
    // unparseable tag as 0.0.0, which would look like "everything is an
    // update" (or "nothing is").
    bool valid = false;
};

[[nodiscard]] SemanticVersion parseVersion(const std::string& text);

// Real three-way compare: negative if a < b, 0 if equal, positive if a > b.
// Only the numeric fields and the prerelease tag participate -- build
// metadata (SemVer's own '+' suffix) is deliberately not compared,
// matching SemVer's own rule that it carries no precedence.
[[nodiscard]] int compareVersions(const SemanticVersion& a, const SemanticVersion& b);

struct UpdateCheckResult {
    // True only when the real GitHub query completed and a real release
    // was parsed out of it. False + a populated `error` on any real
    // network/parse failure -- deliberately distinct from
    // "checked successfully, and you're already up to date".
    bool checked = false;
    bool updateAvailable = false;
    std::string latestTag;    // the real tag as GitHub reports it, e.g. "v0.2.0-alpha"
    std::string releaseUrl;   // real human-facing release page, for a "what's new" link
    std::string error;
};

// Kronos ("Version Checking"): real HTTPS GET against the real GitHub
// Releases API, then a real semver comparison against `currentVersion`
// (normally core::kKronosVersion -- see that constant's own comment on
// why the compiled-in value, not a loose version.txt next to the binary,
// is the real source of truth here: after an update swaps binaries the
// compiled-in constant is correct by construction, whereas a separate
// file is one more thing that can drift out of sync with the executable
// actually running).
//
// Blocking: performs a real network round-trip, so callers on a UI/render
// thread must run this on a real background thread (see
// RuntimeShell::startUpdateCheck()'s own real std::thread usage).
//
// Queries the real /releases list rather than /releases/latest: GitHub's
// "latest" endpoint deliberately skips any release flagged as a
// prerelease, so a single ticked checkbox on a future alpha would
// silently switch off auto-update for everyone. This instead takes the
// highest real semver among the non-draft releases it can see, which is
// both unambiguous and independent of GitHub's own ordering.
[[nodiscard]] UpdateCheckResult checkForUpdate(const std::string& currentVersion, const std::string& repoOwner,
                                                const std::string& repoName);

} // namespace engine::core
