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

// Kronos ("Studio QoL Sprint" -- "catching absolute local path
// references (C:/... or /home/...) so all assets use relative package
// URIs"): real, pure, string-only check -- true for a Unix-style
// absolute path ("/home/...") or a Windows drive-letter absolute path
// ("C:/..." or "C:\\..."), false for anything else (a real relative
// package-URI-style path, or an empty "not set" path). No filesystem
// access -- pure string inspection, the same "pure, headlessly
// testable" contract every other function in this file already keeps.
[[nodiscard]] bool isAbsoluteAssetPath(const std::string& path);

// Real, pure -- every non-empty, real, file-referencing asset path this
// scene/metadata combination references (today: `WorldMetadata::
// thumbnailPath`, plus each entity's own `MeshSource::path` when
// `hasMeshSource && meshSource.kind == MeshSourceKind::Obj`). The one
// real, shared definition both `validateAssetPathsAreRelative()` and the
// real orphan scan below build from, so "what counts as a referenced
// asset" lives in exactly one place, not two independently-drifting
// copies of the same list.
[[nodiscard]] std::vector<std::string> collectReferencedAssetPaths(const WorldMetadata& metadata,
                                                                      const core::SceneFile& scene);

// Real, pure -- flags every referenced asset path (see
// collectReferencedAssetPaths()) that is absolute rather than relative.
// A real, blocking correctness issue, not just hygiene: an absolute path
// baked into a published manifest points at a location that only exists
// on the original creator's own machine, so it silently breaks for
// everyone else who loads the published package.
[[nodiscard]] PublishValidationResult validateAssetPathsAreRelative(const WorldMetadata& metadata,
                                                                       const core::SceneFile& scene);

// Kronos ("Studio QoL Sprint" -- "flagging orphaned asset files in the
// project directory that aren't referenced by any ECS component or
// manifest entry"): real, pure set difference -- `filesOnDisk` (every
// real file a caller already scanned from the project's own asset
// directory) minus `referencedPaths` (collectReferencedAssetPaths()'s
// own output) is every file genuinely never referenced by anything, a
// real orphan, not a guess. Deliberately takes both as plain lists
// rather than doing its own filesystem scan (this file's own "pure, no
// filesystem" contract) -- see scanForOrphanedAssetFiles() below for the
// real, filesystem-touching wrapper an actual caller uses. Path
// comparison is exact-string (not case-folded or symlink-resolved) --
// both lists are expected to already be in the same real, normalized
// form a caller's own scan and the scene's own saved paths already use.
[[nodiscard]] std::vector<std::string> findOrphanedAssetFiles(const std::vector<std::string>& filesOnDisk,
                                                                 const std::vector<std::string>& referencedPaths);

// Real, filesystem-touching wrapper (not headlessly pure, unlike every
// other function in this file -- kept separate specifically so the pure
// core logic above stays testable without a real directory on disk):
// lists every real file under `assetDirectory` (recursive -- creator
// asset directories commonly nest mesh/texture files in subfolders) and
// calls findOrphanedAssetFiles() against whatever
// collectReferencedAssetPaths() finds in `metadata`/`scene`. A real,
// honest empty result (not an error) if `assetDirectory` doesn't exist.
// Deliberately returns a plain list, not a PublishValidationResult --
// orphaned files are a real, worth-surfacing advisory finding, not a
// hard-blocking error the way an absolute path or a missing title is; a
// caller (studio::plugins::PublishingPanel) decides how to present that
// distinction.
[[nodiscard]] std::vector<std::string> scanForOrphanedAssetFiles(const std::string& assetDirectory,
                                                                    const WorldMetadata& metadata,
                                                                    const core::SceneFile& scene);

} // namespace engine::publishing
