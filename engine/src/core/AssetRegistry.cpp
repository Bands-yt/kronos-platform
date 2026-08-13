#include "core/AssetRegistry.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace engine::core {

AssetMetadata AssetRegistry::importAsset(const std::string& path) {
    AssetMetadata metadata = extractAssetMetadata(path);
    if (!metadata.succeeded) return metadata;

    AssetRegistryEntry entry;
    entry.path = path;
    entry.kind = metadata.kind;
    entry.fileSizeBytes = metadata.fileSizeBytes;
    entry.vertexCount = metadata.vertexCount;
    entry.triangleCount = metadata.triangleCount;
    entry.width = metadata.width;
    entry.height = metadata.height;
    entry.channels = metadata.channels;
    entry.durationSeconds = metadata.durationSeconds;
    entry.sampleRate = metadata.sampleRate;
    entry.channelCount = metadata.channelCount;

    auto it = std::find_if(entries_.begin(), entries_.end(), [&](const AssetRegistryEntry& e) { return e.path == path; });
    if (it != entries_.end()) {
        *it = entry; // real re-import -- replaces the stale entry, not a duplicate
    } else {
        entries_.push_back(entry);
    }
    return metadata;
}

void AssetRegistry::removeAsset(const std::string& path) {
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [&](const AssetRegistryEntry& e) { return e.path == path; }),
                   entries_.end());
}

bool AssetRegistry::contains(const std::string& path) const {
    return std::any_of(entries_.begin(), entries_.end(), [&](const AssetRegistryEntry& e) { return e.path == path; });
}

bool AssetRegistry::saveToFile(const std::string& path) const {
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) return false;

    out << "ASSETREGISTRY 1\n";
    for (const auto& e : entries_) {
        // path is last on the line (never quoted -- loadFromFile reads it
        // as "everything after the numeric fields"), same trailing-string
        // convention SceneFile's own MESHSOURCE line already uses, so a
        // real path with spaces round-trips correctly.
        out << "ASSET " << static_cast<int>(e.kind) << ' ' << e.fileSizeBytes << ' ' << e.vertexCount << ' '
            << e.triangleCount << ' ' << e.width << ' ' << e.height << ' ' << e.channels << ' ' << e.durationSeconds
            << ' ' << e.sampleRate << ' ' << e.channelCount << ' ' << e.path << "\n";
    }
    out << "END\n";
    return out.good();
}

bool AssetRegistry::loadFromFile(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return false;

    std::string header;
    if (!std::getline(in, header) || header.rfind("ASSETREGISTRY", 0) != 0) return false;

    std::vector<AssetRegistryEntry> loaded;
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("ASSET ", 0) == 0) {
            AssetRegistryEntry e;
            std::istringstream iss(line.substr(6));
            int kindInt = 0;
            iss >> kindInt >> e.fileSizeBytes >> e.vertexCount >> e.triangleCount >> e.width >> e.height >> e.channels >>
                e.durationSeconds >> e.sampleRate >> e.channelCount;
            e.kind = static_cast<AssetKind>(kindInt);
            std::string rest;
            std::getline(iss, rest);
            if (!rest.empty() && rest.front() == ' ') rest.erase(rest.begin());
            e.path = rest;
            loaded.push_back(std::move(e));
        } else if (line == "END") {
            break;
        }
        // Any other/unrecognized line is skipped -- forward-compatible
        // with a future field addition, same convention as SceneFile/
        // AnimationClip.
    }

    entries_ = std::move(loaded);
    return true;
}

} // namespace engine::core
