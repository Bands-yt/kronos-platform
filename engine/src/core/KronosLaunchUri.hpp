#pragma once

#include <string>

namespace engine::core {

// Parses the kronos:// launch deep link -- kronos://launch?game=<slug>
// -- registered as a URL protocol handler by the installer (see
// installer/src/PlatformIntegration.cpp) and delivered to a running or
// freshly-launched engine_runtime via the --kronos-uri=<uri> argv flag
// (see main.cpp's argv loop).
//
// Kept free of any Win32/X11/argv concerns on purpose: this is pure
// string parsing, so it can be unit tested without a real OS-level
// protocol registration, which is the part that genuinely cannot be
// exercised in an automated test (there is no way to programmatically
// fire a real OS URL-activation event in a headless CI run).
//
// Returns false -- leaving `outSlug` untouched -- for anything that
// isn't recognizably this exact shape: wrong scheme, wrong action, or
// no game parameter. An unexpected or malformed URI is an honest no-op,
// never a guess at what the caller "probably" meant.
[[nodiscard]] bool parseKronosLaunchUri(const std::string& uri, std::string& outSlug);

} // namespace engine::core
