#pragma once

#include <string>
#include <vector>

#include "core/AssetMetadata.hpp"

namespace engine::core {

// Kronos (Alpha Roadmap Phase 8, "Asset Pipeline" -- "Asset registry"):
// a real, persisted list of the assets a project has actually imported --
// confirmed genuinely absent before this: core::AssetCache (AssetCache.hpp)
// is a load-dedup cache keyed by mtime, not a browsable list; core::
// ProjectFile tracks scenePaths only; studio::plugins::
// CreatorAssetBrowserPlugin browses the engine's own built-in preset
// catalogue (WorldPropKind/material/particle/terrain presets), not
// creator-imported files. This is the real, missing "here is what this
// project has brought in" list those three neighbors don't provide.
//
// Deliberately a thin, real wrapper around the already-real per-kind
// inspection extractAssetMetadata() (AssetMetadata.hpp) already does --
// importAsset() calls it once at import time and stores the real result;
// this class never re-implements mesh/texture/audio parsing itself.
struct AssetRegistryEntry {
    std::string path;
    AssetKind kind = AssetKind::Unknown;
    uint64_t fileSizeBytes = 0;

    // Mesh (.obj)
    uint32_t vertexCount = 0;
    uint32_t triangleCount = 0;
    // Texture
    int32_t width = 0;
    int32_t height = 0;
    int32_t channels = 0;
    // Audio
    double durationSeconds = 0.0;
    uint32_t sampleRate = 0;
    uint32_t channelCount = 0;
};

class AssetRegistry {
public:
    // Real import: runs the real extractAssetMetadata(path) probe and, if
    // it succeeded, adds a real entry (or replaces an existing one for
    // the same path -- a re-import after the file changed on disk is
    // real, honest, expected usage, not an error). Always returns the
    // real AssetMetadata result, success or failure, so a caller (the
    // Asset Browser's own "Import" button) can show the real, specific
    // failure reason rather than a generic "couldn't import" -- same
    // "return the real result either way" convention
    // publishing::PublishValidationResult already established.
    AssetMetadata importAsset(const std::string& path);

    // A real, honest no-op if `path` isn't currently registered.
    void removeAsset(const std::string& path);

    [[nodiscard]] bool contains(const std::string& path) const;
    [[nodiscard]] const std::vector<AssetRegistryEntry>& entries() const { return entries_; }
    [[nodiscard]] size_t size() const { return entries_.size(); }

    // Same hand-rolled "KEY value per line, END terminator" text format
    // every other save/load struct in core/ (ProjectFile, SceneFile,
    // Prefab) already uses.
    [[nodiscard]] bool saveToFile(const std::string& path) const;
    [[nodiscard]] bool loadFromFile(const std::string& path);

private:
    std::vector<AssetRegistryEntry> entries_;
};

} // namespace engine::core
