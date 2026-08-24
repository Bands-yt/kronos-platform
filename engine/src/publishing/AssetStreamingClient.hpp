#pragma once

#include <string>

#include "core/KronosApi.hpp"

namespace engine::publishing {

struct PackageFetchResult {
    bool success = false;
    // The real, local, cached .kronos archive path.
    std::string archivePath;
    // The real, local directory the archive was extracted into --
    // scene.txt/metadata.json/package.json/every bundled asset restored
    // to its own original relative path (see extractWorldPackageArchive()).
    std::string extractedDirectory;
    // True when no real download happened this call -- the archive was
    // already present locally, content-addressed by hash, and its own
    // hash was re-verified before trusting it (see .cpp).
    bool wasCached = false;
    std::string error;
};

// Kronos ("Dynamic Asset Streaming"): the real, content-addressed
// fetch-and-cache client both a player's own engine_runtime and a
// dedicated game server use to get a published game's real package
// locally. Asks the backend (KronosApi::fetchGamePackageInfo()) for
// this game's current real sha256 + a real download URL, checks
// `cacheRootDirectory` for a file already named by that hash (a real
// download only ever happens for content genuinely not already on
// disk), downloads via a real, direct, streamed-to-disk HTTP GET if
// needed (never buffers a whole multi-hundred-MB package in memory),
// re-hashes what actually landed on disk before trusting it (the
// backend's own confirm-time verification does not protect against a
// corrupted download in transit), then extracts it via
// extractWorldPackageArchive().
//
// Real, honest failure (success=false, no partial cache/extraction left
// behind to be mistaken for real content) at any step: no package
// published for this game, an unreachable backend, a download that
// doesn't actually hash to what was promised, or a corrupt archive.
[[nodiscard]] PackageFetchResult fetchGamePackage(core::KronosApi& api, const std::string& slug,
                                                    const std::string& cacheRootDirectory);

} // namespace engine::publishing
