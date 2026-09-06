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

// Kronos ("Installer Component Selection" -- "In-Player Tool Manager"):
// the real, generalized form createPlatformShortcut() above is now a
// thin wrapper around -- one real shortcut/launcher for ANY installed
// component (Player, Studio, 3D Tools, Movie Mode, Audio), not just the
// base Player install. `desktopBasename` is the real, extension-less
// filename this writes on Linux (e.g. "kronos-3d-maker" ->
// ~/.local/share/applications/kronos-3d-maker.desktop, matching the
// exact same names engine/assets/desktop/*.desktop already use for a
// manually-extracted, non-installer install); `iconPath` is the real,
// absolute path to that component's own already-installed .png/.ico
// (see engine/assets/icons/kronos_<app>_icon.png) -- empty is a real,
// honest "no icon set" rather than a guessed path. `registerKronosUri`
// is true ONLY for the Player component: the kronos:// URL protocol
// launches the game client specifically, so registering it for every
// creator tool too would make Windows/xdg-open ask the user to choose
// among 5 apps every time a kronos:// link is clicked, which is worse
// than the real, single, correct target this already ships for Player
// alone.
[[nodiscard]] bool createComponentShortcut(const std::string& installedExePath, const std::string& displayName,
                                            const std::string& desktopBasename, const std::string& iconPath,
                                            const std::string& categories, bool registerKronosUri,
                                            std::string& outError);

} // namespace kronos_installer
