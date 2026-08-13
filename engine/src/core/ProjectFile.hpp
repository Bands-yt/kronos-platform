#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace engine::core {

// Kronos (Alpha Completion Checklist, "Project System Finalization" --
// "versioning"): the real, single source of truth for "what project
// format does this build of Kronos write" -- ProjectFile::version used
// to default to an independent "1.0.0" literal with zero connection to
// this constant, so a future format change had nothing to actually bump.
// Real semver convention: a major-version bump means "this build cannot
// promise to understand that file", matching isCompatibleVersion()'s own
// real comparison rule below.
inline constexpr const char* kProjectFormatVersion = "1.0.0";

// A project's metadata -- what ties a set of scene files together into
// "one thing" a user opens, distinct from any single core::SceneFile.
// Same hand-rolled "KEY value per line, END terminator" text format as
// every other save/load struct in core/ (Prefab, AnimationClip,
// PluginManifest) rather than a JSON/serialization library for a shape
// this small.
//
// `scenePaths` are relative to the project file's own directory (same
// convention as PluginManifest::entryScript) -- a project is a metadata
// file sitting next to a folder of .scene files, not a single-file
// archive/bundle format.
//
// Unlike core::SceneFile's clock-comparable-only mtime values (see
// AssetCache.hpp's kUnknownWriteTime comment on why filesystem write
// times aren't real Unix timestamps on this platform), the timestamps
// here ARE real std::chrono::system_clock seconds-since-epoch -- meant
// to be shown to a user ("created 3 days ago"), not just compared to
// each other.
struct ProjectFile {
    std::string name = "New Project";
    std::string version = kProjectFormatVersion;
    int64_t createdAtUnixSeconds = 0;
    int64_t modifiedAtUnixSeconds = 0;

    std::vector<std::string> scenePaths;
    int activeSceneIndex = -1; // index into scenePaths that was open when last saved, -1 = none

    // Stamps modifiedAtUnixSeconds to "now"; also stamps
    // createdAtUnixSeconds if this is the first call (still 0) --
    // callers are expected to call this immediately before saveToFile(),
    // not to set either timestamp field by hand.
    void touch();

    [[nodiscard]] bool saveToFile(const std::string& path) const;
    [[nodiscard]] bool loadFromFile(const std::string& path);

    // Kronos (Alpha Completion Checklist, "Project System Finalization" --
    // "versioning"): a real, honest compatibility check -- `version` was
    // previously round-tripped but never actually compared against
    // anything, so a project written by a hypothetical future,
    // incompatible Kronos build (or a hand-corrupted `VERSION` line)
    // would load silently with no signal anything might be wrong. Real
    // semver major-version comparison against kProjectFormatVersion;
    // an unparseable version string (missing/non-numeric major
    // component) is also treated as incompatible rather than guessed at.
    // Deliberately does not block loadFromFile() itself -- a project
    // still opens either way (this alpha has nothing to actually migrate
    // yet) -- callers decide whether/how to warn.
    [[nodiscard]] bool isCompatibleVersion() const;

    // Kronos (Alpha Completion Checklist, "Project System Finalization" --
    // "auto-backup"/"recovery"): the exact same non-clobbering
    // "write to a sibling .autosave path, never over the real file"
    // pattern studio::SceneManager::recoveryPathFor()/hasRecoveryFile()
    // already established for scene content -- applied here to the
    // project's own metadata (name/scene list/active tab), so losing an
    // in-progress project-structure edit (e.g. which scenes are open) to
    // a crash is recoverable the same way losing scene content already
    // is, not a second, weaker guarantee.
    [[nodiscard]] static std::string recoveryPathFor(const std::string& projectPath);
    [[nodiscard]] static bool hasRecoveryFile(const std::string& projectPath);
};

} // namespace engine::core
