#include "studio/RustPluginProject.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <deque>
#include <fstream>
#include <sstream>
#include <thread>

#include "core/ProcessLaunch.hpp"

namespace engine::studio {

namespace fs = std::filesystem;

namespace {

std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

bool writeFile(const fs::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    out << text;
    return static_cast<bool>(out);
}

// Replaces the whole line that starts with `prefix`.
bool replaceLine(std::string& text, const std::string& prefix, const std::string& line) {
    size_t at = 0;
    while (at < text.size()) {
        const size_t end = text.find('\n', at);
        const size_t stop = end == std::string::npos ? text.size() : end;
        if (text.compare(at, prefix.size(), prefix) == 0) {
            text.replace(at, stop - at, line);
            return true;
        }
        if (end == std::string::npos) break;
        at = end + 1;
    }
    return false;
}

} // namespace

std::string RustPluginProject::crateName(const std::string& userName) {
    std::string out;
    for (char c : userName) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u)) {
            out += static_cast<char>(std::tolower(u));
        } else if (!out.empty() && out.back() != '_') {
            out += '_';
        }
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    while (!out.empty() && !std::isalpha(static_cast<unsigned char>(out.front()))) out.erase(out.begin());
    return out;
}

std::string RustPluginProject::libraryFileName(const std::string& crate) {
#if defined(_WIN32)
    return crate + ".dll";
#elif defined(__APPLE__)
    return "lib" + crate + ".dylib";
#else
    return "lib" + crate + ".so";
#endif
}

fs::path RustPluginProject::builtLibrary(const fs::path& projectDir, const std::string& crate) {
    return projectDir / "target" / "release" / libraryFileName(crate);
}

bool RustPluginProject::create(const fs::path& root, const std::string& name, const fs::path& templateDir,
                               const fs::path& sdkDir, fs::path& createdDir, std::string& error) {
    const std::string crate = crateName(name);
    if (crate.empty()) {
        error = "Use letters and digits in the name (it has to start with a letter).";
        return false;
    }
    if (!fs::is_regular_file(templateDir / "Cargo.toml") || !fs::is_regular_file(templateDir / "src" / "lib.rs")) {
        error = "The Rust plugin template is missing: " + templateDir.string();
        return false;
    }
    if (!fs::is_regular_file(sdkDir / "Cargo.toml")) {
        error = "The Kronos Rust crate is missing: " + sdkDir.string();
        return false;
    }
    const fs::path dir = root / crate;
    std::error_code ec;
    if (fs::exists(dir, ec)) {
        error = "There is already a plugin called \"" + crate + "\".";
        return false;
    }
    fs::create_directories(dir / "src", ec);
    if (ec) {
        error = "Can't create " + dir.string();
        return false;
    }
    std::string manifest = readFile(templateDir / "Cargo.toml");
    const bool named = replaceLine(manifest, "name = ", "name = \"" + crate + "\"");
    const bool linked =
        replaceLine(manifest, "kronos = ", "kronos = { path = \"" + fs::absolute(sdkDir).generic_string() + "\" }");
    if (!named || !linked || !writeFile(dir / "Cargo.toml", manifest)) {
        error = "Couldn't write Cargo.toml";
        fs::remove_all(dir, ec);
        return false;
    }
    fs::copy_file(templateDir / "src" / "lib.rs", dir / "src" / "lib.rs", ec);
    if (ec) {
        error = "Couldn't copy lib.rs";
        fs::remove_all(dir, ec);
        return false;
    }
    createdDir = dir;
    return true;
}

std::vector<RustProjectInfo> RustPluginProject::scan(const fs::path& root) {
    std::vector<RustProjectInfo> out;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(root, ec)) {
        if (!entry.is_directory(ec)) continue;
        if (!fs::is_regular_file(entry.path() / "Cargo.toml", ec) ||
            !fs::is_regular_file(entry.path() / "src" / "lib.rs", ec)) {
            continue;
        }
        RustProjectInfo info;
        info.name = info.crate = entry.path().filename().string();
        info.dir = entry.path();
        const std::string manifest = readFile(entry.path() / "Cargo.toml");
        if (const size_t at = manifest.find("name = \""); at != std::string::npos) {
            const size_t start = at + 8;
            if (const size_t end = manifest.find('"', start); end != std::string::npos && end > start) {
                info.crate = manifest.substr(start, end - start);
            }
        }
        out.push_back(std::move(info));
    }
    std::sort(out.begin(), out.end(), [](const RustProjectInfo& a, const RustProjectInfo& b) { return a.name < b.name; });
    return out;
}

bool RustPluginProject::install(const fs::path& library, const fs::path& pluginDir, std::string& error) {
    std::error_code ec;
    if (!fs::is_regular_file(library, ec)) {
        error = "Build the plugin first (no library at " + library.string() + ").";
        return false;
    }
    fs::create_directories(pluginDir, ec);
    const fs::path target = pluginDir / library.filename();
    // A running Studio has the old library mapped; renaming over it leaves that copy intact.
    const fs::path staging = pluginDir / (library.filename().string() + ".new");
    fs::copy_file(library, staging, fs::copy_options::overwrite_existing, ec);
    if (!ec) fs::rename(staging, target, ec);
    if (ec) {
        error = "Couldn't copy into " + pluginDir.string() + ": " + ec.message();
        fs::remove(staging, ec);
        return false;
    }
    return true;
}

bool RustPluginProject::cargoAvailable() {
    std::string error;
    core::ChildProcess probe;
    core::ChildProcess::Options options;
    options.executable = "cargo";
    options.args = {"--version"};
    options.logFile = (fs::temp_directory_path() / "kronos-cargo-probe.log").string();
    if (!probe.start(options, error)) return false;
    for (int i = 0; i < 100 && probe.running(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return !probe.running() && probe.exitStatus() == 0;
}

bool RustBuild::start(const fs::path& projectDir, std::string& error) {
    cancel();
    logFile_ = (fs::temp_directory_path() / ("kronos-rust-build-" + std::to_string(core::currentProcessId()) + ".log")).string();
    core::ChildProcess::Options options;
    options.executable = "cargo";
    options.args = {"build", "--release", "--offline", "--color", "never"};
    options.workingDirectory = projectDir.string();
    options.logFile = logFile_;
    if (!process_.start(options, error)) {
        error = "Couldn't run cargo. Is Rust installed? (" + error + ")";
        state_ = State::Failed;
        return false;
    }
    state_ = State::Running;
    return true;
}

void RustBuild::update() {
    if (state_ != State::Running || process_.running()) return;
    state_ = process_.exitStatus() == 0 ? State::Succeeded : State::Failed;
}

void RustBuild::cancel() {
    if (state_ == State::Running) process_.stop();
    state_ = State::Idle;
}

std::vector<std::string> RustBuild::logTail(size_t lines) const {
    std::ifstream in(logFile_);
    std::deque<std::string> last;
    std::string line;
    while (std::getline(in, line)) {
        last.push_back(line);
        if (last.size() > lines) last.pop_front();
    }
    return {last.begin(), last.end()};
}

} // namespace engine::studio
