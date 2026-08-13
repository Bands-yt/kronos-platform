#pragma once

#include <string>
#include <vector>

#include "studio/PluginManifest.hpp"

namespace engine::studio {

// Kronos (Alpha Roadmap Phase 9, "Platform Services" -- "Plugin
// marketplace (local only for alpha)"): scans a real local directory for
// `*.manifest` files and real-parses each one via
// PluginManifest::loadFromFile() -- turning PluginBrowserPlugin's "type
// the exact path" into a real "browse what's actually here" experience.
// Not a marketplace in the publish/download sense -- explicitly "local
// only for alpha" per the roadmap's own parenthetical: this discovers
// manifests already sitting in a folder on this machine, nothing more.
struct DiscoveredPlugin {
    std::string manifestPath;
    PluginManifest manifest; // only meaningful when parseSucceeded is true
    bool parseSucceeded = false;
};

// A real, honest empty result (not an error) if `directoryPath` doesn't
// exist or has no `*.manifest` files -- matches this codebase's own
// "missing input is a real zero-result answer, not a thrown exception"
// convention (see core::AssetCache's own miss-is-not-an-error shape).
// A manifest file that fails to parse is still included, with
// parseSucceeded = false, so a caller can show *which* file is broken
// rather than silently skipping it.
[[nodiscard]] std::vector<DiscoveredPlugin> scanLocalPluginDirectory(const std::string& directoryPath);

} // namespace engine::studio
