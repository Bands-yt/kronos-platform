#include "publishing/PublishValidation.hpp"

#include <cctype>

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

    result.valid = result.errors.empty();
    return result;
}

} // namespace engine::publishing
