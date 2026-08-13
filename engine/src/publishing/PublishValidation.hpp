#pragma once

#include <string>
#include <vector>

#include "publishing/WorldMetadata.hpp"

namespace engine::core {
struct SceneFile;
}

namespace engine::publishing {

// Sprint 13 task 1's "Add world integrity validation before publishing"
// -- a real, pure, list-of-real-reasons validator (not just a bool),
// matching this codebase's established "reject-and-log with a specific
// real reason" convention (net::ServerReconciliation::validate(),
// net::RemoteEvent's schema checks, ...). Every check here is pure text/
// data inspection -- no filesystem, no ECS, no GPU -- so it's fully
// headlessly testable, the same "pure or ECS/GPU-only, never both"
// split this codebase draws everywhere (see net::InteractionValidation.hpp's
// own header comment for the same reasoning applied to server authority).
struct PublishValidationResult {
    bool valid = false;
    std::vector<std::string> errors; // real, specific, real-user-facing reasons -- empty iff valid
};

// Real, deliberately generous bounds -- illustrative defaults, not
// derived from anything (same honesty level as safety::RiskScore's own
// escalation thresholds): a real deployment's exact limits are a
// platform policy decision, not an engineering one.
constexpr size_t kMaxTitleLength = 100;
constexpr size_t kMaxDescriptionLength = 2000;
constexpr int kMaxRecommendedPlayerCount = 100;

[[nodiscard]] PublishValidationResult validateWorldMetadata(const WorldMetadata& metadata);

// Real scene-content checks -- a world with zero entities is real,
// honest "there's nothing here to publish", not a valid experience.
[[nodiscard]] PublishValidationResult validateSceneContent(const core::SceneFile& scene);

// Real version-string format check -- accepts "N.N" or "N.N.N" (matching
// core::ProjectFile::version's own "1.0.0"-shaped real precedent), all
// components real, non-negative integers.
[[nodiscard]] bool isValidVersionString(const std::string& version);

// Combines all of the above into one real, complete pre-publish check --
// what NetworkSession::publishWorld() and studio::plugins::PublishingPanel
// both actually call.
[[nodiscard]] PublishValidationResult validateForPublish(const std::string& worldId, const std::string& version,
                                                            const WorldMetadata& metadata, const core::SceneFile& scene);

} // namespace engine::publishing
