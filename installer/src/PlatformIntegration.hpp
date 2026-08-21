#pragma once

#include <string>

namespace kronos_installer {

// Kronos ("Bootstrap Installer" -- "Create a desktop shortcut (Windows)
// or update the local path (Linux)"): real, platform-native post-
// install integration. `installedRuntimePath` is the real, absolute
// path to the just-extracted `engine_runtime` (or `engine_runtime.exe`)
// binary. Real, honest best-effort -- returns false (with a real,
// human-readable reason via `outError`) rather than silently pretending
// to succeed; the archive is already fully extracted and usable by this
// point regardless, so a failure here is real but non-fatal to "Kronos
// is installed."
[[nodiscard]] bool createPlatformShortcut(const std::string& installedRuntimePath, std::string& outError);

} // namespace kronos_installer
