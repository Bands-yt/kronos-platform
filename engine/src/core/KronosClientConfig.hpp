#pragma once

#include <string>

namespace engine::core {

// Kronos Client endpoint configuration.
//
// Resolution order, highest priority first:
//   1. config.json next to the executable
//   2. KRONOS_API_URL / KRONOS_AUTH_URL environment variables
//   3. http://localhost:8080
//
// config.json wins over the environment on purpose: it is the file an
// *installed* build ships with, and a stray environment variable left in
// somebody's shell should not silently redirect a real install at a
// different backend. The environment stays available for development and
// for CI, where editing a file is the awkward option.
//
// The fallback keeps a build with no configuration at all working against
// a local backend, so offline development needs no setup step.
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
