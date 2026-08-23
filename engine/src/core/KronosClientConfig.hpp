#pragma once

#include <string>

namespace engine::core {

// Kronos Client endpoint configuration.
//
// Resolution order, highest priority first:
//   1. config.json next to the executable
//   2. KRONOS_API_URL / KRONOS_AUTH_URL environment variables
//   3. http://159.65.17.24 (the live Kronos production server)
//
// config.json wins over the environment on purpose: it is the file an
// *installed* build ships with, and a stray environment variable left in
// somebody's shell should not silently redirect a real install at a
// different backend. The environment stays available for development and
// for CI, where editing a file is the awkward option.
//
// The built-in fallback used to be localhost:8080, which let a build with
// no configuration at all run against a local backend with no setup step.
// It now points at production instead, so anyone doing local/offline
// development MUST set KRONOS_API_URL=http://localhost:8080 (or drop a
// config.json) -- without that, a locally-built client talks to the live
// server by default. Set deliberately per explicit instruction; if that
// trade turns out to be wrong, the fix is a local override, not reverting
// this default, since the whole point was "the client should point at
// production out of the box."
struct KronosClientConfig {
    std::string apiUrl;
    std::string authUrl;
    // Where apiUrl actually came from, for the log line at startup --
    // "which backend am I talking to, and why" should never be a mystery
    // when something is misconfigured.
    std::string source;
};

// Reads config.json from `executableDir`. Missing file, unreadable file
// and malformed JSON are all non-fatal: each falls through to the next
// source rather than refusing to start, because a client that will not
// launch because of a typo'd config is worse than one that launches
// against the default.
[[nodiscard]] KronosClientConfig loadKronosClientConfig(const std::string& executableDir);

} // namespace engine::core
