#pragma once

#include <string>

namespace engine::studio {

// A third-party plugin's metadata -- the same hand-rolled, forward-
// compatible "KEY value per line, END terminator" text format
// core::AnimationClip/core::Prefab already use (see either's .cpp for
// the shared convention), applied here to plugin packaging instead of
// content. No JSON/serialization library pulled in for a data shape this
// small, same reasoning as those two.
//
// A plugin "package" today is just this manifest file plus one Luau
// entry script, sitting next to each other on disk (entryScript is a
// path relative to the manifest's own directory) -- no zip/archive
// format, no asset bundling, no dependency list. See
// studio/plugins/ScriptedPlugin.hpp for what loading one actually does.
struct PluginManifest {
    std::string name = "New Plugin";
    std::string version = "1.0.0";
    std::string author;
    std::string description;
    std::string entryScript = "main.lua"; // relative to the manifest file's own directory

    [[nodiscard]] bool saveToFile(const std::string& path) const;
    [[nodiscard]] bool loadFromFile(const std::string& path);
};

} // namespace engine::studio
