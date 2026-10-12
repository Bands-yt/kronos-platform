#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "core/ChildProcess.hpp"

namespace engine::studio {

struct RustProjectInfo {
    std::string name;  // the folder name
    std::string crate; // the package name from Cargo.toml
    std::filesystem::path dir;
};

// Rust plugin projects for Studio's "Rust Plugins" window: make one from templates/rust-plugin,
// build it with cargo and copy the library into native_plugins/, where Studio loads it.
class RustPluginProject {
public:
    // Lower-case letters, digits and '_' only, starting with a letter; empty if nothing usable is left.
    [[nodiscard]] static std::string crateName(const std::string& userName);
    [[nodiscard]] static std::string libraryFileName(const std::string& crate);
    [[nodiscard]] static std::filesystem::path builtLibrary(const std::filesystem::path& projectDir,
                                                            const std::string& crate);
    // Copies the template (Cargo.toml + src/) to root/<crate>, pointing it at the Kronos crate in sdkDir.
    static bool create(const std::filesystem::path& root, const std::string& name,
                       const std::filesystem::path& templateDir, const std::filesystem::path& sdkDir,
                       std::filesystem::path& createdDir, std::string& error);
    // Folders in `root` that have a Cargo.toml and src/lib.rs.
    [[nodiscard]] static std::vector<RustProjectInfo> scan(const std::filesystem::path& root);
    // Replaces the file in place-safely (copy next to it, then rename).
    static bool install(const std::filesystem::path& library, const std::filesystem::path& pluginDir,
                        std::string& error);
    [[nodiscard]] static bool cargoAvailable();
};

// One `cargo build --release` run; poll update() from the UI.
class RustBuild {
public:
    enum class State { Idle, Running, Succeeded, Failed };

    [[nodiscard]] bool start(const std::filesystem::path& projectDir, std::string& error);
    void update();
    void cancel();
    [[nodiscard]] State state() const { return state_; }
    [[nodiscard]] const std::string& logFile() const { return logFile_; }
    [[nodiscard]] std::vector<std::string> logTail(size_t lines) const;

private:
    core::ChildProcess process_;
    State state_ = State::Idle;
    std::string logFile_;
};

} // namespace engine::studio
