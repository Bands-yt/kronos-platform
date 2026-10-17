#pragma once

#include <string>

namespace engine::core {

// Luau `declare class` text for every Roblox class in the reflection table, so the script editor
// can complete members. Every class also accepts any other key (children by name).
[[nodiscard]] std::string robloxLuauDefinitions();

} // namespace engine::core
