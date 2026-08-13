#include "core/ProjectFile.hpp"

#include <charconv>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>

namespace engine::core {

namespace {
// Real, guarded major-version extraction: the digits before the first
// '.', or nothing if `version` doesn't start with a digit run at all.
// std::from_chars never throws on malformed input -- same "never throws
// on a corrupt field" discipline ProjectFile::loadFromFile() already
// uses for its own numeric fields.
[[nodiscard]] std::optional<int> parseMajorVersion(const std::string& version) {
    int major = 0;
    auto result = std::from_chars(version.data(), version.data() + version.size(), major);
    if (result.ec != std::errc{} || result.ptr == version.data()) return std::nullopt;
    return major;
}
} // namespace

void ProjectFile::touch() {
    int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                      std::chrono::system_clock::now().time_since_epoch())
                      .count();
    if (createdAtUnixSeconds == 0) createdAtUnixSeconds = now;
    modifiedAtUnixSeconds = now;
}

bool ProjectFile::saveToFile(const std::string& path) const {
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) return false;

    out << "PROJECT 1\n";
    out << "NAME " << name << "\n";
    out << "VERSION " << version << "\n";
    out << "CREATED " << createdAtUnixSeconds << "\n";
    out << "MODIFIED " << modifiedAtUnixSeconds << "\n";
    out << "ACTIVESCENE " << activeSceneIndex << "\n";
    for (const auto& scenePath : scenePaths) {
        out << "SCENE " << scenePath << "\n";
    }
    out << "END\n";
    return out.good();
}

bool ProjectFile::loadFromFile(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return false;

    std::string header;
    if (!std::getline(in, header) || header.rfind("PROJECT", 0) != 0) return false;

    ProjectFile loaded;
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream iss(line);
        std::string key;
        iss >> key;
        std::string rest;
        std::getline(iss, rest);
        if (!rest.empty() && rest.front() == ' ') rest.erase(rest.begin());

        if (key == "NAME") {
            loaded.name = rest;
        } else if (key == "VERSION") {
            loaded.version = rest;
        } else if (key == "CREATED") {
            // istringstream >> rather than std::stoll -- never throws on a
            // malformed/corrupt field, just leaves the value at its
            // pre-call default, same guarded-parsing discipline as
            // core::ObjLoader.
            std::istringstream(rest) >> loaded.createdAtUnixSeconds;
        } else if (key == "MODIFIED") {
            std::istringstream(rest) >> loaded.modifiedAtUnixSeconds;
        } else if (key == "ACTIVESCENE") {
            std::istringstream(rest) >> loaded.activeSceneIndex;
        } else if (key == "SCENE") {
            loaded.scenePaths.push_back(rest);
        } else if (key == "END") {
            break;
        }
        // Any other/unrecognized key is skipped -- forward-compatible with
        // a future field addition, same convention as every other
        // saveToFile/loadFromFile pair in core/.
    }

    *this = std::move(loaded);
    return true;
}

bool ProjectFile::isCompatibleVersion() const {
    std::optional<int> loadedMajor = parseMajorVersion(version);
    std::optional<int> currentMajor = parseMajorVersion(kProjectFormatVersion);
    return loadedMajor.has_value() && currentMajor.has_value() && *loadedMajor == *currentMajor;
}

std::string ProjectFile::recoveryPathFor(const std::string& projectPath) { return projectPath + ".autosave"; }

bool ProjectFile::hasRecoveryFile(const std::string& projectPath) {
    return std::filesystem::exists(recoveryPathFor(projectPath));
}

} // namespace engine::core
