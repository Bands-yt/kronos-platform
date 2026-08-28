#include "core/PackageManager.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace engine::core {

bool PackageManager::loadFromDirectory(const std::string& packagesRootDir, std::vector<std::string>& outWarnings) {
    std::error_code ec;
    if (!std::filesystem::is_directory(packagesRootDir, ec)) return true; // no packages directory yet -- not an error

    for (const auto& entry : std::filesystem::directory_iterator(packagesRootDir, ec)) {
        if (!entry.is_directory()) continue;
        std::filesystem::path manifestPath = entry.path() / "package.manifest";
        std::string packageDirName = entry.path().filename().string();

        std::ifstream in(manifestPath);
        if (!in.is_open()) continue; // real, ordinary case: a subdirectory that just isn't a package

        std::ostringstream buffer;
        buffer << in.rdbuf();

        polyglot::PackageManifest manifest;
        std::string parseError;
        if (!polyglot::PackageManifestParser::parse(buffer.str(), manifest, parseError)) {
            outWarnings.push_back(packageDirName + ": " + parseError);
            continue;
        }
        if (!registry_.registerManifest(manifest)) {
            outWarnings.push_back(packageDirName + ": manifest parsed but failed registration (empty packageId/version, "
                                                     "empty artifact path, or a self-dependency)");
            continue;
        }
        packageDirectories_[manifest.packageId] = entry.path().string();
    }
    return true;
}

bool PackageManager::verifyArtifactsExist(const std::string& packageId, std::string& outError) const {
    const polyglot::PackageManifest* manifest = registry_.find(packageId);
    if (!manifest) {
        outError = "unknown package \"" + packageId + "\"";
        return false;
    }
    auto dirIt = packageDirectories_.find(packageId);
    if (dirIt == packageDirectories_.end()) {
        outError = "package \"" + packageId + "\" was registered without a known real directory (not loaded via loadFromDirectory())";
        return false;
    }

    std::filesystem::path packageDir(dirIt->second);
    for (const auto& artifact : manifest->artifacts) {
        std::error_code ec;
        std::filesystem::path artifactPath = packageDir / artifact.relativePath;
        if (!std::filesystem::exists(artifactPath, ec)) {
            outError = "package \"" + packageId + "\" declares artifact \"" + artifact.relativePath +
                        "\" but it doesn't exist at " + artifactPath.string();
            return false;
        }
    }
    return true;
}

} // namespace engine::core
