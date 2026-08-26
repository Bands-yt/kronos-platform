#include "publishing/PublishValidation.hpp"

#include <cctype>
#include <filesystem>
#include <unordered_set>

#include "core/SceneFile.hpp"

namespace engine::publishing {

namespace {
void mergeInto(PublishValidationResult& target, const PublishValidationResult& source) {
    for (const auto& error : source.errors) target.errors.push_back(error);
}
} // namespace

PublishValidationResult validateWorldMetadata(const WorldMetadata& metadata) {
    PublishValidationResult result;

    if (metadata.title.empty()) result.errors.push_back("Title is required.");
    if (metadata.title.size() > kMaxTitleLength) {
        result.errors.push_back("Title exceeds the maximum length of " + std::to_string(kMaxTitleLength) + " characters.");
    }
    if (metadata.description.size() > kMaxDescriptionLength) {
        result.errors.push_back("Description exceeds the maximum length of " + std::to_string(kMaxDescriptionLength) +
                                 " characters.");
    }
    if (metadata.creatorName.empty()) result.errors.push_back("Creator name is required.");
    if (metadata.recommendedPlayerCount <= 0) {
        result.errors.push_back("Recommended player count must be positive.");
    } else if (metadata.recommendedPlayerCount > kMaxRecommendedPlayerCount) {
        result.errors.push_back("Recommended player count exceeds the maximum of " +
                                 std::to_string(kMaxRecommendedPlayerCount) + ".");
    }
    if (metadata.thumbnailPath.empty()) result.errors.push_back("A thumbnail must be captured before publishing.");

    result.valid = result.errors.empty();
    return result;
}

PublishValidationResult validateSceneContent(const core::SceneFile& scene) {
    PublishValidationResult result;
    if (scene.entities.empty()) result.errors.push_back("The world has no entities -- there's nothing to publish.");
    result.valid = result.errors.empty();
    return result;
}

bool isValidVersionString(const std::string& version) {
    if (version.empty()) return false;
    int componentCount = 1;
    bool lastWasDigit = false;
    for (char c : version) {
        if (c == '.') {
            if (!lastWasDigit) return false; // no empty components, e.g. "1..0" or leading "."
            ++componentCount;
            lastWasDigit = false;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) == 0) return false;
        lastWasDigit = true;
    }
    if (!lastWasDigit) return false; // no trailing "."
    return componentCount == 2 || componentCount == 3; // "N.N" or "N.N.N"
}

PublishValidationResult validateForPublish(const std::string& worldId, const std::string& version,
                                             const WorldMetadata& metadata, const core::SceneFile& scene) {
    PublishValidationResult result;
    result.valid = true;

    if (worldId.empty()) result.errors.push_back("World id is required.");
    if (!isValidVersionString(version)) result.errors.push_back("Version must be in the form N.N or N.N.N (e.g. \"1.0\" or \"1.0.0\").");

    mergeInto(result, validateWorldMetadata(metadata));
    mergeInto(result, validateSceneContent(scene));
    mergeInto(result, validateAssetPathsAreRelative(metadata, scene));

    result.valid = result.errors.empty();
    return result;
}

bool isAbsoluteAssetPath(const std::string& path) {
    if (path.empty()) return false;
    if (path.front() == '/' || path.front() == '\\') return true; // Unix absolute, or a bare UNC/rooted Windows path
    // Windows drive-letter absolute: "C:/..." or "C:\\..."
    if (path.size() >= 3 && std::isalpha(static_cast<unsigned char>(path[0])) != 0 && path[1] == ':' &&
        (path[2] == '/' || path[2] == '\\')) {
        return true;
    }
    return false;
}

std::vector<std::string> collectReferencedAssetPaths(const WorldMetadata& metadata, const core::SceneFile& scene) {
    std::vector<std::string> paths;
    if (!metadata.thumbnailPath.empty()) paths.push_back(metadata.thumbnailPath);
    for (const core::SceneEntityRecord& entity : scene.entities) {
        if (!entity.hasMeshSource) continue;
        // Box/Plane/Capsule/Quad are procedural, no real file to validate.
        if (entity.meshSource.kind != core::MeshSourceKind::Obj && entity.meshSource.kind != core::MeshSourceKind::Gltf &&
            entity.meshSource.kind != core::MeshSourceKind::Fbx) {
            continue;
        }
        if (!entity.meshSource.path.empty()) paths.push_back(entity.meshSource.path);
    }
    return paths;
}

PublishValidationResult validateAssetPathsAreRelative(const WorldMetadata& metadata, const core::SceneFile& scene) {
    PublishValidationResult result;
    for (const std::string& path : collectReferencedAssetPaths(metadata, scene)) {
        if (isAbsoluteAssetPath(path)) {
            result.errors.push_back("Asset path \"" + path +
                                     "\" is an absolute local path -- publish only accepts package-relative paths, "
                                     "or it will silently break for anyone else who loads this package.");
        }
    }
    result.valid = result.errors.empty();
    return result;
}

std::vector<std::string> findOrphanedAssetFiles(const std::vector<std::string>& filesOnDisk,
                                                  const std::vector<std::string>& referencedPaths) {
    std::unordered_set<std::string> referenced(referencedPaths.begin(), referencedPaths.end());
    std::vector<std::string> orphans;
    for (const std::string& file : filesOnDisk) {
        if (referenced.find(file) == referenced.end()) orphans.push_back(file);
    }
    return orphans;
}

std::vector<std::string> scanForOrphanedAssetFiles(const std::string& assetDirectory, const WorldMetadata& metadata,
                                                      const core::SceneFile& scene) {
    std::vector<std::string> filesOnDisk;
    std::error_code ec;
    if (!std::filesystem::exists(assetDirectory, ec) || ec) return {}; // real, honest empty result, not an error
    for (const auto& entry : std::filesystem::recursive_directory_iterator(
             assetDirectory, std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (ec) break; // a real, mid-scan filesystem error -- return whatever was found before it, not a crash
        if (entry.is_regular_file(ec) && !ec) filesOnDisk.push_back(entry.path().generic_string());
    }
    return findOrphanedAssetFiles(filesOnDisk, collectReferencedAssetPaths(metadata, scene));
}

} // namespace engine::publishing
