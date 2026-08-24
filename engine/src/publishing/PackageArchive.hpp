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
// unchanged), then for each of `referencedAssetPaths` (relative to
// `assetRootDirectory` -- the real project directory these paths were
// validated as relative to, see PublishValidation.hpp's
// validateAssetPathsAreRelative()) reads the real file's bytes and
// bundles them, content-addressed by their own SHA-256 (so two
// different relative paths that happen to reference byte-identical
// content are only ever stored once). `assets_manifest.txt` records
// both columns -- `<original relative path>\t<archived content-
// addressed filename>` -- so extractWorldPackageArchive() below can
// restore the original project layout on unpack. A referenced path
// that doesn't actually exist on disk (a broken/stale reference) is
// still listed, with an empty second column -- a real, honest gap
// noted in the manifest, not a silent skip or a hard failure of the
// whole export.
//
// `thumbnailSourcePath` (optional, empty = none) is a real, already-
// captured image file on disk (e.g. from ThumbnailCapture.hpp's
// captureThumbnailToFile()) -- if non-empty and it exists, it's copied
// into the bundle under the fixed name thumbnailFileName() below. The
// caller is responsible for having set `package.metadata.thumbnailPath`
// to that same fixed name beforehand, so metadata.json inside the
// archive correctly references it.
[[nodiscard]] bool writeWorldPackageArchive(const WorldPackage& package, const std::string& assetRootDirectory,
                                             const std::vector<std::string>& referencedAssetPaths,
                                             const std::string& archivePath,
                                             const std::string& thumbnailSourcePath = std::string());

// The real, first consumer of readArchive(): unpacks `archivePath` into
// `outputDirectory` (created if it doesn't exist), restoring
// scene.txt/metadata.json/package.json[/thumbnail] at the directory's
// own root, and every bundled asset back at its ORIGINAL relative path
// (read from assets_manifest.txt's second column) -- creating whatever
// subdirectories that path needs, even though the archive's own
// internal storage is flat. Real, honest failure (false, nothing
// partially written left inconsistent) if the archive itself is
// unreadable/corrupt; an individual asset with an empty manifest
// column (never found at export time) is simply not written back,
// which is the same honest gap the manifest already recorded, not a
// new failure invented here.
[[nodiscard]] bool extractWorldPackageArchive(const std::string& archivePath, const std::string& outputDirectory);

// The real SHA-256 (hex-encoded) of the whole archive FILE as written
// to disk -- what a caller sends the backend as scene_sha256 for
// integrity verification (see catalog/routes.js's own comment) and
// what content-addresses the archive's own S3 object key. Deliberately
// a distinct, separate real hash from the per-asset content-addressing
// inside the archive above -- one is "does this whole uploaded file
// match what the creator actually built," the other is "do these two
// asset references happen to be the same bytes."
[[nodiscard]] std::string archiveSha256Hex(const std::string& archivePath);

[[nodiscard]] std::string thumbnailFileName();

// The manifest file's own fixed name within a bundled package -- exposed
// so a reader (or a test) doesn't have to duplicate the convention.
[[nodiscard]] std::string assetManifestFileName();

} // namespace engine::publishing
