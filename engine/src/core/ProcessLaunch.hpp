#pragma once

#include <string>
#include <vector>

namespace engine::core {

// Kronos ("Game Catalogue Overhaul", Phase 3): launches a real, separate
// process and does NOT wait for it -- fire-and-forget, matching what
// both real call sites need (opening Kronos Studio as a second running
// app, or relaunching engine_runtime itself with a different CLI flag
// for a still-hardcoded rich mode like TNT Wars). Real, POSIX-only for
// now (`#if defined(__unix__) || defined(__APPLE__)`, same explicit
// platform-scoping precedent as analytics::CrashReporter -- a real,
// stated gap on Windows, not silently unsupported) via posix_spawn(),
// which -- unlike system()/popen() -- takes an explicit argv array with
// no shell involved at all, so no command-injection risk from
// `executablePath`/`args` regardless of their contents.
//
// Returns false immediately if the spawn itself fails (executable not
// found, real OS-level spawn error); does NOT reflect anything about
// whether the spawned process goes on to succeed or crash -- the same
// honest "did the real OS call succeed" scope as
// Physics::attachBodyToEntity()'s own return value.
[[nodiscard]] bool launchProcess(const std::string& executablePath, const std::vector<std::string>& args);

} // namespace engine::core
