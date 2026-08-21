#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace kronos_installer {

struct ReleaseAsset {
    std::string name;
    std::string downloadUrl;
    uint64_t sizeBytes = 0;
};

struct LatestRelease {
    bool success = false;
    std::string tagName;
    std::vector<ReleaseAsset> assets;
    std::string error;
};

// Kronos ("Bootstrap Installer" -- "GitHub Repository Releases API"):
// real, GET https://api.github.com/repos/<owner>/<repo>/releases/latest
// via libcurl, real JSON parsed via nlohmann_json -- no scraping, no
// guessed URL shape. GitHub requires a real `User-Agent` header on
// every API request (undocumented-until-you-hit-it 403 otherwise) and
// a real `Accept: application/vnd.github+json` header for the
// documented v3 REST shape -- both real, sent here, not an accident of
// a bare curl_easy_perform().
[[nodiscard]] LatestRelease fetchLatestRelease(const std::string& owner, const std::string& repo);

// Real, small helper -- finds the one real asset among `release.assets`
// whose name matches `platformSuffix` (e.g. "linux-x64.tar.gz" or
// "windows-x64.zip"), and separately the asset whose name is that same
// name plus ".sha256" (the real checksum file the release workflow
// also publishes -- see .github/workflows/build.yml's own "Generate
// checksum" step). Returns nullptr for either if no real match exists
// -- a real, honest "this release doesn't have what we're looking for"
// rather than guessing.
[[nodiscard]] const ReleaseAsset* findAssetBySuffix(const LatestRelease& release, const std::string& suffix);

} // namespace kronos_installer
