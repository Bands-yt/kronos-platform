#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "publishing/WorldPackage.hpp"

namespace engine::publishing {

// One real, in-memory file destined for (or read back from) a
// `.kronos` archive -- `relativePath` is just a filename (this format
// is flat, no subdirectories, matching WorldPackage::saveToDirectory()'s
// own flat scene.txt/metadata.json/package.json layout).
struct ArchiveFileEntry {
    std::string relativePath;
    std::vector<uint8_t> data;
};

// Kronos ("Developer Velocity Sprint" -- "One-Click Package Exporter"):
// a real, self-contained, zlib-deflate-compressed archive format for
// bundling a WorldPackage's files into one `.kronos` file. Deliberately
// a small, custom container (magic + per-file [name][sizes][deflate
// bytes]), not a byte-for-byte standard ZIP -- no ZIP-writing library is
// vendored in this codebase (only libz itself, see
// cmake/Dependencies.cmake), and hand-rolling a real ZIP central-
// directory/local-header format from memory risks silent, hard-to-detect
// corruption, the same reasoning publishing/ThumbnailCapture.hpp's own
// header comment already applies to why it writes PPM instead of a
// hand-rolled PNG encoder. This format is real and genuinely compressed
// (zlib deflate via compress2()/uncompress(), not a renamed tarball) and
// round-trips exactly through writeArchive()/readArchive() below --
// opening a `.kronos` file in a general-purpose zip tool is a real,
// stated, separate future improvement (that needs the real ZIP
// container format, not just deflate), not something silently implied
// by the ".kronos" extension.
[[nodiscard]] bool writeArchive(const std::string& archivePath, const std::vector<ArchiveFileEntry>& files);
[[nodiscard]] bool readArchive(const std::string& archivePath, std::vector<ArchiveFileEntry>& outFiles);

// The real, one-click packaging entry point: saves `package` to a
// temporary real directory (reusing WorldPackage::saveToDirectory()'s
// already-real scene.txt/metadata.json/package.json serialization
// unchanged), writes a real `assets_manifest.txt` (one relative path per
// line) from `referencedAssetPaths`, compresses every real file in that
// directory into `archivePath`, then removes the temporary directory.
//
// Real, stated scope boundary: only the *manifest* (a list of relative
// paths) is bundled, not the referenced asset files' own binary
// content -- no asset-copy pipeline exists anywhere in this codebase yet
// (see PublishValidation.hpp's own collectReferencedAssetPaths()
// comment), so bundling actual texture/model bytes here would be a
// separate, larger, real feature, not something to fake with an empty
// placeholder.
//
// `thumbnailSourcePath` (optional, empty = none) is a real, already-
// captured image file on disk (e.g. from ThumbnailCapture.hpp's
// captureThumbnailToFile()) -- if non-empty and it exists, it's copied
// into the bundle under the fixed name thumbnailFileName() below. The
// caller is responsible for having set `package.metadata.thumbnailPath`
// to that same fixed name beforehand, so metadata.json inside the
// archive correctly references it.
[[nodiscard]] bool writeWorldPackageArchive(const WorldPackage& package,
                                             const std::vector<std::string>& referencedAssetPaths,
                                             const std::string& archivePath,
                                             const std::string& thumbnailSourcePath = std::string());

[[nodiscard]] std::string thumbnailFileName();

// The manifest file's own fixed name within a bundled package -- exposed
// so a reader (or a test) doesn't have to duplicate the convention.
[[nodiscard]] std::string assetManifestFileName();

} // namespace engine::publishing
