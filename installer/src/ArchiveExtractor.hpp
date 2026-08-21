#pragma once

#include <string>

namespace kronos_installer {

struct ExtractResult {
    bool success = false;
    std::string error;
    int filesExtracted = 0;
    // The real single top-level directory the archive unpacked into
    // ("kronos-linux-x64" / "kronos-windows-x64" for the archives
    // .github/workflows/build.yml actually publishes), reported by the
    // extractor rather than guessed by callers -- the packaging step's
    // own PKG_DIR name is free to change, and a caller hardcoding it
    // would then silently build paths to files that aren't there.
    // Empty if the archive genuinely had no common top-level directory.
    std::string topLevelDirectory;
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
