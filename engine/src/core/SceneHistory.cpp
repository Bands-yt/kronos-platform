#include "core/SceneHistory.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>

namespace engine::core {

std::string SceneHistory::historyDirectoryFor(const std::string& scenePath) { return scenePath + ".history"; }

bool SceneHistory::recordSnapshot(const std::string& scenePath, const SceneFile& capturedScene) {
    if (scenePath.empty()) return false;

    std::string directory = historyDirectoryFor(scenePath);
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        std::fprintf(stderr, "SceneHistory: failed to create \"%s\" (%s)\n", directory.c_str(), ec.message().c_str());
        return false;
    }

    int64_t unixSeconds = std::chrono::duration_cast<std::chrono::seconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();
    // Real, honest collision handling -- two snapshots in the same
    // wall-clock second (a rapid major-edit burst) would otherwise
    // silently overwrite one another; append a small disambiguating
    // suffix instead of losing one.
    std::string path = directory + "/" + std::to_string(unixSeconds) + ".scene";
    int suffix = 1;
    while (std::filesystem::exists(path)) {
        path = directory + "/" + std::to_string(unixSeconds) + "_" + std::to_string(suffix) + ".scene";
        ++suffix;
    }

    if (!capturedScene.saveToFile(path)) {
        std::fprintf(stderr, "SceneHistory: failed to write snapshot \"%s\"\n", path.c_str());
        return false;
    }

    // Prune oldest beyond kMaxSnapshots -- a rotating buffer, not an
    // ever-growing log.
    std::vector<SceneSnapshotEntry> snapshots = listSnapshots(scenePath);
    for (size_t i = kMaxSnapshots; i < snapshots.size(); ++i) {
        std::filesystem::remove(snapshots[i].path, ec);
    }

    return true;
}

std::vector<SceneSnapshotEntry> SceneHistory::listSnapshots(const std::string& scenePath) {
    std::vector<SceneSnapshotEntry> entries;
    std::string directory = historyDirectoryFor(scenePath);
    if (!std::filesystem::exists(directory)) return entries;

    for (const auto& dirEntry : std::filesystem::directory_iterator(directory)) {
        if (!dirEntry.is_regular_file()) continue;
        const std::filesystem::path& p = dirEntry.path();
        if (p.extension() != ".scene") continue;

        // The leading digits of the filename stem are the real capture
        // time -- parsed back out rather than trusting filesystem mtime,
        // which a plain file copy/restore elsewhere could change.
        std::string stem = p.stem().string();
        int64_t unixSeconds = 0;
        size_t i = 0;
        while (i < stem.size() && stem[i] >= '0' && stem[i] <= '9') {
            unixSeconds = unixSeconds * 10 + (stem[i] - '0');
            ++i;
        }
        if (i == 0) continue; // real, honest skip of a name this class didn't write itself

        entries.push_back(SceneSnapshotEntry{p.string(), unixSeconds});
    }

    std::sort(entries.begin(), entries.end(),
              [](const SceneSnapshotEntry& a, const SceneSnapshotEntry& b) { return a.unixSeconds > b.unixSeconds; });
    return entries;
}

bool SceneHistory::loadSnapshot(const std::string& snapshotPath, SceneFile& outFile) {
    return outFile.loadFromFile(snapshotPath);
}

} // namespace engine::core
