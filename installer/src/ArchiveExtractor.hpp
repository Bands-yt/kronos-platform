#pragma once

#include <string>

namespace kronos_installer {

struct ExtractResult {
    bool success = false;
    std::string error;
    int filesExtracted = 0;
};

// Kronos ("Bootstrap Installer" -- "Extract the archive to a user-
// specified installation directory"): real extraction for both real
// archive shapes the release workflow actually publishes
// (.github/workflows/build.yml) -- .zip (Windows, via the real, vendored
// miniz reader) and .tar.gz (Linux, via zlib's real gzip inflate plus
// this file's own real, hand-written POSIX ustar/GNU-tar parser -- zlib
// only speaks raw/gzip *compression*, tar is a separate real archive
// *format* layered on top, so both pieces are needed). Dispatches on
// the real archive's own filename extension -- never guesses format
// from content sniffing.
[[nodiscard]] ExtractResult extractArchive(const std::string& archivePath, const std::string& destinationDir);

} // namespace kronos_installer
