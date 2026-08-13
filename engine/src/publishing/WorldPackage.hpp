#pragma once

#include <string>

#include "core/SceneFile.hpp"
#include "publishing/WorldMetadata.hpp"

namespace engine::publishing {

// Sprint 13 task 1's "Package terrain, props, scripts, materials,
// particles, and metadata" -- the real package format. A world package
// is a real directory on disk (`<directory>/scene.txt` +
// `<directory>/metadata.json` + `<directory>/package.json` [+ a
// thumbnail image file, see ThumbnailCapture.hpp]), reusing two
// already-real, already-tested serialization mechanisms unchanged
// rather than inventing a third bundle format: `core::SceneFile`'s own
// hand-rolled text format for entities/particles/camera (props,
// materials -- inline per-entity fields on SceneEntityRecord -- and
// particle emitters are ALL already real, already-serializable content
// SceneFile covers), and `WorldMetadata`'s real JSON for the publish-
// specific fields. `worldId`/`version` deliberately live in their own
// small `package.json`, not folded into metadata.json: they're
// WorldPackage/WorldRegistry's own real identity concern (a world's id
// never changes across republishes; its metadata does), keeping
// WorldMetadata itself purely about the "Metadata System" task's own
// fields, not duplicating id/version across two JSON files.
//
// Honest scope note, the same class of stated gap `core::SceneFile`'s
// own header comment already draws for terrain: this pass's package
// does NOT include terrain (no heightmap serialization exists anywhere
// in this engine yet -- a real, separate feature) or scripts (no
// per-entity/per-scene script association exists in the ECS to
// serialize -- Studio's scripting surfaces are Debug Console/plugin-
// scoped, not scene-authored content today). Both are real, stated,
// documented gaps, not silently dropped -- see "Publishing & Packaging"
// in the README for the full account.
struct WorldPackage {
    std::string worldId;
    std::string version; // "N.N" or "N.N.N", see PublishValidation.hpp's isValidVersionString()
    WorldMetadata metadata;
    core::SceneFile scene;

    // Real directory save/load -- `directory` is created if it doesn't
    // exist (std::filesystem::create_directories). Returns false (real,
    // honest failure, nothing partially written left inconsistent -- see
    // .cpp) if either sub-save fails.
    [[nodiscard]] bool saveToDirectory(const std::string& directory) const;
    [[nodiscard]] bool loadFromDirectory(const std::string& directory);

    // Real, fixed sub-file names within a package directory -- exposed
    // so a caller (e.g. a thumbnail capture step that needs to know
    // where to write the image file *inside* the same package
    // directory) doesn't have to duplicate the convention.
    [[nodiscard]] static std::string scenePath(const std::string& directory);
    [[nodiscard]] static std::string metadataPath(const std::string& directory);
    [[nodiscard]] static std::string packageInfoPath(const std::string& directory);
};

} // namespace engine::publishing
