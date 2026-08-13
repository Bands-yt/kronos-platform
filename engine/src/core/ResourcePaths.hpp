#pragma once

#include <string>

namespace engine::core {

// Kronos (Alpha Completion Checklist, "Alpha Packaging" -- "engine/studio
// binary packaging"): real bug found while auditing packaging readiness.
// ENGINE_SHADER_DIR/ENGINE_ASSET_DIR (see src/CMakeLists.txt) are
// compile-time absolute paths baked into the binary at build time on
// whatever machine built it -- correct for local development (where the
// binary always runs from the same build tree it was compiled in), but
// fatal for a packaged, distributed Alpha build: copy engine_runtime or
// studio to another machine (or even just move the build folder) and
// they can no longer find their own shaders/assets at all.
//
// executableDirectory()/resolveResourceDir() below let every real call
// site prefer a real directory sitting next to the running executable
// (the flat layout a real packaged distribution uses -- see
// PACKAGING.md) before ever falling back to the compile-time absolute
// path, without changing anything about how a local development build
// already runs (that fallback path is exactly today's existing, unchanged
// behavior).

// Real, Linux-only for now (matches CrashReporter.hpp's own
// #if defined(__linux__) precedent) -- the real directory containing the
// currently-running executable, via /proc/self/exe. Returns an empty
// string on any other platform, or if the real readlink() call fails,
// which resolveResourceDir() below treats as "no packaged-layout
// candidate available", not an error.
[[nodiscard]] std::string executableDirectory();

// Pure, real, and independently testable (takes `exeDir` as a parameter
// rather than calling executableDirectory() itself): prefers
// `exeDir + "/" + subdirName` if it's a real, existing directory (the
// packaged, flat-layout case); otherwise returns `compileTimeFallback`
// unchanged (today's existing dev-build behavior).
[[nodiscard]] std::string resolveResourceDir(const std::string& exeDir, const std::string& subdirName,
                                              const std::string& compileTimeFallback);

} // namespace engine::core
