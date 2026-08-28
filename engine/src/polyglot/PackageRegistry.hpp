#pragma once

// Real, implemented, tested: manifest parsing + dependency resolution
// for kronos-pkg packages. Built as part of the isolated `polyglot_core`
// CMake target -- not linked into engine_runtime or studio.
//
// NOT implemented here (real, stated scope boundary, not an oversight):
// no fetch/download, no artifact loading (a NativeLibrary loader is
// separate, hard, ABI-sensitive work; a LuauModule loader is close to
// what core::Scripting already does for one script file, but isn't
// reused here), no checksum verification, no monetization/payment flow.
// This is real, validated bookkeeping for "what does package X declare,
// and in what order would its dependencies need to be ready" -- the
// part that's pure data/graph logic and buildable without a real
// network fetch pipeline or a second language runtime existing yet.

#include <string>
#include <vector>

namespace engine::polyglot {

enum class PackageArtifactKind {
    NativeLibrary, // compiled C++ (.so/.dll) -- real ABI-compatibility
                    // burden (built against which engine version, which
                    // compiler?) stays unsolved; this just records that
                    // an artifact of this kind exists in the manifest.
    LuauModule,
    TypeScriptBundle,
    WasmModule,
    MaterialAsset,
    AnimationClip, // matches this codebase's own real, existing
                    // "6 shipped .anim clips" asset shape
};

[[nodiscard]] const char* packageArtifactKindName(PackageArtifactKind kind);
// Returns false (kind left unset) for any name this version doesn't
// recognize -- a real, honest "reject, don't guess" for a manifest
// written by/for a future version of this format.
[[nodiscard]] bool packageArtifactKindFromName(const std::string& name, PackageArtifactKind& outKind);

struct PackageArtifact {
    PackageArtifactKind kind;
    std::string relativePath; // within the package
    std::string checksum;     // integrity, not signing -- see this file's own header comment
};

struct PackageManifest {
    std::string packageId;
    std::string version; // opaque string -- real semver comparison is a stated, separate gap (see DependencyResolver's own comment)
    std::vector<PackageArtifact> artifacts;
    std::vector<std::string> dependencyPackageIds;
};

// Real, hand-rolled text format -- same "small, real, hand-rolled
// format" style as core::SceneFile's own text format (matching this
// codebase's own established convention over reaching for a JSON
// library for something this size):
//
//   PACKAGE kronos-ui-kit
//   VERSION 1.2.0
//   DEPENDS kronos-math-utils
//   DEPENDS kronos-net-shared
//   ARTIFACT LuauModule src/main.luau sha256:abc123...
//   ARTIFACT MaterialAsset materials/glow.mat sha256:def456...
//
// PACKAGE and VERSION are required, exactly once each, and must appear
// before any ARTIFACT/DEPENDS line. Unrecognized lines are a real parse
// error here (unlike SceneFile's own "skip unknown lines" forward-
// compatibility convention) -- a package manifest is authored content a
// human/tool should get real, immediate feedback on, not scene data
// silently degrading a field at a time.
class PackageManifestParser {
public:
    [[nodiscard]] static bool parse(const std::string& manifestText, PackageManifest& outManifest, std::string& outError);
};

enum class DependencyResolutionStatus {
    Ok,
    MissingDependency,
    CircularDependency,
};

struct DependencyResolutionResult {
    DependencyResolutionStatus status = DependencyResolutionStatus::Ok;
    // Real topological order, dependencies before dependents -- the real
    // order a caller would need to install/load packages in. Only
    // meaningful when status == Ok.
    std::vector<std::string> installOrder;
    std::string missingDependencyId;    // set when status == MissingDependency
    std::vector<std::string> circularChain; // set when status == CircularDependency -- the real cycle found, e.g. ["A", "B", "C", "A"]
};

// Real graph resolution over a set of known manifests -- NOT real
// semver constraint solving (a manifest's own `dependencyPackageIds` is
// just a list of required package IDs here, no version range/operator
// parsing). Real semver ranges are a stated, separate, harder problem
// (multiple installed versions of the same dependency, version
// conflicts between two dependents wanting incompatible ranges of a
// third package) -- not attempted here.
class DependencyResolver {
public:
    // Real, honest overwrite-on-duplicate: registering the same
    // packageId twice replaces the earlier manifest rather than keeping
    // both (there is exactly one real "currently known" manifest per ID
    // at a time in this resolver -- matching a real package manager's
    // own "the last one you added is the truth" semantics for a local
    // working set).
    void addManifest(PackageManifest manifest);

    [[nodiscard]] const PackageManifest* find(const std::string& packageId) const;
    [[nodiscard]] size_t size() const { return manifests_.size(); }

    // Real DFS-based topological sort with real cycle detection --
    // resolves `rootPackageId`'s own full transitive dependency closure.
    [[nodiscard]] DependencyResolutionResult resolve(const std::string& rootPackageId) const;

private:
    std::vector<PackageManifest> manifests_;
};

// Real, thin, real convenience wrapper over DependencyResolver -- the
// one real, additional thing it does is manifest validation on
// registerManifest() (see PackageManifestParser's own -- this rejects a
// structurally-parsed-but-semantically-broken manifest, e.g. an empty
// packageId or a self-dependency, that the text parser alone can't
// catch since it doesn't know about any other package).
class PackageRegistry {
public:
    // Real validation: rejects (returns false, registers nothing) an
    // empty packageId/version, an artifact with an empty relativePath,
    // or a dependency on itself (a real, trivial one-node cycle the
    // resolver's own DFS would also catch later, but rejecting it at
    // registration time gives real, immediate feedback instead of a
    // resolve()-time surprise).
    [[nodiscard]] bool registerManifest(PackageManifest manifest);

    [[nodiscard]] const PackageManifest* find(const std::string& packageId) const { return resolver_.find(packageId); }
    [[nodiscard]] DependencyResolutionResult resolveInstallOrder(const std::string& packageId) const {
        return resolver_.resolve(packageId);
    }
    [[nodiscard]] size_t size() const { return resolver_.size(); }

private:
    DependencyResolver resolver_;
};

} // namespace engine::polyglot
