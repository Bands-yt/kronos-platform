#pragma once

#include <filesystem>
#include <string>
#include <system_error>

namespace engine::core {

// Copies a project file next to itself as "<name>.bak-<fromVersion>" before it is migrated.
// Never overwrites an existing backup. Returns the backup path, or empty if nothing was copied.
inline std::filesystem::path backupBeforeMigration(const std::filesystem::path& file, const std::string& fromVersion) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) return {};
    fs::path backup = file;
    backup += ".bak-" + fromVersion;
    for (int n = 2; fs::exists(backup, ec); ++n) {
        backup = file;
        backup += ".bak-" + fromVersion + "-" + std::to_string(n);
    }
    fs::copy_file(file, backup, fs::copy_options::none, ec);
    return ec ? fs::path{} : backup;
}

} // namespace engine::core
