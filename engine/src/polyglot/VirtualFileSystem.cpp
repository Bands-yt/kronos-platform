#include "polyglot/VirtualFileSystem.hpp"

#include <algorithm>
#include <filesystem>

namespace engine::polyglot {

namespace {

bool isValidPrefix(const std::string& prefix) {
    if (prefix.empty()) return false;
    if (prefix.find('/') != std::string::npos || prefix.find('\\') != std::string::npos) return false;
    if (prefix == "." || prefix == "..") return false;
    return true;
}

// Splits "assets/models/box.obj" into ("assets", "models/box.obj"), or
// ("assets", "") for a bare "assets" with nothing after it. Real, plain
// string work -- no filesystem access, so this stays safe to call on a
// virtualPath that hasn't been validated against any real mount yet.
std::pair<std::string, std::string> splitFirstSegment(const std::string& virtualPath) {
    size_t slash = virtualPath.find('/');
    if (slash == std::string::npos) return {virtualPath, std::string()};
    return {virtualPath.substr(0, slash), virtualPath.substr(slash + 1)};
}

} // namespace

bool VirtualFileSystem::mount(const std::string& virtualPrefix, const std::string& realDirectory, int priority) {
    if (!isValidPrefix(virtualPrefix)) return false;
    if (realDirectory.empty()) return false;
    mounts_.push_back(VfsMount{virtualPrefix, realDirectory, priority});
    return true;
}

bool VirtualFileSystem::unmount(const std::string& virtualPrefix) {
    // Real, reverse search -- removes the most-recently-added mount for
    // this prefix first, matching this method's own "undo my own last
    // mount()" header comment.
    for (auto it = mounts_.rbegin(); it != mounts_.rend(); ++it) {
        if (it->virtualPrefix == virtualPrefix) {
            mounts_.erase(std::next(it).base());
            return true;
        }
    }
    return false;
}

bool VirtualFileSystem::isMounted(const std::string& virtualPrefix) const {
    return std::any_of(mounts_.begin(), mounts_.end(), [&](const VfsMount& m) { return m.virtualPrefix == virtualPrefix; });
}

bool VirtualFileSystem::resolve(const std::string& virtualPath, std::string& outRealPath) const {
    outRealPath.clear();
    auto [prefix, remainder] = splitFirstSegment(virtualPath);
    if (prefix.empty()) return false;

    // Real "highest priority wins, ties broken by most-recently-mounted"
    // selection -- a later mount at the same priority naturally wins a
    // forward linear scan kept to <=, not <, below.
    const VfsMount* winner = nullptr;
    for (const auto& m : mounts_) {
        if (m.virtualPrefix != prefix) continue;
        if (winner == nullptr || m.priority >= winner->priority) winner = &m;
    }
    if (winner == nullptr) return false;

    std::filesystem::path base = std::filesystem::path(winner->realDirectory).lexically_normal();
    // Real, deliberate special case: appending an empty `remainder` via
    // operator/ appends a trailing directory separator (per the
    // standard's own path::operator/= semantics for an empty rhs), which
    // lexically_normal() doesn't strip -- a bare-prefix virtualPath (no
    // remainder at all) would otherwise resolve to "base/" instead of
    // "base" byte-for-byte. Skipping operator/ entirely when there's
    // nothing to append avoids that formatting quirk outright.
    std::filesystem::path joined = remainder.empty() ? base : (base / remainder).lexically_normal();

    // Real traversal-escape check -- lexical only (no real disk access,
    // matching this class's own "no file I/O" scope), but still a real,
    // meaningful safety check: a remainder like "../../etc/passwd"
    // normalizes to a path that no longer starts with `base`, which is
    // exactly what this catches.
    auto baseIt = base.begin();
    auto joinedIt = joined.begin();
    for (; baseIt != base.end(); ++baseIt, ++joinedIt) {
        if (joinedIt == joined.end() || *joinedIt != *baseIt) return false;
    }

    outRealPath = joined.string();
    return true;
}

} // namespace engine::polyglot
