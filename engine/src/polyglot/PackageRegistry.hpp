#pragma once

// DRAFT SCAFFOLDING -- not wired into the build, not implemented.
// See polyglot/README.md.
//
// Goal: one package can bundle mixed-language content (e.g. a C++
// graphics system + a Luau wrapper + a TS config UI) and install/update
// as one unit through kronos-pkg, instead of each language needing its
// own separate package flow.

#include <string>
#include <vector>

namespace engine::polyglot {

enum class PackageArtifactKind {
    NativeLibrary,  // compiled C++ (.so/.dll) -- real ABI-compatibility
                     // burden: built against which engine version, which
                     // compiler? Not a solved problem here.
    LuauModule,
    TypeScriptBundle,
    WasmModule,
    MaterialAsset,
    AnimationClip,   // matches this codebase's own real, existing
                     // "6 shipped .anim clips" asset shape
};

struct PackageArtifact {
    PackageArtifactKind kind;
    std::string relativePath; // within the package
    std::string checksum;     // integrity, not signing -- see below
};

struct PackageManifest {
    std::string packageId;
    std::string version; // real semver, TBD which flavor
    std::vector<PackageArtifact> artifacts;
    std::vector<std::string> dependencyPackageIds;
};

// Real, unresolved product questions this stub deliberately doesn't
// answer: how is a NativeLibrary artifact's compatibility with the
// installing engine build verified before load (a wrong-ABI native
// library loaded into a running process is a real crash, not a
// graceful failure)? Is monetization (mentioned in the pillar's own
// description) a real payment flow this registry owns, or a separate
// backend service this only reports usage to? Both need real product
// decisions before this becomes buildable, not just an API shape.
class PackageRegistry {
public:
    // TODO: real install -- fetch, verify checksums, resolve
    // dependencyPackageIds transitively, then hand each artifact to its
    // own kind-specific loader (a NativeLibrary loader is real, separate,
    // hard work; a LuauModule loader is close to what core::Scripting
    // already does for a single script file today).
    [[nodiscard]] bool install(const std::string& packageId, const std::string& version);
    [[nodiscard]] bool uninstall(const std::string& packageId);

    [[nodiscard]] std::vector<PackageManifest> listInstalled() const;

private:
    std::vector<PackageManifest> installed_;
};

} // namespace engine::polyglot
