#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace kronos_installer {

struct DownloadResult {
    bool success = false;
    std::string error;
};

// Kronos ("Bootstrap Installer" -- "a progress bar showing the download
// status"): real libcurl GET streamed directly to `destinationPath` on
// disk (never buffered whole in memory -- a real release archive can be
// well over 100MB, see main.cpp's own real dist/kronos-alpha size).
// `onProgress(bytesDownloaded, totalBytes)` is called from libcurl's
// own real xfer-info callback -- genuine byte counts from the real
// in-flight transfer, not a fabricated/simulated ramp. `totalBytes` is
// 0 if the server didn't report a real Content-Length (a real, honest
// "indeterminate" case the caller's own progress bar should handle,
// e.g. by showing bytes-downloaded-so-far instead of a percentage).
[[nodiscard]] DownloadResult downloadFile(const std::string& url, const std::string& destinationPath,
                                           const std::function<void(uint64_t, uint64_t)>& onProgress);

} // namespace kronos_installer
