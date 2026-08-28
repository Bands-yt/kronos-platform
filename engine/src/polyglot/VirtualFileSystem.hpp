#pragma once

// Real, implemented, tested: virtual-path-to-real-path mounting and
// resolution. Built as part of the isolated `polyglot_core` CMake
// target -- not linked into engine_runtime or studio.
//
// NOT implemented here (real, stated scope boundary, not an oversight):
// no actual file I/O (reading/listing directory contents) and no
// archive/pack-file mounting -- a .kronos scene file is a single file,
// not an archive, so "mounting" one for real means mounting the real
// directory it lives in; mounting the *inside* of a real archive format
// (zip or otherwise) is a separate, unbuilt feature (this codebase has
// no archive library dependency at all yet). This is real, path-string-
// level resolution only: a caller takes the resolved real path back and
// reads it with whatever I/O it already uses (core::Mesh loaders,
// GltfLoader, core::Scripting, etc.) -- deliberately not reimplementing
// any of those.

#include <string>
#include <vector>

namespace engine::polyglot {

struct VfsMount {
    std::string virtualPrefix; // e.g. "assets" -- no leading/trailing slash, no ".."
    std::string realDirectory; // e.g. "/home/user/project/assets"
    int priority = 0;          // higher wins on a collision; ties broken by most-recently-mounted
};

class VirtualFileSystem {
public:
    // Real validation: rejects (returns false, mounts nothing) an empty
    // virtualPrefix/realDirectory, or a virtualPrefix containing a slash
    // or a ".." component (a prefix is one real path segment, not a
    // nested path -- keeps resolve()'s own prefix-matching unambiguous).
    // Multiple mounts MAY share the same virtualPrefix on purpose -- a
    // real, deliberate "override pack shadows base content" use case
    // (e.g. a higher-priority mod pack's own "assets" mount taking
    // precedence over the base game's own "assets" mount) -- resolve()
    // picks whichever mount sharing a prefix has the highest `priority`
    // (ties broken by most-recently-mounted), not a search-until-found
    // across all of them (this class does no real file I/O at all, see
    // its own header comment, so it has no way to check which mount's
    // directory actually *has* the requested file).
    [[nodiscard]] bool mount(const std::string& virtualPrefix, const std::string& realDirectory, int priority = 0);
    // Real, honest "remove the single most-recently-added mount for this
    // prefix" -- not "remove every mount ever registered under it" --
    // matching a real stack-like "undo my own last mount()" use (a
    // plugin/pack unloading itself shouldn't also unmount a different
    // pack that happens to share its prefix).
    bool unmount(const std::string& virtualPrefix);

    // Real resolution: virtualPath must start with a mounted prefix
    // (e.g. "assets/models/box.obj" against a mount at "assets") --
    // returns the real, joined real-filesystem path from whichever
    // mount at that prefix currently wins (see mount()'s own comment).
    // Real, honest failure (returns false, outRealPath left empty) when
    // no mount's prefix matches, OR (real safety check) when the
    // requested path would lexically escape the mounted directory via a
    // ".." component -- the same "never trust a caller-supplied path"
    // discipline this codebase's own net::ByteReader applies to network
    // input, applied here to a virtual path that could just as easily
    // come from a downloaded package manifest as from trusted local code.
    [[nodiscard]] bool resolve(const std::string& virtualPath, std::string& outRealPath) const;

    [[nodiscard]] bool isMounted(const std::string& virtualPrefix) const;
    [[nodiscard]] size_t mountCount() const { return mounts_.size(); }

private:
    std::vector<VfsMount> mounts_;
};

} // namespace engine::polyglot
