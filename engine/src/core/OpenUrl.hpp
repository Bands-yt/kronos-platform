#pragma once

#include <string>

namespace engine::core {

// Kronos ("Google OAuth Authentication" -- "open their default system
// browser"): real, platform-native "open this URL with the OS's own
// default handler" -- deliberately NOT core::launchProcess() (that
// function calls posix_spawn() with a literal, already-resolved
// executable path and is real, stated Linux/POSIX-only; there is no
// portable, fixed path to "the user's default browser launcher" to
// give it). Real, separate per-platform implementation instead:
// posix_spawnp() (the PATH-searching variant) to run "xdg-open" on
// Linux, ShellExecuteW() on Windows -- both real OS-native "open"
// mechanisms, no shell/command-line parsing of `url` involved on either
// platform, so a malformed/hostile URL can't achieve command injection.
[[nodiscard]] bool openUrlInDefaultBrowser(const std::string& url);

} // namespace engine::core
