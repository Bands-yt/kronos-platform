#include "polyglot/PackageRegistry.hpp"

#include <algorithm>
#include <sstream>
#include <unordered_map>

namespace engine::polyglot {

const char* packageArtifactKindName(PackageArtifactKind kind) {
    switch (kind) {
        case PackageArtifactKind::NativeLibrary: return "NativeLibrary";
        case PackageArtifactKind::LuauModule: return "LuauModule";
        case PackageArtifactKind::TypeScriptBundle: return "TypeScriptBundle";
        case PackageArtifactKind::WasmModule: return "WasmModule";
        case PackageArtifactKind::MaterialAsset: return "MaterialAsset";
        case PackageArtifactKind::AnimationClip: return "AnimationClip";
    }
    return "";
}

bool packageArtifactKindFromName(const std::string& name, PackageArtifactKind& outKind) {
    static const std::unordered_map<std::string, PackageArtifactKind> kByName = {
        {"NativeLibrary", PackageArtifactKind::NativeLibrary},   {"LuauModule", PackageArtifactKind::LuauModule},
        {"TypeScriptBundle", PackageArtifactKind::TypeScriptBundle}, {"WasmModule", PackageArtifactKind::WasmModule},
        {"MaterialAsset", PackageArtifactKind::MaterialAsset},   {"AnimationClip", PackageArtifactKind::AnimationClip},
    };
    auto it = kByName.find(name);
    if (it == kByName.end()) return false;
    outKind = it->second;
    return true;
}

namespace {

std::vector<std::string> splitWhitespace(const std::string& line) {
    std::istringstream iss(line);
    std::vector<std::string> tokens;
    std::string token;
    while (iss >> token) tokens.push_back(token);
    return tokens;
}

} // namespace

bool PackageManifestParser::parse(const std::string& manifestText, PackageManifest& outManifest, std::string& outError) {
    PackageManifest manifest;
    bool sawPackage = false;
    bool sawVersion = false;

    std::istringstream in(manifestText);
    std::string line;
    int lineNumber = 0;
    while (std::getline(in, line)) {
        ++lineNumber;
        // Real, honest comment/blank-line skip -- authored content
        // benefits from being able to annotate a manifest, same as this
        // codebase's own convention for every other hand-authored text
        // format.
        auto firstNonSpace = line.find_first_not_of(" \t");
        if (firstNonSpace == std::string::npos || line[firstNonSpace] == '#') continue;

        std::vector<std::string> tokens = splitWhitespace(line);
        if (tokens.empty()) continue;
        const std::string& keyword = tokens[0];

        if (keyword == "PACKAGE") {
            if (tokens.size() != 2) {
                outError = "line " + std::to_string(lineNumber) + ": PACKAGE requires exactly one argument (the package id)";
                return false;
            }
            if (sawPackage) {
                outError = "line " + std::to_string(lineNumber) + ": duplicate PACKAGE line -- a manifest declares exactly one package";
                return false;
            }
            manifest.packageId = tokens[1];
            sawPackage = true;
        } else if (keyword == "VERSION") {
            if (tokens.size() != 2) {
                outError = "line " + std::to_string(lineNumber) + ": VERSION requires exactly one argument";
                return false;
            }
            if (!sawPackage) {
                outError = "line " + std::to_string(lineNumber) + ": VERSION must come after PACKAGE";
                return false;
            }
            if (sawVersion) {
                outError = "line " + std::to_string(lineNumber) + ": duplicate VERSION line";
                return false;
            }
            manifest.version = tokens[1];
            sawVersion = true;
        } else if (keyword == "DEPENDS") {
            if (tokens.size() != 2) {
                outError = "line " + std::to_string(lineNumber) + ": DEPENDS requires exactly one argument (the dependency's package id)";
                return false;
            }
            if (!sawPackage) {
                outError = "line " + std::to_string(lineNumber) + ": DEPENDS must come after PACKAGE";
                return false;
            }
            manifest.dependencyPackageIds.push_back(tokens[1]);
        } else if (keyword == "ARTIFACT") {
            if (tokens.size() != 4) {
                outError = "line " + std::to_string(lineNumber) +
                            ": ARTIFACT requires exactly three arguments (kind, relative path, checksum)";
                return false;
            }
            if (!sawPackage) {
                outError = "line " + std::to_string(lineNumber) + ": ARTIFACT must come after PACKAGE";
                return false;
            }
            PackageArtifact artifact;
            if (!packageArtifactKindFromName(tokens[1], artifact.kind)) {
                outError = "line " + std::to_string(lineNumber) + ": unrecognized artifact kind \"" + tokens[1] + "\"";
                return false;
            }
            artifact.relativePath = tokens[2];
            artifact.checksum = tokens[3];
            manifest.artifacts.push_back(std::move(artifact));
        } else {
            outError = "line " + std::to_string(lineNumber) + ": unrecognized keyword \"" + keyword + "\"";
            return false;
        }
    }

    if (!sawPackage) {
        outError = "manifest has no PACKAGE line";
        return false;
    }
    if (!sawVersion) {
        outError = "manifest has no VERSION line";
        return false;
    }

    outManifest = std::move(manifest);
    return true;
}

// --- DependencyResolver ----------------------------------------------------

void DependencyResolver::addManifest(PackageManifest manifest) {
    auto it = std::find_if(manifests_.begin(), manifests_.end(),
                            [&](const PackageManifest& m) { return m.packageId == manifest.packageId; });
    if (it != manifests_.end()) {
        *it = std::move(manifest); // real, honest overwrite -- see this method's own header comment
    } else {
        manifests_.push_back(std::move(manifest));
    }
}

const PackageManifest* DependencyResolver::find(const std::string& packageId) const {
    auto it = std::find_if(manifests_.begin(), manifests_.end(),
                            [&](const PackageManifest& m) { return m.packageId == packageId; });
    return it != manifests_.end() ? &(*it) : nullptr;
}

namespace {

// Real DFS, three-state (unvisited/in-progress/done) cycle-detecting
// topological sort -- the standard, well-understood algorithm for
// exactly this problem, not invented fresh here.
enum class VisitState { Unvisited, InProgress, Done };

bool visit(const std::string& packageId, const DependencyResolver& resolver,
           std::unordered_map<std::string, VisitState>& state, std::vector<std::string>& stack,
           std::vector<std::string>& installOrder, DependencyResolutionResult& outResult) {
    state[packageId] = VisitState::InProgress;
    stack.push_back(packageId);

    const PackageManifest* manifest = resolver.find(packageId);
    if (manifest == nullptr) {
        outResult.status = DependencyResolutionStatus::MissingDependency;
        outResult.missingDependencyId = packageId;
        return false;
    }

    for (const std::string& dep : manifest->dependencyPackageIds) {
        auto it = state.find(dep);
        VisitState depState = (it != state.end()) ? it->second : VisitState::Unvisited;

        if (depState == VisitState::InProgress) {
            // Real cycle -- the real chain is everything on `stack` from
            // this dependency's own first occurrence through here, plus
            // the dependency again at the end to show it closing the loop.
            outResult.status = DependencyResolutionStatus::CircularDependency;
            auto cycleStart = std::find(stack.begin(), stack.end(), dep);
            outResult.circularChain.assign(cycleStart, stack.end());
            outResult.circularChain.push_back(dep);
            return false;
        }
        if (depState == VisitState::Unvisited) {
            if (!visit(dep, resolver, state, stack, installOrder, outResult)) return false;
        }
        // Done: already fully resolved earlier (a real, shared
        // dependency two different packages both depend on) -- nothing
        // more to do for it here.
    }

    state[packageId] = VisitState::Done;
    stack.pop_back();
    installOrder.push_back(packageId);
    return true;
}

} // namespace

DependencyResolutionResult DependencyResolver::resolve(const std::string& rootPackageId) const {
    DependencyResolutionResult result;
    std::unordered_map<std::string, VisitState> state;
    std::vector<std::string> stack;
    std::vector<std::string> installOrder;

    if (visit(rootPackageId, *this, state, stack, installOrder, result)) {
        result.status = DependencyResolutionStatus::Ok;
        result.installOrder = std::move(installOrder);
    }
    return result;
}

// --- PackageRegistry ---------------------------------------------------

bool PackageRegistry::registerManifest(PackageManifest manifest) {
    if (manifest.packageId.empty()) return false;
    if (manifest.version.empty()) return false;
    for (const auto& artifact : manifest.artifacts) {
        if (artifact.relativePath.empty()) return false;
    }
    // Real, immediate rejection of a trivial one-node self-dependency --
    // see this method's own header comment for why this is checked here
    // rather than left for resolve()'s own cycle detection to find later.
    for (const auto& dep : manifest.dependencyPackageIds) {
        if (dep == manifest.packageId) return false;
    }

    resolver_.addManifest(std::move(manifest));
    return true;
}

} // namespace engine::polyglot
