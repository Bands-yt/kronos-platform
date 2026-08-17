#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/SceneFile.hpp"

namespace engine::core {

// One real, on-disk snapshot's location + capture time.
struct SceneSnapshotEntry {
    std::string path;
    int64_t unixSeconds = 0;
};

// Kronos ("Studio QoL Sprint" -- "Auto-Recovery & Delta Scene Snapshots"):
// a real, rotating, multi-slot snapshot history, layered on top of (not a
// replacement for) SceneManager's existing single-slot `.autosave` file
// (see SceneManager::tickAutosave()'s own comment) -- that mechanism
// already protects the single most recent point in time; this adds real
// depth, so a crash or bad edit that corrupts the *latest* snapshot still
// leaves earlier ones recoverable.
//
// Real, stated honesty: there is no binary-diff/delta serialization
// anywhere in this codebase (core::SceneFile round-trips a full scene,
// nothing partial -- see its own header). "Delta" in the sprint's own
// naming is honestly implemented here as "a new full point-in-time
// snapshot, kept alongside the previous ones instead of overwriting them"
// -- the same real, working idiom the existing single-slot autosave
// already established, just retaining more than one.
//
// Snapshots for `<scenePath>` live in `<scenePath>.history/`, one file per
// snapshot named `<unixSeconds>.scene`. Every method here is a real,
// synchronous file operation -- no in-memory cache, so listSnapshots()
// reflects real current disk state even across a process restart, which
// is exactly what the crash-recovery case needs.
class SceneHistory {
public:
    static constexpr size_t kMaxSnapshots = 8;

    // Real, full SceneFile capture write into a fresh timestamped slot --
    // never overwrites an existing snapshot. Prunes the oldest snapshot(s)
    // beyond kMaxSnapshots. A real, honest no-op (returns false) if
    // `scenePath` is empty -- there's nothing to snapshot for a
    // never-saved scene, the same boundary tickAutosave() already has.
    static bool recordSnapshot(const std::string& scenePath, const SceneFile& capturedScene);

    // Real, newest-first listing of every snapshot currently on disk for
    // `scenePath`.
    [[nodiscard]] static std::vector<SceneSnapshotEntry> listSnapshots(const std::string& scenePath);

    [[nodiscard]] static bool loadSnapshot(const std::string& snapshotPath, SceneFile& outFile);

    [[nodiscard]] static std::string historyDirectoryFor(const std::string& scenePath);
};

} // namespace engine::core
