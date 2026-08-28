#pragma once

// Kronos (Beta Roadmap "Package Registry Integration"): the real, local
// engine-side wrapper over polyglot::PackageRegistry -- see that header's
// own comment for exactly what "package resolution" means here (real
// manifest parsing + real DFS dependency resolution, NOT semver ranges).
//
// Real, honest scope: local-only. There is no remote package registry
// endpoint anywhere in this codebase (kronosplatform.com's own API
// surface doesn't expose one), so this never fetches/downloads anything
// -- it resolves and verifies packages that are already present on disk,
// the same way core::SceneFile loads a .kronos file that's already
// local. Fetch/download is a real, separate, unbuilt feature that needs
// an actual server-side registry API to exist first.

#include <string>
#include <unordered_map>
#include <vector>

#include "polyglot/PackageRegistry.hpp"

namespace engine::core {

class PackageManager {
public:
    // Real filesystem scan: every immediate subdirectory of
    // `packagesRootDir` that contains a `package.manifest` file (in
    // polyglot::PackageManifestParser's own text format, see that
    // class's comment) gets parsed and registered. A missing
    // `packagesRootDir` itself is not an error (an empty/not-yet-created
    // packages directory is a real, ordinary state, not a failure) --
    // returns true either way; per-package parse/registration failures
    // are collected into `outWarnings` (one entry per bad package,
    // including its own directory name) rather than aborting the whole
    // scan, so one malformed manifest doesn't block every other real
    // package from loading.
    bool loadFromDirectory(const std::string& packagesRootDir, std::vector<std::string>& outWarnings);

    [[nodiscard]] const polyglot::PackageManifest* find(const std::string& packageId) const {
        return registry_.find(packageId);
    }
    [[nodiscard]] polyglot::DependencyResolutionResult resolveInstallOrder(const std::string& packageId) const {
        return registry_.resolveInstallOrder(packageId);
    }
    [[nodiscard]] size_t size() const { return registry_.size(); }

    // Real local verification -- a manifest can *claim* an artifact
    // exists without it actually being there (a stale manifest, a
    // partially-copied package, a hand-edited file); this is the check
    // that actually confirms it, resolving each declared ARTIFACT's own
    // relativePath against the real directory `loadFromDirectory()`
    // found this package in. Returns false (outError filled) on the
    // first missing artifact or an unknown packageId -- does NOT walk
    // dependencies (a package's own artifacts are its own responsibility
    // to have shipped; verifying a whole dependency closure is exactly
    // what resolveInstallOrder() + a loop over verifyArtifactsExist()
    // per entry already gives a caller, without this method needing to
    // duplicate that iteration itself).
    [[nodiscard]] bool verifyArtifactsExist(const std::string& packageId, std::string& outError) const;

private:
    polyglot::PackageRegistry registry_;
    std::unordered_map<std::string, std::string> packageDirectories_;
};

} // namespace engine::core
